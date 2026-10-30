/*  The Dawn backend's shared internals (#375). Included by Dawn*.mm/.cpp only.
 *
 *  Dawn itself is the same everywhere; what differs per system is which native
 *  API it sits on and how a finished picture reaches a shell. That half is
 *  three functions at the bottom, in DawnMetal.mm on macOS and DawnNative.cpp
 *  on Windows and Linux (#376).
 *
 *  One `Context` per Dawn device, held by shared_ptr from every object made on
 *  it, so a texture outliving its `gpu::Device` (a test's scope ordering) still
 *  has a queue to read back through.
 */

#pragma once

#include "gpu/Backend.h"

#include <webgpu/webgpu_cpp.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace orion::gpu::dawn {

struct Context {
    wgpu::Instance instance;
    wgpu::Adapter  adapter;
    wgpu::Device   device;
    wgpu::Queue    queue;
    void*          metalDevice = nullptr;   // id<MTLDevice>, bridged, non-owning
    DeviceInfo     info;

    /// Every prepared shader loaded on this device, by path, with the
    /// pipelines made from it (DawnKernel.cpp). After the device so it goes
    /// first. Type-erased to keep this header small.
    std::mutex shaderLock;
    std::map<std::string, std::shared_ptr<void>> shaders;

    /// Blocks until `future` completes. Throws if the wait itself fails.
    void wait(const wgpu::Future& future) const;

    /// Records a lost device; called from Dawn's callback. Device-wide, so
    /// every thread's next `ErrorScope::check` throws it.
    void lose(const std::string& message);
    [[nodiscard]] std::string lost();

private:
    std::mutex  lostLock_;
    std::string lost_;
};

/// Dawn reports a failed call through an error scope rather than a return
/// value. One of these around each operation that can fail; `check` pops it
/// and throws Dawn's message. ⚠ Dawn's scopes are per thread, so two threads
/// rendering at once never read each other's errors — a shared slot did.
/// The destructor pops an unchecked scope (an exception unwinding past it).
class ErrorScope {
public:
    explicit ErrorScope(Context&);
    ~ErrorScope();
    ErrorScope(const ErrorScope&) = delete;
    ErrorScope& operator=(const ErrorScope&) = delete;
    void check(const std::string& what);

private:
    std::string pop();
    Context& ctx_;
    bool     open_ = true;
};

wgpu::TextureFormat toWgpu(PixelFormat);
const char* wgslFormat(PixelFormat);

/// The view a Dawn texture binds through. Valid for any texture made on a Dawn
/// device; anything else is a caller mixing devices, which throws.
const wgpu::TextureView& viewOf(const Texture&);

std::unique_ptr<Texture::Impl> makeTexture(std::shared_ptr<Context>, std::uint32_t width,
                                           std::uint32_t height, PixelFormat);
std::unique_ptr<Library::Impl> makeLibrary(std::shared_ptr<Context>, const std::string& path);
std::unique_ptr<Kernel::Impl>  makeKernel(Library&, const std::string& entryPoint);
std::unique_ptr<CommandBuffer::Impl> makeCommandBuffer(std::shared_ptr<Context>);
std::unique_ptr<Readback::Slot> makeStaging(std::shared_ptr<Context>);

// ── The platform half ──────────────────────────────────────────────────────

struct Platform {
    wgpu::BackendType backend;
    /// Required on top of the engine's own (DawnDevice.cpp): the display hand-off's.
    std::vector<wgpu::FeatureName> features;
    /// Bounds checks on every texture access unless ORION_DAWN_ROBUST says
    /// otherwise. Off on Metal only, where the engine was measured (#375).
    bool robust;
    /// Asked for where the adapter has them: what `shareForInterop` needs (#386).
    std::vector<wgpu::FeatureName> optional = {};
};
Platform platform();

/// Fills `ctx.info` and `ctx.metalDevice` once the device exists.
void describe(Context& ctx, const wgpu::Limits& limits);

/// An RGBA8 texture a shell can draw with no copy, and what it draws it with.
struct DisplayShare {
    virtual ~DisplayShare() = default;
    virtual void* handle() const noexcept = 0;
    /// `shareForInterop`'s: the handle another API opens, and Dawn's turn at it.
    virtual void* ntHandle() const noexcept { return nullptr; }
    virtual void begin() {}
    virtual void end() {}
};

/// Makes `tex` as a shareable RGBA8 texture and returns its share, or returns
/// null and leaves `tex` alone where the platform has no hand-off yet; the
/// caller then allocates a plain texture and the shell reads it back.
std::unique_ptr<DisplayShare> shareForDisplay(Context& ctx, std::uint32_t width,
                                              std::uint32_t height, wgpu::TextureUsage usage,
                                              wgpu::Texture& tex);

/// Makes `tex` an RGBA8 texture another API opens by handle (Texture::createShared):
/// a D3D11 texture with an NT handle and a keyed mutex on Windows (#386). Null,
/// and `tex` left alone, where the platform has none or the device lacks the features.
std::unique_ptr<DisplayShare> shareForInterop(Context& ctx, std::uint32_t width,
                                              std::uint32_t height, wgpu::TextureUsage usage,
                                              wgpu::Texture& tex);
std::unique_ptr<Texture::Impl> makeSharedTexture(std::shared_ptr<Context>, std::uint32_t width,
                                                 std::uint32_t height);

}  // namespace orion::gpu::dawn

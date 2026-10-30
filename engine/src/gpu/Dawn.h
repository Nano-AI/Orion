/*  The Dawn backend's shared internals (#375). Included by Dawn*.mm/.cpp only.
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

}  // namespace orion::gpu::dawn

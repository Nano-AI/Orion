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

    /// Throws, with Dawn's message, if an error was reported since the last
    /// call. Dawn reports validation errors through a callback rather than a
    /// return value, so every call that can fail is followed by this.
    void check(const char* what);

    /// Records an uncaptured error; called from Dawn's callback.
    void report(const std::string& message);

    /// Waits for everything submitted so far.
    void finish();

private:
    std::mutex  errorLock_;
    std::string error_;
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

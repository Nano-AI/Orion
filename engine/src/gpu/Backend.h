/*  The backend seam (#375). Internal to engine/src/gpu: nothing else includes it.
 *
 *  Each public GPU type (Device.h, Resources.h) owns one of these abstract
 *  `Impl`s, and a backend is one subclass of each: Metal in Metal*.mm, Dawn in
 *  Dawn*.mm. The public types keep everything backend-neutral (sizes, formats,
 *  names) and forward the rest here, so a call site cannot tell, and the port
 *  could go node by node without touching a node.
 */

#pragma once

#include "gpu/Resources.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orion::gpu {

struct Texture::Impl {
    virtual ~Impl() = default;
    virtual void upload(const void* src, std::size_t bytesPerRow,
                        std::uint32_t width, std::uint32_t height) = 0;
    /// The rectangle at (x, y), already clipped to the texture by the caller.
    virtual void download(void* dst, std::size_t bytesPerRow, std::uint32_t x,
                          std::uint32_t y, std::uint32_t width,
                          std::uint32_t height) const = 0;
    virtual void* raw() const noexcept = 0;
    virtual void* displayHandle() const noexcept = 0;
};

struct Library::Impl {
    virtual ~Impl() = default;
    virtual void* raw() const noexcept = 0;
};

struct Kernel::Impl {
    virtual ~Impl() = default;
    virtual void* raw() const noexcept = 0;
    std::uint32_t execWidth    = 32;
    std::uint32_t maxThreads   = 1024;
    std::uint32_t textureSlots = 0;
};

struct CommandBuffer::Impl {
    virtual ~Impl() = default;
    virtual void dispatch(const Kernel&, const std::vector<const Texture*>& textures,
                          const void* params, std::size_t paramBytes,
                          std::uint32_t width, std::uint32_t height) = 0;
    virtual void commitAndWait() = 0;
    double gpuMs = 0.0;
};

struct Device::Impl {
    virtual ~Impl() = default;
    [[nodiscard]] virtual Backend    backend() const noexcept = 0;
    [[nodiscard]] virtual DeviceInfo info() const = 0;
    [[nodiscard]] virtual void*      metalDevice() const noexcept = 0;

    virtual std::unique_ptr<Texture::Impl> texture(std::uint32_t width, std::uint32_t height,
                                                   PixelFormat, bool linear) = 0;
    virtual std::unique_ptr<Library::Impl> library(const std::string& path) = 0;
    virtual std::unique_ptr<Kernel::Impl>  kernel(Library&, const std::string& entryPoint) = 0;
    virtual std::unique_ptr<CommandBuffer::Impl> commandBuffer() = 0;
};

std::unique_ptr<Device::Impl> makeMetalDevice();
/// Throws std::runtime_error when the engine was built without Dawn.
std::unique_ptr<Device::Impl> makeDawnDevice();

// Metal's resources (MetalResources.mm), made by MetalDevice.mm. The handles
// are `id<MTLDevice>` and `id<MTLCommandQueue>`, bridged.
std::unique_ptr<Texture::Impl> makeMetalTexture(void* device, std::uint32_t width,
                                                std::uint32_t height, PixelFormat,
                                                bool linear);
std::unique_ptr<Library::Impl> makeMetalLibrary(void* device, const std::string& path);
std::unique_ptr<Kernel::Impl>  makeMetalKernel(void* device, Library&,
                                               const std::string& entryPoint);
std::unique_ptr<CommandBuffer::Impl> makeMetalCommandBuffer(void* queue);

}  // namespace orion::gpu

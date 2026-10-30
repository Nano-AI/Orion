#import <Metal/Metal.h>

#include "gpu/Backend.h"

#include <stdexcept>

namespace orion::gpu {
namespace {

struct MetalDevice final : Device::Impl {
    id<MTLDevice>       device = nil;
    id<MTLCommandQueue> queue  = nil;

    Backend backend() const noexcept override { return Backend::Metal; }
    void* metalDevice() const noexcept override { return (__bridge void*)device; }

    DeviceInfo info() const override {
        return DeviceInfo{
            .name                  = device.name.UTF8String ? device.name.UTF8String : "unknown",
            .recommendedWorkingSet = device.recommendedMaxWorkingSetSize,
            .maxBufferLength       = device.maxBufferLength,
            .unifiedMemory         = static_cast<bool>(device.hasUnifiedMemory),
            .supportsApple7        = static_cast<bool>([device supportsFamily:MTLGPUFamilyApple7]),
        };
    }

    std::unique_ptr<Texture::Impl> texture(std::uint32_t w, std::uint32_t h,
                                           PixelFormat f, bool linear) override {
        return makeMetalTexture(metalDevice(), w, h, f, linear);
    }
    std::unique_ptr<Library::Impl> library(const std::string& path) override {
        return makeMetalLibrary(metalDevice(), path);
    }
    std::unique_ptr<Kernel::Impl> kernel(Library& lib, const std::string& entry) override {
        return makeMetalKernel(metalDevice(), lib, entry);
    }
    std::unique_ptr<CommandBuffer::Impl> commandBuffer() override {
        return makeMetalCommandBuffer((__bridge void*)queue);
    }
};

}  // namespace

std::unique_ptr<Device::Impl> makeMetalDevice() {
    auto dev = std::make_unique<MetalDevice>();

    dev->device = MTLCreateSystemDefaultDevice();
    if (dev->device == nil) {
        throw std::runtime_error("no Metal device available");
    }

    dev->queue = [dev->device newCommandQueue];
    if (dev->queue == nil) {
        throw std::runtime_error("could not create Metal command queue");
    }
    return dev;
}

}  // namespace orion::gpu

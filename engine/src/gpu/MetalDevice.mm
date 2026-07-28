#import <Metal/Metal.h>

#include "gpu/MetalDevice.h"

#include <stdexcept>

namespace orion::gpu {

struct Device::Impl {
    id<MTLDevice>       device = nil;
    id<MTLCommandQueue> queue  = nil;
};

Device::Device() : impl_(std::make_unique<Impl>()) {}

Device::~Device() = default;

std::unique_ptr<Device> Device::create() {
    auto dev = std::unique_ptr<Device>(new Device());

    dev->impl_->device = MTLCreateSystemDefaultDevice();
    if (dev->impl_->device == nil) {
        throw std::runtime_error("no Metal device available");
    }

    dev->impl_->queue = [dev->impl_->device newCommandQueue];
    if (dev->impl_->queue == nil) {
        throw std::runtime_error("could not create Metal command queue");
    }

    id<MTLDevice> d = dev->impl_->device;
    dev->info_ = DeviceInfo{
        .name                 = d.name.UTF8String ? d.name.UTF8String : "unknown",
        .recommendedWorkingSet = d.recommendedMaxWorkingSetSize,
        .maxBufferLength       = d.maxBufferLength,
        .unifiedMemory         = static_cast<bool>(d.hasUnifiedMemory),
        .supportsApple7        = static_cast<bool>([d supportsFamily:MTLGPUFamilyApple7]),
    };

    return dev;
}

void* Device::rawDevice() const noexcept { return (__bridge void*)impl_->device; }
void* Device::rawQueue()  const noexcept { return (__bridge void*)impl_->queue; }

}  // namespace orion::gpu

// The Dawn backend's macOS half (#375, #376): Dawn on Metal, and the IOSurface
// that lets a Metal view draw an RGBA8 result with no copy.

#import <IOSurface/IOSurfaceObjC.h>
#import <Metal/Metal.h>

#include "gpu/Dawn.h"

#include <dawn/native/MetalBackend.h>

#include <stdexcept>

namespace orion::gpu::dawn {

Platform platform() {
    // SharedTextureMemoryIOSurface, SharedFenceMTLSharedEvent: the display hand-off.
    return {wgpu::BackendType::Metal,
            {wgpu::FeatureName::SharedTextureMemoryIOSurface,
             wgpu::FeatureName::SharedFenceMTLSharedEvent},
            /*robust=*/false};
}

void describe(Context& c, const wgpu::Limits& limits) {
    id<MTLDevice> mtl = ::dawn::native::metal::GetMTLDevice(c.device.Get());
    c.metalDevice = (__bridge void*)mtl;

    wgpu::AdapterInfo ai{};
    c.adapter.GetInfo(&ai);
    c.info = DeviceInfo{
        .name                  = "Dawn on " + std::string(ai.device.data ? ai.device.data : "",
                                                          ai.device.data ? ai.device.length : 0),
        .recommendedWorkingSet = mtl ? mtl.recommendedMaxWorkingSetSize : 0,
        .maxBufferLength       = limits.maxBufferSize,
        .unifiedMemory         = mtl ? static_cast<bool>(mtl.hasUnifiedMemory) : false,
        .supportsApple7        = mtl ? static_cast<bool>([mtl supportsFamily:MTLGPUFamilyApple7]) : false,
    };
}

namespace {

/// The texture lives in `surface`, Dawn reaches it through `memory`, and
/// `alias` is Metal's view of the same bytes for a shell to draw (#357's
/// present.mm, the Swift canvas). Held in access for its whole life: safe
/// without fences because every engine submit is waited on the CPU before the
/// engine returns, and a shell reads only after that and finishes before the
/// next render (`shim_render` waits).
struct IOSurfaceShare final : DisplayShare {
    IOSurfaceRef              surface = nullptr;
    wgpu::SharedTextureMemory memory;
    wgpu::Texture             tex;
    id<MTLTexture>            alias = nil;

    ~IOSurfaceShare() override {
        if (memory && tex) {
            wgpu::SharedTextureMemoryEndAccessState end{};
            memory.EndAccess(tex, &end);
        }
        if (surface) CFRelease(surface);
    }
    void* handle() const noexcept override { return (__bridge void*)alias; }
};

}  // namespace

std::unique_ptr<DisplayShare> shareForDisplay(Context& ctx, std::uint32_t width,
                                              std::uint32_t height, wgpu::TextureUsage usage,
                                              wgpu::Texture& tex) {
    auto s = std::make_unique<IOSurfaceShare>();
    @autoreleasepool {
        NSDictionary* props = @{IOSurfacePropertyKeyWidth: @(width),
                                IOSurfacePropertyKeyHeight: @(height),
                                IOSurfacePropertyKeyBytesPerElement: @4,
                                IOSurfacePropertyKeyPixelFormat: @((uint32_t)'RGBA')};
        s->surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        if (!s->surface) throw std::runtime_error("Dawn: IOSurfaceCreate failed");

        wgpu::SharedTextureMemoryIOSurfaceDescriptor io{};
        io.ioSurface = s->surface;
        wgpu::SharedTextureMemoryDescriptor md{.nextInChain = &io};
        s->memory = ctx.device.ImportSharedTextureMemory(&md);
        wgpu::TextureDescriptor td{.usage = usage,
                                   .size = {width, height, 1},
                                   .format = wgpu::TextureFormat::RGBA8Unorm};
        s->tex = s->memory.CreateTexture(&td);
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{};
        begin.initialized = false;
        if (!s->tex || s->memory.BeginAccess(s->tex, &begin) != wgpu::Status::Success) {
            throw std::runtime_error("Dawn: IOSurface texture could not begin access");
        }

        id<MTLDevice> mtl = (__bridge id<MTLDevice>)ctx.metalDevice;
        MTLTextureDescriptor* d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        s->alias = [mtl newTextureWithDescriptor:d iosurface:s->surface plane:0];
    }
    tex = s->tex;
    return s;
}

}  // namespace orion::gpu::dawn

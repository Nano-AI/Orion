// The Dawn backend's textures (#375): upload, readback, and the IOSurface
// that lets a Metal view draw an RGBA8 result with no copy.

#import <IOSurface/IOSurfaceObjC.h>
#import <Metal/Metal.h>

#include "gpu/Dawn.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace orion::gpu::dawn {
namespace {

struct DawnTexture final : Texture::Impl {
    std::shared_ptr<Context> ctx;
    wgpu::Texture     tex;
    wgpu::TextureView view;
    std::size_t       bpp = 0;

    // The display path, RGBA8 only: the texture lives in `surface`, Dawn
    // reaches it through `memory`, and `alias` is Metal's view of the same
    // bytes for a shell to draw (#357's present.mm, the Swift canvas).
    IOSurfaceRef             surface = nullptr;
    wgpu::SharedTextureMemory memory;
    id<MTLTexture>           alias = nil;

    ~DawnTexture() override {
        if (memory) {
            wgpu::SharedTextureMemoryEndAccessState end{};
            memory.EndAccess(tex, &end);
        }
        if (surface) CFRelease(surface);
    }

    void upload(const void* src, std::size_t bytesPerRow, std::uint32_t width,
                std::uint32_t height) override {
        wgpu::TexelCopyTextureInfo dst{.texture = tex};
        wgpu::TexelCopyBufferLayout layout{.bytesPerRow = static_cast<std::uint32_t>(bytesPerRow),
                                           .rowsPerImage = height};
        wgpu::Extent3D size{width, height, 1};
        // The last row need not be padded to bytesPerRow; Metal reads no more.
        const std::size_t bytes = bytesPerRow * (height - 1) + width * bpp;
        ctx->queue.WriteTexture(&dst, src, bytes, &layout, &size);
        ctx->check("Dawn texture upload");
    }

    void download(void* dst, std::size_t bytesPerRow, std::uint32_t x, std::uint32_t y,
                  std::uint32_t width, std::uint32_t height) const override {
        // A buffer copy needs rows on 256 bytes; the caller's rows are packed.
        const std::size_t row   = width * bpp;
        const std::size_t pitch = (row + 255) / 256 * 256;
        wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
                                  .size = pitch * height};
        wgpu::Buffer staging = ctx->device.CreateBuffer(&bd);

        wgpu::TexelCopyTextureInfo from{.texture = tex, .origin = {x, y, 0}};
        wgpu::TexelCopyBufferInfo to{.layout = {.bytesPerRow = static_cast<std::uint32_t>(pitch),
                                                .rowsPerImage = height},
                                     .buffer = staging};
        wgpu::Extent3D size{width, height, 1};
        wgpu::CommandEncoder enc = ctx->device.CreateCommandEncoder();
        enc.CopyTextureToBuffer(&from, &to, &size);
        wgpu::CommandBuffer cmd = enc.Finish();
        ctx->queue.Submit(1, &cmd);

        bool mapped = false;
        ctx->wait(staging.MapAsync(wgpu::MapMode::Read, 0, pitch * height,
                                   wgpu::CallbackMode::WaitAnyOnly,
                                   [&](wgpu::MapAsyncStatus st, wgpu::StringView) {
                                       mapped = st == wgpu::MapAsyncStatus::Success;
                                   }));
        ctx->check("Dawn texture readback");
        if (!mapped) throw std::runtime_error("Dawn texture readback: map failed");
        const auto* src = static_cast<const std::uint8_t*>(staging.GetConstMappedRange(0, pitch * height));
        auto* out = static_cast<std::uint8_t*>(dst);
        for (std::uint32_t r = 0; r < height; ++r) {
            std::memcpy(out + r * bytesPerRow, src + r * pitch, row);
        }
        staging.Unmap();
    }

    void* raw() const noexcept override { return tex.Get(); }
    void* displayHandle() const noexcept override { return (__bridge void*)alias; }
};

wgpu::TextureUsage usageFor(PixelFormat) {
    // Every format the engine uses is storage-capable once TextureFormatsTier1
    // is on (DawnDevice.mm refuses a device without it).
    return wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding |
           wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst |
           wgpu::TextureUsage::RenderAttachment;
}

/// An RGBA8 texture in an IOSurface, as spikes/dawn-avalonia/ does it, held
/// in access for its whole life. Safe without fences because every engine
/// submit is waited on the CPU before the engine returns, and a shell reads
/// only after that and finishes before the next render (`shim_render` waits).
void shareThroughIOSurface(DawnTexture& t, std::uint32_t width, std::uint32_t height) {
    @autoreleasepool {
        NSDictionary* props = @{IOSurfacePropertyKeyWidth: @(width),
                                IOSurfacePropertyKeyHeight: @(height),
                                IOSurfacePropertyKeyBytesPerElement: @4,
                                IOSurfacePropertyKeyPixelFormat: @((uint32_t)'RGBA')};
        t.surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        if (!t.surface) throw std::runtime_error("Dawn: IOSurfaceCreate failed");

        wgpu::SharedTextureMemoryIOSurfaceDescriptor io{};
        io.ioSurface = t.surface;
        wgpu::SharedTextureMemoryDescriptor md{.nextInChain = &io};
        t.memory = t.ctx->device.ImportSharedTextureMemory(&md);
        wgpu::TextureDescriptor td{.usage = usageFor(PixelFormat::RGBA8Unorm),
                                   .size = {width, height, 1},
                                   .format = wgpu::TextureFormat::RGBA8Unorm};
        t.tex = t.memory.CreateTexture(&td);
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{};
        begin.initialized = false;
        if (!t.tex || t.memory.BeginAccess(t.tex, &begin) != wgpu::Status::Success) {
            t.ctx->check("Dawn: IOSurface texture");
            throw std::runtime_error("Dawn: IOSurface texture could not begin access");
        }

        id<MTLDevice> mtl = (__bridge id<MTLDevice>)t.ctx->metalDevice;
        MTLTextureDescriptor* d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        t.alias = [mtl newTextureWithDescriptor:d iosurface:t.surface plane:0];
    }
}

}  // namespace

std::unique_ptr<Texture::Impl> makeTexture(std::shared_ptr<Context> ctx, std::uint32_t width,
                                           std::uint32_t height, PixelFormat format) {
    auto t = std::make_unique<DawnTexture>();
    t->ctx = std::move(ctx);
    t->bpp = bytesPerPixel(format);
    if (format == PixelFormat::RGBA8Unorm) {
        shareThroughIOSurface(*t, width, height);
    } else {
        wgpu::TextureDescriptor td{.usage = usageFor(format),
                                   .size = {width, height, 1},
                                   .format = toWgpu(format)};
        t->tex = t->ctx->device.CreateTexture(&td);
    }
    t->ctx->check("Dawn texture allocation");
    if (!t->tex) throw std::runtime_error("Dawn texture allocation failed");
    t->view = t->tex.CreateView();
    return t;
}

const wgpu::TextureView& viewOf(const Texture& t) {
    auto* d = dynamic_cast<const DawnTexture*>(&t.impl());
    if (!d) throw std::runtime_error("a Metal texture was bound on a Dawn device");
    return d->view;
}

const wgpu::Texture& textureOf(const Texture& t) {
    auto* d = dynamic_cast<const DawnTexture*>(&t.impl());
    if (!d) throw std::runtime_error("a Metal texture was bound on a Dawn device");
    return d->tex;
}

}  // namespace orion::gpu::dawn

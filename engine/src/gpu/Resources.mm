#import <Metal/Metal.h>

#include "gpu/Resources.h"

#include <algorithm>
#include <stdexcept>

namespace orion::gpu {
namespace {

MTLPixelFormat toMetal(PixelFormat f) {
    switch (f) {
        case PixelFormat::R16Uint:     return MTLPixelFormatR16Uint;
        case PixelFormat::R16Float:    return MTLPixelFormatR16Float;
        case PixelFormat::R32Float:    return MTLPixelFormatR32Float;
        case PixelFormat::RG32Float:   return MTLPixelFormatRG32Float;
        case PixelFormat::RGBA16Float: return MTLPixelFormatRGBA16Float;
        case PixelFormat::RGBA32Float: return MTLPixelFormatRGBA32Float;
        case PixelFormat::RGBA8Unorm:  return MTLPixelFormatRGBA8Unorm;
    }
    throw std::runtime_error("unknown pixel format");
}

id<MTLDevice> dev(Device& d) { return (__bridge id<MTLDevice>)d.rawDevice(); }

}  // namespace

std::size_t bytesPerPixel(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::R16Uint:     return 2;
        case PixelFormat::R16Float:    return 2;
        case PixelFormat::R32Float:    return 4;
        case PixelFormat::RG32Float:   return 8;
        case PixelFormat::RGBA16Float: return 8;
        case PixelFormat::RGBA32Float: return 16;
        case PixelFormat::RGBA8Unorm:  return 4;
    }
    return 0;
}

const char* formatName(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::R16Uint:     return "r16u";
        case PixelFormat::R16Float:    return "r16f";
        case PixelFormat::R32Float:    return "r32f";
        case PixelFormat::RG32Float:   return "rg32f";
        case PixelFormat::RGBA16Float: return "rgba16f";
        case PixelFormat::RGBA32Float: return "rgba32f";
        case PixelFormat::RGBA8Unorm:  return "rgba8";
    }
    return "?";
}

// ── Texture ────────────────────────────────────────────────────────────────

struct Texture::Impl { id<MTLTexture> tex = nil; };

Texture::~Texture() = default;

std::unique_ptr<Texture> Texture::create(Device& device, std::uint32_t width,
                                         std::uint32_t height, PixelFormat format) {
    if (width == 0 || height == 0) throw std::runtime_error("zero-sized texture");

    auto t = std::unique_ptr<Texture>(new Texture());
    t->impl_   = std::make_unique<Impl>();
    t->width_  = width;
    t->height_ = height;
    t->format_ = format;

    // ⚠ **Pool discipline lives in this file and nowhere above it.** ARC is on
    // here, so strong members are released correctly — but ARC does not *drain*
    // autorelease pools, and nothing in this engine turns a run loop. Every
    // autoreleased Metal temporary therefore accumulated for the life of the
    // process: measured at 393 B per texture, 1.6 KB per library load, 2.3 KB
    // per kernel, adding to **~0.64 MB per DevelopPipeline built** and so ~1.3
    // MB per photo opened, since a photo builds two graphs.
    //
    // ⚠ `leaks` reports zero for this, and correctly: the blocks are still
    // *reachable* from an undrained pool, so they are not leaks by its
    // definition. Only a footprint measurement finds it. The app is shielded by
    // accident today — `pushAndRender` runs on the main thread, whose run loop
    // drains each cycle — but the bench, the tests and the scenario runner are
    // not, and moving a photo open to a background queue would expose it.
    //
    // Safe around a factory: ARC retains into the returned object's strong
    // member before the pool drains.
    @autoreleasepool {
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:toMetal(format)
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    // Shared storage costs nothing on unified memory and lets the CPU read
    // results back without a blit. Revisit only if a discrete GPU ever matters.
    desc.storageMode = MTLStorageModeShared;

    t->impl_->tex = [dev(device) newTextureWithDescriptor:desc];
    }
    if (t->impl_->tex == nil) throw std::runtime_error("texture allocation failed");

    return t;
}

void Texture::upload(const void* src, std::size_t bytesPerRow) {
    [impl_->tex replaceRegion:MTLRegionMake2D(0, 0, width_, height_)
                  mipmapLevel:0
                    withBytes:src
                  bytesPerRow:bytesPerRow];
}

void Texture::download(void* dst, std::size_t bytesPerRow) const {
    [impl_->tex getBytes:dst
             bytesPerRow:bytesPerRow
              fromRegion:MTLRegionMake2D(0, 0, width_, height_)
             mipmapLevel:0];
}

void Texture::download(void* dst, std::size_t bytesPerRow,
                       std::uint32_t width, std::uint32_t height) const {
    [impl_->tex getBytes:dst
             bytesPerRow:bytesPerRow
              fromRegion:MTLRegionMake2D(0, 0, std::min(width, width_),
                                         std::min(height, height_))
             mipmapLevel:0];
}

void Texture::readPixel(std::uint32_t x, std::uint32_t y, void* dst) const {
    if (x >= width_ || y >= height_) return;
    [impl_->tex getBytes:dst
             bytesPerRow:bytesPerPixel(format_)
              fromRegion:MTLRegionMake2D(x, y, 1, 1)
             mipmapLevel:0];
}

void* Texture::raw() const noexcept { return (__bridge void*)impl_->tex; }

// ── Library ────────────────────────────────────────────────────────────────

struct Library::Impl { id<MTLLibrary> lib = nil; };

Library::~Library() = default;

std::unique_ptr<Library> Library::createFromFile(Device& device, const std::string& path) {
    auto l = std::unique_ptr<Library>(new Library());
    l->impl_ = std::make_unique<Impl>();

    // See the note in Texture::create. `@(path)`, the NSURL and the NSError are
    // all autoreleased; the message below is copied into a std::string before
    // the pool drains, and @autoreleasepool unwinds correctly on a throw.
    @autoreleasepool {
        NSError* err = nil;
        NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
        l->impl_->lib = [dev(device) newLibraryWithURL:url error:&err];

        if (l->impl_->lib == nil) {
            const char* why = err ? err.localizedDescription.UTF8String : "unknown";
            throw std::runtime_error("could not load metallib at " + path + ": " + why);
        }
    }
    return l;
}

void* Library::raw() const noexcept { return (__bridge void*)impl_->lib; }

// ── Kernel ─────────────────────────────────────────────────────────────────

struct Kernel::Impl { id<MTLComputePipelineState> pso = nil; };

Kernel::~Kernel() = default;

std::unique_ptr<Kernel> Kernel::create(Device& device, Library& library,
                                       const std::string& entryPoint) {
    auto k = std::unique_ptr<Kernel>(new Kernel());
    k->impl_ = std::make_unique<Impl>();
    k->name_ = entryPoint;

    // ⚠ The pool has to enclose the reflection loop below, not just the
    // pipeline call: `refl` is autoreleased and is read after the pso exists.
    @autoreleasepool {
    id<MTLLibrary> lib = (__bridge id<MTLLibrary>)library.raw();
    id<MTLFunction> fn = [lib newFunctionWithName:@(entryPoint.c_str())];
    if (fn == nil) throw std::runtime_error("no kernel named '" + entryPoint + "' in library");

    NSError* err = nil;
    // ⚠ Reflection, not the plain overload, so `textureSlotsUsed` can be
    // answered — see the note on that accessor for what it prevents.
    MTLComputePipelineReflection* refl = nil;
    k->impl_->pso = [dev(device) newComputePipelineStateWithFunction:fn
                                                             options:MTLPipelineOptionBindingInfo
                                                          reflection:&refl
                                                               error:&err];
    if (k->impl_->pso == nil) {
        const char* why = err ? err.localizedDescription.UTF8String : "unknown";
        throw std::runtime_error("pipeline for '" + entryPoint + "' failed: " + why);
    }

    // One past the highest texture index the compiled kernel refers to.
    //
    // ⚠ Deliberately the highest *used* index rather than the declared argument
    // count. An argument the shader never reads can be eliminated, and would
    // otherwise make this refuse a binding that is in fact complete.
    for (id<MTLBinding> b in refl.bindings) {
        if (b.type == MTLBindingTypeTexture) {
            k->textureSlots_ = std::max(k->textureSlots_,
                                        static_cast<std::uint32_t>(b.index) + 1u);
        }
    }

    k->execWidth_  = static_cast<std::uint32_t>(k->impl_->pso.threadExecutionWidth);
    k->maxThreads_ = static_cast<std::uint32_t>(k->impl_->pso.maxTotalThreadsPerThreadgroup);
    }
    return k;
}

void* Kernel::raw() const noexcept { return (__bridge void*)impl_->pso; }

// ── CommandBuffer ──────────────────────────────────────────────────────────

struct CommandBuffer::Impl {
    id<MTLCommandBuffer>        cb  = nil;
    id<MTLComputeCommandEncoder> enc = nil;
    double gpuMs = 0.0;
};

CommandBuffer::CommandBuffer(Device& device) : impl_(std::make_unique<Impl>()) {
    // ⚠ Both are autoreleased and both outlive this constructor — which is
    // fine, because `Impl`'s members are `__strong` and ARC retains into them
    // before the pool drains. This is the per-frame site: one command buffer
    // and one encoder per render, ~0.22 KB a frame left behind without it.
    @autoreleasepool {
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)device.rawQueue();
        impl_->cb = [queue commandBuffer];
        if (impl_->cb == nil) throw std::runtime_error("could not create command buffer");
        impl_->enc = [impl_->cb computeCommandEncoder];
    }
}

CommandBuffer::~CommandBuffer() = default;

void CommandBuffer::dispatch(const Kernel& kernel,
                             const std::vector<const Texture*>& textures,
                             const void* params, std::size_t paramBytes,
                             std::uint32_t width, std::uint32_t height) {
    id<MTLComputePipelineState> pso = (__bridge id<MTLComputePipelineState>)kernel.raw();
    [impl_->enc setComputePipelineState:pso];

    for (std::size_t i = 0; i < textures.size(); ++i) {
        [impl_->enc setTexture:(__bridge id<MTLTexture>)textures[i]->raw() atIndex:i];
    }
    if (params != nullptr && paramBytes > 0) {
        [impl_->enc setBytes:params length:paramBytes atIndex:0];
    }

    // Shaders declare [numthreads(16,16,1)]; keep the host side in step.
    const MTLSize group = MTLSizeMake(16, 16, 1);
    const MTLSize grid  = MTLSizeMake((width  + 15) / 16, (height + 15) / 16, 1);
    [impl_->enc dispatchThreadgroups:grid threadsPerThreadgroup:group];
}

void CommandBuffer::commitAndWait() {
    [impl_->enc endEncoding];
    [impl_->cb commit];
    [impl_->cb waitUntilCompleted];

    @autoreleasepool {
        if (impl_->cb.status == MTLCommandBufferStatusError) {
            const char* why = impl_->cb.error
                ? impl_->cb.error.localizedDescription.UTF8String : "unknown";
            throw std::runtime_error(std::string("GPU work failed: ") + why);
        }
    }
    impl_->gpuMs = (impl_->cb.GPUEndTime - impl_->cb.GPUStartTime) * 1000.0;
}

double CommandBuffer::gpuMilliseconds() const noexcept { return impl_->gpuMs; }

}  // namespace orion::gpu

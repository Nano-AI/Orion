#import <Metal/Metal.h>

#include "gpu/Backend.h"

#include <algorithm>
#include <stdexcept>

namespace orion::gpu {
namespace {

MTLPixelFormat toMetal(PixelFormat f) {
    switch (f) {
        case PixelFormat::R16Uint:     return MTLPixelFormatR16Uint;
        case PixelFormat::R16Float:    return MTLPixelFormatR16Float;
        case PixelFormat::R32Float:    return MTLPixelFormatR32Float;
        case PixelFormat::RG16Float:   return MTLPixelFormatRG16Float;
        case PixelFormat::RG32Float:   return MTLPixelFormatRG32Float;
        case PixelFormat::RGBA16Float: return MTLPixelFormatRGBA16Float;
        case PixelFormat::RGBA32Float: return MTLPixelFormatRGBA32Float;
        case PixelFormat::RGBA8Unorm:  return MTLPixelFormatRGBA8Unorm;
    }
    throw std::runtime_error("unknown pixel format");
}

// ── Texture ────────────────────────────────────────────────────────────────

struct MetalTexture final : Texture::Impl {
    id<MTLTexture> tex = nil;
    id<MTLBuffer>  buffer = nil;   // the storage of a linear texture, else nil

    void upload(const void* src, std::size_t bytesPerRow, std::uint32_t width,
                std::uint32_t height) override {
        [tex replaceRegion:MTLRegionMake2D(0, 0, width, height)
               mipmapLevel:0
                 withBytes:src
               bytesPerRow:bytesPerRow];
    }

    void download(void* dst, std::size_t bytesPerRow, std::uint32_t x, std::uint32_t y,
                  std::uint32_t width, std::uint32_t height) const override {
        [tex getBytes:dst
          bytesPerRow:bytesPerRow
           fromRegion:MTLRegionMake2D(x, y, width, height)
          mipmapLevel:0];
    }

    void* raw() const noexcept override { return (__bridge void*)tex; }
    void* displayHandle() const noexcept override { return raw(); }
};

}  // namespace

std::unique_ptr<Texture::Impl> makeMetalTexture(void* device, std::uint32_t width,
                                                std::uint32_t height, PixelFormat format,
                                                bool linear) {
    id<MTLDevice> dev = (__bridge id<MTLDevice>)device;
    auto t = std::make_unique<MetalTexture>();

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

    if (linear) {
        // Rows padded to what the device requires of a buffer-backed texture.
        const NSUInteger align =
            [dev minimumLinearTextureAlignmentForPixelFormat:desc.pixelFormat];
        const NSUInteger row = width * bytesPerPixel(format);
        const NSUInteger pitch = (row + align - 1) / align * align;
        t->buffer = [dev newBufferWithLength:pitch * height
                                                    options:MTLResourceStorageModeShared];
        t->tex = [t->buffer newTextureWithDescriptor:desc
                                                            offset:0
                                                       bytesPerRow:pitch];
    } else {
        t->tex = [dev newTextureWithDescriptor:desc];
    }
    }
    if (t->tex == nil) throw std::runtime_error("texture allocation failed");

    return t;
}

// ── Library ────────────────────────────────────────────────────────────────

namespace {
struct MetalLibrary final : Library::Impl {
    id<MTLLibrary> lib = nil;
    void* raw() const noexcept override { return (__bridge void*)lib; }
};
}  // namespace

std::unique_ptr<Library::Impl> makeMetalLibrary(void* device, const std::string& path) {
    auto l = std::make_unique<MetalLibrary>();

    // See the note in makeMetalTexture. `@(path)`, the NSURL and the NSError are
    // all autoreleased; the message below is copied into a std::string before
    // the pool drains, and @autoreleasepool unwinds correctly on a throw.
    @autoreleasepool {
        NSError* err = nil;
        NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
        l->lib = [(__bridge id<MTLDevice>)device newLibraryWithURL:url error:&err];

        if (l->lib == nil) {
            const char* why = err ? err.localizedDescription.UTF8String : "unknown";
            throw std::runtime_error("could not load metallib at " + path + ": " + why);
        }
    }
    return l;
}

// ── Kernel ─────────────────────────────────────────────────────────────────

namespace {
struct MetalKernel final : Kernel::Impl {
    id<MTLComputePipelineState> pso = nil;
    void* raw() const noexcept override { return (__bridge void*)pso; }
};
}  // namespace

std::unique_ptr<Kernel::Impl> makeMetalKernel(void* device, Library& library,
                                              const std::string& entryPoint) {
    auto k = std::make_unique<MetalKernel>();

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
    k->pso = [(__bridge id<MTLDevice>)device newComputePipelineStateWithFunction:fn
                                                             options:MTLPipelineOptionBindingInfo
                                                          reflection:&refl
                                                               error:&err];
    if (k->pso == nil) {
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
            k->textureSlots = std::max(k->textureSlots,
                                        static_cast<std::uint32_t>(b.index) + 1u);
        }
    }

    k->execWidth  = static_cast<std::uint32_t>(k->pso.threadExecutionWidth);
    k->maxThreads = static_cast<std::uint32_t>(k->pso.maxTotalThreadsPerThreadgroup);
    }
    return k;
}

// ── CommandBuffer ──────────────────────────────────────────────────────────

namespace {

struct MetalCommandBuffer final : CommandBuffer::Impl {
    id<MTLCommandBuffer>         cb  = nil;
    id<MTLComputeCommandEncoder> enc = nil;

    void dispatch(const Kernel& kernel, const std::vector<const Texture*>& textures,
                  const void* params, std::size_t paramBytes,
                  std::uint32_t width, std::uint32_t height) override {
        id<MTLComputePipelineState> pso = (__bridge id<MTLComputePipelineState>)kernel.raw();
        [enc setComputePipelineState:pso];

        for (std::size_t i = 0; i < textures.size(); ++i) {
            [enc setTexture:(__bridge id<MTLTexture>)textures[i]->raw() atIndex:i];
        }
        if (params != nullptr && paramBytes > 0) {
            [enc setBytes:params length:paramBytes atIndex:0];
        }

        // Shaders declare [numthreads(16,16,1)]; keep the host side in step.
        const MTLSize group = MTLSizeMake(16, 16, 1);
        const MTLSize grid  = MTLSizeMake((width  + 15) / 16, (height + 15) / 16, 1);
        [enc dispatchThreadgroups:grid threadsPerThreadgroup:group];
    }

    void commitAndWait() override {
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];

        @autoreleasepool {
            if (cb.status == MTLCommandBufferStatusError) {
                const char* why = cb.error ? cb.error.localizedDescription.UTF8String : "unknown";
                throw std::runtime_error(std::string("GPU work failed: ") + why);
            }
        }
        gpuMs = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
    }
};

}  // namespace

std::unique_ptr<CommandBuffer::Impl> makeMetalCommandBuffer(void* queue) {
    auto c = std::make_unique<MetalCommandBuffer>();
    // ⚠ Both are autoreleased and both outlive this function — which is fine,
    // because the members are `__strong` and ARC retains into them before the
    // pool drains. This is the per-frame site: one command buffer and one
    // encoder per render, ~0.22 KB a frame left behind without it.
    @autoreleasepool {
        c->cb = [(__bridge id<MTLCommandQueue>)queue commandBuffer];
        if (c->cb == nil) throw std::runtime_error("could not create command buffer");
        c->enc = [c->cb computeCommandEncoder];
    }
    return c;
}

}  // namespace orion::gpu

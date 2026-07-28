#import <Metal/Metal.h>

#include "gpu/Resources.h"

#include <stdexcept>

namespace orion::gpu {
namespace {

MTLPixelFormat toMetal(PixelFormat f) {
    switch (f) {
        case PixelFormat::R16Uint:     return MTLPixelFormatR16Uint;
        case PixelFormat::R32Float:    return MTLPixelFormatR32Float;
        case PixelFormat::RGBA16Float: return MTLPixelFormatRGBA16Float;
        case PixelFormat::RGBA8Unorm:  return MTLPixelFormatRGBA8Unorm;
    }
    throw std::runtime_error("unknown pixel format");
}

id<MTLDevice> dev(Device& d) { return (__bridge id<MTLDevice>)d.rawDevice(); }

}  // namespace

std::size_t bytesPerPixel(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::R16Uint:     return 2;
        case PixelFormat::R32Float:    return 4;
        case PixelFormat::RGBA16Float: return 8;
        case PixelFormat::RGBA8Unorm:  return 4;
    }
    return 0;
}

const char* formatName(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::R16Uint:     return "r16u";
        case PixelFormat::R32Float:    return "r32f";
        case PixelFormat::RGBA16Float: return "rgba16f";
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

void* Texture::raw() const noexcept { return (__bridge void*)impl_->tex; }

// ── Library ────────────────────────────────────────────────────────────────

struct Library::Impl { id<MTLLibrary> lib = nil; };

Library::~Library() = default;

std::unique_ptr<Library> Library::createFromFile(Device& device, const std::string& path) {
    auto l = std::unique_ptr<Library>(new Library());
    l->impl_ = std::make_unique<Impl>();

    NSError* err = nil;
    NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
    l->impl_->lib = [dev(device) newLibraryWithURL:url error:&err];

    if (l->impl_->lib == nil) {
        const char* why = err ? err.localizedDescription.UTF8String : "unknown";
        throw std::runtime_error("could not load metallib at " + path + ": " + why);
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

    id<MTLLibrary> lib = (__bridge id<MTLLibrary>)library.raw();
    id<MTLFunction> fn = [lib newFunctionWithName:@(entryPoint.c_str())];
    if (fn == nil) throw std::runtime_error("no kernel named '" + entryPoint + "' in library");

    NSError* err = nil;
    k->impl_->pso = [dev(device) newComputePipelineStateWithFunction:fn error:&err];
    if (k->impl_->pso == nil) {
        const char* why = err ? err.localizedDescription.UTF8String : "unknown";
        throw std::runtime_error("pipeline for '" + entryPoint + "' failed: " + why);
    }

    k->execWidth_  = static_cast<std::uint32_t>(k->impl_->pso.threadExecutionWidth);
    k->maxThreads_ = static_cast<std::uint32_t>(k->impl_->pso.maxTotalThreadsPerThreadgroup);
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
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)device.rawQueue();
    impl_->cb = [queue commandBuffer];
    if (impl_->cb == nil) throw std::runtime_error("could not create command buffer");
    impl_->enc = [impl_->cb computeCommandEncoder];
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

    if (impl_->cb.status == MTLCommandBufferStatusError) {
        const char* why = impl_->cb.error ? impl_->cb.error.localizedDescription.UTF8String
                                          : "unknown";
        throw std::runtime_error(std::string("GPU work failed: ") + why);
    }
    impl_->gpuMs = (impl_->cb.GPUEndTime - impl_->cb.GPUStartTime) * 1000.0;
}

double CommandBuffer::gpuMilliseconds() const noexcept { return impl_->gpuMs; }

}  // namespace orion::gpu

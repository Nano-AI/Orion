// The engine's picture to Avalonia on the GPU, with no CPU copy (#317, decision #354).
//
// Each graph (1 preview, 2 full) has an IOSurface holding its picture at the
// level the canvas asked for: level 0 is the render as it is; level k is
// CanvasReduce.swift's chain, one 2x2 average then mip levels, k-1 levels down.
// present_frame fills it in one command buffer on its own queue:
//   reduce  the render into the chain (level >= 1 only)
//   wait    consumer >= what Avalonia signalled after its last read of this surface
//   copy    the render, or a level of the chain, into the surface
//   signal  producer = n
// Avalonia's UpdateWithTimelineSemaphoresAsync waits for producer n, snapshots
// the surface on the GPU and signals consumer n (Slices/Canvas/SharedPicture.cs).
// Nothing here maps, reads back or uploads a pixel. The engine's allocations,
// and the Swift app that shares them, are untouched: this reads the engine's
// output texture and writes only textures of its own.
//
// A plain C facade: no C++ exception or Objective-C object crosses it.
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceObjC.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>

struct Shim;
extern "C" void* shim_picture(Shim*, int which, int* w, int* h);
extern "C" void shim_hold(Shim*, void* commandBuffer);

namespace {
// CanvasReduce.swift's orionHalve, except that it clamps to the picture's own
// region rather than the square texture's, so an odd edge averages no dead texel.
const char* kHalve = R"(
#include <metal_stdlib>
using namespace metal;
kernel void orionHalve(texture2d<float, access::read>  src [[texture(0)]],
                       texture2d<float, access::write> dst [[texture(1)]],
                       constant uint2& valid [[buffer(0)]],
                       uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= dst.get_width() || gid.y >= dst.get_height()) return;
    const uint2 m = valid - 1;
    const uint2 p = gid * 2;
    const float4 a = src.read(min(p, m));
    const float4 b = src.read(min(p + uint2(1, 0), m));
    const float4 c = src.read(min(p + uint2(0, 1), m));
    const float4 d = src.read(min(p + uint2(1, 1), m));
    dst.write((a + b + c + d) * 0.25, gid);
}
)";

struct Slot {
    IOSurfaceRef surface = nullptr;
    id<MTLTexture> texture = nil;   // the surface as Metal sees it; Avalonia imports the surface
    id<MTLTexture> chain = nil;     // half the picture, with mip levels
    id<MTLTexture> base = nil;      // the chain's level 0 alone, for the kernel to write
    uint64_t released = 0;          // the consumer value that frees `surface`: its last hand-off
    int generation = 0;
};
}  // namespace

struct Present {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> halve = nil;
    id<MTLSharedEvent> producer = nil, consumer = nil;
    Slot slots[3];   // by graph
    uint64_t frame = 0;
    int generations = 0;
};

namespace {
// The slot's surface at w x h; a new one, with a new generation, if the size changed.
bool fit(Present* p, Slot& s, int w, int h) {
    if (s.texture && int(s.texture.width) == w && int(s.texture.height) == h) return true;
    if (s.surface) CFRelease(s.surface);   // Avalonia's import holds its own reference
    s.texture = nil;
    NSDictionary* props = @{IOSurfacePropertyKeyWidth: @(w), IOSurfacePropertyKeyHeight: @(h),
                            IOSurfacePropertyKeyBytesPerElement: @4,
                            IOSurfacePropertyKeyPixelFormat: @((uint32_t)'RGBA')};
    s.surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
    if (!s.surface) return false;
    auto* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                 width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    s.texture = [p->device newTextureWithDescriptor:d iosurface:s.surface plane:0];
    s.released = 0;   // nobody has read this surface yet
    s.generation = ++p->generations;
    return s.texture != nil;
}

// The slot's chain with a bw x bh base, new if the size changed. Private and
// renderable: generateMipmaps reduces by rendering.
bool chain(Present* p, Slot& s, int bw, int bh) {
    if (s.chain && int(s.chain.width) == bw && int(s.chain.height) == bh) return true;
    auto* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                 width:bw height:bh mipmapped:YES];
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
    d.storageMode = MTLStorageModePrivate;
    s.chain = [p->device newTextureWithDescriptor:d];
    s.base = [s.chain newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D
                                             levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)];
    return s.chain && s.base;
}
}  // namespace

extern "C" {

void present_destroy(Present* p) {
    if (!p) return;
    for (auto& s : p->slots) if (s.surface) CFRelease(s.surface);
    delete p;
}

// On the system default device, which is the engine's (engine/src/gpu/MetalDevice.mm).
Present* present_create(void) {
    @autoreleasepool {
        auto* p = new Present;
        p->device = MTLCreateSystemDefaultDevice();
        NSError* error = nil;
        id<MTLLibrary> lib = [p->device newLibraryWithSource:@(kHalve) options:nil error:&error];
        id<MTLFunction> fn = [lib newFunctionWithName:@"orionHalve"];
        if (fn) p->halve = [p->device newComputePipelineStateWithFunction:fn error:&error];
        p->queue = [p->device newCommandQueue];
        p->producer = [p->device newSharedEvent];
        p->consumer = [p->device newSharedEvent];
        if (p->halve && p->queue && p->producer && p->consumer) return p;
        std::fprintf(stderr, "present_create: %s\n", error ? error.localizedDescription.UTF8String : "no Metal");
        present_destroy(p);
        return nullptr;
    }
}

// The MTLSharedEvents, for Avalonia to import: it waits on the producer and signals the consumer.
void* present_producer(Present* p) { return (__bridge void*)p->producer; }
void* present_consumer(Present* p) { return (__bridge void*)p->consumer; }

// Copies `shim`'s last render of graph `which` into that graph's surface,
// `level` halvings down (clamped to the chain), on the GPU. Returns 1 with the
// surface, its generation (new whenever the surface is: import it again), its
// size and n, the producer value Avalonia waits for and the consumer value it
// must signal. 0 when there is nothing to present, or the render is not RGBA8
// (the wide output, a measurement mode): the caller copies through the CPU.
int present_frame(Present* p, Shim* shim, int which, int level, void** surface, int* generation,
                  int* width, int* height, uint64_t* value) {
    @autoreleasepool {
        int iw = 0, ih = 0;
        if (!p || which < 1 || which > 2) return 0;
        id<MTLTexture> src = (__bridge id<MTLTexture>)shim_picture(shim, which, &iw, &ih);
        if (!src || src.pixelFormat != MTLPixelFormatRGBA8Unorm || src.device != p->device) return 0;
        Slot& s = p->slots[which];
        const int bw = (iw + 1) / 2, bh = (ih + 1) / 2;   // the chain's base, as CanvasReduce rounds it
        if (level > 0 && (bw < 2 || bh < 2 || !chain(p, s, bw, bh))) level = 0;
        if (level > 0) level = std::min<int>(level, int(s.chain.mipmapLevelCount));
        const int w = level ? std::max(1, bw >> (level - 1)) : iw, h = level ? std::max(1, bh >> (level - 1)) : ih;
        if (!fit(p, s, w, h)) return 0;

        id<MTLCommandBuffer> cb = [p->queue commandBuffer];
        if (level > 0) {
            id<MTLComputeCommandEncoder> c = [cb computeCommandEncoder];
            const uint32_t valid[2] = {uint32_t(iw), uint32_t(ih)};
            [c setComputePipelineState:p->halve];
            [c setTexture:src atIndex:0];
            [c setTexture:s.base atIndex:1];
            [c setBytes:valid length:sizeof valid atIndex:0];
            [c dispatchThreadgroups:MTLSizeMake((bw + 15) / 16, (bh + 15) / 16, 1)
              threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            [c endEncoding];
        }
        // The hand-off: no write to the surface until Avalonia has read its last frame.
        if (s.released) [cb encodeWaitForEvent:p->consumer value:s.released];
        id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
        if (level > 1) [b generateMipmapsForTexture:s.chain];
        [b copyFromTexture:level ? s.chain : src sourceSlice:0 sourceLevel:level ? level - 1 : 0
              sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                 toTexture:s.texture destinationSlice:0 destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
        [b endEncoding];
        const uint64_t n = ++p->frame;
        [cb encodeSignalEvent:p->producer value:n];
        [cb commit];
        shim_hold(shim, (__bridge void*)cb);   // the engine's next render waits for this read
        s.released = n;
        *surface = s.surface;
        *generation = s.generation;
        *width = w;
        *height = h;
        *value = n;
        return 1;
    }
}

// A hand-off Avalonia will never signal (its import or update failed): frees
// the surface for the next one, which would otherwise wait on the GPU forever.
void present_release(Present* p, uint64_t value) {
    if (p && p->consumer.signaledValue < value) p->consumer.signaledValue = value;
}

}  // extern "C"

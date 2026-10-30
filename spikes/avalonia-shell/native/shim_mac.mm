// The shim's macOS half (#376): present.mm's hooks into a Shim, and SVG
// watermarks through NSImage. shim.cpp is the rest, on every platform.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <vector>
#include "shim.h"

// Waits until present.mm has read this engine's picture. Its copy waits on the
// GPU only for a hand-off Avalonia has already submitted (SharedPicture keeps
// one in flight), so this returns within a frame; in practice it is long done.
// ponytail: an export through orion_engine_export directly does not settle; a
// copy lands within a frame of its hand-off, long before a click. Route it here if that changes.
void shim_settle(Shim* s) {
    if (s->reader) [(__bridge id<MTLCommandBuffer>)s->reader.get() waitUntilCompleted];
    s->reader.reset();
}

extern "C" {

// The last render of `which` (what shim_render returned) and the region of it
// that holds the picture. The texture is square so a straightened frame fits;
// only the top-left region holds the picture (Engine+Render.swift:286):
// image_size for the full graph, preview_size for the preview, each its own
// graph's answer. Null when there is nothing to read. present.mm reads it too.
void* shim_picture(Shim* s, int which, int* w, int* h) {
    void* raw = which == 1 ? orion_engine_preview_texture(s->engine)
              : which == 3 ? orion_engine_region_texture(s->engine)
                           : orion_engine_output_texture(s->engine);
    if (!raw) return nullptr;
    id<MTLTexture> t = (__bridge id<MTLTexture>)raw;
    unsigned iw = 0, ih = 0;
    if (which == 1) orion_engine_preview_size(s->engine, &iw, &ih);
    else if (which == 3) orion_engine_region_size(s->engine, &iw, &ih);
    else orion_engine_image_size(s->engine, &iw, &ih);
    *w = int(std::min<NSUInteger>(t.width, iw));
    *h = int(std::min<NSUInteger>(t.height, ih));
    return *w > 0 && *h > 0 ? raw : nullptr;
}

// present.mm hands over the command buffer that reads this engine's picture.
void shim_hold(Shim* s, void* commandBuffer) {
    s->reader = commandBuffer ? std::shared_ptr<void>(const_cast<void*>(CFBridgingRetain((__bridge id)commandBuffer)), CFRelease)
                              : nullptr;
}

// An SVG watermark's shape, through NSImage, as WatermarkRaster.swift's Art
// draws it (SVG is native since macOS 10.15). The only SVG rasterizer .NET has
// under a licence Orion may ship is none: Svg.Skia rests on SVG.NET, MS-PL.
// ponytail: macOS only; a Windows or Linux port needs its own rasterizer here.
static NSImage* svg_image(const uint8_t* bytes, int length) {
    if (!bytes || length <= 0) return nil;
    // An Objective-C exception must not cross the C facade: a file that makes AppKit throw is no image.
    @try {
        NSImage* image = [[NSImage alloc] initWithData:[NSData dataWithBytes:bytes length:NSUInteger(length)]];
        return image && image.size.width > 0 && image.size.height > 0 ? image : nil;
    } @catch (NSException*) {
        return nil;
    }
}

// Its natural size. 0 when it is not an image NSImage can read.
int shim_svg_size(const uint8_t* bytes, int length, double* w, double* h) {
    @autoreleasepool {
        NSImage* image = svg_image(bytes, length);
        if (!image) return 0;
        *w = image.size.width;
        *h = image.size.height;
        return 1;
    }
}

// Drawn `w`×`h`, centred at (cx, cy) of a bw×bh tile (y down), turned `angle`
// radians counter-clockwise as the viewer sees it; its alpha into `alpha`,
// one byte a pixel, rows top first. RGBA, because NSImage will not draw into
// an alpha-only context.
int shim_svg_coverage(const uint8_t* bytes, int length, uint8_t* alpha, int bw, int bh,
                      double cx, double cy, double w, double h, double angle) {
    if (!alpha || bw <= 0 || bh <= 0) return 0;
    @autoreleasepool {
        NSImage* image = svg_image(bytes, length);
        if (!image) return 0;
        std::vector<uint8_t> tile(size_t(bw) * size_t(bh) * 4);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(tile.data(), size_t(bw), size_t(bh), 8, size_t(bw) * 4, space,
                                                 kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(space);
        if (!ctx) return 0;
        // CoreGraphics is y-up and the tile is y-down: flip the centre, then turn.
        CGContextTranslateCTM(ctx, cx, bh - cy);
        CGContextRotateCTM(ctx, angle);
        bool drawn = true;
        [NSGraphicsContext saveGraphicsState];
        @try {
            NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
            [image drawInRect:NSMakeRect(-w / 2, -h / 2, w, h)];
        } @catch (NSException*) {
            drawn = false;   // 0 covers nothing; an exception past here would end the process
        }
        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(ctx);
        if (!drawn) return 0;
        for (size_t i = 0; i < size_t(bw) * size_t(bh); ++i) alpha[i] = tile[i * 4 + 3];
        return 1;
    }
}

}  // extern "C"

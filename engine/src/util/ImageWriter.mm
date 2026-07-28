#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "util/ImageWriter.h"

#include <stdexcept>

namespace orion::util {

void writePng(const std::string& path, const std::uint8_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow) {
    @autoreleasepool {
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        if (space == nullptr) throw std::runtime_error("could not create colour space");

        CGContextRef ctx = CGBitmapContextCreate(
            const_cast<std::uint8_t*>(rgba), width, height, 8, bytesPerRow, space,
            static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) |
                kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if (ctx == nullptr) throw std::runtime_error("could not create bitmap context");

        CGImageRef image = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
        if (image == nullptr) throw std::runtime_error("could not create image");

        NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
        CGImageDestinationRef dest = CGImageDestinationCreateWithURL(
            (__bridge CFURLRef)url, CFSTR("public.png")  /* UTTypePNG, without pulling in the framework */, 1, nullptr);
        if (dest == nullptr) {
            CGImageRelease(image);
            throw std::runtime_error("could not create PNG destination: " + path);
        }

        CGImageDestinationAddImage(dest, image, nullptr);
        const bool ok = CGImageDestinationFinalize(dest);

        CFRelease(dest);
        CGImageRelease(image);

        if (!ok) throw std::runtime_error("could not write PNG: " + path);
    }
}

}  // namespace orion::util

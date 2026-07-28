#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "util/ImageWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace orion::util {
namespace {

// The UTI strings directly, rather than importing UniformTypeIdentifiers just
// for three constants.
CFStringRef uti(ImageFormat f) {
    switch (f) {
        case ImageFormat::Png:  return CFSTR("public.png");
        case ImageFormat::Jpeg: return CFSTR("public.jpeg");
        case ImageFormat::Tiff: return CFSTR("public.tiff");
    }
    return CFSTR("public.jpeg");
}

/// RAII for the CoreFoundation handles below. Without it every early throw
/// leaks, and there are several paths that can throw.
template <class T>
struct CFHolder {
    T ref = nullptr;
    explicit CFHolder(T r) : ref(r) {}
    ~CFHolder() { if (ref) CFRelease(ref); }
    CFHolder(const CFHolder&)            = delete;
    CFHolder& operator=(const CFHolder&) = delete;
    explicit operator bool() const { return ref != nullptr; }
};

/// Sixteen bits per component.
///
/// PNG and TIFF carry that through; JPEG does not and CoreGraphics quantises on
/// the way out, which is correct — the point is that the depth survives as far
/// as the container allows rather than being thrown away at the graph's edge.
CGImageRef makeImage(const std::uint16_t* rgba, std::uint32_t width,
                     std::uint32_t height, std::size_t bytesPerRow) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (space == nullptr) throw std::runtime_error("could not create color space");

    // Little-endian, because that is how the sixteen-bit samples were written.
    CGContextRef ctx = CGBitmapContextCreate(
        const_cast<std::uint16_t*>(rgba), width, height, 16, bytesPerRow, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    CGColorSpaceRelease(space);
    if (ctx == nullptr) throw std::runtime_error("could not create bitmap context");

    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (image == nullptr) throw std::runtime_error("could not create image");
    return image;
}

/// Returns a scaled copy, or nullptr when no resize is needed.
///
/// Resampling happens in CoreGraphics rather than on the GPU: export is not on
/// the interaction path, and a correctly filtered downscale matters more here
/// than shaving milliseconds.
CGImageRef resample(CGImageRef source, std::uint32_t maxDimension) {
    const std::size_t w = CGImageGetWidth(source);
    const std::size_t h = CGImageGetHeight(source);
    const std::size_t longest = std::max(w, h);
    if (maxDimension == 0 || longest <= maxDimension) return nullptr;

    const double scale = static_cast<double>(maxDimension) / static_cast<double>(longest);
    // Round rather than truncate: asking for 2048 px and getting 2047 is the
    // kind of detail that makes software feel sloppy.
    const std::size_t nw = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(w * scale)));
    const std::size_t nh = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(h * scale)));

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(
        nullptr, nw, nh, 8, 0, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (ctx == nullptr) throw std::runtime_error("could not create resize context");

    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(nw),
                                       static_cast<CGFloat>(nh)), source);

    CGImageRef scaled = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (scaled == nullptr) throw std::runtime_error("could not resize image");
    return scaled;
}

}  // namespace

ImageFormat formatForPath(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return ImageFormat::Jpeg;

    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == "png") return ImageFormat::Png;
    if (ext == "tif" || ext == "tiff") return ImageFormat::Tiff;
    return ImageFormat::Jpeg;
}

void writeImage(const std::string& path, const std::uint16_t* rgba,
                std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow,
                const ExportOptions& options) {
    @autoreleasepool {
        CFHolder<CGImageRef> full(makeImage(rgba, width, height, bytesPerRow));
        CFHolder<CGImageRef> scaled(resample(full.ref, options.maxDimension));
        CGImageRef image = scaled ? scaled.ref : full.ref;

        NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
        CFHolder<CGImageDestinationRef> dest(CGImageDestinationCreateWithURL(
            (__bridge CFURLRef)url, uti(options.format), 1, nullptr));
        if (!dest) throw std::runtime_error("could not create destination: " + path);

        NSDictionary* props = @{
            (__bridge NSString*)kCGImageDestinationLossyCompressionQuality :
                @(std::clamp(options.quality, 0.0f, 1.0f))
        };
        CGImageDestinationAddImage(dest.ref, image, (__bridge CFDictionaryRef)props);

        if (!CGImageDestinationFinalize(dest.ref)) {
            throw std::runtime_error("could not write: " + path);
        }
    }
}

std::size_t encodedSize(const std::uint16_t* rgba,
                        std::uint32_t width, std::uint32_t height,
                        std::size_t bytesPerRow, const ExportOptions& options) {
    @autoreleasepool {
        CFHolder<CGImageRef> full(makeImage(rgba, width, height, bytesPerRow));
        CFHolder<CGImageRef> scaled(resample(full.ref, options.maxDimension));
        CGImageRef image = scaled ? scaled.ref : full.ref;

        // To memory, not to a file. An estimate from bytes-per-pixel was off by
        // enough to be misleading — the whole point of the number is that you
        // can trust it before committing to the write.
        NSMutableData* data = [NSMutableData data];
        CFHolder<CGImageDestinationRef> dest(CGImageDestinationCreateWithData(
            (__bridge CFMutableDataRef)data, uti(options.format), 1, nullptr));
        if (!dest) return 0;

        NSDictionary* props = @{
            (__bridge NSString*)kCGImageDestinationLossyCompressionQuality :
                @(std::clamp(options.quality, 0.0f, 1.0f))
        };
        CGImageDestinationAddImage(dest.ref, image, (__bridge CFDictionaryRef)props);
        if (!CGImageDestinationFinalize(dest.ref)) return 0;

        return static_cast<std::size_t>(data.length);
    }
}

void writePng(const std::string& path, const std::uint16_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow) {
    writeImage(path, rgba, width, height, bytesPerRow, {ImageFormat::Png, 1.0f, 0});
}

}  // namespace orion::util

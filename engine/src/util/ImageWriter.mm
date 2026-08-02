#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "util/ImageWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <vector>

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

/// The CoreGraphics space for one of ours. Caller releases.
///
/// Named spaces rather than hand-built primaries: ColorSync's own definitions
/// are what every other application on the machine will read the file against,
/// and a matrix typed in here that disagreed with them by a rounding error
/// would be a cast nobody could trace.
CGColorSpaceRef colorSpace(ColorSpace s) {
    CFStringRef name = kCGColorSpaceSRGB;
    switch (s) {
        case ColorSpace::Srgb:      name = kCGColorSpaceSRGB;         break;
        case ColorSpace::DisplayP3: name = kCGColorSpaceDisplayP3;    break;
        case ColorSpace::AdobeRgb:  name = kCGColorSpaceAdobeRGB1998; break;
    }
    CGColorSpaceRef space = CGColorSpaceCreateWithName(name);
    if (space == nullptr) throw std::runtime_error("could not create color space");
    return space;
}

/// The EXIF, TIFF and GPS blocks of `source`, ready to hand to a destination.
///
/// ImageIO reads them straight out of the RAW container, which is why this is
/// three dozen lines rather than a dependency: exiv2 is GPL and would decide
/// Orion's license for it (DECISIONS #10).
///
/// Orientation and the pixel dimensions are dropped deliberately. They describe
/// the RAW, and the export has already been rotated, cropped and possibly
/// resized — copying them over would tell every viewer to turn the picture
/// again, which is the one metadata bug users notice immediately.
NSMutableDictionary* metadata(const std::string& source, int rating, Metadata policy) {
    NSMutableDictionary* out = [NSMutableDictionary dictionary];

    if (!source.empty() && policy != Metadata::None) {
        NSURL* url = [NSURL fileURLWithPath:@(source.c_str())];
        CFHolder<CGImageSourceRef> src(
            CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr));
        if (src) {
            CFHolder<CFDictionaryRef> all(
                CGImageSourceCopyPropertiesAtIndex(src.ref, 0, nullptr));
            if (all) {
                NSDictionary* props = (__bridge NSDictionary*)all.ref;
                for (NSString* key in @[(__bridge NSString*)kCGImagePropertyExifDictionary,
                                        (__bridge NSString*)kCGImagePropertyTIFFDictionary,
                                        (__bridge NSString*)kCGImagePropertyGPSDictionary,
                                        (__bridge NSString*)kCGImagePropertyExifAuxDictionary,
                                        (__bridge NSString*)kCGImagePropertyIPTCDictionary]) {
                    const bool isGps =
                        [key isEqualToString:(__bridge NSString*)kCGImagePropertyGPSDictionary];
                    if (isGps && policy != Metadata::All) continue;
                    if (NSDictionary* block = props[key]) {
                        out[key] = [block mutableCopy];
                    }
                }
            }
        }

        // ⚠️ GPS is not the only place a location hides. IPTC carries the
        // place in words — city, sub-location, province, country — and a photo
        // tagged in any cataloguing application has them filled in. Stripping
        // the coordinates and leaving "Sub-location: <the street>" behind is a
        // control that reads as honest and is not, which is worse than not
        // offering it.
        if (policy != Metadata::All) {
            NSMutableDictionary* iptc = out[(__bridge NSString*)kCGImagePropertyIPTCDictionary];
            for (NSString* key in @[(__bridge NSString*)kCGImagePropertyIPTCSubLocation,
                                    (__bridge NSString*)kCGImagePropertyIPTCCity,
                                    (__bridge NSString*)kCGImagePropertyIPTCProvinceState,
                                    (__bridge NSString*)kCGImagePropertyIPTCCountryPrimaryLocationCode,
                                    (__bridge NSString*)kCGImagePropertyIPTCCountryPrimaryLocationName,
                                    (__bridge NSString*)kCGImagePropertyIPTCContentLocationCode,
                                    (__bridge NSString*)kCGImagePropertyIPTCContentLocationName]) {
                [iptc removeObjectForKey:key];
            }
        }

        // The RAW's own orientation is already baked into the pixels by the
        // geometry node, and its dimensions are not the export's.
        NSMutableDictionary* tiff = out[(__bridge NSString*)kCGImagePropertyTIFFDictionary];
        [tiff removeObjectForKey:(__bridge NSString*)kCGImagePropertyTIFFOrientation];
        NSMutableDictionary* exif = out[(__bridge NSString*)kCGImagePropertyExifDictionary];
        [exif removeObjectForKey:(__bridge NSString*)kCGImagePropertyExifPixelXDimension];
        [exif removeObjectForKey:(__bridge NSString*)kCGImagePropertyExifPixelYDimension];

        // Say who developed it. The camera stays in TIFF Make and Model, which
        // is what a photographer is actually looking for.
        if (tiff == nil) {
            tiff = [NSMutableDictionary dictionary];
            out[(__bridge NSString*)kCGImagePropertyTIFFDictionary] = tiff;
        }
        tiff[(__bridge NSString*)kCGImagePropertyTIFFSoftware] = @"Orion";
    }

    // Even a stripped file says what developed it. That is not identifying —
    // and a file with no software tag at all reads as something to be
    // suspicious of.
    if (policy == Metadata::None) {
        NSMutableDictionary* tiff = [NSMutableDictionary dictionary];
        tiff[(__bridge NSString*)kCGImagePropertyTIFFSoftware] = @"Orion";
        out[(__bridge NSString*)kCGImagePropertyTIFFDictionary] = tiff;
    }

    if (rating >= 0) {
        NSMutableDictionary* iptc = out[(__bridge NSString*)kCGImagePropertyIPTCDictionary];
        if (iptc == nil) {
            iptc = [NSMutableDictionary dictionary];
            out[(__bridge NSString*)kCGImagePropertyIPTCDictionary] = iptc;
        }
        iptc[(__bridge NSString*)kCGImagePropertyIPTCStarRating] = @(std::clamp(rating, 0, 5));
    }

    return out;
}

/// Output sharpening, as an unsharp mask over the **resized** image.
///
/// ⚠️ The placement is sourced and the numbers are not. Fraser's multipass
/// model — capture, creative, output — puts this pass last, after the image is
/// at its final size, because resampling is what softens it and sharpening
/// before the resample is thrown away by it. That is why this runs inside
/// `convert`, on the resized buffer, rather than as a node in the graph.
///
/// The sigma and amount below are **chosen, not measured**, and are listed in
/// `research/UNSOURCED.md`. The invariant the tests hold is the one the control
/// promises: Print sharpens more than Screen, Screen more than None, and None
/// is bit-identical to no pass at all.
struct Unsharp {
    float sigma;   ///< Gaussian sigma in output pixels
    float amount;  ///< how much of the high-pass is added back
};

Unsharp unsharpFor(Sharpen s) {
    switch (s) {
        case Sharpen::None:   return {0.0f, 0.0f};
        case Sharpen::Screen: return {0.6f, 0.40f};
        case Sharpen::Print:  return {1.0f, 0.80f};
    }
    return {0.0f, 0.0f};
}

/// Unsharp mask in place over 16-bit RGBA, on luminance only.
///
/// Luminance only, not per channel: adding the same high-pass to R, G and B
/// moves the pixel along the grey axis, which sharpens the edge without moving
/// its hue. Sharpening the three channels independently is what puts colored
/// fringes on high-contrast edges.
///
/// Rec.709 luma weights, which are the primaries the display transform already
/// ends in — using any other set here would sharpen against a luminance the
/// rest of the pipeline does not agree with.
///
/// Cost, measured at 24 Mpx full size: two float planes, ~194 MB transient, and
/// a few hundred milliseconds single-threaded. Both are the same order as the
/// readback and the bitmap contexts either side of it, and export is off the
/// interaction path — but the panel's live size estimate runs this too, which
/// is why it is debounced and shows a spinner.
void unsharpMask(std::uint16_t* rgba, std::size_t w, std::size_t h,
                 std::size_t bytesPerRow, Unsharp s) {
    if (s.amount <= 0.0f || s.sigma <= 0.0f || w == 0 || h == 0) return;

    const std::size_t rowPixels = bytesPerRow / sizeof(std::uint16_t);

    // Three sigma either side: past that the Gaussian's weight is under 0.3%
    // and the taps cost more than they change.
    const int half = std::max(1, static_cast<int>(std::ceil(3.0f * s.sigma)));
    std::vector<float> kernel(static_cast<std::size_t>(2 * half + 1));
    float sum = 0.0f;
    for (int i = -half; i <= half; ++i) {
        const float v = std::exp(-(static_cast<float>(i) * static_cast<float>(i))
                                 / (2.0f * s.sigma * s.sigma));
        kernel[static_cast<std::size_t>(i + half)] = v;
        sum += v;
    }
    for (float& v : kernel) v /= sum;

    const auto at = [&](std::size_t x, std::size_t y) -> std::uint16_t* {
        return rgba + y * rowPixels + x * 4;
    };

    // The luminance plane, and the horizontal pass over it. Two planes rather
    // than three: the vertical pass reads `blurred` and `luma` together and
    // writes the correction straight into the pixels.
    std::vector<float> luma(w * h), blurred(w * h);
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const std::uint16_t* p = at(x, y);
            luma[y * w + x] = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2])
                            / 65535.0f;
        }
    }

    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -half; i <= half; ++i) {
                // Clamp at the border. Wrapping would sharpen the left edge
                // against the right one.
                const std::size_t sx = static_cast<std::size_t>(
                    std::clamp<long>(static_cast<long>(x) + i, 0,
                                     static_cast<long>(w) - 1));
                acc += kernel[static_cast<std::size_t>(i + half)] * luma[y * w + sx];
            }
            blurred[y * w + x] = acc;
        }
    }

    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -half; i <= half; ++i) {
                const std::size_t sy = static_cast<std::size_t>(
                    std::clamp<long>(static_cast<long>(y) + i, 0,
                                     static_cast<long>(h) - 1));
                acc += kernel[static_cast<std::size_t>(i + half)] * blurred[sy * w + x];
            }
            const float delta = s.amount * (luma[y * w + x] - acc) * 65535.0f;
            std::uint16_t* p = at(x, y);
            for (int c = 0; c < 3; ++c) {
                p[c] = static_cast<std::uint16_t>(
                    std::clamp(static_cast<float>(p[c]) + delta, 0.0f, 65535.0f));
            }
        }
    }
}

/// Sixteen bits per component.
///
/// PNG and TIFF carry that through; JPEG does not and CoreGraphics quantises on
/// the way out, which is correct — the point is that the depth survives as far
/// as the container allows rather than being thrown away at the graph's edge.
CGImageRef makeImage(const std::uint16_t* rgba, std::uint32_t width,
                     std::uint32_t height, std::size_t bytesPerRow) {
    // Always sRGB here, whatever the export asks for: this is what the pixels
    // *are*, and it is the honest input to any conversion. Tagging them as the
    // destination space instead would relabel them without moving them, which
    // is the classic way to ship a file that opens desaturated.
    CGColorSpaceRef space = colorSpace(ColorSpace::Srgb);

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

/// Resizes and converts, or returns nullptr when neither is needed.
///
/// Both in one pass, because both are a draw into a bitmap context and doing
/// them separately would resample twice.
///
/// Sixteen bits per component, not eight. It used to be eight, which quietly
/// undid the 16-bit output path for every export that asked for a smaller
/// image — the depth survived exactly as far as the first resize.
///
/// Resampling happens in CoreGraphics rather than on the GPU: export is not on
/// the interaction path, and a correctly filtered downscale matters more here
/// than shaving milliseconds. The conversion is ColorSync's, for the reason
/// CLAUDE.md gives — a mature implementation beats a hand-rolled one, and a
/// chromatic adaptation typed in by hand is a cast waiting to happen.
/// Quantises to eight bits per component, at the same size and in the same
/// space. A straight redraw, no resampling.
///
/// ⚠️ Undithered, deliberately. The graph dithers whichever node writes the
/// eight bits (see `ops/dither_ops.slang`), and `Engine::exportImage` picks the
/// narrow graph for exactly the exports that land here — so the values arriving
/// have already been broken up, and a second dither would add noise twice.
CGImageRef quantiseToEight(CGImageRef source, ColorSpace target) {
    const std::size_t w = CGImageGetWidth(source);
    const std::size_t h = CGImageGetHeight(source);

    CGColorSpaceRef space = colorSpace(target);
    CGContextRef ctx = CGBitmapContextCreate(
        nullptr, w, h, 8, 0, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast));
    CGColorSpaceRelease(space);
    if (ctx == nullptr) throw std::runtime_error("could not create eight-bit context");

    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(w),
                                       static_cast<CGFloat>(h)), source);

    CGImageRef out = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (out == nullptr) throw std::runtime_error("could not quantise image");
    return out;
}

/// Resizes, converts and sharpens, or returns nullptr when none is needed.
///
/// ⚠️ **The order is the feature.** Resample, then sharpen, then quantise.
/// Fraser's multipass model puts output sharpening after the image reaches its
/// final size because resampling is what softened it; sharpening first would be
/// resampled away. Quantising last keeps the sharpening's sub-level detail out
/// of the rounding.
///
/// Resize and convert share one pass, because both are a draw into a bitmap
/// context and doing them separately would resample twice.
///
/// Sixteen bits per component through all of it, not eight. It used to be
/// eight, which quietly undid the 16-bit output path for every export that
/// asked for a smaller image — the depth survived exactly as far as the first
/// resize.
///
/// Resampling happens in CoreGraphics rather than on the GPU: export is not on
/// the interaction path, and a correctly filtered downscale matters more here
/// than shaving milliseconds. The conversion is ColorSync's, for the reason
/// CLAUDE.md gives — a mature implementation beats a hand-rolled one, and a
/// chromatic adaptation typed in by hand is a cast waiting to happen.
CGImageRef convert(CGImageRef source, std::uint32_t maxDimension, ColorSpace target,
                   BitDepth depth, Sharpen sharpen) {
    const std::size_t w = CGImageGetWidth(source);
    const std::size_t h = CGImageGetHeight(source);
    const std::size_t longest = std::max(w, h);
    const bool resizing = maxDimension != 0 && longest > maxDimension;
    const bool sharpening = sharpen != Sharpen::None;

    // Nothing to resample, nothing to convert, nothing to sharpen and the depth
    // is already what the caller asked for.
    if (!resizing && !sharpening && target == ColorSpace::Srgb
        && depth == BitDepth::Sixteen) {
        return nullptr;
    }

    // Eight bits and nothing else to do: quantise the source directly rather
    // than round-tripping it through a full-size sixteen-bit redraw.
    if (!resizing && !sharpening && target == ColorSpace::Srgb) {
        return quantiseToEight(source, target);
    }

    std::size_t nw = w, nh = h;
    if (resizing) {
        const double scale = static_cast<double>(maxDimension) / static_cast<double>(longest);
        // Round rather than truncate: asking for 2048 px and getting 2047 is
        // the kind of detail that makes software feel sloppy.
        nw = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(w * scale)));
        nh = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(h * scale)));
    }

    CGColorSpaceRef space = colorSpace(target);
    CGContextRef ctx = CGBitmapContextCreate(
        nullptr, nw, nh, 16, 0, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    CGColorSpaceRelease(space);
    if (ctx == nullptr) throw std::runtime_error("could not create resize context");

    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(nw),
                                       static_cast<CGFloat>(nh)), source);

    // On the resized pixels, before they become an image.
    if (sharpening) {
        if (auto* data = static_cast<std::uint16_t*>(CGBitmapContextGetData(ctx))) {
            unsharpMask(data, nw, nh, CGBitmapContextGetBytesPerRow(ctx),
                        unsharpFor(sharpen));
        }
    }

    CGImageRef scaled = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (scaled == nullptr) throw std::runtime_error("could not convert image");

    if (depth == BitDepth::Sixteen) return scaled;

    CFHolder<CGImageRef> wide(scaled);
    return quantiseToEight(wide.ref, target);
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
        CFHolder<CGImageRef> scaled(convert(full.ref, options.maxDimension, options.space,
                                            options.depth, options.sharpen));
        CGImageRef image = scaled ? scaled.ref : full.ref;

        NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
        CFHolder<CGImageDestinationRef> dest(CGImageDestinationCreateWithURL(
            (__bridge CFURLRef)url, uti(options.format), 1, nullptr));
        if (!dest) throw std::runtime_error("could not create destination: " + path);

        NSMutableDictionary* props = metadata(options.metadataFrom, options.rating, options.metadata);
        props[(__bridge NSString*)kCGImageDestinationLossyCompressionQuality] =
            @(std::clamp(options.quality, 0.0f, 1.0f));
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
        CFHolder<CGImageRef> scaled(convert(full.ref, options.maxDimension, options.space,
                                            options.depth, options.sharpen));
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
    writeImage(path, rgba, width, height, bytesPerRow,
               {ImageFormat::Png, 1.0f, 0, ColorSpace::Srgb});
}

}  // namespace orion::util

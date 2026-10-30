#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "util/ImageWriter.h"
#include "util/ExportOps.h"
#include "util/JxlMetadata.h"
#include "util/JxlWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
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
        case ImageFormat::Jxl:  break;  // ImageIO reads JPEG XL but cannot write it
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

/// Blends the watermark's one color through its coverage mask, over the whole
/// frame of `ctx`.
///
/// Porter and Duff's "over" with the mask as alpha: `ctx` clipped to the mask
/// and filled, which is `m * rgb + (1 - m) * dst` in the file's own encoding.
/// The mask is stretched over `(0, 0, nw, nh)`, so if the app rendered it a
/// rounding pixel off the output size CoreGraphics resamples it invisibly and
/// neither side has to know the other's arithmetic. The color is stated in
/// sRGB and ColorSync puts it into the target space, so a neutral gray stays
/// neutral in Display P3 and Adobe RGB as well. DECISIONS #284.
///
/// Returns false rather than throwing, so the caller can release its context
/// before it throws.
bool drawWatermark(CGContextRef ctx, std::size_t nw, std::size_t nh,
                   const Watermark& wm) {
    CFHolder<CFDataRef> bytes(CFDataCreate(
        nullptr, wm.mask, static_cast<CFIndex>(std::size_t(wm.width) * wm.height)));
    if (!bytes) return false;
    CFHolder<CGDataProviderRef> provider(CGDataProviderCreateWithCFData(bytes.ref));

    // DeviceGray with no alpha, not CGImageMaskCreate: a gray image used as a
    // clip means 255 paints, where an image mask means the opposite.
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CFHolder<CGImageRef> mask(CGImageCreate(
        wm.width, wm.height, 8, 8, wm.width, gray,
        static_cast<CGBitmapInfo>(kCGImageAlphaNone), provider.ref, nullptr,
        true, kCGRenderingIntentDefault));
    CGColorSpaceRelease(gray);
    if (!mask) return false;

    CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGFloat components[4] = {wm.rgb[0], wm.rgb[1], wm.rgb[2], 1.0};
    CFHolder<CGColorRef> color(CGColorCreate(srgb, components));
    CGColorSpaceRelease(srgb);

    const CGRect frame = CGRectMake(0, 0, static_cast<CGFloat>(nw), static_cast<CGFloat>(nh));
    CGContextSaveGState(ctx);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextClipToMask(ctx, frame, mask.ref);
    CGContextSetFillColorWithColor(ctx, color.ref);
    CGContextFillRect(ctx, frame);
    CGContextRestoreGState(ctx);
    return true;
}

/// Resizes, converts, sharpens and watermarks, or returns nullptr when none is
/// needed.
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
///
/// The watermark goes on after the sharpening, so the mark's own edge is never
/// given a halo, and before the quantise, like everything else here.
CGImageRef convert(CGImageRef source, const ExportOptions& options) {
    const std::uint32_t maxDimension = options.maxDimension;
    const ColorSpace target = options.space;
    const BitDepth depth = options.depth;
    const Sharpen sharpen = options.sharpen;

    const std::size_t w = CGImageGetWidth(source);
    const std::size_t h = CGImageGetHeight(source);
    const std::size_t longest = std::max(w, h);
    const bool resizing = maxDimension != 0 && longest > maxDimension;
    const bool sharpening = sharpen != Sharpen::None;
    // Treated like sharpening: it needs the pixels in a context, so it takes
    // neither of the two shortcuts below.
    const bool marking = options.watermark.active();

    // Nothing to resample, nothing to convert, nothing to sharpen or mark and
    // the depth is already what the caller asked for.
    if (!resizing && !sharpening && !marking && target == ColorSpace::Srgb
        && depth == BitDepth::Sixteen) {
        return nullptr;
    }

    // Eight bits and nothing else to do: quantise the source directly rather
    // than round-tripping it through a full-size sixteen-bit redraw.
    if (!resizing && !sharpening && !marking && target == ColorSpace::Srgb) {
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

    if (marking && !drawWatermark(ctx, nw, nh, options.watermark)) {
        CGContextRelease(ctx);
        throw std::runtime_error("could not draw watermark");
    }

    CGImageRef scaled = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (scaled == nullptr) throw std::runtime_error("could not convert image");

    if (depth == BitDepth::Sixteen) return scaled;

    CFHolder<CGImageRef> wide(scaled);
    return quantiseToEight(wide.ref, target);
}

/// JPEG XL bytes for one of this file's images, with `props` as its metadata
/// (nil writes none). Every image reaching here came out of `makeImage`,
/// `convert` or `quantiseToEight`, so the layout is RGBX at 8 or 16 bits; the
/// check is for the day that stops being true.
std::vector<std::uint8_t> jxlBytes(CGImageRef image, const ExportOptions& options,
                                   NSDictionary* props) {
    const auto bits = static_cast<int>(CGImageGetBitsPerComponent(image));
    const CGBitmapInfo info = CGImageGetBitmapInfo(image);
    const bool rgbx = (bits == 8 || bits == 16)
        && CGImageGetBitsPerPixel(image) == std::size_t(bits) * 4
        && (info & kCGBitmapAlphaInfoMask) == kCGImageAlphaNoneSkipLast
        && (bits == 8 || (info & kCGBitmapByteOrderMask) == kCGBitmapByteOrder16Little);
    if (!rgbx) throw std::runtime_error("jxl: unexpected pixel layout");

    CFHolder<CFDataRef> pixels(CGDataProviderCopyData(CGImageGetDataProvider(image)));
    CFHolder<CFDataRef> icc(CGColorSpaceCopyICCData(CGImageGetColorSpace(image)));
    if (!pixels || !icc) throw std::runtime_error("jxl: could not read the image");

    JxlInput in;
    in.pixels = CFDataGetBytePtr(pixels.ref);
    in.width = static_cast<std::uint32_t>(CGImageGetWidth(image));
    in.height = static_cast<std::uint32_t>(CGImageGetHeight(image));
    in.bytesPerRow = CGImageGetBytesPerRow(image);
    in.bits = bits;
    in.quality = options.quality;
    const std::uint8_t* p = CFDataGetBytePtr(icc.ref);
    in.icc.assign(p, p + CFDataGetLength(icc.ref));
    if (props != nil) {
        serialiseMetadata((__bridge CFDictionaryRef)props, in.width, in.height, in.exif, in.xmp);
    }
    return encodeJxl(in);
}

}  // namespace

void writeImage(const std::string& path, const std::uint16_t* rgba,
                std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow,
                const ExportOptions& options) {
    @autoreleasepool {
        CFHolder<CGImageRef> full(makeImage(rgba, width, height, bytesPerRow));
        CFHolder<CGImageRef> scaled(convert(full.ref, options));
        CGImageRef image = scaled ? scaled.ref : full.ref;

        if (options.format == ImageFormat::Jxl) {
            const auto bytes = jxlBytes(
                image, options, metadata(options.metadataFrom, options.rating, options.metadata));
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            if (!file.flush()) throw std::runtime_error("could not write: " + path);
            return;
        }

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
        CFHolder<CGImageRef> scaled(convert(full.ref, options));
        CGImageRef image = scaled ? scaled.ref : full.ref;

        // No metadata, as for the other formats below: the measurement leaves
        // out the EXIF and XMP boxes, an underestimate of a few hundred bytes
        // to a few kilobytes against a file measured in megabytes.
        if (options.format == ImageFormat::Jxl) return jxlBytes(image, options, nil).size();

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

std::vector<std::uint8_t> encodeJpeg(const std::uint16_t* rgba,
                                     std::uint32_t width, std::uint32_t height,
                                     std::size_t bytesPerRow, float quality) {
    @autoreleasepool {
        CFHolder<CGImageRef> image(makeImage(rgba, width, height, bytesPerRow));
        if (!image) return {};

        NSMutableData* data = [NSMutableData data];
        CFHolder<CGImageDestinationRef> dest(CGImageDestinationCreateWithData(
            (__bridge CFMutableDataRef)data, uti(ImageFormat::Jpeg), 1, nullptr));
        if (!dest) return {};

        NSDictionary* props = @{
            (__bridge NSString*)kCGImageDestinationLossyCompressionQuality :
                @(std::clamp(quality, 0.0f, 1.0f))
        };
        CGImageDestinationAddImage(dest.ref, image.ref, (__bridge CFDictionaryRef)props);
        if (!CGImageDestinationFinalize(dest.ref)) return {};

        std::vector<std::uint8_t> out(data.length);
        memcpy(out.data(), data.bytes, data.length);
        return out;
    }
}

void writePng(const std::string& path, const std::uint16_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow) {
    writeImage(path, rgba, width, height, bytesPerRow,
               {ImageFormat::Png, 1.0f, 0, ColorSpace::Srgb});
}

}  // namespace orion::util

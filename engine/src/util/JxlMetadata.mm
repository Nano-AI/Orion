#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "util/JxlMetadata.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace orion::util {
namespace {

/// ImageIO writes the carrier's own 1x1 as the EXIF pixel dimensions and
/// ignores any it is handed, so the export's are put back here. Walks IFD0 to
/// the EXIF IFD; either byte order; bounds-checked, and a no-op on anything it
/// does not recognise.
void setExifDimensions(std::vector<std::uint8_t>& t, std::uint32_t w, std::uint32_t h) {
    if (t.size() < 8) return;
    const bool le = t[0] == 'I';
    const auto rd16 = [&](std::size_t o) -> std::uint32_t {
        return le ? t[o] | t[o + 1] << 8 : t[o] << 8 | t[o + 1];
    };
    const auto rd32 = [&](std::size_t o) -> std::uint32_t {
        return le ? rd16(o) | rd16(o + 2) << 16 : rd16(o) << 16 | rd16(o + 2);
    };
    const auto wr = [&](std::size_t o, std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) t[o + i] = std::uint8_t(v >> (8 * (le ? i : n - 1 - i)));
    };
    const auto find = [&](std::size_t ifd, std::uint32_t tag) -> std::size_t {
        if (ifd + 2 > t.size()) return 0;
        for (std::uint32_t i = 0, n = rd16(ifd); i < n; ++i) {
            const std::size_t e = ifd + 2 + std::size_t(i) * 12;
            if (e + 12 > t.size()) return 0;
            if (rd16(e) == tag) return e;
        }
        return 0;
    };
    const std::size_t pointer = find(rd32(4), 0x8769);  // ExifIFDPointer
    if (pointer == 0) return;
    const std::size_t exifIfd = rd32(pointer + 8);
    for (const auto& [tag, v] : {std::pair{0xA002u, w}, std::pair{0xA003u, h}}) {
        if (const std::size_t e = find(exifIfd, tag)) {
            wr(e + 2, 4, 2);  // LONG: a count of one still fits the value field
            wr(e + 4, 1, 4);
            wr(e + 8, v, 4);
        }
    }
}

/// A one-pixel JPEG in memory, carrying `props`. Nil on failure.
NSData* carrier(CFDictionaryRef props) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(nullptr, 1, 1, 8, 0, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast));
    CGColorSpaceRelease(space);
    if (ctx == nullptr) return nil;
    CGImageRef pixel = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (pixel == nullptr) return nil;

    NSMutableData* data = [NSMutableData data];
    CGImageDestinationRef dest = CGImageDestinationCreateWithData(
        (__bridge CFMutableDataRef)data, CFSTR("public.jpeg"), 1, nullptr);
    bool ok = dest != nullptr;
    if (ok) {
        CGImageDestinationAddImage(dest, pixel, props);
        ok = CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CGImageRelease(pixel);
    return ok ? data : nil;
}

}  // namespace

/// libjxl stores metadata as opaque boxes, so something has to serialise it.
/// ImageIO already does, for every other format, from this same dictionary,
/// so a one-pixel JPEG is written to memory and its APP1 segments taken. The
/// policy (GPS, IPTC place names, orientation) was applied when `props` was
/// built, which is what keeps JPEG XL's metadata the same as the others'.
/// IPTC has no box in JPEG XL; ImageIO mirrors the rating and keywords into
/// the XMP, which is where they travel.
void serialiseMetadata(CFDictionaryRef props, std::uint32_t w, std::uint32_t h,
                       std::vector<std::uint8_t>& exif, std::vector<std::uint8_t>& xmp) {
    // A strong reference outlives the pool, so nothing is thrown inside it.
    NSData* data = nil;
    @autoreleasepool { data = carrier(props); }
    if (data == nil) throw std::runtime_error("could not serialise metadata");

    // Both prefixes are compared with their terminating NUL: "Exif\0\0" and
    // the XMP namespace URI followed by one NUL.
    static const char kExif[] = "Exif\0";
    static const char kXmp[] = "http://ns.adobe.com/xap/1.0/";
    const auto* b = static_cast<const std::uint8_t*>(data.bytes);
    const std::size_t n = data.length;
    // Marker segments up to the first scan; ImageIO writes no padding between.
    for (std::size_t i = 2; i + 4 <= n && b[i] == 0xFF && b[i + 1] != 0xDA;) {
        const std::size_t len = std::size_t(b[i + 2]) << 8 | b[i + 3];
        if (len < 2 || i + 2 + len > n) break;
        const std::uint8_t* body = b + i + 4;
        const std::size_t size = len - 2;
        if (b[i + 1] == 0xE1 && size > sizeof kExif &&
            std::memcmp(body, kExif, sizeof kExif) == 0) {
            exif.assign(body + sizeof kExif, body + size);
        } else if (b[i + 1] == 0xE1 && size > sizeof kXmp &&
                   std::memcmp(body, kXmp, sizeof kXmp) == 0) {
            xmp.assign(body + sizeof kXmp, body + size);
        }
        i += 2 + len;
    }
    setExifDimensions(exif, w, h);
}

}  // namespace orion::util

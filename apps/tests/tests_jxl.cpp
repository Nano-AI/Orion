// JPEG XL export (#6): written by the engine with libjxl, read back by libjxl
// for what the codestream says and by ImageIO for what the metadata says.
//
// ImageIO is the second reader on purpose: it is what Preview, Photos and
// Finder use, so a file it cannot read the EXIF of is a file whose EXIF is lost
// in practice, whatever libjxl thinks of it.

#include "harness.h"

#include <jxl/cms.h>
#include <jxl/decode_cxx.h>

#include <fstream>
#include <iterator>

namespace {

struct Decoded {
    std::uint32_t width = 0, height = 0, bits = 0;
    bool lossless = false;
    std::vector<std::uint8_t> icc;
    bool enumerated = false;     ///< the space is stored as a JPEG XL color encoding
    JxlColorEncoding encoding{}; ///< valid when `enumerated`
    std::vector<std::string> boxes;
    std::vector<std::uint16_t> rgb;  ///< 16-bit, in the file's own space
};

/// The whole file through libjxl. XYB (lossy) frames are converted back to the
/// space the file declares, with libjxl's own CMS, so both kinds compare
/// against the TIFF in the same space.
bool decodeJxl(const std::string& path, Decoded& out) {
    std::ifstream f(path, std::ios::binary);
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(f), {}};
    if (data.empty()) return false;

    auto dec = JxlDecoderMake(nullptr);
    if (JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING |
                                                 JXL_DEC_FULL_IMAGE | JXL_DEC_BOX) != JXL_DEC_SUCCESS) {
        return false;
    }
    JxlDecoderSetInput(dec.get(), data.data(), data.size());
    JxlDecoderCloseInput(dec.get());
    const JxlPixelFormat format{3, JXL_TYPE_UINT16, JXL_LITTLE_ENDIAN, 0};

    for (;;) {
        switch (JxlDecoderProcessInput(dec.get())) {
            case JXL_DEC_BASIC_INFO: {
                JxlBasicInfo info;
                if (JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS) return false;
                out.width = info.xsize;
                out.height = info.ysize;
                out.bits = info.bits_per_sample;
                out.lossless = info.uses_original_profile == JXL_TRUE;
                break;
            }
            case JXL_DEC_COLOR_ENCODING: {
                out.enumerated = JxlDecoderGetColorAsEncodedProfile(
                    dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &out.encoding) == JXL_DEC_SUCCESS;
                std::size_t size = 0;
                if (JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &size)
                        != JXL_DEC_SUCCESS) return false;
                out.icc.resize(size);
                if (JxlDecoderGetColorAsICCProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL,
                                                   out.icc.data(), size) != JXL_DEC_SUCCESS) return false;
                if (!out.lossless) {
                    JxlDecoderSetCms(dec.get(), *JxlGetDefaultCms());
                    if (JxlDecoderSetOutputColorProfile(dec.get(), nullptr, out.icc.data(),
                                                        out.icc.size()) != JXL_DEC_SUCCESS) return false;
                }
                break;
            }
            case JXL_DEC_BOX: {
                JxlBoxType type;
                if (JxlDecoderGetBoxType(dec.get(), type, JXL_TRUE) == JXL_DEC_SUCCESS) {
                    out.boxes.emplace_back(type, 4);
                }
                break;
            }
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
                out.rgb.assign(std::size_t(out.width) * out.height * 3, 0);
                if (JxlDecoderSetImageOutBuffer(dec.get(), &format, out.rgb.data(),
                                                out.rgb.size() * sizeof(std::uint16_t))
                        != JXL_DEC_SUCCESS) return false;
                break;
            }
            case JXL_DEC_FULL_IMAGE: break;
            case JXL_DEC_SUCCESS: return !out.rgb.empty();
            default: return false;
        }
    }
}

/// A written TIFF, whole, as 16-bit RGBA in **its own** space. `decode16`
/// converts to sRGB, which would compare a P3 TIFF against a P3 JPEG XL in two
/// different spaces.
bool decodeNative(const std::string& path, std::vector<std::uint16_t>& out,
                  std::vector<std::uint8_t>& icc) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), false);
    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return false;
    CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (image == nullptr) return false;

    const std::size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
    CGColorSpaceRef space = CGImageGetColorSpace(image);
    if (CFDataRef data = CGColorSpaceCopyICCData(space)) {
        icc.assign(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
        CFRelease(data);
    }
    out.assign(w * h * 4, 0);
    CGContextRef ctx = CGBitmapContextCreate(
        out.data(), w, h, 16, w * 4 * sizeof(std::uint16_t), space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    if (ctx == nullptr) { CGImageRelease(image); return false; }
    CGContextDrawImage(ctx, CGRectMake(0, 0, CGFloat(w), CGFloat(h)), image);
    CGContextRelease(ctx);
    CGImageRelease(image);
    return true;
}

/// The EXIF pixel dimensions ImageIO reads from a file, 0 when absent.
std::pair<long, long> exifDimensions(const std::string& path) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), false);
    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return {0, 0};
    CFDictionaryRef all = CGImageSourceCopyPropertiesAtIndex(src, 0, nullptr);
    CFRelease(src);
    long w = 0, h = 0;
    if (all != nullptr) {
        if (auto exif = static_cast<CFDictionaryRef>(
                CFDictionaryGetValue(all, kCGImagePropertyExifDictionary))) {
            if (auto n = static_cast<CFNumberRef>(
                    CFDictionaryGetValue(exif, kCGImagePropertyExifPixelXDimension))) {
                CFNumberGetValue(n, kCFNumberLongType, &w);
            }
            if (auto n = static_cast<CFNumberRef>(
                    CFDictionaryGetValue(exif, kCGImagePropertyExifPixelYDimension))) {
                CFNumberGetValue(n, kCFNumberLongType, &h);
            }
        }
        CFRelease(all);
    }
    return {w, h};
}

bool has(const std::vector<std::string>& v, const char* s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

void testExportJxl() {
    section("Export: JPEG XL");

    using orion::util::BitDepth;
    using orion::util::ColorSpace;
    using orion::util::ImageFormat;
    using orion::util::Metadata;

    report(orion::util::formatForPath("a.jxl") == ImageFormat::Jxl, "jxl by extension");
    report(orion::util::formatForPath("a.JXL") == ImageFormat::Jxl, "JXL by extension");

    // Three ramps and a diagonal, so a lossy encoder has edges and gradients to
    // get wrong, and a channel swap cannot pass.
    constexpr std::uint32_t kW = 96, kH = 64;
    std::vector<std::uint16_t> pixels(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            pixels[i + 0] = static_cast<std::uint16_t>(x * 680);
            pixels[i + 1] = static_cast<std::uint16_t>(y * 1020);
            pixels[i + 2] = static_cast<std::uint16_t>(((x + y) % 32) < 16 ? 52000 : 9000);
            pixels[i + 3] = 65535;
        }
    }
    const std::size_t stride = std::size_t(kW) * 4 * sizeof(std::uint16_t);
    const std::string dir = "/tmp/";
    const std::string source = dir + "orion-jxl-source.jpg";
    report(writeJpegWithGps(source, pixels.data(), kW, kH, stride),
           "a stand-in camera file with GPS is written");

    // Tolerances are on 0..1 values. Lossless is exact: the same pixels as the
    // TIFF, bit for bit. Lossy at 0.9 is `cjxl -q 90`, distance about 1, which
    // libjxl calibrates as *visually* lossless, not numerically: measured here
    // at a mean of 0.018 (16-bit) and 0.012 (8-bit P3), worst 0.29, all of the
    // worst on the blue square wave's one-pixel edges, where XYB spends least.
    // djxl's own decode of the same file agrees. So the lossy bound is on the
    // mean only, at 0.03: a channel swap here is about 0.3.
    struct Case {
        BitDepth depth; float quality; ColorSpace space;
        double meanTol, maxTol; const char* name;
    };
    const Case cases[] = {
        {BitDepth::Sixteen, 1.0f, ColorSpace::Srgb,      0.0,  0.0, "16-bit lossless"},
        {BitDepth::Eight,   1.0f, ColorSpace::Srgb,      0.0,  0.0, "8-bit lossless"},
        {BitDepth::Sixteen, 0.9f, ColorSpace::Srgb,      0.03, 1.0, "16-bit lossy"},
        {BitDepth::Eight,   0.9f, ColorSpace::DisplayP3, 0.03, 1.0, "8-bit lossy, Display P3"},
    };

    for (const auto& c : cases) {
        orion::util::ExportOptions o{};
        o.depth = c.depth;
        o.quality = c.quality;
        o.space = c.space;
        o.metadataFrom = source;
        o.rating = 3;
        const std::string stem = dir + "orion-jxl-" + std::to_string(&c - cases);
        try {
            o.format = ImageFormat::Tiff;
            orion::util::writeImage(stem + ".tif", pixels.data(), kW, kH, stride, o);
            o.format = ImageFormat::Jxl;
            orion::util::writeImage(stem + ".jxl", pixels.data(), kW, kH, stride, o);
        } catch (const std::exception& e) {
            report(false, std::string(c.name) + " writes", e.what());
            continue;
        }

        Decoded jxl;
        std::vector<std::uint16_t> tiff;
        std::vector<std::uint8_t> tiffIcc;
        if (!decodeJxl(stem + ".jxl", jxl) || !decodeNative(stem + ".tif", tiff, tiffIcc)) {
            report(false, std::string(c.name) + " decodes");
            continue;
        }
        const std::string n = c.name;
        report(jxl.width == kW && jxl.height == kH, n + " keeps the size",
               std::to_string(jxl.width) + "x" + std::to_string(jxl.height));
        report(jxl.bits == (c.depth == BitDepth::Sixteen ? 16u : 8u), n + " keeps the bit depth",
               "got " + std::to_string(jxl.bits));
        report(jxl.lossless == (c.quality >= 1.0f), n + (c.quality >= 1.0f
               ? " keeps the original space (lossless)" : " is XYB (lossy)"));
        // Lossless keeps the ICC profile byte for byte. Lossy goes through XYB,
        // and libjxl stores a profile it recognises as its own color encoding
        // instead, so there the check is the encoding: the right primaries and
        // the sRGB curve, which is what both of these spaces use.
        if (jxl.lossless) {
            report(!jxl.icc.empty() && jxl.icc == tiffIcc, n + " carries the TIFF's ICC profile",
                   std::to_string(jxl.icc.size()) + " vs " + std::to_string(tiffIcc.size()) + " bytes");
        } else {
            const auto want = c.space == ColorSpace::DisplayP3 ? JXL_PRIMARIES_P3 : JXL_PRIMARIES_SRGB;
            report(!jxl.icc.empty() && jxl.enumerated && jxl.encoding.primaries == want &&
                       jxl.encoding.transfer_function == JXL_TRANSFER_FUNCTION_SRGB,
                   n + " declares the export's color space",
                   "primaries " + std::to_string(int(jxl.encoding.primaries)) + ", transfer " +
                       std::to_string(int(jxl.encoding.transfer_function)));
        }
        report(has(jxl.boxes, "Exif") && has(jxl.boxes, "xml "), n + " has Exif and XMP boxes");

        bool gps = true, exif = false;
        report(metadataBlocks(stem + ".jxl", gps, exif) && exif && !gps,
               n + ": ImageIO reads the camera EXIF and no GPS");
        const auto dims = exifDimensions(stem + ".jxl");
        report(dims.first == kW && dims.second == kH, n + ": EXIF states the export's size",
               std::to_string(dims.first) + "x" + std::to_string(dims.second));

        // Against the TIFF export of the same options.
        double sum = 0.0, worst = 0.0;
        for (std::size_t p = 0; p < std::size_t(kW) * kH; ++p) {
            for (int ch = 0; ch < 3; ++ch) {
                const double d = std::abs(double(jxl.rgb[p * 3 + ch]) - double(tiff[p * 4 + ch]))
                               / 65535.0;
                sum += d;
                worst = std::max(worst, d);
            }
        }
        const double mean = sum / (double(kW) * kH * 3);
        report(mean <= c.meanTol && worst <= c.maxTol, n + " matches the TIFF export",
               "mean " + std::to_string(mean) + ", max " + std::to_string(worst));
    }

    // "Absent" above is only worth something if the box can carry it.
    {
        orion::util::ExportOptions o{};
        o.format = ImageFormat::Jxl;
        o.quality = 1.0f;
        o.metadataFrom = source;
        o.metadata = Metadata::All;
        const std::string path = dir + "orion-jxl-all.jxl";
        bool gps = false, exif = false;
        try {
            orion::util::writeImage(path, pixels.data(), kW, kH, stride, o);
            report(metadataBlocks(path, gps, exif) && gps && exif, "keep all keeps GPS in JPEG XL");

            o.metadataFrom.clear();
            Decoded bare;
            orion::util::writeImage(path, pixels.data(), kW, kH, stride, o);
            report(decodeJxl(path, bare), "an export with no source still decodes");

            o.quality = 0.9f;
            report(orion::util::encodedSize(pixels.data(), kW, kH, stride, o) > 0,
                   "the size estimate encodes JPEG XL");
        } catch (const std::exception& e) {
            report(false, "JPEG XL writes", e.what());
        }
    }
}

// Export on Windows and Linux (#376): ImageWriter.mm's job without ImageIO.
//
// OpenCV's imgcodecs encodes JPEG and PNG (libjpeg-turbo, libpng underneath)
// and resamples; libjxl writes JPEG XL as on macOS; TIFF is written here, on
// util/TiffIfd.h, because OpenCV's TIFF encoder carries no metadata. Colour is
// Little CMS's (ExportColour.h) and metadata LibRaw's (ExportMetadata.h). The
// order is ImageWriter.mm's and is the feature: resample, convert, sharpen,
// watermark, then quantise.

#include "util/ExportColour.h"
#include "util/ExportMetadata.h"
#include "util/ExportOps.h"
#include "util/ImageWriter.h"
#include "util/JxlWriter.h"
#include "util/TiffIfd.h"
#include "util/Utf8Path.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <zlib.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace orion::util {
namespace {

/// The picture at its final size, sharpened and marked, as RGBA at the
/// requested depth. Always a copy: the caller's rows are borrowed.
cv::Mat prepare(const std::uint16_t* rgba, std::uint32_t width, std::uint32_t height,
                std::size_t bytesPerRow, const ExportOptions& o) {
    const cv::Mat source(int(height), int(width), CV_16UC4, const_cast<std::uint16_t*>(rgba),
                         bytesPerRow);
    cv::Mat img;
    const std::uint32_t longest = std::max(width, height);
    if (o.maxDimension != 0 && longest > o.maxDimension) {
        const double scale = double(o.maxDimension) / double(longest);
        const int nw = std::max(1, int(std::llround(width * scale)));
        const int nh = std::max(1, int(std::llround(height * scale)));
        // Area averaging: the correctly filtered downscale (no aliasing).
        cv::resize(source, img, cv::Size(nw, nh), 0, 0, cv::INTER_AREA);
    } else {
        img = source.clone();
    }
    convertFromSrgb(img.ptr<std::uint16_t>(), std::uint32_t(img.cols), std::uint32_t(img.rows),
                    img.step, o.space);

    unsharpMask(img.ptr<std::uint16_t>(), std::size_t(img.cols), std::size_t(img.rows),
                img.step, unsharpFor(o.sharpen));

    // Porter-Duff "over" with the mask as alpha, in the file's own encoding,
    // the mask stretched over the frame as CoreGraphics does (DECISIONS #284).
    // The colour is stated in sRGB and converted like the pixels under it.
    if (o.watermark.active()) {
        const cv::Mat mask(int(o.watermark.height), int(o.watermark.width), CV_8UC1,
                           const_cast<std::uint8_t*>(o.watermark.mask));
        cv::Mat cover;
        cv::resize(mask, cover, img.size(), 0, 0, cv::INTER_LINEAR);
        std::uint16_t ink[4] = {0, 0, 0, 65535};
        for (int c = 0; c < 3; ++c) {
            ink[c] = std::uint16_t(std::lround(std::clamp(o.watermark.rgb[c], 0.0f, 1.0f) * 65535.0f));
        }
        convertFromSrgb(ink, 1, 1, sizeof ink, o.space);
        for (int y = 0; y < img.rows; ++y) {
            auto* p = img.ptr<std::uint16_t>(y);
            const auto* m = cover.ptr<std::uint8_t>(y);
            for (int x = 0; x < img.cols; ++x) {
                const float a = float(m[x]) / 255.0f;
                for (int c = 0; c < 3; ++c) {
                    p[4 * x + c] = std::uint16_t(std::lround(a * ink[c] + (1.0f - a) * p[4 * x + c]));
                }
            }
        }
    }

    if (o.depth == BitDepth::Eight || o.format == ImageFormat::Jpeg) {
        cv::Mat eight;
        img.convertTo(eight, CV_8UC4, 255.0 / 65535.0);   // undithered: the graph dithered
        return eight;
    }
    return img;
}

/// One strip's rows as RGB, horizontally differenced (TIFF 6.0 §14, Predictor
/// 2) and deflated (Compression 8, the TIFF Technical Note "Adobe Deflate").
template <class T>
std::vector<std::uint8_t> deflateStrip(const cv::Mat& rgba, int first, int rows, bool predict) {
    const std::size_t w = std::size_t(rgba.cols);
    std::vector<T> rgb(w * 3 * std::size_t(rows));
    for (int y = 0; y < rows; ++y) {
        const T* src = rgba.ptr<T>(first + y);
        T* dst = rgb.data() + std::size_t(y) * w * 3;
        for (std::size_t x = 0; x < w; ++x) std::memcpy(dst + 3 * x, src + 4 * x, 3 * sizeof(T));
        if (predict) {
            for (std::size_t i = w * 3 - 1; i >= 3; --i) dst[i] = T(dst[i] - dst[i - 3]);
        }
    }
    const auto* raw = reinterpret_cast<const Bytef*>(rgb.data());
    const uLong rawBytes = uLong(rgb.size() * sizeof(T));
    uLongf size = compressBound(rawBytes);
    std::vector<std::uint8_t> out(size);
    if (compress2(out.data(), &size, raw, rawBytes, Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw std::runtime_error("could not deflate a TIFF strip");
    }
    out.resize(size);
    return out;
}

/// Baseline RGB (TIFF 6.0 §6) in deflated strips of about 256 KB of pixels,
/// with the profile (34675, ICC.1 annex B), the XMP (700) and the EXIF and
/// GPS directories. The strips compress in parallel.
std::vector<std::uint8_t> encodeTiff(const cv::Mat& rgba, const std::vector<std::uint8_t>& icc,
                                     const CaptureFacts& facts, const std::string& xmp) {
    static_assert(std::endian::native == std::endian::little, "samples are written as held");
    using namespace tiff;
    const auto w = std::uint32_t(rgba.cols), h = std::uint32_t(rgba.rows);
    const bool wide = rgba.depth() == CV_16U;
    const std::uint16_t bits = wide ? 16 : 8;
    const std::size_t rowBytes = std::size_t(w) * 3 * (bits / 8);
    const std::uint32_t rowsPerStrip =
        std::clamp<std::uint32_t>(std::uint32_t((256 * 1024) / rowBytes), 1, h);
    const std::uint32_t count = (h + rowsPerStrip - 1) / rowsPerStrip;
    // Measured on _PIC8095.ARW: differencing takes an 8-bit export from 0.885
    // of its raw size to 0.609, and a 16-bit one from 0.774 to 0.775, at
    // twice the time. Its sensor noise fills the low bytes either way.
    const bool predict = !wide;

    std::vector<std::vector<std::uint8_t>> strips(count);
    cv::parallel_for_(cv::Range(0, int(count)), [&](const cv::Range& range) {
        for (int i = range.start; i < range.end; ++i) {
            const int first = i * int(rowsPerStrip);
            const int rows = std::min(int(rowsPerStrip), int(h) - first);
            strips[std::size_t(i)] = wide ? deflateStrip<std::uint16_t>(rgba, first, rows, predict)
                                          : deflateStrip<std::uint8_t>(rgba, first, rows, predict);
        }
    });
    std::vector<std::uint32_t> sizes(count);
    Entry offsets{273, kLong, count, std::vector<std::uint8_t>(std::size_t(count) * 4)};
    Entry byteCounts{279, kLong, count, {}};
    for (std::uint32_t i = 0; i < count; ++i) {
        sizes[i] = std::uint32_t(strips[i].size());
        put32(byteCounts.data, sizes[i]);
    }

    Ifd ifd0, exif, gps;
    exifDirectories(facts, w, h, ifd0, exif, gps);
    ifd0.push_back(longEntry(256, w));
    ifd0.push_back(longEntry(257, h));
    ifd0.push_back(shortEntry(258, {bits, bits, bits}));
    ifd0.push_back(shortEntry(259, {8}));                 // Compression: Adobe Deflate
    ifd0.push_back(shortEntry(262, {2}));                 // Photometric: RGB
    ifd0.push_back(std::move(offsets));                   // StripOffsets, set by layout
    ifd0.push_back(shortEntry(277, {3}));
    ifd0.push_back(longEntry(278, rowsPerStrip));
    ifd0.push_back(std::move(byteCounts));
    ifd0.push_back(rationalEntry(282, {{72, 1}}));        // XResolution
    ifd0.push_back(rationalEntry(283, {{72, 1}}));        // YResolution
    ifd0.push_back(shortEntry(284, {1}));                 // PlanarConfiguration: chunky
    ifd0.push_back(shortEntry(296, {2}));                 // ResolutionUnit: inch
    if (predict) ifd0.push_back(shortEntry(317, {2}));   // Predictor: horizontal differencing
    if (!xmp.empty()) {
        ifd0.push_back(bytesEntry(700, kByte, reinterpret_cast<const std::uint8_t*>(xmp.data()),
                                  xmp.size()));
    }
    ifd0.push_back(bytesEntry(34675, kUndefined, icc.data(), icc.size()));

    std::vector<std::uint8_t> out = layout(std::move(ifd0), std::move(exif), std::move(gps), sizes);
    for (const auto& strip : strips) out.insert(out.end(), strip.begin(), strip.end());
    return out;
}

/// `facts` null writes no EXIF or XMP, which is what a size measurement
/// encodes: on macOS too it leaves the metadata out.
std::vector<std::uint8_t> encode(const cv::Mat& rgba, const ExportOptions& o,
                                 const CaptureFacts* facts) {
    const CaptureFacts none;
    const CaptureFacts& f = facts != nullptr ? *facts : none;
    const auto icc = iccProfile(o.space);
    const auto exif = exifBlock(f, std::uint32_t(rgba.cols), std::uint32_t(rgba.rows));
    const std::string xmp = xmpPacket(f);

    if (o.format == ImageFormat::Tiff) return encodeTiff(rgba, icc, f, xmp);
    if (o.format == ImageFormat::Jxl) {
        JxlInput in;
        in.pixels = rgba.data;
        in.width = std::uint32_t(rgba.cols);
        in.height = std::uint32_t(rgba.rows);
        in.bytesPerRow = rgba.step;
        in.bits = rgba.depth() == CV_16U ? 16 : 8;
        in.quality = o.quality;
        in.icc = icc;
        in.exif = exif;
        in.xmp.assign(xmp.begin(), xmp.end());
        return encodeJxl(in);
    }
    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    const int q = int(std::lround(std::clamp(o.quality, 0.0f, 1.0f) * 100.0f));
    const bool png = o.format == ImageFormat::Png;
    std::vector<int> params;
    if (!png) params = {cv::IMWRITE_JPEG_QUALITY, q};

    std::vector<int> types{cv::IMAGE_METADATA_ICCP};
    std::vector<std::vector<std::uint8_t>> chunks{icc};
    if (!exif.empty()) {   // OpenCV adds JPEG's "Exif\0\0"; PNG's eXIf has none
        types.push_back(cv::IMAGE_METADATA_EXIF);
        chunks.push_back(exif);
    }
    if (!xmp.empty()) {
        // JPEG's APP1 opens with the namespace and a NUL (XMP Part 3, 1.1.3);
        // PNG's text chunk is keyed by OpenCV.
        static const char kXmpNs[] = "http://ns.adobe.com/xap/1.0/";
        std::vector<std::uint8_t> chunk;
        if (!png) chunk.assign(kXmpNs, kXmpNs + sizeof kXmpNs);
        chunk.insert(chunk.end(), xmp.begin(), xmp.end());
        types.push_back(cv::IMAGE_METADATA_XMP);
        chunks.push_back(std::move(chunk));
    }
    std::vector<std::uint8_t> out;
    if (!cv::imencodeWithMetadata(png ? ".png" : ".jpg", bgr, types, chunks, out, params)) {
        throw std::runtime_error("could not encode image");
    }
    return out;
}

}  // namespace

void writeImage(const std::string& path, const std::uint16_t* rgba,
                std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow,
                const ExportOptions& options) {
    const CaptureFacts facts = captureFacts(options);
    const auto bytes = encode(prepare(rgba, width, height, bytesPerRow, options), options, &facts);
    // A temporary beside it, then a rename: a full disk leaves the old file whole.
    const auto partial = utf8Path(path + ".part");
    const auto drop = [&] { std::error_code ignored; std::filesystem::remove(partial, ignored); };
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!file.flush()) {
            file.close();
            drop();
            throw std::runtime_error("could not write: " + path);
        }
    }
    std::error_code moved;
    std::filesystem::rename(partial, utf8Path(path), moved);   // replaces on Windows too (#376)
    if (moved) {
        drop();
        throw std::runtime_error("could not move into place: " + path);
    }
}

std::size_t encodedSize(const std::uint16_t* rgba, std::uint32_t width, std::uint32_t height,
                        std::size_t bytesPerRow, const ExportOptions& options) {
    try {
        return encode(prepare(rgba, width, height, bytesPerRow, options), options, nullptr).size();
    } catch (const std::exception&) {
        return 0;
    }
}

std::vector<std::uint8_t> encodeJpeg(const std::uint16_t* rgba, std::uint32_t width,
                                     std::uint32_t height, std::size_t bytesPerRow,
                                     float quality) {
    ExportOptions o;
    o.format = ImageFormat::Jpeg;
    o.quality = quality;
    try {
        return encode(prepare(rgba, width, height, bytesPerRow, o), o, nullptr);
    } catch (const std::exception&) {
        return {};
    }
}

void writePng(const std::string& path, const std::uint16_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow) {
    ExportOptions o;
    o.format = ImageFormat::Png;
    o.quality = 1.0f;
    writeImage(path, rgba, width, height, bytesPerRow, o);
}

}  // namespace orion::util

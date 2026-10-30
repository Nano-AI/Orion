// Export on Windows and Linux (#376): ImageWriter.mm's job without ImageIO.
//
// OpenCV's imgcodecs encodes (libpng, libjpeg-turbo, libtiff underneath) and
// resamples; OpenCV is already the merge aligner's dependency, so this adds
// no library. libjxl writes JPEG XL as on macOS. The order is ImageWriter.mm's
// and is the feature: resample, sharpen, watermark, then quantise.
//
// ⚠ What this writer does not do, by design until a library is chosen:
//   - colour spaces other than sRGB. Display P3 and Adobe RGB throw, said,
//     rather than relabel sRGB pixels (ImageWriter.mm explains why that is the
//     classic desaturated-file bug). The pixels are sRGB, and PNG, JPEG and
//     TIFF without a profile are read as sRGB.
//   - the camera's metadata (`metadataFrom`, `rating`). ImageIO read it on
//     macOS; exiv2 is GPL (#10), so a file here carries none.

#include "util/ExportOps.h"
#include "util/ImageWriter.h"
#include "util/JxlWriter.h"
#include "util/Utf8Path.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
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
    if (o.space != ColorSpace::Srgb) {
        throw std::runtime_error("Display P3 and Adobe RGB export are not available on this "
                                 "system yet; export as sRGB");
    }
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

    unsharpMask(img.ptr<std::uint16_t>(), std::size_t(img.cols), std::size_t(img.rows),
                img.step, unsharpFor(o.sharpen));

    // Porter-Duff "over" with the mask as alpha, in the file's own encoding,
    // the mask stretched over the frame as CoreGraphics does (DECISIONS #284).
    if (o.watermark.active()) {
        const cv::Mat mask(int(o.watermark.height), int(o.watermark.width), CV_8UC1,
                           const_cast<std::uint8_t*>(o.watermark.mask));
        cv::Mat cover;
        cv::resize(mask, cover, img.size(), 0, 0, cv::INTER_LINEAR);
        for (int y = 0; y < img.rows; ++y) {
            auto* p = img.ptr<std::uint16_t>(y);
            const auto* m = cover.ptr<std::uint8_t>(y);
            for (int x = 0; x < img.cols; ++x) {
                const float a = float(m[x]) / 255.0f;
                for (int c = 0; c < 3; ++c) {
                    const float mark = std::clamp(o.watermark.rgb[c], 0.0f, 1.0f) * 65535.0f;
                    p[4 * x + c] = std::uint16_t(std::lround(a * mark + (1.0f - a) * p[4 * x + c]));
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

std::vector<std::uint8_t> encode(const cv::Mat& rgba, const ExportOptions& o) {
    if (o.format == ImageFormat::Jxl) {
        JxlInput in;
        in.pixels = rgba.data;
        in.width = std::uint32_t(rgba.cols);
        in.height = std::uint32_t(rgba.rows);
        in.bytesPerRow = rgba.step;
        in.bits = rgba.depth() == CV_16U ? 16 : 8;
        in.quality = o.quality;
        return encodeJxl(in);   // no ICC: sRGB
    }
    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    const int q = int(std::lround(std::clamp(o.quality, 0.0f, 1.0f) * 100.0f));
    const char* ext = o.format == ImageFormat::Png ? ".png"
                    : o.format == ImageFormat::Tiff ? ".tif" : ".jpg";
    std::vector<int> params;
    if (o.format == ImageFormat::Jpeg) params = {cv::IMWRITE_JPEG_QUALITY, q};
    std::vector<std::uint8_t> out;
    if (!cv::imencode(ext, bgr, out, params)) throw std::runtime_error("could not encode image");
    return out;
}

}  // namespace

void writeImage(const std::string& path, const std::uint16_t* rgba,
                std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow,
                const ExportOptions& options) {
    const auto bytes = encode(prepare(rgba, width, height, bytesPerRow, options), options);
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
        return encode(prepare(rgba, width, height, bytesPerRow, options), options).size();
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
        return encode(prepare(rgba, width, height, bytesPerRow, o), o);
    } catch (const std::exception&) {
        return {};
    }
}

void writePng(const std::string& path, const std::uint16_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow) {
    writeImage(path, rgba, width, height, bytesPerRow,
               {ImageFormat::Png, 1.0f, 0, ColorSpace::Srgb});
}

}  // namespace orion::util

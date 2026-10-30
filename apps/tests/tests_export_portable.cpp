// Export on Windows and Linux (ImageWriterPortable.cpp, decision #376).
//
// macOS reads its exports back through ImageIO, a second reader on purpose
// (tests_io.cpp, tests_watermark.cpp, tests_jxl.cpp). Here the only reader at
// hand is OpenCV, which also wrote the file, so these hold what a shared
// reader can still catch: the size, the depth, the channel order, the resize,
// the watermark landing, and the refusals the writer promises.

#include "harness.h"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

constexpr std::uint32_t kW = 64, kH = 48;

std::vector<std::uint16_t> gradient() {
    std::vector<std::uint16_t> px(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            std::uint16_t* p = &px[(std::size_t(y) * kW + x) * 4];
            p[0] = std::uint16_t(1000 * x);    // red rises left to right
            p[1] = std::uint16_t(1000 * y);    // green top to bottom
            p[2] = 30000;
            p[3] = 65535;
        }
    return px;
}

std::string scratch(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

std::vector<unsigned char> bytesOf(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

}  // namespace

void testExportFormats() {
    section("Export (Windows/Linux writer)");
    using namespace orion::util;
    report(formatForPath("a.PNG") == ImageFormat::Png, "extension is case-insensitive");
    report(formatForPath("a.tiff") == ImageFormat::Tiff, "tiff by extension");
    report(formatForPath("a.jxl") == ImageFormat::Jxl, "jxl by extension");

    const auto px = gradient();
    const std::size_t stride = std::size_t(kW) * 8;

    for (BitDepth depth : {BitDepth::Sixteen, BitDepth::Eight}) {
        const bool wide = depth == BitDepth::Sixteen;
        for (ImageFormat f : {ImageFormat::Png, ImageFormat::Tiff}) {
            const std::string path = scratch(f == ImageFormat::Png ? "orion-export.png" : "orion-export.tif");
            ExportOptions o;
            o.format = f;
            o.depth = depth;
            try { writeImage(path, px.data(), kW, kH, stride, o); }
            catch (const std::exception& e) { report(false, path + " writes", e.what()); continue; }
            const cv::Mat back = cv::imread(path, cv::IMREAD_UNCHANGED);
            const std::string what = path + (wide ? " at 16 bits" : " at 8 bits");
            report(back.cols == int(kW) && back.rows == int(kH), what + ": the size survives");
            report(back.depth() == (wide ? CV_16U : CV_8U), what + ": the depth survives");
            if (back.empty() || back.channels() < 3) continue;
            // OpenCV is BGR: red is channel 2. At (10, 5) red is 10000, green 5000.
            const double scale = wide ? 1.0 : 65535.0 / 255.0;
            const double red = (wide ? back.at<cv::Vec3w>(5, 10)[2] : back.at<cv::Vec3b>(5, 10)[2]) * scale;
            const double green = (wide ? back.at<cv::Vec3w>(5, 10)[1] : back.at<cv::Vec3b>(5, 10)[1]) * scale;
            report(std::abs(red - 10000) <= (wide ? 0 : 200) && std::abs(green - 5000) <= (wide ? 0 : 200),
                   what + ": red and green stay in their channels",
                   "red " + std::to_string(red) + " green " + std::to_string(green));
        }
    }

    {
        ExportOptions o;
        o.format = ImageFormat::Jpeg;
        o.maxDimension = 32;
        const std::string path = scratch("orion-export.jpg");
        writeImage(path, px.data(), kW, kH, stride, o);
        const cv::Mat back = cv::imread(path, cv::IMREAD_UNCHANGED);
        report(back.cols == 32 && back.rows == 24, "a long edge of 32 resizes 64x48 to 32x24",
               std::to_string(back.cols) + "x" + std::to_string(back.rows));
        report(back.depth() == CV_8U, "JPEG is eight bits whatever was asked");
        const auto jpeg = encodeJpeg(px.data(), kW, kH, stride, 0.8f);
        report(jpeg.size() > 2 && jpeg[0] == 0xFF && jpeg[1] == 0xD8, "encodeJpeg makes a JPEG");
        report(encodedSize(px.data(), kW, kH, stride, o) > 0, "the size readout encodes");
    }

    {
        ExportOptions o;
        o.format = ImageFormat::Png;
        o.space = ColorSpace::DisplayP3;
        bool threw = false;
        try { writeImage(scratch("orion-export-p3.png"), px.data(), kW, kH, stride, o); }
        catch (const std::exception&) { threw = true; }
        report(threw, "Display P3 is refused, not written as relabelled sRGB");
    }
}

void testExportWatermark() {
    section("Export watermark (Windows/Linux writer)");
    using namespace orion::util;
    const auto px = gradient();
    const std::vector<std::uint8_t> mask(16 * 12, 255);   // covers everything, stretched
    ExportOptions o;
    o.format = ImageFormat::Png;
    o.watermark.mask = mask.data();
    o.watermark.width = 16;
    o.watermark.height = 12;
    o.watermark.rgb[0] = 1.0f; o.watermark.rgb[1] = 0.0f; o.watermark.rgb[2] = 0.0f;
    const std::string path = scratch("orion-export-mark.png");
    writeImage(path, px.data(), kW, kH, std::size_t(kW) * 8, o);
    const cv::Mat back = cv::imread(path, cv::IMREAD_UNCHANGED);
    const bool red = !back.empty() && back.at<cv::Vec3w>(20, 30) == cv::Vec3w(0, 0, 65535);
    report(red, "a full-coverage red mark paints the frame red");
}

void testExportJxl() {
    section("Export JPEG XL (Windows/Linux writer)");
    using namespace orion::util;
    const auto px = gradient();
    for (float q : {0.9f, 1.0f}) {
        ExportOptions o;
        o.format = ImageFormat::Jxl;
        o.quality = q;
        const std::string path = scratch("orion-export.jxl");
        try { writeImage(path, px.data(), kW, kH, std::size_t(kW) * 8, o); }
        catch (const std::exception& e) { report(false, "JPEG XL writes", e.what()); continue; }
        const auto b = bytesOf(path);
        // A bare codestream opens FF 0A; a container opens with the 12-byte JXL box.
        const bool jxl = (b.size() > 2 && b[0] == 0xFF && b[1] == 0x0A) ||
                         (b.size() > 12 && b[4] == 'J' && b[5] == 'X' && b[6] == 'L');
        report(jxl, std::string("a JPEG XL file at quality ") + (q >= 1.0f ? "1 (lossless)" : "0.9"));
    }
}

#pragma once

#include <cstdint>
#include <string>

namespace orion::util {

enum class ImageFormat { Png, Jpeg, Tiff };

/// What the written file is tagged as, and converted to.
///
/// ⚠️ The pipeline's display transform ends in Rec.709 primaries and saturates
/// there, so today no pixel Orion produces lies outside sRGB's gamut. Choosing
/// a wider space converts and tags correctly — which is what a print shop or a
/// managed workflow needs — but it cannot add saturation that the transform
/// never generated. Widening the gamut for real means giving the display node
/// its output primaries as a parameter; see research/color-pipeline.md.
enum class ColorSpace { Srgb, DisplayP3, AdobeRgb };

struct ExportOptions {
    ImageFormat format = ImageFormat::Jpeg;
    /// JPEG only, 0..1. Ignored by PNG and TIFF, which are lossless.
    float quality = 0.9f;
    /// Longest edge in pixels; 0 keeps full resolution.
    std::uint32_t maxDimension = 0;
    ColorSpace space = ColorSpace::Srgb;
};

/// Writes 8-bit RGBA pixels. Throws std::runtime_error on failure.
///
/// Resampling, when requested, happens in CoreGraphics rather than on the GPU:
/// export is not on the interaction path, and a correct downscale with proper
/// filtering matters more here than shaving milliseconds.
void writeImage(const std::string& path, const std::uint16_t* rgba,
                std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow,
                const ExportOptions& options);

/// Convenience for the PNG case, used by the bench harness.
/// Encodes to memory and reports the byte count, without writing anything.
/// The export panel needs a size it can trust before you commit to the write,
/// and only a real encode gives one.
[[nodiscard]] std::size_t encodedSize(const std::uint16_t* rgba,
                                      std::uint32_t width, std::uint32_t height,
                                      std::size_t bytesPerRow,
                                      const ExportOptions& options);

void writePng(const std::string& path, const std::uint16_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow);

/// Picks a format from the path's extension, defaulting to JPEG.
ImageFormat formatForPath(const std::string& path);

}  // namespace orion::util

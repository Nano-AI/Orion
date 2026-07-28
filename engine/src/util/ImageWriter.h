#pragma once

#include <cstdint>
#include <string>

namespace orion::util {

enum class ImageFormat { Png, Jpeg, Tiff };

struct ExportOptions {
    ImageFormat format = ImageFormat::Jpeg;
    /// JPEG only, 0..1. Ignored by PNG and TIFF, which are lossless.
    float quality = 0.9f;
    /// Longest edge in pixels; 0 keeps full resolution.
    std::uint32_t maxDimension = 0;
};

/// Writes 8-bit RGBA pixels. Throws std::runtime_error on failure.
///
/// Resampling, when requested, happens in CoreGraphics rather than on the GPU:
/// export is not on the interaction path, and a correct downscale with proper
/// filtering matters more here than shaving milliseconds.
void writeImage(const std::string& path, const std::uint8_t* rgba,
                std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow,
                const ExportOptions& options);

/// Convenience for the PNG case, used by the bench harness.
void writePng(const std::string& path, const std::uint8_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow);

/// Picks a format from the path's extension, defaulting to JPEG.
ImageFormat formatForPath(const std::string& path);

}  // namespace orion::util

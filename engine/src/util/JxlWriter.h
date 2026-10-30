#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orion::util {

/// One picture, ready for libjxl: pixels as the writer's contexts hold them
/// (RGBX, the fourth sample padding), and the metadata already serialised.
struct JxlInput {
    const void* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t bytesPerRow = 0;
    int bits = 16;                       ///< 8 or 16; 16 is little-endian
    float quality = 0.9f;                ///< 0..1; 1 is mathematically lossless
    std::vector<std::uint8_t> icc;       ///< the space the pixels are in; empty is sRGB
    std::vector<std::uint8_t> exif;      ///< TIFF-header EXIF, no "Exif\0\0"; may be empty
    std::vector<std::uint8_t> xmp;       ///< an XMP packet; may be empty
};

/// Encodes with libjxl (BSD-3-Clause, dynamically linked; NOTICE). Throws
/// std::runtime_error on failure.
///
/// Quality maps through libjxl's own `JxlEncoderDistanceFromQuality`, the
/// mapping `cjxl -q` uses, so 0.9 here is `cjxl -q 90`. At 1.0 the frame is
/// encoded losslessly, as `cjxl -q 100` is (DECISIONS #363).
[[nodiscard]] std::vector<std::uint8_t> encodeJxl(const JxlInput& in);

}  // namespace orion::util

/*  Preview — the DNG's embedded viewable rendering, shared by the HDR merge
 *  and the stacker. Not the develop pipeline's rendering and not trying to be:
 *  an embedded preview is the camera-JPEG of the file (WB, the camera matrix
 *  into sRGB, a shoulder, gamma). Moved out of HdrMerge.cpp unchanged.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orion::merge {

struct Preview {
    std::vector<std::uint8_t> jpeg;
    std::uint32_t width = 0, height = 0;
};

/// rgb: packed linear camera RGB; gainEv: the BaselineExposure the reader applies.
/// Upright: the EXIF turn for `flip` is baked into the pixels. Empty jpeg on failure.
[[nodiscard]] Preview buildPreview(const std::vector<float>& rgb, std::uint32_t width, std::uint32_t height,
                                   float gainEv, const std::array<float, 4>& camMul,
                                   const std::array<float, 9>& camToXyz, int flip);

}  // namespace orion::merge

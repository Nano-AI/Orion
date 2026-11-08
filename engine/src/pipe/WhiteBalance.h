/*  White balance — color temperature and tint to camera multipliers.
 *
 *  The camera records raw sensor response; making a neutral surface look
 *  neutral means scaling each channel so the illuminant maps to equal values.
 *  Rather than exposing those multipliers directly (meaningless numbers that
 *  differ per camera), we let the user pick a color temperature in Kelvin and
 *  derive the multipliers from the black-body locus.
 */

#pragma once

#include <array>

namespace orion::pipe {

struct WhiteBalance {
    float temperatureK = 5500.0f;
    float tint         = 0.0f;   // -1..1, green (-) to magenta (+)
};

/// Per-channel multipliers (R, G, B) that neutralize the given illuminant for
/// a camera whose XYZ->camera matrix is `xyzToCam`, row-major 3x3.
/// Normalized so green is 1.
/// The white point this temperature and tint describe, in CIE 1931 xy.
///
/// Exposed because it is the part worth checking against Adobe directly: the
/// multipliers that follow depend on the camera matrix, so a disagreement there
/// could be either the locus or the profile, and the test would not say which.
void whitePointXy(const WhiteBalance&, float& x, float& y);

std::array<float, 3> multipliersFor(const WhiteBalance&, const float xyzToCam[9]);

/// Best-fitting temperature and tint for a set of as-shot multipliers, so the
/// UI can open on the camera's own choice rather than an arbitrary default.
WhiteBalance estimateFrom(const std::array<float, 3>& camMul, const float xyzToCam[9]);

/// The temperature and tint under which `patch`, camera RGB as rendered with
/// `gains` (green 1), comes out neutral: the white balance eyedropper's
/// inversion. `asShotMul` and `asShotRef` are the camera's multipliers and the
/// locus's at its estimate, the anchor the render divides by
/// (DevelopPipeline::applyCapture). research/white-balance.md.
WhiteBalance neutralFor(const std::array<float, 3>& patch, const std::array<float, 3>& gains,
                        const std::array<float, 3>& asShotMul, const std::array<float, 3>& asShotRef,
                        const float xyzToCam[9]);

/// Lightroom's fixed presets, in its order, as points on the DNG model.
/// Daylight, Cloudy and Shade sit on the CIE daylight locus, +10 of Adobe's
/// tint units above the Planckian; the rest on the Planckian.
/// research/white-balance.md, research/UNSOURCED.md §33.
struct WhiteBalancePreset { const char* name; WhiteBalance wb; };
inline constexpr float kDaylightTint = 10.0f / 150.0f;
inline constexpr WhiteBalancePreset kWhiteBalancePresets[] = {
    {"Daylight", {5500.0f, kDaylightTint}}, {"Cloudy", {6500.0f, kDaylightTint}},
    {"Shade", {7500.0f, kDaylightTint}},    {"Tungsten", {2850.0f, 0.0f}},
    {"Fluorescent", {3800.0f, 0.0f}},       {"Flash", {5500.0f, 0.0f}},
};

}  // namespace orion::pipe

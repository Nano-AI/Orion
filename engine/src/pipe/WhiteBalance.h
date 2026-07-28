/*  White balance — colour temperature and tint to camera multipliers.
 *
 *  The camera records raw sensor response; making a neutral surface look
 *  neutral means scaling each channel so the illuminant maps to equal values.
 *  Rather than exposing those multipliers directly (meaningless numbers that
 *  differ per camera), we let the user pick a colour temperature in Kelvin and
 *  derive the multipliers from the black-body locus.
 */

#pragma once

#include <array>

namespace orion::pipe {

struct WhiteBalance {
    float temperatureK = 5500.0f;
    float tint         = 0.0f;   // -1..1, green (-) to magenta (+)
};

/// Per-channel multipliers (R, G, B) that neutralise the given illuminant for
/// a camera whose XYZ->camera matrix is `xyzToCam`, row-major 3x3.
/// Normalised so green is 1.
std::array<float, 3> multipliersFor(const WhiteBalance&, const float xyzToCam[9]);

/// Best-fitting temperature and tint for a set of as-shot multipliers, so the
/// UI can open on the camera's own choice rather than an arbitrary default.
WhiteBalance estimateFrom(const std::array<float, 3>& camMul, const float xyzToCam[9]);

}  // namespace orion::pipe

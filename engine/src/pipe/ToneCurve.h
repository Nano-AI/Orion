/*  Tone curve — control points to lookup table.
 *
 *  Monotone cubic Hermite (Fritsch–Carlson), the same interpolation the design
 *  mockup uses and the same one Lightroom and darktable use. It passes through
 *  every control point and cannot overshoot between them: a plain Catmull-Rom
 *  would put a wiggle in the curve you did not ask for, which shows up in an
 *  image as banding or a tonal reversal.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orion::pipe {

struct CurvePoint {
    float x = 0.0f;  // input,  0..1
    float y = 0.0f;  // output, 0..1
};

/// One channel's curve. Two points (identity) unless the user adds more.
using CurveChannel = std::vector<CurvePoint>;

struct ToneCurveSpec {
    CurveChannel master{{0.0f, 0.0f}, {1.0f, 1.0f}};
    CurveChannel red{{0.0f, 0.0f}, {1.0f, 1.0f}};
    CurveChannel green{{0.0f, 0.0f}, {1.0f, 1.0f}};
    CurveChannel blue{{0.0f, 0.0f}, {1.0f, 1.0f}};

    [[nodiscard]] bool isIdentity() const noexcept;
};

/// Rows are master, red, green, blue — matching tone_curve.slang.
inline constexpr std::uint32_t kCurveResolution = 256;
inline constexpr std::uint32_t kCurveRows       = 4;

/// Evaluates all four channels into a kCurveResolution x kCurveRows table of
/// floats, laid out row-major for direct upload.
std::vector<float> buildCurveLut(const ToneCurveSpec&);

/// Evaluates a single channel at x. Exposed for tests and for the UI's own
/// preview drawing, so both sides agree on the shape.
float evaluateCurve(const CurveChannel&, float x);

}  // namespace orion::pipe

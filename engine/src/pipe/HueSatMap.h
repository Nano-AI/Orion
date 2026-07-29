/*  The camera profile's hue/saturation table, and the space it lives in.
 *
 *  Builds a DNG-shaped HueSatMap: HueDivisions x SaturationDivisions entries of
 *  (hue shift in degrees, saturation scale, value scale), stored in the spec's
 *  nested loop order with saturation innermost. ValueDivisions is 1.
 *
 *  Orion has no .dcp for any body yet, so the table is *populated* by a fitted
 *  correction in one hue region rather than read from a profile — but it is
 *  populated into the real structure, so a loader is a reader and nothing else.
 *
 *  research/camera-profiles.md carries the citations. Matrices are Lindbloom's
 *  published RGB/XYZ values with the Bradford adaptation, kept as the three
 *  factors rather than one pre-multiplied product so each is checkable.
 */

#pragma once

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

namespace orion::pipe::huesat {

using Mat3 = std::array<float, 9>;

/// Adobe's usual grid is 90 x 25 (dcpTool). Matching it means a real profile of
/// the common shape needs no resampling.
inline constexpr int kHueDivisions = 90;
inline constexpr int kSatDivisions = 25;
inline constexpr int kValDivisions = 1;

// ── The published matrices ────────────────────────────────────────────────
//
// Bruce Lindbloom, "RGB/XYZ Matrices" and "Chromatic Adaptation":
// http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
// ProPhoto (ROMM RGB) is a D50 space and Rec.2020 is D65, so the Bradford
// transform between the white points is required, not optional.

inline constexpr Mat3 kRec2020ToXyzD65{
    0.6369580f, 0.1446169f, 0.1688810f,
    0.2627002f, 0.6779981f, 0.0593017f,
    0.0000000f, 0.0280727f, 1.0609851f};

inline constexpr Mat3 kBradfordD65ToD50{
     1.0478112f,  0.0228866f, -0.0501270f,
     0.0295424f,  0.9904844f, -0.0170491f,
    -0.0092345f,  0.0150436f,  0.7521316f};

inline constexpr Mat3 kXyzD50ToProPhoto{
     1.3459433f, -0.2556075f, -0.0511118f,
    -0.5445989f,  1.5081673f,  0.0205351f,
     0.0000000f,  0.0000000f,  1.2118128f};

inline Mat3 multiply(const Mat3& a, const Mat3& b) noexcept {
    Mat3 m{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] +
                           a[r * 3 + 1] * b[1 * 3 + c] +
                           a[r * 3 + 2] * b[2 * 3 + c];
    return m;
}

inline Mat3 invert(const Mat3& m) noexcept {
    const float d = m[0] * (m[4] * m[8] - m[5] * m[7]) -
                    m[1] * (m[3] * m[8] - m[5] * m[6]) +
                    m[2] * (m[3] * m[7] - m[4] * m[6]);
    const float k = (d != 0.0f) ? 1.0f / d : 0.0f;
    return Mat3{ (m[4] * m[8] - m[5] * m[7]) * k,
                -(m[1] * m[8] - m[2] * m[7]) * k,
                 (m[1] * m[5] - m[2] * m[4]) * k,
                -(m[3] * m[8] - m[5] * m[6]) * k,
                 (m[0] * m[8] - m[2] * m[6]) * k,
                -(m[0] * m[5] - m[2] * m[3]) * k,
                 (m[3] * m[7] - m[4] * m[6]) * k,
                -(m[0] * m[7] - m[1] * m[6]) * k,
                 (m[0] * m[4] - m[1] * m[3]) * k };
}

inline Mat3 rec2020ToProPhoto() noexcept {
    return multiply(kXyzD50ToProPhoto, multiply(kBradfordD65ToD50, kRec2020ToXyzD65));
}
inline Mat3 proPhotoToRec2020() noexcept { return invert(rec2020ToProPhoto()); }

// ── The fitted correction ─────────────────────────────────────────────────
//
// One region, fitted against two independent renderings of the same frame
// (the camera's own JPEG and Apple's RAW pipeline) by sweeping the four
// numbers below and measuring the rendered sky's R/B and G/B ratios. The
// method and the numbers are in research/camera-profiles.md.
//
// Blue only, because blue is the only hue where the matrix's error is visible
// on an ordinary photograph: skylight is the most saturated, most narrow-band
// stimulus a camera meets outdoors, and no fixed 3x3 can be right for it
// (Luther-Ives). Everything else the matrix handles.

struct Correction {
    float centreDeg   = 0.0f;   ///< hue the correction is centred on
    float widthDeg    = 0.0f;   ///< half-width of the raised-cosine window
    float hueShiftDeg = 0.0f;   ///< positive rotates toward the next hue
    float satScale    = 1.0f;   ///< multiplies saturation at full weight
    float satOnset    = 0.0f;   ///< saturation where the correction fades in
    float satFull     = 1.0f;   ///< saturation where it reaches full weight
};

/// The fit. Overridable through `ORION_HUESAT="centre,width,shift,satScale"` —
/// that hook is the instrument the constants were measured with, and leaving it
/// in is what makes them re-measurable when a second body arrives.
///
/// **Measured on `_PIC8095.ARW`**, scored as the mean absolute error in R/B and
/// G/B over two sky patches against the mean of the camera's own JPEG and
/// Apple's RAW rendering, with a foliage patch and a white sign watched to
/// catch a correction that had spread outside blue:
///
///     shift    sat 0.95   sat 1.00   sat 1.05
///      -8      0.0542     0.0262     0.0051   <- taken
///     -10      0.0550     0.0268     0.0158
///     -12      0.0556     0.0270     0.0311
///     -14      0.0557     0.0300     0.0464
///
/// Centre and width were checked the same way at the chosen shift: 250 degrees
/// beats 235 (0.0072) and 265 (0.0181), and a 60-degree half-width beats 40
/// (0.0057). The surface is shallow toward 235 and steep past 265, which is
/// what a real hue region looks like — the fit is not balanced on a spike.
///
/// At the taken values the saturated upper sky lands at R/B 0.451 against a
/// target of 0.450, and the hazier lower sky at 0.636 against 0.647 — one set
/// of numbers, two saturations, because the correction is weighted by
/// saturation rather than applied flat across the hue.
inline Correction blueSky() noexcept {
    Correction c{};
    c.centreDeg   = 250.0f;
    c.widthDeg    =  60.0f;
    c.hueShiftDeg =  -8.0f;
    c.satScale    =   1.05f;
    c.satOnset    =   0.10f;
    c.satFull     =   0.35f;

    if (const char* v = std::getenv("ORION_HUESAT"); v != nullptr && *v != '\0') {
        float a = c.centreDeg, b = c.widthDeg, d = c.hueShiftDeg, e = c.satScale;
        if (std::sscanf(v, "%f,%f,%f,%f", &a, &b, &d, &e) == 4) {
            c.centreDeg = a; c.widthDeg = b; c.hueShiftDeg = d; c.satScale = e;
        }
    }
    return c;
}

inline float smoothStep(float edge0, float edge1, float x) noexcept {
    if (edge1 <= edge0) return x >= edge1 ? 1.0f : 0.0f;
    const float t = (x - edge0) / (edge1 - edge0);
    const float u = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return u * u * (3.0f - 2.0f * u);
}

/// RGBA rows, `kSatDivisions` wide and `kHueDivisions` tall: R is the hue shift
/// in degrees, G the saturation scale, B the value scale, A unused. That is the
/// spec's entry order with the spec's loop order — saturation innermost.
///
/// The spec requires every zero-saturation entry to carry a value scale of 1.0.
/// This table keeps the *whole* neutral column at (0, 1, 1), which is stronger
/// and is what makes a grey ramp provably survive the node untouched.
inline std::vector<float> buildTable(const Correction& c) {
    std::vector<float> table(static_cast<std::size_t>(kHueDivisions) * kSatDivisions * 4);

    for (int h = 0; h < kHueDivisions; ++h) {
        const float hue = 360.0f * static_cast<float>(h) / static_cast<float>(kHueDivisions);

        // Angular distance the short way round, so a window straddling 0 works.
        float delta = std::fmod(hue - c.centreDeg + 540.0f, 360.0f) - 180.0f;
        const float t = (c.widthDeg > 0.0f) ? std::fabs(delta) / c.widthDeg : 2.0f;
        // Raised cosine: one at the centre, zero and flat at the edges, so a
        // gradient crossing out of the window does not show a seam.
        const float hueWeight =
            (t >= 1.0f) ? 0.0f : 0.5f * (1.0f + std::cos(3.14159265f * t));

        for (int s = 0; s < kSatDivisions; ++s) {
            const float sat = static_cast<float>(s) / static_cast<float>(kSatDivisions - 1);
            const float w   = hueWeight * smoothStep(c.satOnset, c.satFull, sat);

            const std::size_t i = (static_cast<std::size_t>(h) * kSatDivisions + s) * 4;
            table[i + 0] = c.hueShiftDeg * w;
            table[i + 1] = 1.0f + (c.satScale - 1.0f) * w;
            table[i + 2] = 1.0f;
            table[i + 3] = 0.0f;
        }
    }
    return table;
}

}  // namespace orion::pipe::huesat

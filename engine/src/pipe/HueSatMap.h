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
// Regions, each fitted against two independent renderings of the same frame
// (the camera's own JPEG and Apple's RAW pipeline) by sweeping the numbers
// below and measuring the rendered patch's hue against both. The method and
// the numbers are in research/camera-profiles.md.
//
// ⚠ **"Everything else the matrix handles" (the line this comment used to
// end on) was never checked past blue, and decision #225 found it false.**
// A second, independent measurement — hue only, not R/B — found the same
// class of defect in the orange/tan region: 35 of 37 chromatic patches
// across 9 frames rotate the same direction against both references, mean
// +8.24° against the camera JPEG where the two references disagree with
// each other by +2.09°. The Luther-Ives argument that motivated `blueSky()`
// is not blue-specific — no fixed 3×3 is correct for every narrow-band
// reflectance, and warm/earth tones (bark, soil, skin) are the other
// register where real-world spectra commonly fall outside what a matrix
// alone can carry. `buildTable` now composes as many regions as are passed
// to it; two so far.

struct Correction {
    float centerDeg   = 0.0f;   ///< hue the correction is centerd on
    float widthDeg    = 0.0f;   ///< half-width of the raised-cosine window
    float hueShiftDeg = 0.0f;   ///< positive rotates toward the next hue
    float satScale    = 1.0f;   ///< multiplies saturation at full weight
    float satOnset    = 0.0f;   ///< saturation where the correction fades in
    float satFull     = 1.0f;   ///< saturation where it reaches full weight
};

/// The fit. Overridable through `ORION_HUESAT="center,width,shift,satScale"` —
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
/// Center and width were checked the same way at the chosen shift: 250 degrees
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
    c.centerDeg   = 250.0f;
    c.widthDeg    =  60.0f;
    c.hueShiftDeg =  -8.0f;
    c.satScale    =   1.05f;
    c.satOnset    =   0.10f;
    c.satFull     =   0.35f;

    if (const char* v = std::getenv("ORION_HUESAT"); v != nullptr && *v != '\0') {
        float a = c.centerDeg, b = c.widthDeg, d = c.hueShiftDeg, e = c.satScale;
        if (std::sscanf(v, "%f,%f,%f,%f", &a, &b, &d, &e) == 4) {
            c.centerDeg = a; c.widthDeg = b; c.hueShiftDeg = d; c.satScale = e;
        }
    }
    return c;
}

/// The second region — decision #225. Same method as `blueSky()`, this time
/// scored on hue directly (HSV, circular, linearized sRGB so the two 8-bit
/// references and Orion's own linear buffer are compared in the same domain)
/// rather than R/B and G/B, because what was reported and measured was a hue
/// rotation, not a saturated-blue ratio.
///
/// **Measured on `DSC09762.ARW` and `DSC09759.ARW`** (11 bark/soil/driftwood
/// patches total, `~/Pictures/sept 5th forks/`), mean |hue error| in degrees
/// of the real `--batch-export`ed render against the mean of the camera's own
/// JPEG and macOS ImageIO, sweeping shift at center 38 / width 45:
///
///     shift     MAE     mean(orion - camera)
///       0      7.171          9.315
///      -4      4.022          6.142
///      -6      2.797          4.618
///      -8      1.869          3.105
///      -9      1.764          2.361
///     -10      1.685          1.620   <- taken
///     -11      1.755          0.890
///     -12      2.154          0.165
///
/// -10 both minimizes the error against the two references and lands
/// `mean(orion - camera)` at 1.62°, next to the macOS-camera baseline itself
/// (+2.09°, decision #225's own measurement) — matching two independent
/// renderers rather than copying one, which is what -12's near-zero number
/// would have done. Center and width checked the same way at shift -10: 38/45
/// (MAE 1.685) beat 30/45 (1.976), 45/45 (1.837), 38/30 (1.980) and 38/60
/// (2.071). `satScale` left at 1.0: the chroma deficit (`research/
/// UNSOURCED.md` §3) is `saturation`/`vibrance` shipping at zero against
/// `contrast` at 1.45, not this table, and stacking a scale here on top of an
/// unmeasured global one would double-count whichever turns out to be real —
/// re-measured after this fix in `research/color-pipeline.md`.
///
/// Overridable through `ORION_HUESAT_WARM="center,width,shift,satScale"`,
/// same shape as `ORION_HUESAT` — the instrument the numbers above were
/// measured with.
inline Correction warmTan() noexcept {
    Correction c{};
    c.centerDeg   = 38.0f;
    c.widthDeg    = 45.0f;
    c.hueShiftDeg = -10.0f;
    c.satScale    =   1.0f;
    c.satOnset    =   0.05f;
    c.satFull     =   0.20f;

    if (const char* v = std::getenv("ORION_HUESAT_WARM"); v != nullptr && *v != '\0') {
        float a = c.centerDeg, b = c.widthDeg, d = c.hueShiftDeg, e = c.satScale;
        if (std::sscanf(v, "%f,%f,%f,%f", &a, &b, &d, &e) == 4) {
            c.centerDeg = a; c.widthDeg = b; c.hueShiftDeg = d; c.satScale = e;
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
/// and is what makes a gray ramp provably survive the node untouched.
///
/// Takes as many regions as are fitted — decision #225 added a second, far
/// enough from the first (blue, centered 250°) on the wheel that their
/// raised-cosine windows do not meet, so each cell is touched by at most one
/// region's non-zero weight in practice. Composed as a sum of hue shifts and a
/// product of saturation scales rather than assuming disjointness outright,
/// so two regions that did overlap would blend instead of one silently
/// clobbering the other.
inline std::vector<float> buildTable(const std::vector<Correction>& regions) {
    std::vector<float> table(static_cast<std::size_t>(kHueDivisions) * kSatDivisions * 4);

    for (int h = 0; h < kHueDivisions; ++h) {
        const float hue = 360.0f * static_cast<float>(h) / static_cast<float>(kHueDivisions);

        for (int s = 0; s < kSatDivisions; ++s) {
            const float sat = static_cast<float>(s) / static_cast<float>(kSatDivisions - 1);

            float hueShift = 0.0f;
            float satScale = 1.0f;
            for (const Correction& c : regions) {
                // Angular distance the short way round, so a window
                // straddling 0 works.
                const float delta = std::fmod(hue - c.centerDeg + 540.0f, 360.0f) - 180.0f;
                const float t = (c.widthDeg > 0.0f) ? std::fabs(delta) / c.widthDeg : 2.0f;
                // Raised cosine: one at the center, zero and flat at the
                // edges, so a gradient crossing out of the window does not
                // show a seam.
                const float hueWeight =
                    (t >= 1.0f) ? 0.0f : 0.5f * (1.0f + std::cos(3.14159265f * t));
                const float w = hueWeight * smoothStep(c.satOnset, c.satFull, sat);
                hueShift += c.hueShiftDeg * w;
                satScale *= 1.0f + (c.satScale - 1.0f) * w;
            }

            const std::size_t i = (static_cast<std::size_t>(h) * kSatDivisions + s) * 4;
            table[i + 0] = hueShift;
            table[i + 1] = satScale;
            table[i + 2] = 1.0f;
            table[i + 3] = 0.0f;
        }
    }
    return table;
}

}  // namespace orion::pipe::huesat

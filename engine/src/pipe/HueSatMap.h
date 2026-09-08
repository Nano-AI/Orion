/*  The camera profile's hue/saturation table, and the space it lives in.
 *
 *  Builds a DNG-shaped HueSatMap: HueDivisions x SaturationDivisions entries of
 *  (hue shift in degrees, saturation scale, value scale), stored in the spec's
 *  nested loop order with saturation innermost. ValueDivisions is 1.
 *
 *  Orion has no .dcp for any body yet, so the table is *populated* by fitted
 *  corrections (two hue regions and, decision #232, a full-wheel saturation
 *  curve) rather than read from a profile — but it is populated into the real
 *  structure, so a loader is a reader and nothing else.
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
/// (2.071). `satScale` left at 1.0 here: the note this line used to carry —
/// that the chroma deficit was `saturation`/`vibrance` shipping at zero —
/// was **wrong** (decisions #226, #229): reds render *more* saturated than
/// the camera, which a global shortfall cannot produce. The real, hue-shaped
/// gap is `satCurve()`'s job below, not this region's.
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

// ── The residual saturation curve — decision #232 ─────────────────────────
//
// `blueSky()` and `warmTan()` fix HUE. Even with both applied, camera JPEGs
// still render warm hues (orange/yellow) markedly less saturated than Orion,
// and red/magenta slightly more — a defect in *saturation*, not rotation.
//
// A third `Correction` region cannot fix it: red and orange need opposite
// corrections only 30° apart on the wheel, and the narrowest region already
// in the table (`warmTan`, half-width 45°) bleeds past its own neighbor
// before it reaches full strength — raising its `satScale` was tried and
// overshoots red by the time orange arrives (research/camera-profiles.md).
// So this is fitted directly into the table's own 90 hue bins (4° each)
// rather than through the wide raised-cosine shape above.
//
// Fitted by `tools/huesatfit.py --fit` against real per-hue-bin pixel data —
// the camera's own embedded JPEG vs a real `--batch-export`ed render,
// aggregated over a 26-frame corpus of the developer's own shoot — not
// swept by hand like the two regions above. Method, corpus and the
// convergence table are in research/camera-profiles.md.
//
// ⚠ **Clamped to [0.65, 1.35], and one band (red) did not fully converge
// inside it.** Boosting orange/yellow's saturation to close *their* gap
// measurably raises red's — a direct test with only orange/yellow boosted
// moved red's own ratio from 1.04 to 1.31 with red's curve untouched at
// 1.0 — so red is chasing a target that moves every round the correction
// runs. It converges (12 rounds, monotonically after round 3), just slowly:
// worst residual left is red's 0.196, against orange 0.004, yellow 0.062,
// green 0.001, cyan 0.048, blue 0.005, magenta 0.128. The clamp is not
// hiding this — pushing the bound wider chases the same slow asymptote
// further at more risk to the rest of the wheel, for a diminishing return
// per round; research/camera-profiles.md has the full table.
//
// ⚠ **This is a camera-matching profile, not a correctness fix** — decision
// #229 found Orion already within 2% of Apple's own RAW pipeline. Closing
// the larger gap to the camera JPEG means matching one body's house style,
// the way Lightroom ships a camera profile, and it is recorded as that
// rather than as a bug.
inline std::array<float, kHueDivisions> satCurve() noexcept {
    // clang-format off
    static constexpr std::array<float, kHueDivisions> kFitted = {
        0.650f, 0.708f, 0.766f, 0.824f, 0.882f, 0.940f, 0.997f, 1.055f, 1.102f, 1.137f,
        1.173f, 1.208f, 1.244f, 1.279f, 1.315f, 1.350f, 1.328f, 1.306f, 1.284f, 1.263f,
        1.241f, 1.219f, 1.197f, 1.175f, 1.153f, 1.131f, 1.110f, 1.088f, 1.066f, 1.044f,
        1.022f, 1.044f, 1.066f, 1.088f, 1.110f, 1.131f, 1.153f, 1.175f, 1.197f, 1.219f,
        1.241f, 1.263f, 1.284f, 1.306f, 1.328f, 1.350f, 1.311f, 1.271f, 1.232f, 1.192f,
        1.153f, 1.114f, 1.074f, 1.035f, 0.995f, 0.956f, 0.917f, 0.897f, 0.883f, 0.869f,
        0.855f, 0.842f, 0.828f, 0.814f, 0.801f, 0.787f, 0.773f, 0.760f, 0.746f, 0.732f,
        0.718f, 0.705f, 0.691f, 0.677f, 0.664f, 0.650f, 0.650f, 0.650f, 0.650f, 0.650f,
        0.650f, 0.650f, 0.650f, 0.650f, 0.650f, 0.650f, 0.650f, 0.650f, 0.650f, 0.650f,
    };
    // clang-format on
    std::array<float, kHueDivisions> c = kFitted;

    // `ORION_HUESAT_CURVE`: exactly `kHueDivisions` comma-separated scales,
    // hue order starting at 0°. The instrument `--fit` drives while
    // converging; a malformed or wrong-length value is ignored outright
    // rather than partially applied, so a truncated env var cannot silently
    // zero the back half of the wheel.
    if (const char* v = std::getenv("ORION_HUESAT_CURVE"); v != nullptr && *v != '\0') {
        std::array<float, kHueDivisions> parsed{};
        const char* p = v;
        int n = 0;
        for (; n < kHueDivisions && *p != '\0'; ++n) {
            char* end = nullptr;
            parsed[static_cast<std::size_t>(n)] = std::strtof(p, &end);
            if (end == p) break;
            p = (*end == ',') ? end + 1 : end;
        }
        if (n == kHueDivisions) c = parsed;
    }
    return c;
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
///
/// `curve`, if given, is decision #232's per-bin saturation-only fit
/// (`satCurve()`) multiplied in on top of the regions, gated by the same
/// low-saturation fade every region already uses. **The curve reaches every
/// hue**, unlike a region, so `kCurveSatOnset`/`Full` (0.08/0.22) sit
/// between the two regions' own onset values rather than below them —
/// slightly more conservative than `warmTan`'s 0.05/0.20 since a curve with
/// no hue it skips is more likely to catch faint, incidental chroma (sensor
/// noise, a demosaic edge) that a 45-60° window would simply miss. ⚠ It is
/// not a complete guard: `testCreativeVignetteGpu`'s flat fixture has a
/// demosaic-edge color cast at its corners around **0.29 saturation**,
/// above even this threshold, and the curve legitimately touches it —
/// caught as a widened corner-spread tolerance in that test (decision
/// #232), not as a lower onset here, because a threshold high enough to
/// exclude a 0.29-saturation corner would exclude real mid-saturation
/// photographic content too. The zero-saturation column stays exactly
/// (0, 1, 1) whatever the curve says: `smoothStep` is 0 there regardless of
/// onset/full, same as a region.
inline std::vector<float> buildTable(const std::vector<Correction>& regions,
                                     const std::array<float, kHueDivisions>* curve = nullptr) {
    std::vector<float> table(static_cast<std::size_t>(kHueDivisions) * kSatDivisions * 4);

    constexpr float kCurveSatOnset = 0.08f;
    constexpr float kCurveSatFull  = 0.22f;

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
            if (curve != nullptr) {
                const float w = smoothStep(kCurveSatOnset, kCurveSatFull, sat);
                satScale *= 1.0f + ((*curve)[static_cast<std::size_t>(h)] - 1.0f) * w;
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

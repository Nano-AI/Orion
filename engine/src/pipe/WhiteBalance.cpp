#include "pipe/WhiteBalance.h"

#include <algorithm>
#include <cmath>

namespace orion::pipe {
namespace {

/// Robertson's isotemperature lines, as Adobe's DNG SDK ships them.
///
/// `r` is reciprocal megakelvin (T = 1e6/r), `u` and `v` are the Planckian
/// radiator's CIE 1960 UCS coordinates there, and `t` is the slope dv/du of the
/// isotemperature line through it.
///
/// Method: Robertson, A. R., "Computation of Correlated Color Temperature and
/// Distribution Temperature", JOSA 58(11), 1968, 1528–1535. The numbers are not
/// his — Adobe's own comment attributes them to Wyszecki & Stiles, *Color
/// Science*, 2nd ed., Table 1(3.11), p. 228.
///
/// ⚠️ **Row r = 325 carries a typo, and it is kept deliberately.** Adobe ships
/// u = 0.24702 there; Bruce Lindbloom's transcription and every implementation
/// descended from it use 0.24792, and recomputing the locus from Planck's law
/// against the CIE 1931 2° observer puts the correct value at 0.247924 — the
/// error at that row is two hundred times any other row's. So 0.24702 is a
/// genuine mistake in the source book, copied verbatim into the DNG SDK and
/// still shipping.
///
/// Orion keeps it, because the goal here is to agree with Adobe rather than
/// with physics: a photographer cross-checking a tungsten frame against
/// Lightroom should see the same numbers. Correcting it would move the white
/// point by up to 0.0011 in xy around 3080 K — about 23 K and 1.1 tint units.
/// Recorded in research/color-pipeline.md.
struct Isotherm { double r, u, v, t; };

static const Isotherm kTempTable[] = {
    {   0, 0.18006, 0.26352,   -0.24341 },
    {  10, 0.18066, 0.26589,   -0.25479 },
    {  20, 0.18133, 0.26846,   -0.26876 },
    {  30, 0.18208, 0.27119,   -0.28539 },
    {  40, 0.18293, 0.27407,   -0.30470 },
    {  50, 0.18388, 0.27709,   -0.32675 },
    {  60, 0.18494, 0.28021,   -0.35156 },
    {  70, 0.18611, 0.28342,   -0.37915 },
    {  80, 0.18740, 0.28668,   -0.40955 },
    {  90, 0.18880, 0.28997,   -0.44278 },
    { 100, 0.19032, 0.29326,   -0.47888 },
    { 125, 0.19462, 0.30141,   -0.58204 },
    { 150, 0.19962, 0.30921,   -0.70471 },
    { 175, 0.20525, 0.31647,   -0.84901 },
    { 200, 0.21142, 0.32312,   -1.0182  },
    { 225, 0.21807, 0.32909,   -1.2168  },
    { 250, 0.22511, 0.33439,   -1.4512  },
    { 275, 0.23247, 0.33904,   -1.7298  },
    { 300, 0.24010, 0.34308,   -2.0637  },
    { 325, 0.24702, 0.34655,   -2.4681  },
    { 350, 0.25591, 0.34951,   -2.9641  },
    { 375, 0.26400, 0.35200,   -3.5814  },
    { 400, 0.27218, 0.35407,   -4.3633  },
    { 425, 0.28039, 0.35577,   -5.3762  },
    { 450, 0.28863, 0.35714,   -6.7262  },
    { 475, 0.29685, 0.35823,   -8.5955  },
    { 500, 0.30505, 0.35907,  -11.324   },
    { 525, 0.31320, 0.35968,  -15.628   },
    { 550, 0.32129, 0.36011,  -23.325   },
    { 575, 0.32931, 0.36038,  -40.770   },
    { 600, 0.33724, 0.36051, -116.45    },
};
static constexpr int kTempRows = int(sizeof(kTempTable) / sizeof(kTempTable[0]));

/// Adobe's `kTintScale`. Purely a unit conversion: the displacement along the
/// isotemperature line is `tint / -3000`. The sign is what makes positive tint
/// read as magenta in the picture, matching the slider everyone already knows.
inline constexpr double kTintScale = -3000.0;

/// Orion's tint is -1…1; Adobe's is ±150, which is also Lightroom's range.
inline constexpr double kTintPerUnit = 150.0;

/// `dng_temperature::Get_xy_coord` — temperature and tint to a white point.
///
/// The tint offset moves **along the interpolated isotemperature line**, in
/// CIE 1960 UCS. That is the correction this replaces: shifting CIE 1931 y by a
/// constant was wrong in the space, wrong in the direction (which is
/// temperature-dependent), and wrong in the scale.
void locusXy(float cct, float tintUnits, float& outX, float& outY) {
    const double r = 1.0e6 / std::clamp(double(cct), 1000.0, 1.0e6);
    const double offset = double(tintUnits) * kTintPerUnit / kTintScale;

    for (int i = 0; i <= kTempRows - 2; ++i) {
        if (r < kTempTable[i + 1].r || i == kTempRows - 2) {
            // Weight of the first line. Note this brackets on reciprocal
            // temperature, not on temperature — the table is uniform in r.
            const double f = (kTempTable[i + 1].r - r) /
                             (kTempTable[i + 1].r - kTempTable[i].r);

            double u = kTempTable[i].u * f + kTempTable[i + 1].u * (1.0 - f);
            double v = kTempTable[i].v * f + kTempTable[i + 1].v * (1.0 - f);

            // Unit vectors along each bracketing isotherm, blended and
            // renormalized — not the blend of the slopes, which is not the same
            // thing once they are steep.
            const double len1 = std::sqrt(1.0 + kTempTable[i].t * kTempTable[i].t);
            const double len2 = std::sqrt(1.0 + kTempTable[i + 1].t * kTempTable[i + 1].t);

            const double uu1 = 1.0 / len1, vv1 = kTempTable[i].t / len1;
            const double uu2 = 1.0 / len2, vv2 = kTempTable[i + 1].t / len2;

            double uu3 = uu1 * f + uu2 * (1.0 - f);
            double vv3 = vv1 * f + vv2 * (1.0 - f);
            const double len3 = std::sqrt(uu3 * uu3 + vv3 * vv3);
            uu3 /= len3;
            vv3 /= len3;

            u += uu3 * offset;
            v += vv3 * offset;

            // CIE 1960 uv back to CIE 1931 xy.
            const double denom = u - 4.0 * v + 2.0;
            outX = float(1.5 * u / denom);
            outY = float(v / denom);
            return;
        }
    }
}

std::array<float, 3> applyMatrix(const float m[9], const std::array<float, 3>& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
            m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

}  // namespace

void whitePointXy(const WhiteBalance& wb, float& x, float& y) {
    locusXy(wb.temperatureK, wb.tint, x, y);
}

std::array<float, 3> multipliersFor(const WhiteBalance& wb, const float xyzToCam[9]) {
    float x = 0.0f, y = 0.0f;
    locusXy(wb.temperatureK, wb.tint, x, y);
    y = std::max(y, 1e-4f);

    // xy at unit luminance -> XYZ.
    const std::array<float, 3> xyz{x / y, 1.0f, (1.0f - x - y) / y};

    // The illuminant as the camera sees it. Neutralizing means dividing by it.
    const auto cam = applyMatrix(xyzToCam, xyz);

    std::array<float, 3> mul{};
    for (int i = 0; i < 3; ++i) {
        mul[i] = (std::abs(cam[i]) > 1e-6f) ? 1.0f / cam[i] : 1.0f;
    }

    // Normalize to green, matching what the linearize shader expects.
    const float g = (mul[1] != 0.0f) ? mul[1] : 1.0f;
    for (auto& m : mul) m /= g;
    return mul;
}

WhiteBalance estimateFrom(const std::array<float, 3>& camMul, const float xyzToCam[9]) {
    // The camera's multipliers came from its own estimate of the scene
    // illuminant; recover the temperature by finding the locus point whose
    // multipliers match. A coarse sweep then a refinement is plenty — this
    // runs once per file, and the answer only needs to be right to a few Kelvin.
    const float g = (camMul[1] != 0.0f) ? camMul[1] : 1.0f;
    const std::array<float, 3> target{camMul[0] / g, 1.0f, camMul[2] / g};

    auto errorAt = [&](float cct) {
        const auto m = multipliersFor({cct, 0.0f}, xyzToCam);
        const float dr = std::log(std::max(m[0], 1e-6f)) - std::log(std::max(target[0], 1e-6f));
        const float db = std::log(std::max(m[2], 1e-6f)) - std::log(std::max(target[2], 1e-6f));
        return dr * dr + db * db;
    };

    float best = 5500.0f, bestErr = errorAt(best);
    for (float cct = 2000.0f; cct <= 15000.0f; cct += 100.0f) {
        const float e = errorAt(cct);
        if (e < bestErr) { bestErr = e; best = cct; }
    }
    for (float cct = best - 100.0f; cct <= best + 100.0f; cct += 5.0f) {
        const float e = errorAt(cct);
        if (e < bestErr) { bestErr = e; best = cct; }
    }

    // Tint is whatever green offset is still needed once temperature is fixed.
    const auto atBest = multipliersFor({best, 0.0f}, xyzToCam);
    float tint = 0.0f, tintErr = 1e30f;
    for (float t = -1.0f; t <= 1.0f; t += 0.01f) {
        const auto m = multipliersFor({best, t}, xyzToCam);
        const float dr = m[0] - target[0], db = m[2] - target[2];
        const float e = dr * dr + db * db;
        if (e < tintErr) { tintErr = e; tint = t; }
    }
    (void)atBest;

    return {best, tint};
}

}  // namespace orion::pipe

#pragma once

/*  Lens correction geometry — the one place it lives.
 *
 *  Three sources describe a lens correction, in three coordinate systems:
 *
 *    lensfun   ptlens/poly3, r normalized by half the frame diagonal
 *    DNG       WarpRectilinear / FixVignetteRadial (DNG 1.6 ch. 7), r by the
 *              distance from the optical center to the farthest pixel
 *    Adobe LCP the Adobe Camera Model, r in units of the focal length, which
 *              the file stores as a fraction of the larger image dimension
 *
 *  `LensModel` holds any of them **without pixels**: the full-resolution graph
 *  and the decimated preview graph receive the same `Adjustments` and must
 *  each place the optical center on their own frame. `resolve` puts pixels on
 *  it; `fetchPoint` maps an output pixel to the place the shader reads (the
 *  forward map, a copy of lens.slang line for line) and `outputPoint` is its
 *  inverse — the question region-of-interest rendering asks. Everything else
 *  (the autoscale, the tests) is built on those two.
 *
 *  See research/lens-corrections.md.
 */

#include "pipe/ShaderParams.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace orion::pipe {

/// What one unit of normalized radius is, for a given source.
enum class LensNorm : std::uint32_t {
    HalfDiagonal  = 0,   // lensfun: ½·√(w² + h²), center = ĉ·size
    FarthestPixel = 1,   // DNG: distance from the center to the farthest pixel
    MaxDimension  = 2,   // Adobe: focal × max(w, h), center = ĉ·max(w, h)
};

/// Where a correction came from. Precedence is DNG, then LCP, then lensfun;
/// a lens the photographer picked by hand is lensfun and overrides all three.
enum class LensSource : std::int32_t { None = 0, Lensfun = 1, AdobeLcp = 2, DngOpcode = 3 };

/// A lens correction, resolution-independent.
///
/// The warp maps an **output** position to the **source** position it reads —
/// the direction all three published models are written in.
///
///     d = (q − c) / norm,  r² = |d|²
///     f = k0 + k1·r + k2·r² + k3·r³ + k4·r⁴ + k6·r⁶
///     t = ( t0·2dxdy + t1·(r² + 2dx²),  t1·2dxdy + t0·(r² + 2dy²) )
///     source = c + f·(q − c) + norm·t      (= c + norm·(f·d + t))
///
/// ptlens fills k0..k3; DNG and Adobe fill k0, k2, k4, k6 and the tangential
/// pair (Adobe's k4, k5 are DNG's kt0, kt1 — the same Brown–Conrady terms).
struct LensModel {
    LensNorm norm = LensNorm::HalfDiagonal;
    float center[2]{0.5f, 0.5f};
    float focal[2]{1.0f, 1.0f};          // MaxDimension only
    float radial[3][6]{{1.0f}, {1.0f}, {1.0f}};   // per channel R, G, B
    float tangential[3][2]{};

    /// V(r) = 1 + v1·r² + v2·r⁴ + v3·r⁶ + v4·r⁸ + v5·r¹⁰. lensfun and Adobe
    /// publish a falloff (divide); DNG publishes a gain (multiply).
    LensNorm vigNorm = LensNorm::HalfDiagonal;
    float vigCenter[2]{0.5f, 0.5f};
    float vigFocal[2]{1.0f, 1.0f};
    float vig[5]{};
    bool  vigGain = false;
    /// Evaluated where the light fell (the source position) unless a DNG
    /// vignette opcode runs after the warp, which places it on the output.
    bool  vigAtOutput = false;

    bool operator==(const LensModel&) const = default;
};

namespace lens {

/// lensfun's ptlens (a, b, c) and pa (k1, k2, k3), which is also what the
/// manual sliders drive: distortion is `b` alone (poly3), vignetting `k1`.
inline LensModel ptlens(float a, float b, float c, float k1 = 0.0f, float k2 = 0.0f,
                        float k3 = 0.0f, float centerX = 0.5f, float centerY = 0.5f) {
    LensModel m;
    m.center[0] = m.vigCenter[0] = centerX;
    m.center[1] = m.vigCenter[1] = centerY;
    for (auto& k : m.radial) {
        k[0] = 1.0f - a - b - c;   // pins r_d(1) = 1: the corners stay put
        k[1] = c;
        k[2] = b;
        k[3] = a;
    }
    m.vig[0] = k1; m.vig[1] = k2; m.vig[2] = k3;
    return m;
}

namespace detail {
/// Center and per-axis norm in pixels for one normalization convention.
inline void place(LensNorm kind, const float c[2], const float focal[2],
                  float w, float h, float outCenter[2], float outNorm[2]) {
    switch (kind) {
        case LensNorm::FarthestPixel: {
            // DNG: c = x0 + ĉ·(x1 − x0) over pixel *indices*, so pixel i is
            // at i here and at i + ½ in Orion's coordinates.
            const float cx = c[0] * (w - 1.0f), cy = c[1] * (h - 1.0f);
            const float mx = std::max(cx, w - 1.0f - cx);
            const float my = std::max(cy, h - 1.0f - cy);
            outCenter[0] = cx + 0.5f;
            outCenter[1] = cy + 0.5f;
            outNorm[0] = outNorm[1] = std::max(std::sqrt(mx * mx + my * my), 1.0f);
            return;
        }
        case LensNorm::MaxDimension: {
            const float d = std::max(w, h);
            outCenter[0] = c[0] * d;
            outCenter[1] = c[1] * d;
            outNorm[0] = std::max(focal[0] * d, 1.0f);
            outNorm[1] = std::max(focal[1] * d, 1.0f);
            return;
        }
        case LensNorm::HalfDiagonal:
        default:
            outCenter[0] = c[0] * w;
            outCenter[1] = c[1] * h;
            outNorm[0] = outNorm[1] = std::max(0.5f * std::sqrt(w * w + h * h), 1.0f);
            return;
    }
}
}  // namespace detail

/// The shader block for one frame size. Scale and the manual TCA are left at
/// zero for the caller.
inline params::Lens resolve(const LensModel& m, std::uint32_t width, std::uint32_t height) {
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    params::Lens p{};
    p.size[0] = width;
    p.size[1] = height;
    detail::place(m.norm, m.center, m.focal, w, h, p.center, p.norm);
    detail::place(m.vigNorm, m.vigCenter, m.vigFocal, w, h, p.vigCenter, p.vigNorm);
    for (int ch = 0; ch < 3; ++ch) {
        for (int i = 0; i < 4; ++i) p.radialLo[ch][i] = m.radial[ch][i];
        p.radialHi[ch][0] = m.radial[ch][4];
        p.radialHi[ch][1] = m.radial[ch][5];
        p.radialHi[ch][2] = m.tangential[ch][0];
        p.radialHi[ch][3] = m.tangential[ch][1];
    }
    for (int i = 0; i < 4; ++i) p.vig[i] = m.vig[i];
    p.vig10 = m.vig[4];
    p.vigFlags = (m.vigGain ? 1u : 0u) | (m.vigAtOutput ? 2u : 0u);
    p.rDiag = std::max(0.5f * std::sqrt(w * w + h * h), 1.0f);
    return p;
}

/// The source position for one channel, from a (zoomed) output position, both
/// in pixels. **lens.slang's `warpSource`, line for line** — if these disagree
/// the autoscale is right for a transform nobody performs.
inline void warpSource(const params::Lens& p, int ch, float qx, float qy,
                       float& sx, float& sy) {
    const float ex = qx - p.center[0], ey = qy - p.center[1];
    const float dx = ex / p.norm[0], dy = ey / p.norm[1];
    const float r2 = dx * dx + dy * dy;
    const float r = std::sqrt(r2);
    const float* lo = p.radialLo[ch];
    const float* hi = p.radialHi[ch];
    const float f = lo[0] + r * (lo[1] + r * (lo[2] + r * lo[3])) + r2 * r2 * (hi[0] + r2 * hi[1]);
    const float tx = hi[2] * 2.0f * dx * dy + hi[3] * (r2 + 2.0f * dx * dx);
    const float ty = hi[3] * 2.0f * dx * dy + hi[2] * (r2 + 2.0f * dy * dy);
    sx = p.center[0] + f * ex + tx * p.norm[0];
    sy = p.center[1] + f * ey + ty * p.norm[1];
}

/// **The forward map.** Output pixel (x, y) — pixel centers are at i + ½ — to
/// the texel-space point the shader samples for channel `ch` (0 R, 1 G, 2 B),
/// including the autoscale zoom, the manual TCA and the half-texel the sampler
/// subtracts.
inline void fetchPoint(const params::Lens& p, int ch, float x, float y, float& fx, float& fy) {
    const float zoom = p.scale > 0.0f ? p.scale : 1.0f;
    const float qx = p.center[0] + (x - p.center[0]) * zoom;
    const float qy = p.center[1] + (y - p.center[1]) * zoom;
    float sx = 0.0f, sy = 0.0f;
    warpSource(p, ch, qx, qy, sx, sy);
    // Manual TCA rescales red and blue about the optical center, measured at
    // lensfun's radius so the slider means the same thing whatever the source.
    if (ch != 1) {
        const float ex = qx - p.center[0], ey = qy - p.center[1];
        const float ru2 = (ex * ex + ey * ey) / (p.rDiag * p.rDiag);
        const float k = 1.0f - (ch == 0 ? p.caRed : p.caBlue) * ru2;
        sx = p.center[0] + (sx - p.center[0]) * k;
        sy = p.center[1] + (sy - p.center[1]) * k;
    }
    fx = sx - 0.5f;
    fy = sy - 0.5f;
}

/// **The inverse map.** The output pixel whose fetch for channel `ch` lands on
/// texel-space point (fx, fy) — which source texels feed a screen rectangle is
/// `fetchPoint`; which screen pixels a source point shows up at is this.
///
/// Newton's method on `fetchPoint` with a central-difference Jacobian. The DNG
/// specification requires the warp to be invertible and monotone in each axis
/// (WarpRectilinear, "Notes and Restrictions"), so from the identity guess it
/// converges in a handful of steps; twenty is a ceiling, not an expectation.
/// Returns false if it did not converge to a hundredth of a pixel.
inline bool outputPoint(const params::Lens& p, int ch, float fx, float fy, float& x, float& y) {
    x = fx + 0.5f;
    y = fy + 0.5f;
    constexpr float h = 0.5f;
    for (int i = 0; i < 20; ++i) {
        float ax, ay, bx, by, cx, cy, ex, ey, gx, gy;
        fetchPoint(p, ch, x, y, gx, gy);
        const float rx = gx - fx, ry = gy - fy;
        if (std::abs(rx) < 1e-2f && std::abs(ry) < 1e-2f) return true;
        fetchPoint(p, ch, x + h, y, ax, ay);
        fetchPoint(p, ch, x - h, y, bx, by);
        fetchPoint(p, ch, x, y + h, cx, cy);
        fetchPoint(p, ch, x, y - h, ex, ey);
        const float j00 = (ax - bx) / (2 * h), j10 = (ay - by) / (2 * h);
        const float j01 = (cx - ex) / (2 * h), j11 = (cy - ey) / (2 * h);
        const float det = j00 * j11 - j01 * j10;
        if (std::abs(det) < 1e-8f) return false;
        x -= ( j11 * rx - j01 * ry) / det;
        y -= (-j10 * rx + j00 * ry) / det;
    }
    float gx, gy;
    fetchPoint(p, ch, x, y, gx, gy);
    return std::abs(gx - fx) < 1e-2f && std::abs(gy - fy) < 1e-2f;
}

/// The largest zoom that keeps every output pixel reading real data.
///
/// **Why this exists.** poly3 pins `r_d(1) = 1`, so the frame *corners* stay
/// put — but the frame is a rectangle: the edge midpoints sit at r ≈ 0.83 on a
/// 3:2 frame, where a negative k₁ reaches past the border. At the shipped
/// maximum that was 325 px past a 6024 px frame on `_PIC8148.ARW`, and the
/// sampler's clamp turned it into a band of one column smeared sideways. The
/// same is true of every DNG and Adobe warp, which pin nothing at all.
///
/// **What it does.** Bisects for the zoom at which the outermost fetch lands
/// exactly on the frame edge — what lensfun's `lf_modifier_get_auto_scale`,
/// darktable's "auto scale" and Lightroom's constrained crop all do.
///
/// **Why the perimeter is enough.** For a radial model the fetch distance grows
/// monotonically outward along any ray (for ptlens with |k₁| < ½, which the
/// sliders stay inside; for a DNG warp the specification *requires* it). A
/// monotone ray leaves the source rectangle where it leaves the destination,
/// so the perimeter bounds the interior. A tangential term breaks strict
/// radial symmetry but stays well inside that margin at real magnitudes.
///
/// Returns 1 when nothing overreaches, so pincushion and vignetting-only never
/// pay a zoom they do not need.
inline float autoScale(params::Lens p) {
    const float w = static_cast<float>(p.size[0]);
    const float h = static_cast<float>(p.size[1]);
    if (w < 2.0f || h < 2.0f) return 1.0f;

    // A thousandth of a texel of clamp is not visible, and the slack is what
    // keeps an uncorrected frame at exactly 1.0 instead of a hair under it.
    constexpr float kSlack = 1e-3f;
    constexpr int kPerEdge = 64;

    const auto fits = [&](float s) {
        p.scale = s;
        const auto inside = [&](float x, float y) {
            for (int ch = 0; ch < 3; ++ch) {
                float fx = 0.0f, fy = 0.0f;
                fetchPoint(p, ch, x, y, fx, fy);
                if (!(fx >= -kSlack && fx <= w - 1.0f + kSlack &&
                      fy >= -kSlack && fy <= h - 1.0f + kSlack)) return false;
            }
            return true;
        };
        for (int i = 0; i <= kPerEdge; ++i) {
            const float t = static_cast<float>(i) / kPerEdge;
            const float x = 0.5f + t * (w - 1.0f);
            const float y = 0.5f + t * (h - 1.0f);
            if (!inside(x, 0.5f) || !inside(x, h - 0.5f) ||
                !inside(0.5f, y) || !inside(w - 0.5f, y)) return false;
        }
        return true;
    };

    if (fits(1.0f)) return 1.0f;

    // Monotone in s, so bisection converges on the one crossing. Twenty-four
    // halvings of [0.05, 1] is well past float precision.
    float lo = 0.05f, hi = 1.0f;
    for (int i = 0; i < 24; ++i) {
        const float mid = 0.5f * (lo + hi);
        (fits(mid) ? lo : hi) = mid;
    }
    return lo;
}

/// The ptlens case, by coefficients. poly3's k₁ is the `b`-only case.
inline float autoScale(std::uint32_t width, std::uint32_t height,
                       float centerX, float centerY,
                       float a, float b, float c, float caRed, float caBlue) {
    auto p = resolve(ptlens(a, b, c, 0, 0, 0, centerX, centerY), width, height);
    p.caRed = caRed;
    p.caBlue = caBlue;
    return autoScale(p);
}

}  // namespace lens
}  // namespace orion::pipe

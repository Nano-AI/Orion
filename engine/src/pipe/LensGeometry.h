#pragma once

/*  Lens correction geometry that has to be known on the host.
 *
 *  The shader can only see one pixel. "Does the corrected picture still fill
 *  the frame?" is a question about all of them at once, so the answer is
 *  computed here and handed down as a uniform.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace orion::pipe::lens {

/// The largest zoom that keeps every output pixel reading real data.
///
/// **Why this exists.** poly3 pins `r_d(1) = 1`, so the frame *corners* stay
/// put — which is why the correction reads as a correction rather than as a
/// zoom, and why it looked finished. But r = 1 is the corner, and the frame is
/// a rectangle: the edge midpoints sit at r ≈ 0.83 on a 3:2 frame, where a
/// negative k₁ gives a multiplier of 1 + 0.31·|k₁|. At the shipped maximum
/// (k₁ = −0.35) that reaches 325 px past a 6024 px frame. `sampleClamped` then
/// returns the border pixel for every one of those, so the edges came out as
/// bands of one column smeared sideways. Measured on `_PIC8148.ARW`: three
/// columns 18 px apart returned identical means to four decimals.
///
/// **What it does.** Scales the sampling offset so the outermost thing the
/// shader reads lands exactly on the frame edge. The picture is magnified by a
/// few percent and the smeared band is replaced by real pixels — which is what
/// lensfun's `lf_modifier_get_auto_scale`, darktable's "auto scale" and
/// Lightroom's constrained crop all do, for this reason.
///
/// **Why the perimeter is enough.** Along any ray from the optical center the
/// fetch distance is `t·m(t)` with `m` the radial multiplier; its derivative is
/// `(1 − k₁) + 3·k₁·t²/R²`, positive throughout for |k₁| < ½, so the distance
/// grows monotonically outward. Both components scale by the same factor, so
/// each is largest where the ray leaves the destination rectangle. Checking the
/// perimeter therefore bounds the interior. The shipped range is |k₁| ≤ 0.35,
/// inside that bound.
///
/// Returns 1 when nothing overreaches, so pincushion (which pulls samples
/// inward) and vignetting-only never pay a zoom they do not need.
///
/// Takes ptlens `a`, `b`, `c`; poly3's k₁ is the `b`-only case.
inline float autoScale(std::uint32_t width, std::uint32_t height,
                       float centerX, float centerY,
                       float a, float b, float c, float caRed, float caBlue) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    if (w < 2.0f || h < 2.0f) return 1.0f;

    const float cx = centerX * w;
    const float cy = centerY * h;
    const float rNorm = 0.5f * std::sqrt(w * w + h * h);

    // A thousandth of a texel of clamp is not visible, and the slack is what
    // keeps an uncorrected frame at exactly 1.0 instead of a hair under it.
    constexpr float kSlack = 1e-3f;

    // Where the shader will actually fetch, in texel coordinates — including
    // the half-texel it subtracts to turn a pixel center into a texel index.
    // If this disagreed with the shader the scale would be right for a
    // transform nobody performs.
    const auto fetch = [&](float dx, float dy, float s, float& fx, float& fy) {
        const float px = dx * s, py = dy * s;
        const float ru = std::sqrt(px * px + py * py) / rNorm;
        const float r2 = ru * ru;
        // The same polynomial the shader evaluates. If these disagreed the
        // scale would be right for a transform nobody performs.
        const float distort = a * ru * r2 + b * r2 + c * ru + (1.0f - a - b - c);

        // The channel that reaches furthest bounds all three. Green is the
        // reference and rides at 1; red and blue can sit either side of it.
        const float widest = std::max({1.0f, 1.0f - caRed * r2, 1.0f - caBlue * r2});
        const float m = distort * widest;

        fx = cx + px * m - 0.5f;
        fy = cy + py * m - 0.5f;
    };

    // The destination rectangle's perimeter, walked as pixel centers.
    constexpr int kPerEdge = 64;
    const auto fits = [&](float s) {
        float fx = 0.0f, fy = 0.0f;
        const auto inside = [&](float dx, float dy) {
            fetch(dx, dy, s, fx, fy);
            return fx >= -kSlack && fx <= w - 1.0f + kSlack &&
                   fy >= -kSlack && fy <= h - 1.0f + kSlack;
        };

        for (int i = 0; i <= kPerEdge; ++i) {
            const float t = static_cast<float>(i) / kPerEdge;
            const float x = 0.5f + t * (w - 1.0f);   // left edge to right edge
            const float y = 0.5f + t * (h - 1.0f);
            if (!inside(x - cx, 0.5f - cy)) return false;          // top
            if (!inside(x - cx, h - 0.5f - cy)) return false;      // bottom
            if (!inside(0.5f - cx, y - cy)) return false;          // left
            if (!inside(w - 0.5f - cx, y - cy)) return false;      // right
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

}  // namespace orion::pipe::lens

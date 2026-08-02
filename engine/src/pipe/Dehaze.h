/*  Dehaze — the dark channel prior's constants, and the reduction the GPU
 *  cannot do on its own.
 *
 *  He, Sun & Tang, "Single Image Haze Removal Using Dark Channel Prior",
 *  CVPR 2009 and IEEE TPAMI 33(12), 2011. research/dehaze.md carries the
 *  quotations these numbers came out of.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace orion::pipe::dehaze {

/// The patch Omega in Eq. (5). "In the remainder of this paper, we use a patch
/// size of 15 x 15" (TPAMI section 4.4), so radius 7.
inline constexpr int kPatchRadius = 7;

/// Eq. (12)'s aerial-perspective term, at the paper's own value: "We fix it to
/// 0.95 for all results reported in this paper."
///
/// It is also the strength control. omega = 0 gives t = 1 everywhere, and
/// Eq. (16) is then exactly the identity — so the slider's zero is exact by
/// construction rather than by blending the result back afterwards.
inline constexpr float kOmega = 0.95f;

/// The floor on transmission in Eq. (16). "A typical value of t0 is 0.1."
inline constexpr float kT0 = 0.1f;

/// "We first pick the top 0.1 percent brightest pixels in the dark channel."
inline constexpr float kTopFraction = 0.001f;

/// The pooling factor for the candidates. See `airlightFrom`.
inline constexpr int kPeakScale = 4;

/// Guided-filter refinement. TPAMI 35 (2013) Fig. 16 uses r = 20 on a 600 x 400
/// image — 1/30 of the long edge — so the radius is expressed as that fraction
/// rather than as twenty pixels, which would be a fifth of the relative reach
/// on a 6024 px frame and would leave the patch edges in the transmission map.
inline constexpr int kGuideRadiusDivisor = 30;

/// Also from that figure. Meaningful because the guide is normalized into
/// [0, 1], as the paper's images are.
inline constexpr float kEpsilon = 1e-3f;

/// The subsampling the coefficients are solved at (He & Sun, "Fast Guided
/// Filter"). Same reasoning, and the same value, as the tone chain's.
inline constexpr int kGuideScale = 4;

/// One pooled candidate: the block's largest dark-channel value, and the color
/// of the pixel that produced it.
struct Candidate {
    float dark = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

/// The atmospheric light, from the pooled candidates.
///
/// The paper's two stages, in order: take the most haze-opaque pixels by dark
/// channel, then among *those* take the brightest in the input image. Doing it
/// the other way round — or skipping the first stage — hands A to a specular
/// highlight, and A is a constant the whole frame is then solved against.
///
/// ⚠️ The percentile is over pooled blocks rather than over pixels; see
/// research/UNSOURCED.md. Max pooling is the right pooling for a step looking
/// for extremes, but it is not literally the paper's top 0.1% of pixels.
[[nodiscard]] inline std::array<float, 3> airlightFrom(std::vector<Candidate> c) {
    if (c.empty()) return {1.0f, 1.0f, 1.0f};

    const std::size_t keep = std::max<std::size_t>(
        1, static_cast<std::size_t>(static_cast<double>(c.size()) * kTopFraction));

    std::nth_element(c.begin(), c.begin() + static_cast<std::ptrdiff_t>(keep - 1), c.end(),
                     [](const Candidate& a, const Candidate& b) { return a.dark > b.dark; });

    const Candidate* best = &c.front();
    float brightest = -1.0f;
    for (std::size_t i = 0; i < keep; ++i) {
        const float sum = c[i].r + c[i].g + c[i].b;
        if (sum > brightest) { brightest = sum; best = &c[i]; }
    }

    // A channel at zero would divide the frame by nothing in dehaze_chan; a
    // frame that dark has no haze to remove either way.
    return {std::max(best->r, 1e-4f), std::max(best->g, 1e-4f), std::max(best->b, 1e-4f)};
}

}  // namespace orion::pipe::dehaze

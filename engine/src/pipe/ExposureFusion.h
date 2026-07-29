/*  Simulated Exposure Fusion — the maths, and the reference it is checked
 *  against.
 *
 *  Hessel & Morel, WACV 2020, and Hessel, "Simulated Exposure Fusion", IPOL 9
 *  (2019) 469–484, on top of Mertens, Kautz & Van Reeth, Pacific Graphics 2007.
 *  research/exposure-fusion.md carries the quotations these came from.
 *
 *  Everything here works on an intensity `t` in [0, 1]. That is the domain the
 *  method is defined in — the restrained-range centre, the well-exposedness
 *  Gaussian at 0.5 and the median-derived split all assume it — so the mapping
 *  from Orion's scene-linear light into this domain is a separate decision made
 *  at the call site, not smuggled in here.
 */

#pragma once

#include "pipe/Pyramid.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace orion::pipe::sef {

/// Maximum contrast amplification. "The values alpha = 8 and beta = 0.5 are
/// recommended by the authors" — [H279] Fig. 5. Usable range 2…16, and this is
/// the one the user drives.
inline constexpr float kAlphaDefault = 8.0f;

/// The restrained dynamic range. beta = 1 makes `g` the identity and the method
/// degenerates to plain exposure fusion ([H278] Fig. 5), which is exactly the
/// behaviour with the halo and out-of-range artefacts this paper exists to fix.
inline constexpr float kBeta = 0.5f;

/// Decay speed of the smooth clip. Fixed by the authors and not exposed.
inline constexpr float kLambda = 0.125f;

/// Well-exposedness, inherited from Mertens et al. unchanged.
inline constexpr float kSigma = 0.2f;

/// The final stretch allows this much clipping at each end ([HM20] section 4).
inline constexpr float kRobustClip = 0.01f;

/// A bound on the simulated set. Every simulated image is a pyramid, so a
/// derivation that fails to converge must not run away.
inline constexpr int kMaxImages = 9;

/// ⚠️ **The search starts at the paper's own worked-example count rather than
/// at two.** [H279] Eq. (7) solves for the smallest M whose simulated images
/// still overlap, and the exact form of that constraint could not be
/// transcribed with enough confidence to implement it as the sole authority —
/// see research/UNSOURCED.md. Five is what [HM20] Fig. 10 reports for the
/// recommended alpha = 8, beta = 0.5, and starting there also fixes a real
/// defect: at M = 2 the median-derived split has only one image to allocate,
/// so a bright frame cannot get a darkened one at all and the asymmetry the
/// method depends on never appears.
inline constexpr int kMinImages = 5;

/// How many simulated images, and how they split between darker and brighter.
struct Plan {
    int images = 0;   // M
    int bright = 0;   // N,  indices k = 1..N
    int dark   = 0;   // N*, indices k = -N*..-1
    [[nodiscard]] int span()  const noexcept { return std::max(bright, dark); }  // Nmax
    [[nodiscard]] bool valid() const noexcept { return images >= 2; }
};

/// [H279] Eq. (4). The centre of image k's valid range, walking from near the
/// top of the range down to near the bottom as k increases.
[[nodiscard]] inline float rho(int k, const Plan& p, float beta) noexcept {
    const int total = p.bright + p.dark;          // M - 1, at least 1
    return 1.0f - beta * 0.5f -
           float(k + p.dark) * (1.0f - beta) / float(std::max(total, 1));
}

/// alpha^(|k| / Nmax) — the exposure factor's magnitude, used both to simulate
/// image k and to weight it.
[[nodiscard]] inline float factor(int k, const Plan& p, float alpha) noexcept {
    return std::pow(alpha, float(std::abs(k)) / float(std::max(p.span(), 1)));
}

/// [H279] Eq. (3). Both branches are >= 1, so every simulated image gains
/// contrast somewhere — the dark end is reached by shifting intensities toward
/// the bottom rather than by simulating a shorter exposure, which "would have
/// lower contrast and still no information in the clipped pixels".
[[nodiscard]] inline float simulate(float t, int k, const Plan& p, float alpha) noexcept {
    const float a = factor(k, p, alpha);
    return (k < 0) ? a * (t - 1.0f) + 1.0f : a * t;
}

/// [H279] Eq. (5). Identity inside the restrained range, and a 1/x² decay
/// outside it that never leaves [rho - a, rho + a].
///
/// C¹ at the join by construction: at |t - rho| = beta/2 the tail evaluates
/// lambda²/lambda = lambda, so a - lambda = beta/2, and both branches have
/// slope one there.
[[nodiscard]] inline float clip(float t, float centre, float beta, float lambda) noexcept {
    const float d = t - centre;
    const float m = std::fabs(d);
    if (m <= beta * 0.5f) return t;

    const float a = beta * 0.5f + lambda;
    const float b = beta * 0.5f - lambda;
    return (d < 0.0f ? -1.0f : 1.0f) * (a - lambda * lambda / (m - b)) + centre;
}

/// [H279] Eq. (10). The derivative of `clip`, which is the whole contrast
/// measure — Hessel & Morel replace Mertens' Laplacian filter with it, so there
/// is no filtering pass per simulated image at all.
[[nodiscard]] inline float clipSlope(float t, float centre, float beta, float lambda) noexcept {
    const float m = std::fabs(t - centre);
    if (m <= beta * 0.5f) return 1.0f;

    const float b = beta * 0.5f - lambda;
    const float denom = m - b;
    return lambda * lambda / (denom * denom);
}

/// The simulated image k, at intensity t: g(f(t, k), k).
[[nodiscard]] inline float remap(float t, int k, const Plan& p,
                                 float alpha, float beta) noexcept {
    return clip(simulate(t, k, p, alpha), rho(k, p, beta), beta, kLambda);
}

/// [H279] Eq. (9). **Implemented from the IPOL text on purpose**: the WACV
/// camera-ready's Eq. (11) prints these branch conditions swapped, contradicting
/// its own Eq. (9).
[[nodiscard]] inline float contrastWeight(float t, int k, const Plan& p,
                                          float alpha, float beta) noexcept {
    return factor(k, p, alpha) *
           clipSlope(simulate(t, k, p, alpha), rho(k, p, beta), beta, kLambda);
}

/// Mertens et al. section 3.1, one channel rather than three.
[[nodiscard]] inline float wellExposed(float t, float sigma) noexcept {
    const float d = t - 0.5f;
    return std::exp(-(d * d) / (2.0f * sigma * sigma));
}

/// Whether every neighbouring pair of simulated images still overlaps in range
/// — [H279] Eq. (7). Without the overlap the fused result has a gap in it.
[[nodiscard]] inline bool overlaps(const Plan& p, float alpha, float beta) noexcept {
    const auto edge = [&](int k, float side) {
        return simulate(rho(k, p, beta) + side * beta * 0.5f, k, p, alpha);
    };
    // The paper notes only k = -1, 0, +1 can bind in practice; checking every
    // pair costs nothing here and does not rely on that holding.
    for (int k = -p.dark + 1; k <= p.bright; ++k) {
        if (!(edge(k, +1.0f) > edge(k - 1, -1.0f))) return false;
    }
    for (int k = -p.dark; k <= p.bright - 1; ++k) {
        if (!(edge(k, -1.0f) < edge(k + 1, +1.0f))) return false;
    }
    return true;
}

/// [H279] Eq. (6) and Alg. 1. The count is solved, not chosen: the smallest set
/// whose ranges still overlap. The split between darker and brighter comes from
/// the image's median, because "contrast enhancement is generally needed in the
/// dark parts only".
[[nodiscard]] inline Plan planFor(float median, float alpha, float beta) noexcept {
    Plan best{};
    for (int m = kMinImages; m <= kMaxImages; ++m) {
        Plan p{};
        p.images = m;
        p.dark   = std::clamp(int(std::floor(float(m - 1) * std::clamp(median, 0.0f, 1.0f))),
                              0, m - 1);
        p.bright = m - p.dark - 1;
        best = p;
        if (overlaps(p, alpha, beta)) return p;
    }
    // Ran out of room. The largest set is the closest to satisfying it, and a
    // slightly gapped fusion beats refusing to render.
    return best;
}

// ── The reference ─────────────────────────────────────────────────────────
//
// Mertens' blend, on one channel: a Laplacian pyramid per simulated image,
// weighted by the Gaussian pyramid of that image's normalised weight, summed
// per level and collapsed. Slow and literal; it exists to be the thing the GPU
// is measured against.

/// No paper specifies a guard on the weight normalisation, and the denominator
/// can reach zero where every simulated image is equally hopeless. See
/// research/UNSOURCED.md.
inline constexpr float kWeightEpsilon = 1e-6f;

[[nodiscard]] inline pyr::Plane fuseReference(const pyr::Plane& v, const Plan& p,
                                              float alpha, float beta,
                                              float sigma, int levels) {
    const std::size_t n = v.v.size();

    // Weights first, so they can be normalised across the set before anything
    // is decomposed.
    std::vector<std::vector<float>> weight;
    std::vector<float> total(n, 0.0f);
    for (int k = -p.dark; k <= p.bright; ++k) {
        std::vector<float> w(n);
        for (std::size_t i = 0; i < n; ++i) {
            const float t = std::clamp(v.v[i], 0.0f, 1.0f);
            w[i] = wellExposed(remap(t, k, p, alpha, beta), sigma) *
                   contrastWeight(t, k, p, alpha, beta);
            total[i] += w[i];
        }
        weight.push_back(std::move(w));
    }

    std::vector<pyr::Plane> out;
    bool first = true;

    int index = 0;
    for (int k = -p.dark; k <= p.bright; ++k, ++index) {
        pyr::Plane image{v.width, v.height, std::vector<float>(n)};
        pyr::Plane wmap {v.width, v.height, std::vector<float>(n)};
        for (std::size_t i = 0; i < n; ++i) {
            image.v[i] = remap(std::clamp(v.v[i], 0.0f, 1.0f), k, p, alpha, beta);
            wmap.v[i]  = weight[std::size_t(index)][i] / std::max(total[i], kWeightEpsilon);
        }

        const std::vector<pyr::Plane> gi = pyr::gaussianPyramid(image, levels);
        const std::vector<pyr::Plane> gw = pyr::gaussianPyramid(wmap,  levels);

        for (int l = 0; l < levels; ++l) {
            // The Laplacian at l, except at the coarsest level which is the
            // residual and is carried whole.
            pyr::Plane lap = gi[std::size_t(l)];
            if (l + 1 < levels) {
                const pyr::Plane up = pyr::expand(gi[std::size_t(l) + 1],
                                                  lap.width, lap.height);
                for (std::size_t i = 0; i < lap.v.size(); ++i) lap.v[i] -= up.v[i];
            }

            if (first) {
                pyr::Plane zero{lap.width, lap.height,
                                std::vector<float>(lap.v.size(), 0.0f)};
                out.push_back(std::move(zero));
            }
            for (std::size_t i = 0; i < lap.v.size(); ++i) {
                out[std::size_t(l)].v[i] += gw[std::size_t(l)].v[i] * lap.v[i];
            }
        }
        first = false;
    }

    for (int l = levels - 2; l >= 0; --l) {
        pyr::Plane& dst = out[std::size_t(l)];
        const pyr::Plane up = pyr::expand(out[std::size_t(l) + 1], dst.width, dst.height);
        for (std::size_t i = 0; i < dst.v.size(); ++i) dst.v[i] += up.v[i];
    }
    return out.front();
}

/// [HM20] section 4 — stretch to [0, 1] allowing `clip` at each end. Not
/// optional: this method stretches rather than compresses, so the fused result
/// leaves the range by construction.
inline void robustNormalise(pyr::Plane& v, float clipFraction) {
    if (v.v.empty()) return;
    std::vector<float> sorted = v.v;
    std::sort(sorted.begin(), sorted.end());

    const std::size_t last = sorted.size() - 1;
    const auto at = [&](double f) {
        return sorted[std::min(last, std::size_t(f * double(last)))];
    };
    const float lo = at(clipFraction);
    const float hi = at(1.0 - clipFraction);

    // A frame whose clipped percentiles coincide has no range to stretch —
    // a flat field, or one value plus an outlier. Dividing by the epsilon
    // instead maps everything to a single end of the scale, which is a far
    // more destructive answer than leaving it alone.
    if (hi - lo < 1e-4f) return;

    const float range = hi - lo;
    for (float& t : v.v) t = std::clamp((t - lo) / range, 0.0f, 1.0f);
}

}  // namespace orion::pipe::sef

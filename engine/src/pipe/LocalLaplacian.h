/*  Local Laplacian filters — the constants, and the definition they are
 *  measured against.
 *
 *  Paris, Hasinoff & Kautz, SIGGRAPH 2011; Aubry, Paris, Hasinoff, Kautz &
 *  Durand, ACM TOG 33(5), 2014. research/local-laplacian.md.
 *
 *  Two things live here rather than in DevelopPipeline.cpp. The constants,
 *  because several of them are *derived from each other* and putting them
 *  side by side is what makes that visible. And `reference()`, which is Paris
 *  et al.'s Algorithm 1 written out literally — one pyramid per output
 *  coefficient, no approximation of any kind. It is far too slow to render
 *  with and it is not meant to: it is what the GPU's fast approximation is
 *  checked against, so that "the shader implements the paper" is a measured
 *  claim rather than a hopeful one.
 */

#pragma once

#include "pipe/Pyramid.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace orion::pipe::llf {

/// The filter runs on log2 luminance normalized into [0, 1] over this window.
/// Everything below is stated in units of the window, so a threshold means the
/// same thing on a night frame and on a beach.
inline constexpr float kWindowLoEv = -10.0f;
inline constexpr float kWindowHiEv = 2.0f;
inline constexpr float kWindowEv   = kWindowHiEv - kWindowLoEv;   // 12 stops

/// How many remapped pyramids get precomputed, and therefore how far apart the
/// gamma samples are.
inline constexpr int   kGammaLevels = 16;
inline constexpr float kGammaStep   = 1.0f / float(kGammaLevels - 1);

/// The threshold that separates detail from edges — 12/7 EV, or 1.714 stops.
/// A local swing smaller than that is texture; anything larger is an edge and
/// Eq. 2 passes it through untouched.
inline constexpr float kSigmaR = 1.0f / 7.0f;

/// **Deliberately finer than the paper recommends, because it was measured.**
///
/// Aubry et al. section 3: "for the detail enhancement, we recommend sampling
/// the intensity range every standard deviation sigma" — which would be eight
/// gamma levels here, and which is what Orion built first. Measured against
/// Paris et al.'s exact Algorithm 1 on a frame carrying a step edge, a fine
/// ripple and a ramp, at the strongest setting the slider reaches (alpha =
/// 0.25):
///
///     samples/sigma   gamma levels   mean error   max error
///     1.0              8             0.354 EV     1.359 EV
///     2.1             16             0.151 EV     0.696 EV
///     4.4             32             0.159 EV     0.408 EV
///
/// Sampling every sigma left 28.0 dB against the paper's own stated accuracy
/// of "above 30 dB", so it is not enough here. Doubling it recovers the paper's
/// figure; doubling it again buys nothing, which is what says the remaining
/// error is the linear interpolation and not the sampling rate. Sixteen is
/// therefore the measured knee and not a round number.
///
/// `testLocalLaplacianGpu` prints that table on every run, so if any of these
/// constants moves the evidence for the choice moves with it.
inline constexpr float kGammaOversample = kSigmaR / kGammaStep;   // 2.14

/// Six levels. The coarsest is about 190 px on a 6024 px frame — past that is
/// global contrast, which is the tone curve's job and not clarity's.
inline constexpr int kPyramidLevels = 6;

/// Paris et al. section 5.2, "Reducing Noise Amplification": tau is 0 below 1%
/// of the maximum intensity and 1 above 2%. In EV that is 0.12 to 0.24 stops.
inline constexpr float kNoiseLo = 0.01f;
inline constexpr float kNoiseHi = 0.02f;

/// A cap on what one pixel's luminance may be moved by, in stops. Not part of
/// the paper — a guard, so that a pathological frame cannot produce a value
/// the rest of the pipeline has to cope with.
inline constexpr float kMaxCorrectionEv = 4.0f;

/// The slider, mapped onto the published exponent.
///
/// alpha < 1 increases detail contrast and alpha > 1 smooths it (section 5.2).
/// The endpoints are chosen to land on the paper's own illustrated values —
/// 0.25 is Figure 7c, 4 is Figure 7b — rather than on numbers picked here.
/// The curve between them is ours; see research/UNSOURCED.md.
[[nodiscard]] inline float alphaForClarity(float clarity) noexcept {
    return std::exp2(-2.0f * std::clamp(clarity, -1.0f, 1.0f));
}

/// Paris et al. Eq. 1 and Eq. 2 with fd(D) = D^alpha and fe(a) = a, plus the
/// noise term from section 5.2. Must stay identical to `llfRemap` in
/// shaders/ops/llf_ops.slang — `testLocalLaplacianRemap` checks that the two
/// agree at the points where the branches meet.
[[nodiscard]] inline float remap(float i, float g, float sigmaR, float alpha,
                                 float noiseLo, float noiseHi) noexcept {
    const float d = i - g;
    const float a = std::fabs(d);
    if (a > sigmaR) return i;             // re, with fe the identity

    const float dn = a / std::max(sigmaR, 1e-8f);
    float fd = std::pow(std::max(dn, 1e-8f), alpha);

    if (alpha < 1.0f) {
        const float t = std::clamp((a - noiseLo) / std::max(noiseHi - noiseLo, 1e-8f),
                                   0.0f, 1.0f);
        const float tau = t * t * (3.0f - 2.0f * t);   // smoothstep
        fd = tau * fd + (1.0f - tau) * dn;
    }

    return g + (d < 0.0f ? -1.0f : 1.0f) * sigmaR * fd;
}

// The pyramid itself lives in Pyramid.h — exposure fusion blends over the same
// construction, and one copy is the only way two features stay comparable.
using pyr::Plane;
using pyr::nextSize;
using pyr::downsample;
using pyr::expandTaps;
using pyr::expand;
using pyr::gaussianPyramid;

/// Paris et al. Algorithm 1, without the sub-region optimization and without
/// Aubry's discretization: for every output coefficient, remap the whole image
/// and build the whole pyramid. O(N^2). Only ever call it on a small frame.
[[nodiscard]] inline Plane reference(const Plane& img, int levels, float sigmaR,
                                     float alpha, float noiseLo, float noiseHi) {
    const std::vector<Plane> gin = gaussianPyramid(img, levels);
    std::vector<Plane> out = gin;   // coarsest level is the residual, unchanged

    for (int l = 0; l < levels - 1; ++l) {
        Plane& dst = out[static_cast<std::size_t>(l)];
        for (int y = 0; y < dst.height; ++y) {
            for (int x = 0; x < dst.width; ++x) {
                const float g0 = gin[static_cast<std::size_t>(l)]
                                     .v[static_cast<std::size_t>(y) * dst.width + x];

                Plane remapped{img.width, img.height, {}};
                remapped.v.resize(img.v.size());
                for (std::size_t i = 0; i < img.v.size(); ++i)
                    remapped.v[i] = remap(img.v[i], g0, sigmaR, alpha, noiseLo, noiseHi);

                const std::vector<Plane> gr = gaussianPyramid(remapped, levels);
                const Plane up = expand(gr[static_cast<std::size_t>(l) + 1],
                                        dst.width, dst.height);
                const std::size_t idx = static_cast<std::size_t>(y) * dst.width + x;
                dst.v[idx] = gr[static_cast<std::size_t>(l)].v[idx] - up.v[idx];
            }
        }
    }

    // Collapse, coarse to fine.
    for (int l = levels - 2; l >= 0; --l) {
        Plane& dst = out[static_cast<std::size_t>(l)];
        const Plane up = expand(out[static_cast<std::size_t>(l) + 1],
                                dst.width, dst.height);
        for (std::size_t i = 0; i < dst.v.size(); ++i) dst.v[i] += up.v[i];
    }
    return out.front();
}

/// Aubry et al.'s approximation, on the CPU — the same algorithm the shaders
/// run, at whatever discretization you ask for.
///
/// Two references rather than one, because "the GPU disagrees with the paper"
/// has two very different causes and one number cannot tell them apart. This
/// one shares the discretization with the shaders and nothing else, so a gap
/// between it and the GPU is a bug in a shader; a gap between it and
/// `reference()` is the approximation doing what an approximation does, and
/// it has to shrink when `gammaCount` grows.
[[nodiscard]] inline Plane referenceFast(const Plane& img, int levels, int gammaCount,
                                         float sigmaR, float alpha,
                                         float noiseLo, float noiseHi) {
    const std::vector<Plane> gin = gaussianPyramid(img, levels);
    const float step = 1.0f / float(gammaCount - 1);

    std::vector<std::vector<Plane>> gr;
    gr.reserve(static_cast<std::size_t>(gammaCount));
    for (int j = 0; j < gammaCount; ++j) {
        Plane r{img.width, img.height, {}};
        r.v.resize(img.v.size());
        const float g = float(j) * step;
        for (std::size_t i = 0; i < img.v.size(); ++i)
            r.v[i] = remap(img.v[i], g, sigmaR, alpha, noiseLo, noiseHi);
        gr.push_back(gaussianPyramid(r, levels));
    }

    std::vector<Plane> out = gin;
    for (int l = levels - 2; l >= 0; --l) {
        Plane& dst = out[static_cast<std::size_t>(l)];
        const Plane up = expand(out[static_cast<std::size_t>(l) + 1],
                                dst.width, dst.height);
        std::vector<Plane> upG;
        upG.reserve(static_cast<std::size_t>(gammaCount));
        for (int j = 0; j < gammaCount; ++j) {
            upG.push_back(expand(gr[static_cast<std::size_t>(j)][static_cast<std::size_t>(l) + 1],
                                 dst.width, dst.height));
        }

        for (std::size_t i = 0; i < dst.v.size(); ++i) {
            const float v = gin[static_cast<std::size_t>(l)].v[i];
            const float t = std::clamp(v / step, 0.0f, float(gammaCount - 1));
            const int   j = std::min(int(std::floor(t)), gammaCount - 2);
            const float a = t - float(j);

            const float l0 = gr[static_cast<std::size_t>(j)][static_cast<std::size_t>(l)].v[i]
                           - upG[static_cast<std::size_t>(j)].v[i];
            const float l1 = gr[static_cast<std::size_t>(j) + 1][static_cast<std::size_t>(l)].v[i]
                           - upG[static_cast<std::size_t>(j) + 1].v[i];
            dst.v[i] = (1.0f - a) * l0 + a * l1 + up.v[i];
        }
    }
    return out.front();
}

}  // namespace orion::pipe::llf

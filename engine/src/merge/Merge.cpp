/*  HDR merge core — see Merge.h for the contract and the sources.
 *
 *  Everything here is deliberately scalar CPU code with no I/O: the merge is
 *  a one-shot batch operation, not an interaction path, and a plain loop that
 *  a test can pin against a synthetic stack beats a clever one nobody can
 *  check. If profiling in the orchestrator story says this is the wall, rows
 *  are independent and the loop parallelizes trivially — measured first,
 *  though, per the house rule.
 */

#include "merge/Merge.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace orion::merge {
namespace {

/// The scalar the weights and the ghost test run on. The data is camera RGB,
/// where no published luminance weights apply (they are defined for
/// colorimetric spaces); the plain mean is the honest choice and is stated
/// in research/hdr-merge.md.
float luma(const float rgb[3]) noexcept {
    return (rgb[0] + rgb[1] + rgb[2]) * (1.0f / 3.0f);
}

/// Keeps the inverse-variance weight finite when a frame declares no noise.
/// With every frame at (a, b) = 0 the denominators cancel and the weights
/// degrade to E² — the photon-limited ideal — rather than to a division by
/// zero.
constexpr float kVarianceFloor = 1e-12f;

struct Sample {
    float rgb[3] = {0.0f, 0.0f, 0.0f};
    bool  inside = false;
};

/// Bilinear tap of a frame at reference texel (x, y) through its dest->source
/// homography. Outside the frame's data is outside — the merge treats it as
/// "this frame has nothing to say here", never as black.
Sample tap(const Frame& f, float x, float y) noexcept {
    persp::apply(f.refToSource, x, y);

    Sample s;
    if (!(x >= 0.0f) || !(y >= 0.0f) ||
        x > float(f.width - 1) || y > float(f.height - 1)) {
        return s;
    }

    const auto x0 = std::uint32_t(x);
    const auto y0 = std::uint32_t(y);
    const std::uint32_t x1 = std::min(x0 + 1, f.width - 1);
    const std::uint32_t y1 = std::min(y0 + 1, f.height - 1);
    const float fx = x - float(x0);
    const float fy = y - float(y0);

    const float* p00 = f.rgb + (std::size_t(y0) * f.width + x0) * 3;
    const float* p10 = f.rgb + (std::size_t(y0) * f.width + x1) * 3;
    const float* p01 = f.rgb + (std::size_t(y1) * f.width + x0) * 3;
    const float* p11 = f.rgb + (std::size_t(y1) * f.width + x1) * 3;

    for (int c = 0; c < 3; ++c) {
        const float top = p00[c] + (p10[c] - p00[c]) * fx;
        const float bot = p01[c] + (p11[c] - p01[c]) * fx;
        s.rgb[c] = top + (bot - top) * fy;
    }
    s.inside = true;
    return s;
}

/// w_sat: 1 below the ramp, 0 above it, smoothstep between. The ramp exists
/// because near-clip data is *biased* — the shoulder bends before the stop —
/// so it must fade out rather than merely count for less.
float saturationWeight(float maxChannel, const Options& o) noexcept {
    if (maxChannel <= o.satRampLow)  return 1.0f;
    if (maxChannel >= o.satRampHigh) return 0.0f;
    const float t = (maxChannel - o.satRampLow) / (o.satRampHigh - o.satRampLow);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

}  // namespace

float lightGathered(float shutterSeconds, float iso, float fNumber) {
    if (shutterSeconds <= 0.0f || iso <= 0.0f || fNumber <= 0.0f) return 0.0f;
    return shutterSeconds * iso / (fNumber * fNumber);
}

float measuredExposureRatio(const Frame& frame, const Frame& reference,
                            int minSamples) {
    // The band where the ratio is trustworthy: above it the shoulder bends,
    // below it the noise floor dominates the quotient.
    constexpr float kLow = 0.1f, kHigh = 0.8f;

    // A grid, not every pixel: the median of fifty thousand ratios is the
    // same median at a fraction of the sort.
    const std::size_t pixels = std::size_t(reference.width) * reference.height;
    const auto step = std::max<std::uint32_t>(
        1, std::uint32_t(std::sqrt(double(pixels) / 50000.0)));

    std::vector<float> ratios;
    ratios.reserve(pixels / (std::size_t(step) * step) + 1);

    for (std::uint32_t y = 0; y < reference.height; y += step) {
        for (std::uint32_t x = 0; x < reference.width; x += step) {
            const float* r = reference.rgb + (std::size_t(y) * reference.width + x) * 3;
            const float lr = luma(r);
            if (lr < kLow || lr > kHigh) continue;

            const Sample s = tap(frame, float(x), float(y));
            if (!s.inside) continue;
            const float lf = luma(s.rgb);
            if (lf < kLow || lf > kHigh) continue;

            ratios.push_back(lf / lr);
        }
    }
    if (int(ratios.size()) < minSamples) return 0.0f;

    auto mid = ratios.begin() + std::ptrdiff_t(ratios.size() / 2);
    std::nth_element(ratios.begin(), mid, ratios.end());
    return *mid;
}

float resolveExposureRatio(float exifRatio, float measuredRatio) {
    if (measuredRatio <= 0.0f) return exifRatio;
    if (exifRatio <= 0.0f) return measuredRatio;
    // A sixth of a stop: past that the metadata is describing a different
    // photograph than the pixels are.
    const float disagreementEv =
        std::abs(std::log2(measuredRatio / exifRatio));
    return disagreementEv > (1.0f / 6.0f) ? measuredRatio : exifRatio;
}

Result merge(const std::vector<Frame>& frames, int referenceIndex,
             const Options& options) {
    if (frames.empty()) throw std::runtime_error("merge: empty stack");
    if (referenceIndex < 0 || referenceIndex >= int(frames.size())) {
        throw std::runtime_error("merge: reference index out of range");
    }
    const Frame& ref = frames[std::size_t(referenceIndex)];
    if (ref.rgb == nullptr || ref.width == 0 || ref.height == 0) {
        throw std::runtime_error("merge: reference frame has no data");
    }
    if (!persp::isIdentity(ref.refToSource)) {
        throw std::runtime_error("merge: the reference must map to itself");
    }
    for (const Frame& f : frames) {
        if (f.exposureRatio <= 0.0f) {
            throw std::runtime_error("merge: non-positive exposure ratio");
        }
        if (f.rgb == nullptr) throw std::runtime_error("merge: frame has no data");
    }

    // Exposures are used relative to the reference, so absolute
    // light-gathered values are as valid an input as pre-normalized ratios.
    const float eRef = ref.exposureRatio;
    std::vector<float> expo(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        expo[i] = frames[i].exposureRatio / eRef;
    }

    const auto shortest = std::size_t(
        std::min_element(expo.begin(), expo.end()) - expo.begin());
    const float headroom = 1.0f / expo[shortest];   // H ≥ 1 for a real bracket
    const float invHeadroom = 1.0f / std::max(headroom, 1.0f);

    Result out;
    out.width  = ref.width;
    out.height = ref.height;
    out.headroomEv = std::log2(std::max(headroom, 1.0f));
    out.rgb.resize(std::size_t(ref.width) * ref.height * 3);

    for (std::uint32_t y = 0; y < ref.height; ++y) {
        for (std::uint32_t x = 0; x < ref.width; ++x) {
            const float* yr = ref.rgb + (std::size_t(y) * ref.width + x) * 3;
            const float refLuma = luma(yr);
            const float refMax  = std::max({yr[0], yr[1], yr[2]});

            // Where the reference itself is saturated, the other frames are
            // the only evidence — the ghost test has no ground truth and is
            // waived rather than allowed to reject the recovery it exists
            // to protect.
            const bool refSaturated = refMax >= options.satRampHigh;
            const float sigmaRef = std::sqrt(std::max(
                ref.noiseA * refLuma + ref.noiseB, 0.0f));
            const float ghostGate = options.ghostSigmas *
                                    std::max(sigmaRef, 1e-4f);

            float sumW = 0.0f;
            float sum[3] = {0.0f, 0.0f, 0.0f};

            for (std::size_t i = 0; i < frames.size(); ++i) {
                const Frame& f = frames[i];
                const bool isRef = int(i) == referenceIndex;

                Sample s;
                if (isRef) {
                    s.rgb[0] = yr[0]; s.rgb[1] = yr[1]; s.rgb[2] = yr[2];
                    s.inside = true;
                } else {
                    s = tap(f, float(x), float(y));
                }
                if (!s.inside) continue;

                const float maxc = std::max({s.rgb[0], s.rgb[1], s.rgb[2]});
                const float wSat = saturationWeight(maxc, options);
                if (wSat <= 0.0f) continue;

                const float e     = expo[i];
                const float meanY = luma(s.rgb);
                const float xr[3] = {s.rgb[0] / e, s.rgb[1] / e, s.rgb[2] / e};

                if (!isRef && !refSaturated) {
                    // Reference-consistency deghost, in reference radiance
                    // units. Binary by design: a half-trusted moving subject
                    // is a half-visible ghost.
                    if (std::abs(luma(xr) - refLuma) > ghostGate) continue;
                }

                const float variance =
                    std::max(f.noiseA * meanY + f.noiseB, 0.0f) + kVarianceFloor;
                const float wSnr = (e * e) / variance;

                const float w = wSat * wSnr;
                sumW += w;
                sum[0] += w * xr[0];
                sum[1] += w * xr[1];
                sum[2] += w * xr[2];
            }

            float* dst = out.rgb.data() + (std::size_t(y) * ref.width + x) * 3;
            if (sumW > 0.0f) {
                for (int c = 0; c < 3; ++c) {
                    dst[c] = std::clamp(sum[c] / sumW * invHeadroom, 0.0f, 1.0f);
                }
            } else {
                // Clipped in every frame: the shortest exposure's reading is
                // the best lower bound there is [GL]. If even that frame has
                // no data here (warped out), the reference carries the pixel.
                Sample s = int(shortest) == referenceIndex
                               ? Sample{{yr[0], yr[1], yr[2]}, true}
                               : tap(frames[shortest], float(x), float(y));
                const float e = s.inside ? expo[shortest] : 1.0f;
                const float* v = s.inside ? s.rgb : yr;
                for (int c = 0; c < 3; ++c) {
                    dst[c] = std::clamp(v[c] / e * invHeadroom, 0.0f, 1.0f);
                }
            }
        }
    }
    return out;
}

}  // namespace orion::merge

/*  HDR merge — exposure-weighted radiance estimation from a bracketed stack.
 *
 *  The core of the merge and nothing else: frames arrive as scene-linear
 *  camera RGB (each normalized so 1.0 is its own clip), already demosaiced,
 *  each carrying its exposure ratio, its noise model and its alignment to the
 *  reference. This file combines them into one radiance map. Decoding, the
 *  GPU demosaic, alignment estimation and the DNG that comes out are other
 *  stories' files — keeping the maths free of I/O is what makes it testable
 *  against a synthetic stack with a known answer.
 *
 *  Sources (full table in research/hdr-merge.md): the radiance-map framing
 *  and discard-the-clipped rule are Debevec & Malik 1997 §2.2 — their
 *  response-curve recovery is unnecessary here, raw data is linear; the
 *  inverse-variance weighting is Hasinoff, Durand & Freeman 2010 §3; the
 *  shortest-frame fallback for pixels clipped everywhere is Luijk's Zero
 *  Noise; the single-scalar exposure ratio for a linear response is the
 *  degenerate case of Grossberg & Nayar 2003's brightness transfer function.
 *
 *  ⚠ The saturation ramp bounds and the deghost threshold are ours, not
 *  published — research/UNSOURCED.md carries them.
 */

#pragma once

#include "pipe/Perspective.h"

#include <cstdint>
#include <vector>

namespace orion::merge {

namespace persp = orion::pipe::persp;

/// One frame of the bracket, as the merge wants it.
struct Frame {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;

    /// Scene-linear camera RGB, tightly packed, row-major, own-frame
    /// normalized: 1.0 is *this* frame's saturation, whatever its exposure.
    const float* rgb = nullptr;

    /// Exposure relative to the reference: E = (t·ISO/N²) / (t·ISO/N²)_ref.
    /// The longest frame of a bracket has E > 1, the shortest E < 1.
    float exposureRatio = 1.0f;

    /// Var(y) = a·y + b in own-normalized units, from raw::NoiseProfile.
    /// Zeros are legal and mean "trust this frame's SNR completely at its
    /// exposure weight" — the weight degrades to E² alone.
    float noiseA = 0.0f;
    float noiseB = 0.0f;

    /// Reference texel -> this frame's texel, persp::Matrix3's dest->source
    /// convention (a warp is evaluated backwards). identity() when aligned.
    persp::Matrix3 refToSource;
};

struct Result {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;

    /// The merged radiance, scaled by 1/H so the stack's true ceiling is 1.0.
    /// RGB, tightly packed. H = 1/min(Eᵢ) is the headroom the shortest
    /// exposure buys; a reader renders at intended brightness by applying
    /// +log2(H) EV, which is what the DNG's BaselineExposure will carry.
    std::vector<float> rgb;

    /// log2(H), in EV.
    float headroomEv = 0.0f;
};

/// The tuning constants, overridable so tests can pin them and the decision
/// point (story D) can revisit them without an API change.
struct Options {
    /// w_sat: full weight below the ramp, zero above it. The ramp starts
    /// well under the clip because a channel goes non-linear on the shoulder
    /// before it stops, and near-clip data is biased, not merely noisy.
    float satRampLow  = 0.85f;
    float satRampHigh = 0.98f;

    /// w_ghost: a frame disagreeing with the reference by more than
    /// k·σ_ref (in reference radiance units, per luma) contributes nothing
    /// there. The reference never deghosts itself, and the test is waived
    /// where the reference is saturated — that is exactly where the other
    /// frames are the only evidence.
    float ghostSigmas = 6.0f;
};

/// Exposure from the EXIF triplet, in linear light-gathering units t·ISO/N².
/// Zero or negative fields make the triplet unusable and return 0.
[[nodiscard]] float lightGathered(float shutterSeconds, float iso, float fNumber);

/// The image-derived exposure ratio: the median of per-pixel value ratios
/// frame/reference over aligned pixels where both lie in [0.1, 0.8] — the
/// band where neither noise floor nor shoulder bends the ratio. Returns 0
/// when fewer than `minSamples` pixels qualify (frames too far apart, or
/// barely overlapping), in which case the EXIF ratio stands.
[[nodiscard]] float measuredExposureRatio(const Frame& frame,
                                          const Frame& reference,
                                          int minSamples = 1000);

/// EXIF against measurement: the measured ratio wins when it is valid and
/// the two disagree by more than a sixth of a stop — ISO metadata rounds,
/// electronic shutters quantize, and the pixels do not lie.
[[nodiscard]] float resolveExposureRatio(float exifRatio, float measuredRatio);

/// The merge. `frames[referenceIndex]` defines the output's framing and
/// dimensions and must have an identity refToSource. Throws
/// std::runtime_error on an empty stack, a bad reference index, or a
/// non-positive exposure ratio.
[[nodiscard]] Result merge(const std::vector<Frame>& frames, int referenceIndex,
                           const Options& options = {});

}  // namespace orion::merge

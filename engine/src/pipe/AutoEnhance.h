/*  Auto-enhance — measure the picture, then set the ordinary sliders.
 *
 *  Deliberately not a filter and not a node. Auto-enhance moves the same
 *  exposure, blacks, whites and look controls the user has, so whatever it
 *  decides is visible, adjustable, undoable and stored in the sidecar like any
 *  other edit. An "auto" that applied a hidden transform would be a second,
 *  invisible editing model sitting underneath the first.
 *
 *  Everything here is a pure function of a histogram, which is what makes the
 *  policy testable without a GPU. research/auto-enhance.md.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace orion::pipe::auto_enhance {

/// Clipped at each end. ⚠️ **Inference, not a recommendation** — Simplest Color
/// Balance recommends no percentage at all, and its reference implementation
/// takes the levels as mandatory arguments. Its figure captions call a total of
/// 1% "optimal" and "moderate", and §7 splits it half and half. See
/// research/UNSOURCED.md §14.
inline constexpr double kClipPerSide = 0.005;

/// Where middle grey lands through the sRGB transfer function — CIPA
/// DC-004:2004 §2.3(3), `MAX × 0.461`.
///
/// ⚠️ The standard defines this for a uniform 18% reflectance card under
/// controlled lighting. Aiming a *photograph's median* at it is a step the
/// standard does not take, and the standard calls its own value conventional.
inline constexpr float kMidGrey = 0.461f;

/// A ceiling on how much contrast an automatic stretch may add — Lisani, Petro
/// & Sbert, IPOL 2012: `smax = 2` "provides a good compromise between contrast
/// enhancement and saturation".
inline constexpr float kMaxSlope = 2.0f;

/// Gain on each solver step.
///
/// One might expect this to need damping below 1, and it does not: the step is
/// computed as `log2(target / median)` — the correction that *would* be right
/// if the rendered median moved in proportion to exposure — while the display
/// transform is compressive, so the true response is smaller than that estimate
/// and every step already undershoots. Damping it further only makes
/// convergence slower. Measured: at 0.7 the solver was still 0.064 away from
/// the anchor after five passes; at 1.0 it converges well inside 0.02.
inline constexpr float kDamping = 1.0f;

/// The most exposure auto-enhance may add or remove. Five stops is already a
/// drastic correction; past that the picture wanted a different photograph, and
/// an automatic control that will move ten stops is one that turns a mistake
/// into a bigger one.
inline constexpr float kMaxExposureEv = 5.0f;

/// How many times to render, measure and adjust.
inline constexpr int kPasses = 6;


/// What the histogram says about the rendered picture.
///
/// All in display units, [0, 1], because that is the space the percentiles are
/// meaningful in: the question auto-levels asks is "does this picture use the
/// range the screen has", and the screen's range is what the display transform
/// produced.
struct Stats {
    float shadow = 0.0f;   // the low percentile
    float high   = 1.0f;   // the high percentile
    float median = 0.5f;
    /// Fraction of samples already at the very bottom and very top. A picture
    /// that is *already* clipping does not want its endpoints pushed further.
    float atFloor   = 0.0f;
    float atCeiling = 0.0f;
};

/// The value below which `fraction` of the weight lies.
///
/// Reads the bin's lower edge rather than its centre, which matters at the ends:
/// with a coarse histogram, taking the centre of bin 0 reports a black point
/// above zero on an image that genuinely contains black, and the correction then
/// lifts a picture that needed nothing.
[[nodiscard]] inline float percentileOf(const std::uint32_t* bins, std::uint32_t count,
                                        double fraction) noexcept {
    if (bins == nullptr || count == 0) return 0.0f;

    double total = 0.0;
    for (std::uint32_t i = 0; i < count; ++i) total += double(bins[i]);
    if (total <= 0.0) return 0.0f;

    const double want = std::clamp(fraction, 0.0, 1.0) * total;
    double seen = 0.0;
    for (std::uint32_t i = 0; i < count; ++i) {
        seen += double(bins[i]);
        if (seen >= want) return float(i) / float(count);
    }
    return 1.0f;
}

/// Combines the three channel histograms into one luminance-ish histogram.
///
/// **Deliberately not per channel.** Stretching each channel to its own
/// endpoints performs a white balance as a side effect — it is the same
/// operation as a grey-world correction — and this editor already has a white
/// balance the photographer set, often from the camera's own reading. An auto
/// contrast that silently re-balanced colour would be overriding a decision the
/// user made on purpose, and it would do so invisibly.
///
/// The channel histograms are summed rather than luminance-weighted because the
/// three are separate distributions, not three components of one pixel: the
/// bin index is a value, and a value's frequency is the sum of how often each
/// channel took it.
inline void combine(const std::uint32_t* rgb, std::uint32_t bins, std::uint32_t* out) noexcept {
    for (std::uint32_t i = 0; i < bins; ++i) {
        out[i] = rgb[i] + rgb[bins + i] + rgb[2 * bins + i];
    }
}

[[nodiscard]] inline Stats measure(const std::uint32_t* combined, std::uint32_t bins,
                                   double clipFraction) noexcept {
    Stats s{};
    if (combined == nullptr || bins == 0) return s;

    s.shadow = percentileOf(combined, bins, clipFraction);
    s.high   = percentileOf(combined, bins, 1.0 - clipFraction);
    s.median = percentileOf(combined, bins, 0.5);

    double total = 0.0;
    for (std::uint32_t i = 0; i < bins; ++i) total += double(combined[i]);
    if (total > 0.0) {
        s.atFloor   = float(double(combined[0]) / total);
        s.atCeiling = float(double(combined[bins - 1]) / total);
    }
    return s;
}

/// The controls auto-enhance is allowed to move. Deliberately a small struct
/// rather than the full adjustment set: what this may touch is a decision, and
/// making it a type means the decision cannot be widened by accident.
struct Controls {
    float exposureEv = 0.0f;
    float blacks     = 0.0f;
    float whites     = 0.0f;
    float fusion     = 0.0f;
    float clarity    = 0.0f;
};

/// One step of the solve.
///
/// The relationship between the exposure slider and the rendered histogram runs
/// through AgX, the tone curve and everything the user has already set, so it
/// is not invertible in closed form. This measures, corrects, and is called
/// again — which makes it right whatever the curve is doing.
[[nodiscard]] inline Controls refine(const Controls& current, const Stats& s) noexcept {
    Controls next = current;

    // Exposure, toward the mid-grey anchor. In stops, because that is what the
    // slider is; damped, because the display transform compresses the move.
    if (s.median > 1e-4f) {
        const float stops = std::log2(kMidGrey / s.median);
        next.exposureEv = std::clamp(current.exposureEv + kDamping * stops, -kMaxExposureEv, kMaxExposureEv);
    }

    // Endpoints. A picture already against the stops does not want pushing
    // further out — that is the "flat white regions or flat black regions that
    // may look unnatural" the paper warns about.
    const float span = std::max(s.high - s.shadow, 1e-3f);
    const float slope = 1.0f / span;

    if (slope < kMaxSlope) {
        // Unit gain, not the doubled one this started with. Measured: at 2.0
        // the whites slider railed at its maximum on two of the three sample
        // frames, which is an automatic control handing the user a setting with
        // nowhere left to go. At 1.0 the median still lands on the anchor and
        // the endpoints stay somewhere a person can argue with.
        if (s.atFloor < 0.02f) {
            next.blacks = std::clamp(current.blacks - kDamping * s.shadow, -1.0f, 1.0f);
        }
        if (s.atCeiling < 0.02f) {
            next.whites = std::clamp(current.whites + kDamping * (1.0f - s.high),
                                     -1.0f, 1.0f);
        }
    }

    return next;
}

/// The look controls, set once from the first measurement rather than solved.
///
/// ⚠️ Taste, and recorded as such. There is no measurable target for how much
/// clarity a photograph wants. Both land on visible sliders, so disagreeing
/// costs one drag.
///
/// Dehaze is deliberately absent: deciding whether a frame is hazy needs the
/// dark-channel statistic, which this path does not compute, and applying it to
/// a haze-free frame is exactly the case the dark channel prior is documented
/// to get wrong.
[[nodiscard]] inline Controls look(const Stats& s) noexcept {
    Controls c{};
    // A dark picture gets shadow lift, in proportion to how dark. Measured
    // before any exposure correction, so it responds to the photograph rather
    // than to what the solver has already done about it.
    c.fusion  = std::clamp(1.6f * (kMidGrey - s.median) / kMidGrey, 0.0f, 1.0f);
    c.clarity = 0.25f;
    return c;
}

}  // namespace orion::pipe::auto_enhance

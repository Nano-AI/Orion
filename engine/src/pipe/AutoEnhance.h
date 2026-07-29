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

}  // namespace orion::pipe::auto_enhance

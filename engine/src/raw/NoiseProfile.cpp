#include "raw/NoiseProfile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace orion::raw {
namespace {

/// Bins across the samples, not across the tonal range — see the note on
/// quantile binning below. Enough to see a slope, few enough that each still
/// holds thousands of samples.
constexpr int kBins = 12;

/// Below this many samples in total there is nothing worth fitting.
constexpr std::size_t kMinSamples = 4000;

/// MAD to standard deviation for Gaussian data. The constant is 1/Φ⁻¹(3/4).
constexpr double kMadToSigma = 1.4826;

/// Ignore anything this close to saturation: clipped pixels have no noise, and
/// including them drags the slope down at exactly the end that sets it.
constexpr float kClipGuard = 0.95f;

/// Below this spread of brightness the slope is not identifiable, and fitting
/// one anyway produces a wild extrapolation from a very short lever arm.
constexpr double kMinBrightnessSpread = 0.02;

/// 1/sqrt(6): var(p - 2q + r) = 6·var(p) for independent, identically
/// distributed noise, so a second difference is scaled back down by this.
constexpr double kSecondDiffToOne = 0.40824829;

/// Both in raw counts, so the binning and the medians below can count rather
/// than compare (#319). Black cancels out of the difference, and the mean is
/// (sum - 3·black) / (3·range).
struct Sample {
    std::uint32_t sum;   // the three samples added: brightness
    std::uint32_t diff;  // |p - 2q + r|: the absolute second difference
};

}  // namespace

NoiseProfile estimateNoise(const BayerImage& image) {
    NoiseProfile profile;

    const std::uint32_t w = image.width, h = image.height;
    if (w < 32 || h < 32 || image.samples.size() < static_cast<std::size_t>(w) * h) {
        return profile;
    }

    // Normalize out of sensor counts. A per-channel black point would be more
    // exact, but the CFA phase is not known here and the black points of one
    // sensor's channels differ by a few counts out of thousands.
    const float black = static_cast<float>(
        (image.black[0] + image.black[1] + image.black[2] + image.black[3]) / 4);
    const float range = static_cast<float>(image.white) - black;
    if (range <= 1.0f) return profile;

    // 0 <= p <= kClipGuard, as a window on the raw counts.
    const auto lowest  = static_cast<std::uint32_t>(std::ceil(black));
    const auto highest = static_cast<std::uint32_t>(black + kClipGuard * range);

    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(w / 4) * (h / 4));

    // Every fourth row and column: plenty for a stable median on any frame
    // worth denoising, and about a sixteenth of the work.
    for (std::uint32_t y = 0; y + 1 < h; y += 4) {
        const std::uint16_t* row = image.samples.data() + static_cast<std::size_t>(y) * w;

        for (std::uint32_t x = 0; x + 4 < w; x += 4) {
            // Three samples two apart, so all three are the same color in any
            // 2x2 CFA.
            const std::uint32_t p = row[x], q = row[x + 2], r = row[x + 4];
            if (std::min({p, q, r}) < lowest || std::max({p, q, r}) > highest) continue;

            // A *second* difference, not a first. A first difference measures
            // the local gradient as though it were noise, which on any frame
            // with a sky in it is most of what it measures — a gentle ramp came
            // out as more noise than the noise. A second difference annihilates
            // a linear ramp exactly and leaves the noise alone.
            //
            // Scaled back to one sigma by kSecondDiffToOne, once, at the median.
            samples.push_back({p + q + r, p + r > 2 * q ? p + r - 2 * q : 2 * q - p - r});
        }
    }

    if (samples.size() < kMinSamples) return profile;

    // Bins of equal *population*, not equal width.
    //
    // Equal-width bins across 0..1 assume the frame uses the whole tonal range.
    // A night sky does not: every pixel lands in the bottom bin, eleven bins
    // come back empty, and the fit is abandoned — which is exactly what
    // happened, and it made the denoiser silently do nothing on the frames that
    // needed it most. Quantile bins adapt to whatever exposure arrived.
    //
    // Ordered by a counting sort on the integer sum, not a comparison sort on
    // the float mean: the mean is (sum - 3·black) / (3·range), so the order is
    // the same, in two linear passes instead of n·log n comparisons. The
    // std::sort was ~75% of this function, and this function ~40% of a RAW open
    // (#319). Every kept sample is under the clip guard, so sum <= 3·white.
    {
        std::vector<std::uint32_t> start(3u * image.white + 1, 0);
        for (const Sample& s : samples) ++start[s.sum];
        std::exclusive_scan(start.begin(), start.end(), start.begin(), 0u);
        std::vector<Sample> sorted(samples.size());
        for (const Sample& s : samples) sorted[start[s.sum]++] = s;
        samples.swap(sorted);
    }

    struct Bin {
        double x;
        double variance;
        double weight;
    };
    std::vector<Bin> bins;
    bins.reserve(kBins);

    // Each bin's median difference by counting, for the same reason: the
    // differences are integers no larger than 2·white, so a histogram finds the
    // middle one in one pass where a selection takes several and mispredicts
    // half its comparisons. Same element as nth_element would pick.
    std::vector<std::uint32_t> histogram(2u * image.white + 1);

    const std::size_t total = samples.size();
    for (int i = 0; i < kBins; ++i) {
        const std::size_t lo = total * static_cast<std::size_t>(i) / kBins;
        const std::size_t hi = total * static_cast<std::size_t>(i + 1) / kBins;
        if (hi <= lo) continue;

        std::fill(histogram.begin(), histogram.end(), 0u);
        double meanSum = 0.0;
        for (std::size_t k = lo; k < hi; ++k) {
            meanSum += samples[k].sum;
            ++histogram[samples[k].diff];
        }
        const std::size_t middle = (hi - lo) / 2;
        std::uint32_t median = 0;
        for (std::size_t seen = histogram[0]; seen <= middle; seen += histogram[++median]) {}

        const double count = static_cast<double>(hi - lo);
        const double sigma = median * kSecondDiffToOne / range * kMadToSigma;
        const double mean = (meanSum / count - 3.0 * black) / (3.0 * range);
        bins.push_back({mean, sigma * sigma, count});
    }

    if (bins.size() < 3) return profile;

    const double spread = bins.back().x - bins.front().x;

    // Weighted least squares of var = a·x + b through the bins.
    double sw = 0.0, swx = 0.0, swy = 0.0, swxx = 0.0, swxy = 0.0;
    for (const Bin& bin : bins) {
        sw += bin.weight;
        swx += bin.weight * bin.x;
        swy += bin.weight * bin.variance;
        swxx += bin.weight * bin.x * bin.x;
        swxy += bin.weight * bin.x * bin.variance;
    }
    if (sw <= 0.0) return profile;

    const double denominator = sw * swxx - swx * swx;
    double a = 0.0;
    double b = swy / sw;

    // A frame with no brightness range gives no lever arm for the slope, and
    // fitting one anyway extrapolates wildly. Fall back to the constant term,
    // which is still a correct model for that frame — just not a general one.
    if (spread >= kMinBrightnessSpread && std::abs(denominator) > 1e-18) {
        const double slope = (sw * swxy - swx * swy) / denominator;
        const double intercept = (swy - slope * swx) / sw;
        if (slope >= 0.0 && intercept >= 0.0) {
            a = slope;
            b = intercept;
        }
    }

    profile.a = static_cast<float>(std::max(a, 0.0));
    profile.b = static_cast<float>(std::max(b, 0.0));
    profile.measured = true;
    return profile;
}

}  // namespace orion::raw

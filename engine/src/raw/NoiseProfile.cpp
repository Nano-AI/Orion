#include "raw/NoiseProfile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

struct Sample {
    float mean;
    float delta;   // already absolute, already scaled to one sigma
};

double medianOfAbs(std::vector<float>& v) {
    if (v.empty()) return 0.0;
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    return static_cast<double>(v[mid]);
}

}  // namespace

NoiseProfile estimateNoise(const BayerImage& image) {
    NoiseProfile profile;

    const std::uint32_t w = image.width, h = image.height;
    if (w < 32 || h < 32 || image.samples.size() < static_cast<std::size_t>(w) * h) {
        return profile;
    }

    // Normalise out of sensor counts. A per-channel black point would be more
    // exact, but the CFA phase is not known here and the black points of one
    // sensor's channels differ by a few counts out of thousands.
    const float black = static_cast<float>(
        (image.black[0] + image.black[1] + image.black[2] + image.black[3]) / 4);
    const float range = static_cast<float>(image.white) - black;
    if (range <= 1.0f) return profile;

    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(w / 4) * (h / 4));

    // Every fourth row and column: plenty for a stable median on any frame
    // worth denoising, and about a sixteenth of the work.
    for (std::uint32_t y = 0; y + 1 < h; y += 4) {
        const std::uint16_t* row = image.samples.data() + static_cast<std::size_t>(y) * w;

        for (std::uint32_t x = 0; x + 4 < w; x += 4) {
            // Three samples two apart, so all three are the same colour in any
            // 2x2 CFA.
            const float p = (static_cast<float>(row[x]) - black) / range;
            const float q = (static_cast<float>(row[x + 2]) - black) / range;
            const float r = (static_cast<float>(row[x + 4]) - black) / range;
            if (p < 0.0f || q < 0.0f || r < 0.0f) continue;
            if (p > kClipGuard || q > kClipGuard || r > kClipGuard) continue;

            // A *second* difference, not a first. A first difference measures
            // the local gradient as though it were noise, which on any frame
            // with a sky in it is most of what it measures — a gentle ramp came
            // out as more noise than the noise. A second difference annihilates
            // a linear ramp exactly and leaves the noise alone.
            //
            // var(p - 2q + r) = 6·var(p) for independent, identically
            // distributed noise, so it is scaled back down here.
            const float d = (p - 2.0f * q + r) * 0.40824829f;   // 1/sqrt(6)

            samples.push_back({(p + q + r) / 3.0f, std::abs(d)});
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
    std::sort(samples.begin(), samples.end(),
              [](const Sample& a, const Sample& b) { return a.mean < b.mean; });

    struct Bin {
        double x;
        double variance;
        double weight;
    };
    std::vector<Bin> bins;
    bins.reserve(kBins);

    const std::size_t total = samples.size();
    for (int i = 0; i < kBins; ++i) {
        const std::size_t lo = total * static_cast<std::size_t>(i) / kBins;
        const std::size_t hi = total * static_cast<std::size_t>(i + 1) / kBins;
        if (hi <= lo) continue;

        std::vector<float> deltas;
        deltas.reserve(hi - lo);
        double meanSum = 0.0;
        for (std::size_t k = lo; k < hi; ++k) {
            deltas.push_back(samples[k].delta);
            meanSum += samples[k].mean;
        }

        const double count = static_cast<double>(hi - lo);
        const double sigma = medianOfAbs(deltas) * kMadToSigma;
        bins.push_back({meanSum / count, sigma * sigma, count});
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

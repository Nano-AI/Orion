/*  Calibrate — see the header. */

#include "stack/Calibrate.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace orion::stack {

std::vector<float> medianOf(const std::vector<std::vector<float>>& frames) {
    if (frames.empty()) return {};
    const std::size_t n = frames.size(), mid = n / 2;
    std::vector<float> out(frames[0].size()), v(n);
    for (std::size_t i = 0; i < out.size(); ++i) {
        for (std::size_t k = 0; k < n; ++k) v[k] = frames[k][i];
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(mid), v.end());
        out[i] = v[mid];
        if (n % 2 == 0) out[i] = 0.5f * (out[i] + *std::max_element(v.begin(), v.begin() + std::ptrdiff_t(mid)));
    }
    return out;
}

std::vector<float> flatGain(const std::vector<float>& flat, const std::vector<float>& dark,
                            const std::vector<std::uint8_t>& channel) {
    std::vector<float> gain(flat.size());
    // Two passes: the channel's mean over samples with signal, then again without those under kNoSignal of it,
    // so the gains that are kept have mean 1 exactly.
    std::array<double, 4> sum{}, count{}, floor{};
    for (std::size_t i = 0; i < flat.size(); ++i) {
        gain[i] = flat[i] - (dark.empty() ? 0.0f : dark[i]);
        if (gain[i] > 0.0f) { sum[channel[i]] += gain[i]; count[channel[i]] += 1; }
    }
    for (std::size_t c = 0; c < 4; ++c) {
        floor[c] = count[c] > 0 ? kNoSignal * sum[c] / count[c] : 0.0;
        sum[c] = count[c] = 0;
    }
    for (std::size_t i = 0; i < gain.size(); ++i)
        if (gain[i] > 0.0f && gain[i] >= floor[channel[i]]) { sum[channel[i]] += gain[i]; count[channel[i]] += 1; }
    for (std::size_t i = 0; i < gain.size(); ++i) {
        const auto c = channel[i];
        gain[i] = gain[i] > 0.0f && gain[i] >= floor[c] ? float(gain[i] * count[c] / sum[c]) : 1.0f;
    }
    return gain;
}

void calibrate(float* samples, std::size_t n, const std::vector<float>& dark, const std::vector<float>& gain,
               float clip) {
    for (std::size_t i = 0; i < n; ++i) {
        if (samples[i] >= clip) continue;
        if (!dark.empty()) samples[i] -= dark[i];
        if (!gain.empty()) samples[i] /= gain[i];
    }
}

std::vector<std::uint8_t> channelsOf(const raw::BayerImage& image) {
    std::vector<std::uint8_t> ch(image.pixelCount());
    for (std::uint32_t y = 0; y < image.height; ++y)
        for (std::uint32_t x = 0; x < image.width; ++x)
            ch[std::size_t(y) * image.width + x] = std::uint8_t(image.channelAt(x, y));
    return ch;
}

std::vector<float> mosaicOf(const raw::BayerImage& image) {
    const auto ch = channelsOf(image);
    std::vector<float> m(image.samples.size());
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = float(image.samples[i]) - float(image.black[ch[i]]);
    return m;
}

void storeMosaic(raw::BayerImage& image, const std::vector<float>& mosaic) {
    const auto ch = channelsOf(image);
    for (std::size_t i = 0; i < mosaic.size(); ++i) {
        if (image.samples[i] >= image.white) continue;   // clipped: no value to calibrate, and it must stay clipped
        const float b = image.black[ch[i]];
        image.samples[i] = std::uint16_t(std::lround(std::clamp(mosaic[i] + b, b, std::max(b, float(image.white)))));
    }
}

}  // namespace orion::stack

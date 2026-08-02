/*  orion-bench — the instruments.
 *
 *  Every number the report prints comes through one of these. The long
 *  comments are the calibration history: each is a measurement that lied once,
 *  and the note is why the instrument has the shape it has.
 */
#include "bench.h"

#include "util/ImageWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace bench {

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

/// Drops the first few frames: the first dispatch of a pipeline state pays for
/// shader warm-up and first-touch page faults, which is real but is not what a
/// slider drag costs.
constexpr int kWarmupFrames = 8;

Stats summarise(std::vector<double> v) {
    if (v.size() > kWarmupFrames) v.erase(v.begin(), v.begin() + kWarmupFrames);
    std::sort(v.begin(), v.end());
    return {v.front(),
            v[v.size() / 2],
            v[static_cast<std::size_t>(v.size() * 0.95)],
            std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size())};
}

/// The developed output as 16-bit unsigned. The graph ends in half float; the
/// image formats want integers.
/// Format-aware: the tail of the graph is eight bits for the screen and
/// sixteen only around an export. Reading an RGBA8Unorm texture with a stride
/// computed for half float does not fail, it just returns nonsense — which is
/// exactly what it did, and every mean in this file went with it.
std::vector<std::uint16_t> output16(const orion::pipe::DevelopPipeline& d,
                                    std::uint32_t w, std::uint32_t h) {
    const std::size_t count = std::size_t(w) * h * 4;
    std::vector<std::uint16_t> out(count);

    if (d.output().format() == orion::gpu::PixelFormat::RGBA16Float) {
        std::vector<__fp16> half(count);
        d.output().download(half.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        for (std::size_t i = 0; i < count; ++i) {
            const float v = std::clamp(float(half[i]), 0.0f, 1.0f);
            out[i] = static_cast<std::uint16_t>(v * 65535.0f + 0.5f);
        }
    } else {
        std::vector<std::uint8_t> bytes(count);
        d.output().download(bytes.data(), std::size_t(w) * 4, w, h);
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = static_cast<std::uint16_t>(bytes[i] * 257);   // 255 -> 65535
        }
    }
    return out;
}

/// Mean absolute per-channel change between two renders, in 0..1 display units.
///
/// The summary metrics below answer *what* changed — brightness, colorfulness,
/// detail. This answers *how much*, and it is the one that cannot cancel. A
/// grading wheel rotates hue at constant saturation, so it moves every pixel
/// and changes the frame's mean luma, mean chroma and mean saturation by very
/// little; three separate instruments all reported it as doing nothing. A
/// per-pixel difference cannot be fooled that way, so it is what the pass/fail
/// gate reads.
double meanAbsDiff(const std::vector<std::uint16_t>& a,
                   const std::vector<std::uint16_t>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0;
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.size(); i += 4 * 37) {
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(double(a[i + c]) - double(b[i + c]));
            ++n;
        }
    }
    return n ? sum / double(n) / 65535.0 : 0.0;
}

void writeOut(const orion::pipe::DevelopPipeline& d, const std::string& path) {
    const std::uint32_t w = d.outputWidth(), h = d.outputHeight();
    const auto pixels = output16(d, w, h);
    orion::util::writePng(path, pixels.data(), w, h,
                          std::size_t(w) * 4 * sizeof(std::uint16_t));
    std::printf("  wrote %s  (%u x %u)\n", path.c_str(), w, h);
}

/// Mean luma, or mean distance from gray, for asserting that an adjustment
/// actually did something rather than silently no-op'ing.
double meanOf(const orion::pipe::DevelopPipeline& d, Metric metric) {
    const std::uint32_t tw = d.outputWidth(), th = d.outputHeight();
    const auto pixels = output16(d, tw, th);

    const auto lumaAt = [&](std::size_t i) {
        const double r = pixels[i * 4 + 0] / 257.0;   // 16-bit to the 0..255
        const double g = pixels[i * 4 + 1] / 257.0;   // scale these numbers
        const double b = pixels[i * 4 + 2] / 257.0;   // have always been on
        return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    };

    const std::size_t count = static_cast<std::size_t>(tw) * th;
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 1 < count; i += 37) {
        const double y = lumaAt(i);
        switch (metric) {
            case Metric::Luma:
                sum += y;
                break;
            case Metric::Chroma: {
                const double r = pixels[i * 4 + 0] / 257.0;
                const double g = pixels[i * 4 + 1] / 257.0;
                const double b = pixels[i * 4 + 2] / 257.0;
                sum += (std::abs(r - y) + std::abs(g - y) + std::abs(b - y)) / 3.0;
                break;
            }
            case Metric::Saturation: {
                const double r = pixels[i * 4 + 0] / 257.0;
                const double g = pixels[i * 4 + 1] / 257.0;
                const double b = pixels[i * 4 + 2] / 257.0;
                const double mx = std::max(r, std::max(g, b));
                const double mn = std::min(r, std::min(g, b));
                // Below a quarter of an 8-bit step there is no color to speak
                // of, only quantisation, and dividing by it manufactures
                // saturation out of noise.
                sum += mx > 0.25 ? (mx - mn) / mx * 255.0 : 0.0;
                break;
            }
            case Metric::Detail:
                // The horizontal neighbor. The stride steps within a row for
                // all but one sample in 163, so wrapping is noise.
                sum += std::abs(y - lumaAt(i + 1));
                break;
        }
        ++n;
    }
    return n ? sum / static_cast<double>(n) / 255.0 : 0.0;
}

}  // namespace bench

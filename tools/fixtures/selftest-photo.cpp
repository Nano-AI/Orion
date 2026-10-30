// The photograph CI hands the Avalonia shell's --selftest (decision #376): made
// here, by Orion's own DNG writer, so it carries no one's pictures, no personal
// data and no licence but this repository's. Built as `orion-selftest-photo`;
//   orion-selftest-photo OUT.dng
//
// 3600x2400, big enough that the canvas fits it well below 100%, a render past
// fit is a region and a full render's memory is worth releasing. Hue runs left
// to right and brightness bottom to top (so Monochrome has colour to remove and
// every tone slider has tones to move), with a sharp-edged clipped square for
// highlight recovery and detail, and a little deterministic noise so the
// denoise and sharpening sliders have something to act on. Same colour metadata as
// mask-invert.cpp. No EXIF exposure or lens: the writer has none to write.
#include "util/DngWriter.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s OUT.dng\n", argv[0]); return 2; }
    constexpr unsigned w = 3600, h = 2400;
    std::vector<float> rgb(std::size_t(w) * h * 3);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const float hue = 6.0f * float(x) / float(w);            // 0..6 around the wheel
            const float level = 0.01f * std::pow(60.0f, float(h - 1 - y) / float(h - 1));
            float c[3];
            for (int k = 0; k < 3; ++k) {
                const float d = std::fabs(std::fmod(hue + 6.0f - 2.0f * float(k), 6.0f) - 3.0f);
                c[k] = level * (0.25f + 0.75f * std::fmin(std::fmax(d - 1.0f, 0.0f), 1.0f));
            }
            const bool square = x > 2600 && x < 3000 && y > 400 && y < 800;
            for (int k = 0; k < 3; ++k) {
                // An integer hash, not a library generator: the same file on every system.
                std::uint32_t n = (x * 73856093u) ^ (y * 19349663u) ^ (std::uint32_t(k) * 83492791u);
                n ^= n >> 13; n *= 0x5bd1e995u; n ^= n >> 15;
                const float noise = (float(n & 0xffff) / 65535.0f - 0.5f) * (0.08f * c[k] + 0.004f);
                rgb[(std::size_t(y) * w + x) * 3 + k] = square ? 1.0f : std::fmax(c[k] + noise, 0.0f);
            }
        }
    orion::util::DngLinearImage image;
    image.width = w;
    image.height = h;
    image.rgb = rgb.data();
    image.camera = "SONY ILCE-7RM3";
    image.xyzToCam = {0.6640f, -0.1847f, -0.0503f,
                      -0.5238f, 1.3465f, 0.1916f,
                      -0.0879f, 0.1636f, 0.6271f};
    image.asShotNeutral = {1, 1, 1};
    orion::util::writeDngLinear(argv[1], image);
    return 0;
}

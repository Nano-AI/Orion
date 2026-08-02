/*  orion-rawstat — what the sensor actually recorded, per CFA channel.
 *
 *  Exists because a hue error in the finished picture has at least four
 *  possible homes — the sensor clipping, the white balance, the color matrix,
 *  or the display transform — and only one of them can be settled by looking at
 *  the raw numbers. The purple sky on _PIC8095.ARW was diagnosed as a profile
 *  limitation; this is the tool that rules out the cheaper explanation first.
 *
 *      orion-rawstat <raw> [x,y,w,h normalized]
 *
 *  Reports, per CFA channel: black, white, the as-shot multiplier, the raw
 *  mean, and — the number that matters — what fraction of samples sit at or
 *  above the level where the pipeline clips them.
 */

#include "raw/RawImage.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: orion-rawstat <raw-file> [x,y,w,h]\n");
        return 2;
    }

    double rx = 0.0, ry = 0.0, rw = 1.0, rh = 1.0;
    if (argc > 2) {
        std::sscanf(argv[2], "%lf,%lf,%lf,%lf", &rx, &ry, &rw, &rh);
    }

    const auto img = orion::raw::decodeBayer(argv[1]);

    const std::uint32_t x0 = std::min<std::uint32_t>(
        img.width - 1, static_cast<std::uint32_t>(rx * img.width));
    const std::uint32_t y0 = std::min<std::uint32_t>(
        img.height - 1, static_cast<std::uint32_t>(ry * img.height));
    const std::uint32_t w = std::max<std::uint32_t>(
        1, std::min<std::uint32_t>(img.width - x0, static_cast<std::uint32_t>(rw * img.width)));
    const std::uint32_t h = std::max<std::uint32_t>(
        1, std::min<std::uint32_t>(img.height - y0, static_cast<std::uint32_t>(rh * img.height)));

    std::printf("%s  %ux%u  %s  region %ux%u at (%u,%u)\n",
                img.camera.c_str(), img.width, img.height,
                img.patternString().c_str(), w, h, x0, y0);
    std::printf("  white level %u   black %u/%u/%u/%u\n",
                img.white, img.black[0], img.black[1], img.black[2], img.black[3]);

    // The multipliers the pipeline actually applies, normalized to green the
    // way linearize does.
    const float g = (img.camMul[1] != 0.0f) ? img.camMul[1] : 1.0f;
    float mul[4];
    for (int c = 0; c < 4; ++c) mul[c] = img.camMul[c] / g;
    std::printf("  as-shot multipliers (green = 1)  R %.4f  G %.4f  B %.4f  G2 %.4f\n",
                mul[0], mul[1], mul[2], mul[3]);

    // Per CFA channel: the raw mean, and how much of it is already at the
    // ceiling. `linearize` clips every channel to one common level *after*
    // white balance (decision #29), so the level a channel can reach before
    // being cut is white/multiplier, not white.
    struct Acc { double sum = 0; std::uint64_t n = 0; std::uint16_t peak = 0; std::uint64_t clipped = 0; };
    Acc acc[4];

    for (std::uint32_t y = y0; y < y0 + h; ++y) {
        for (std::uint32_t x = x0; x < x0 + w; ++x) {
            const int c = static_cast<int>(img.channelAt(x, y));
            const std::uint16_t v = img.samples[std::size_t(y) * img.width + x];
            acc[c].sum += v;
            acc[c].n += 1;
            acc[c].peak = std::max(acc[c].peak, v);
            // What the common post-white-balance ceiling means for this
            // channel, expressed back in sensor counts.
            const double headroom = (mul[c] > 1e-6f)
                ? (double(img.white) - img.black[c]) / mul[c] + img.black[c]
                : double(img.white);
            if (v >= headroom - 0.5) acc[c].clipped += 1;
        }
    }

    static const char* kName[4] = {"R", "G", "B", "G2"};
    std::printf("\n  ch   raw mean    peak   ceiling   %% at ceiling\n");
    for (int c = 0; c < 4; ++c) {
        if (acc[c].n == 0) continue;
        const double headroom = (mul[c] > 1e-6f)
            ? (double(img.white) - img.black[c]) / mul[c] + img.black[c]
            : double(img.white);
        std::printf("  %-3s  %8.1f  %6u  %8.1f   %6.2f%%\n",
                    kName[c], acc[c].sum / double(acc[c].n), acc[c].peak, headroom,
                    100.0 * double(acc[c].clipped) / double(acc[c].n));
    }

    // And the same means white balanced, which is what the demosaic and the
    // color matrix actually see. If the hue is already wrong here, nothing
    // downstream is to blame.
    std::printf("\n  white balanced, black subtracted, normalized to green:\n");
    double lin[3] = {0, 0, 0};
    const double gsum = ((acc[1].n ? acc[1].sum / acc[1].n - img.black[1] : 0) +
                         (acc[3].n ? acc[3].sum / acc[3].n - img.black[3] : 0)) / 2.0;
    lin[0] = (acc[0].sum / std::max<std::uint64_t>(acc[0].n, 1) - img.black[0]) * mul[0];
    lin[1] = gsum * mul[1];
    lin[2] = (acc[2].sum / std::max<std::uint64_t>(acc[2].n, 1) - img.black[2]) * mul[2];
    const double norm = (lin[1] > 1e-9) ? lin[1] : 1.0;
    std::printf("    R %.4f   G %.4f   B %.4f     R/B %.3f   G/B %.3f\n",
                lin[0] / norm, lin[1] / norm, lin[2] / norm,
                lin[0] / std::max(lin[2], 1e-9), lin[1] / std::max(lin[2], 1e-9));
    return 0;
}

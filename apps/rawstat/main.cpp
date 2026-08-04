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

    // ── Does §3.4's gate condition hold? — decision #170 ─────────────────
    //
    // Rouf et al.'s gradient fill-in applies **only** where one channel k has
    // its clipped region wholly inside every other channel's — `Ω^∩ = Ω_k`.
    // Whether that is ever true of a real photograph is a property of the
    // sensor and the scene, not of the algorithm, so it can only be counted.
    // Piece 5 is not worth its nodes if the answer is "almost never".
    //
    // ⚠ **A proxy, and the difference matters.** The shipping mask
    // (`hl_mask.slang`) runs on demosaiced data *after* the window fit, and
    // counts a channel as a hole only where the fit failed to lift it. This
    // counts raw CFA blocks before any of that, so it is an **upper bound** on
    // how blown the frame is and a lower bound on containment. It answers "is
    // this worth pursuing", not "this is the number the kernel will see".
    //
    // One 2x2 block per sample: R, two G, B. A block counts a channel clipped
    // when its sample is at that channel's own post-white-balance ceiling.
    {
        const auto ceilingOf = [&](int c) {
            return (mul[c] > 1e-6f)
                ? (double(img.white) - img.black[c]) / mul[c] + img.black[c]
                : double(img.white);
        };
        std::uint64_t blocks = 0, allThree = 0, onlyMissing[3] = {0, 0, 0};
        for (std::uint32_t y = y0; y + 1 < y0 + h; y += 2) {
            for (std::uint32_t x = x0; x + 1 < x0 + w; x += 2) {
                bool clip[3] = {false, false, false};   // R, G, B
                bool seen[3] = {false, false, false};
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const int c = static_cast<int>(img.channelAt(x + dx, y + dy));
                        const int k = (c == 3) ? 1 : c;   // G2 is green
                        const std::uint16_t v =
                            img.samples[std::size_t(y + dy) * img.width + x + dx];
                        // ⚠ Green is clipped only when **both** greens are: one
                        // green at the ceiling and one below is a block that
                        // still carries green information.
                        if (!seen[k]) { clip[k] = v >= ceilingOf(c) - 0.5; seen[k] = true; }
                        else          { clip[k] = clip[k] && v >= ceilingOf(c) - 0.5; }
                    }
                }
                ++blocks;
                const int n = int(clip[0]) + int(clip[1]) + int(clip[2]);
                if (n == 3) ++allThree;
                // `Ω_k \ Ω^∩` is where k is clipped and the pixel is not fully
                // clipped — the set that must be **empty** for containment.
                else for (int k = 0; k < 3; ++k) if (clip[k]) ++onlyMissing[k];
            }
        }
        // ⚠ **Where those channel-only blocks sit decides everything** — #171.
        // The global count above cannot distinguish "red clips alone inside the
        // blown lamp", which kills containment for red, from "red clips alone
        // in a sign on the other side of the frame", which does not: the
        // paper's test is over a *region*, and two lamps are two regions.
        //
        // Connected-component labelling would answer it exactly and needs its
        // own session. This bounds it instead: dilate the fully-clipped set by
        // `kReach` blocks and count how many channel-only blocks land inside.
        // **Near** means they share a region and containment really is broken;
        // **far** means they are separate features and a per-region gate would
        // still fire. A bound is enough to decide whether to build the exact
        // thing.
        constexpr int kReach = 8;   // 16 px at 2x2 blocks
        {
            const std::uint32_t bw = (w) / 2, bh = (h) / 2;
            if (bw > 0 && bh > 0) {
                std::vector<std::uint8_t> full(std::size_t(bw) * bh, 0), only(std::size_t(bw) * bh, 0);
                std::size_t bi = 0;
                for (std::uint32_t y = y0; y + 1 < y0 + h; y += 2) {
                    for (std::uint32_t x = x0; x + 1 < x0 + w; x += 2) {
                        bool clip[3] = {false, false, false}; bool seen[3] = {false, false, false};
                        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                            const int c = static_cast<int>(img.channelAt(x + dx, y + dy));
                            const int k = (c == 3) ? 1 : c;
                            const std::uint16_t v = img.samples[std::size_t(y + dy) * img.width + x + dx];
                            const bool hit = v >= ceilingOf(c) - 0.5;
                            if (!seen[k]) { clip[k] = hit; seen[k] = true; } else { clip[k] = clip[k] && hit; }
                        }
                        const int n = int(clip[0]) + int(clip[1]) + int(clip[2]);
                        if (bi < full.size()) { full[bi] = (n == 3); only[bi] = (n > 0 && n < 3); }
                        ++bi;
                    }
                }
                // Separable dilation by kReach, rows then columns.
                std::vector<std::uint8_t> near_ = full;
                std::vector<std::uint8_t> tmp(near_.size(), 0);
                for (int pass = 0; pass < 2; ++pass) {
                    for (std::uint32_t r = 0; r < bh; ++r) for (std::uint32_t c = 0; c < bw; ++c) {
                        std::uint8_t v = 0;
                        for (int d = -kReach; d <= kReach && !v; ++d) {
                            const long rr = pass ? long(r) + d : long(r);
                            const long cc = pass ? long(c) : long(c) + d;
                            if (rr < 0 || cc < 0 || rr >= long(bh) || cc >= long(bw)) continue;
                            v = near_[std::size_t(rr) * bw + std::size_t(cc)];
                        }
                        tmp[std::size_t(r) * bw + c] = v;
                    }
                    near_.swap(tmp);
                }
                std::uint64_t onlyNear = 0, onlyFar = 0;
                for (std::size_t i = 0; i < only.size(); ++i) {
                    if (!only[i]) continue;
                    if (near_[i]) ++onlyNear; else ++onlyFar;
                }
                const double tot = double(onlyNear + onlyFar);
                std::printf("\n  §3.4 gate, where the partial blocks sit (#171, %d-block reach)\n", kReach);
                std::printf("    partial blocks NEAR a fully clipped one   %llu  (%.1f%%)\n",
                            (unsigned long long)onlyNear, tot ? 100.0 * double(onlyNear) / tot : 0.0);
                std::printf("    partial blocks FAR from any               %llu  (%.1f%%)"
                            "  <- a per-region gate would ignore these\n",
                            (unsigned long long)onlyFar, tot ? 100.0 * double(onlyFar) / tot : 0.0);
            }
        }

        std::printf("\n  §3.4 gate (raw CFA proxy, decision #170)\n");
        std::printf("    fully clipped blocks           %llu of %llu  (%.4f%%)\n",
                    (unsigned long long)allThree, (unsigned long long)blocks,
                    blocks ? 100.0 * double(allThree) / double(blocks) : 0.0);
        static const char* kRGB[3] = {"R", "G", "B"};
        for (int k = 0; k < 3; ++k) {
            std::printf("    %s clipped but not all three     %llu%s\n", kRGB[k],
                        (unsigned long long)onlyMissing[k],
                        onlyMissing[k] == 0 ? "   <- containment HOLDS for this channel" : "");
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

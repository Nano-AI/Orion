/*  tests_merge_render — the merge's six-node demosaic graph against known
 *  mosaics and against the develop graph it borrows its kernels from.
 *
 *  The constant-card check is the exact control: a uniform mosaic must come
 *  back as exactly its own per-channel values (black subtracted, range
 *  scaled, no white balance) — any parameter mis-wired in the borrowed
 *  kernels shows up as a wrong constant. The develop-graph parity check then
 *  covers the interesting part: on a smooth gradient, dividing the as-shot
 *  gains back out of the develop graph's post-demosaic node must land on
 *  MergeRender's camera-native output. The tolerance is loose-ish (2^-8)
 *  because the two graphs interpolate on different data — white-balanced
 *  versus native — and RCD is not exactly gain-equivariant; on smooth data
 *  the difference is far below visibility, and the check pins that.
 */

#include "harness.h"
#include "merge/MergeRender.h"
#include "util/Half.h"

namespace {

constexpr std::uint32_t kW = 32;
constexpr std::uint32_t kH = 32;

orion::raw::BayerImage baseImage() {
    orion::raw::BayerImage img;
    img.width = kW;
    img.height = kH;
    img.filters = 0x94949494u;   // RGGB
    img.white = 4095;
    img.black = {256, 256, 256, 256};
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};
    img.samples.assign(std::size_t(kW) * kH, 0);
    return img;
}

}  // namespace

void testMergeRenderDemosaic() {
    section("merge render: six nodes, exact on a constant card");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device for merge render", e.what());
        return;
    }

    // The budget the story promised, at the a7riii's full frame.
    const std::size_t at42MP = orion::merge::MergeRender::gpuBytes(7952, 5304);
    report(at42MP <= std::size_t(1.2 * 1024 * 1024 * 1024),
           "42 MP graph stays under 1.2 GiB",
           std::to_string(at42MP / (1024 * 1024)) + " MiB");

    orion::merge::MergeRender render(*device, ORION_SHADER_DIR);

    // ── Constant card: (v - black) / (white - black), no gains ────────────
    {
        auto img = baseImage();
        // Per-CFA-channel constants, chosen unequal so a channel swap fails.
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const auto c = img.channelAt(x, y);
                const std::uint16_t v =
                    c == orion::raw::Channel::R ? 2304
                    : (c == orion::raw::Channel::B ? 1280 : 1792);
                img.samples[std::size_t(y) * kW + x] = v;
            }
        }
        const auto rgb = render.demosaic(img);

        const float invRange = 1.0f / float(4095 - 256);
        const float wantR = float(2304 - 256) * invRange;
        const float wantG = float(1792 - 256) * invRange;
        const float wantB = float(1280 - 256) * invRange;

        // Interior pixel, clear of any border clamping.
        const std::size_t p = (std::size_t(16) * kW + 16) * 3;
        const float tol = 0x1p-10f;
        checkNear(rgb[p + 0], wantR, tol, "constant card R, native units");
        checkNear(rgb[p + 1], wantG, tol, "constant card G, native units");
        checkNear(rgb[p + 2], wantB, tol, "constant card B, native units");
        report(rgb[p + 0] != rgb[p + 2], "R and B stay distinct — no gain applied");
    }

    // ── Reuse: a second frame through the same compiled graph ─────────────
    {
        auto img = baseImage();
        for (auto& s : img.samples) s = 2000;
        const auto rgb = render.demosaic(img);
        const std::size_t p = (std::size_t(16) * kW + 16) * 3;
        checkNear(rgb[p + 1], float(2000 - 256) / float(4095 - 256), 0x1p-10f,
                  "the reloaded graph carries the second frame, not the first");
    }

    // ── Parity with the develop graph on a smooth gradient ────────────────
    {
        auto img = baseImage();
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                // Smooth, mid-range, far from both graphs' clips.
                img.samples[std::size_t(y) * kW + x] = std::uint16_t(
                    600 + (x + y) * 24);
            }
        }
        const auto native = render.demosaic(img);

        orion::pipe::DevelopPipeline dev(*device, ORION_SHADER_DIR, img);
        dev.graph().render();
        const auto stages = dev.highlightStages();
        std::vector<std::uint16_t> half(std::size_t(kW) * kH * 4);
        stages.input->download(half.data(),
                               std::size_t(kW) * 4 * sizeof(std::uint16_t),
                               kW, kH);

        // The develop graph white-balances before demosaic; divide the
        // as-shot gains back out (normalized to green, as linearize applies
        // them).
        const float mul[3] = {2.0f, 1.0f, 1.5f};
        float worst = 0.0f;
        for (std::uint32_t y = 4; y < kH - 4; ++y) {
            for (std::uint32_t x = 4; x < kW - 4; ++x) {
                const std::size_t p = std::size_t(y) * kW + x;
                for (int c = 0; c < 3; ++c) {
                    const float dev_c =
                        orion::util::halfToFloat(half[p * 4 + std::size_t(c)]) / mul[c];
                    worst = std::max(worst,
                                     std::abs(dev_c - native[p * 3 + std::size_t(c)]));
                }
            }
        }
        report(worst < 0x1p-8f,
               "develop graph with gains divided out lands on the native render",
               "worst " + std::to_string(worst));
    }
}

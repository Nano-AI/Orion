// Spot removal: the clone and heal pair.
//
// Moved out of tests_brush.cpp (decision #129), which was 1,142 lines. Its own
// fixture end to end — two kernels nothing else dispatches, `spotMeasure` and
// `spotApply`, and a flat two-field frame nothing else in the tree reads.

#include "harness.h"

// Spot removal — research/spot-removal.md.
//
// Two kernels, and the interesting one is the pair together: clone pastes the
// source unchanged, heal pastes it plus the boundary difference. The synthetic
// frame is built so that difference has an exact known value, which is what
// separates "the spot did something" from "the spot did the right thing".
void testSpotRemovalGpu() {
    section("Spot removal (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 128, kH = 128;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    const std::string dir = std::string(ORION_SHADER_DIR) + "/";
    auto libM = orion::gpu::Library::createFromFile(*device, dir + "spotMeasure.metallib");
    auto libA = orion::gpu::Library::createFromFile(*device, dir + "spotApply.metallib");
    auto kM = orion::gpu::Kernel::create(*device, *libM, "spotMeasure");
    auto kA = orion::gpu::Kernel::create(*device, *libA, "spotApply");

    auto src  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto corr = orion::gpu::Texture::create(*device, params::kMaxSpots, 1,
                                            PixelFormat::RGBA32Float);
    auto dst  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

    // Two flat fields of different brightness, left and right, with a blemish
    // painted on the left one. Flat on purpose: the constant correction is the
    // *exact* answer when the boundary difference is constant, so the numbers
    // below are the algorithm's, not a tolerance around a picture.
    constexpr float kLeft = 0.30f, kRight = 0.50f, kBlemish = 0.05f;
    const auto paint = [&]() {
        std::vector<__fp16> px(std::size_t(kW) * kH * 4);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                float v = (x < kW / 2) ? kLeft : kRight;
                // The blemish, centerd at (0.25, 0.5) with radius 0.06 in x.
                const double dx = (x + 0.5) / kW - 0.25;
                const double dy = ((y + 0.5) / kH - 0.5) * double(kH) / double(kW);
                if (dx * dx + dy * dy < 0.06 * 0.06) v = kBlemish;
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                px[i + 0] = __fp16(v); px[i + 1] = __fp16(v);
                px[i + 2] = __fp16(v); px[i + 3] = __fp16(1.0f);
            }
        }
        src->upload(px.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    };
    paint();

    const auto run = [&](float destX, float srcX, float radius, bool heal) {
        params::SpotMeasure sm{};
        sm.size[0] = kW; sm.size[1] = kH;
        sm.count = 1; sm.samples = 32;
        params::SpotApply sa{};
        sa.size[0] = kW; sa.size[1] = kH;
        sa.count = 1;
        const float sp[4] = {destX, 0.5f, srcX, 0.5f};
        const float sh[4] = {radius, 0.25f, heal ? 1.0f : 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k) {
            sm.spots[0][k] = sp[k]; sm.shape[0][k] = sh[k];
            sa.spots[0][k] = sp[k]; sa.shape[0][k] = sh[k];
        }

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kM, {src.get(), corr.get()}, &sm, sizeof sm, params::kMaxSpots, 1);
        cb.dispatch(*kA, {src.get(), corr.get(), dst.get()}, &sa, sizeof sa, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH * 4);
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& v, double u, double w) {
        const auto x = std::uint32_t(u * kW), y = std::uint32_t(w * kH);
        return double(v[(std::size_t(y) * kW + x) * 4]);
    };

    // ── Clone pastes the source exactly ───────────────────────────────────
    //
    // Destination on the blemish, source on the right-hand field. Clone takes
    // the right field's value verbatim, which is visibly wrong against the
    // left field around it — and being visibly wrong is the point of clone
    // existing separately from heal.
    {
        const auto got = run(0.25f, 0.75f, 0.08f, /*heal=*/false);
        report(std::abs(at(got, 0.25, 0.5) - kRight) < 0.01,
               "clone pastes the source's own value, unmodified",
               std::to_string(at(got, 0.25, 0.5)));
    }

    // ── Heal takes the destination's tone ─────────────────────────────────
    //
    // ⚠ The exact answer, not a range. The rim of the destination disc sits on
    // the left field at 0.30 and the matching rim on the source sits on the
    // right field at 0.50, so the boundary difference is exactly -0.20 and the
    // healed patch is 0.50 - 0.20 = 0.30: the left field, with the blemish
    // gone. A correction computed from the disc's *interior* instead of its rim
    // would measure the blemish and land near 0.05.
    {
        const auto got = run(0.25f, 0.75f, 0.08f, /*heal=*/true);
        report(std::abs(at(got, 0.25, 0.5) - kLeft) < 0.01,
               "heal pastes the source's detail with the destination's tone",
               std::to_string(at(got, 0.25, 0.5)));

        // And the blemish is actually gone — the check above would also pass if
        // the patch simply were not applied, since the field it lands on is the
        // same 0.30.
        report(std::abs(at(got, 0.25, 0.5) - kBlemish) > 0.2,
               "and the blemish underneath it is replaced, not left showing",
               std::to_string(at(got, 0.25, 0.5)));
    }

    // ── Nothing outside the disc is touched ───────────────────────────────
    {
        const auto got = run(0.25f, 0.75f, 0.08f, true);
        report(std::abs(at(got, 0.60, 0.5) - kRight) < 1e-3 &&
               std::abs(at(got, 0.05, 0.1) - kLeft) < 1e-3,
               "and the rest of the frame is left alone",
               std::to_string(at(got, 0.60, 0.5)) + ", " + std::to_string(at(got, 0.05, 0.1)));
    }

    // ── The rim is measured outside the disc, not inside it ───────────────
    //
    // ⚠ The check that pins where the boundary is sampled. With the source
    // placed on the *same* field as the destination, a correct rim measurement
    // gives a difference of zero and heal reduces to clone — both paste 0.30.
    // Sampling the interior instead would put the blemish into the source term,
    // give a correction of about +0.25, and blow the patch out to 0.55.
    {
        const auto got = run(0.25f, 0.10f, 0.06f, true);
        report(at(got, 0.25, 0.5) < 0.40,
               "the boundary is sampled outside the disc, so a same-field heal "
               "is not inflated by the blemish it is covering",
               std::to_string(at(got, 0.25, 0.5)));
    }

    // ── The patch carries the source's DETAIL, not just its level ─────────
    //
    // ⚠ Added because a mutation survived. Every case above uses flat fields,
    // so sampling the source once at its center gives the same answer as
    // sampling it per pixel — and replacing the per-pixel lookup with the
    // center's passed the whole suite. Copying detail is the entire reason
    // clone and heal exist rather than "fill with a color".
    {
        // Fine stripes in the source half, flat in the destination half.
        std::vector<__fp16> px(std::size_t(kW) * kH * 4);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const float v = (x < kW / 2) ? kLeft
                                             : ((y / 4) % 2 == 0 ? 0.65f : 0.35f);
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                px[i + 0] = __fp16(v); px[i + 1] = __fp16(v);
                px[i + 2] = __fp16(v); px[i + 3] = __fp16(1.0f);
            }
        }
        src->upload(px.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        const auto got = run(0.25f, 0.75f, 0.10f, /*heal=*/false);

        // Walk down the middle of the patch. A per-pixel source reproduces the
        // stripes; a single center sample gives one flat value.
        double lo = 1.0, hi = 0.0;
        for (std::uint32_t y = kH / 2 - 8; y <= kH / 2 + 8; ++y) {
            const double v = double(got[(std::size_t(y) * kW + kW / 4) * 4]);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        report(hi - lo > 0.2,
               "the patch reproduces the source's texture rather than one "
               "averaged value",
               "range " + std::to_string(hi - lo));

        paint();   // restore the flat frame for anything after this
    }

    // ── A spot is a disc on the picture, not an ellipse ───────────────────
    //
    // The frame here is square, so this uses a deliberately non-square one: the
    // same trap the brush nib fell into, where a radius measured in normalized
    // coordinates stretched with the frame's aspect instead of staying round.
    {
        constexpr std::uint32_t wW = 192, wH = 96;
        auto wide = orion::gpu::Texture::create(*device, wW, wH, PixelFormat::RGBA16Float);
        auto wout = orion::gpu::Texture::create(*device, wW, wH, PixelFormat::RGBA16Float);
        std::vector<__fp16> px(std::size_t(wW) * wH * 4, __fp16(0.4f));
        for (std::size_t i = 3; i < px.size(); i += 4) px[i] = __fp16(1.0f);
        wide->upload(px.data(), std::size_t(wW) * 4 * sizeof(__fp16));

        params::SpotApply sa{};
        sa.size[0] = wW; sa.size[1] = wH;
        sa.count = 1;
        sa.spots[0][0] = 0.5f; sa.spots[0][1] = 0.5f;
        sa.spots[0][2] = 0.5f; sa.spots[0][3] = 0.5f;
        sa.shape[0][0] = 0.10f; sa.shape[0][1] = 0.0f; sa.shape[0][2] = 0.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kA, {wide.get(), corr.get(), wout.get()}, &sa, sizeof sa, wW, wH);
        cb.commitAndWait();

        // The disc's extent is measured by how far the coverage reaches, which
        // needs the patch to differ from the frame — so the source is offset
        // and the frame given a ramp. Simpler: count the pixels the kernel
        // treats as inside, by running a clone from a distinct source.
        std::vector<__fp16> ramp(std::size_t(wW) * wH * 4);
        for (std::uint32_t y = 0; y < wH; ++y) {
            for (std::uint32_t x = 0; x < wW; ++x) {
                const std::size_t i = (std::size_t(y) * wW + x) * 4;
                const float v = (x < wW / 2) ? 0.2f : 0.8f;
                ramp[i + 0] = __fp16(v); ramp[i + 1] = __fp16(v);
                ramp[i + 2] = __fp16(v); ramp[i + 3] = __fp16(1.0f);
            }
        }
        wide->upload(ramp.data(), std::size_t(wW) * 4 * sizeof(__fp16));
        sa.spots[0][0] = 0.25f; sa.spots[0][2] = 0.75f;
        orion::gpu::CommandBuffer cb2(*device);
        cb2.dispatch(*kA, {wide.get(), corr.get(), wout.get()}, &sa, sizeof sa, wW, wH);
        cb2.commitAndWait();

        std::vector<__fp16> out(std::size_t(wW) * wH * 4);
        wout->download(out.data(), std::size_t(wW) * 4 * sizeof(__fp16), wW, wH);
        const auto val = [&](std::uint32_t x, std::uint32_t y) {
            return double(out[(std::size_t(y) * wW + x) * 4]);
        };

        // Half-width and half-height of the changed region, in pixels. A disc
        // has them equal; an ellipse stretched by the 2:1 aspect does not.
        std::uint32_t cx = wW / 4, cy = wH / 2;
        int halfW = 0, halfH = 0;
        while (cx + std::uint32_t(halfW) + 1 < wW && val(cx + halfW + 1, cy) > 0.5) ++halfW;
        while (cy + std::uint32_t(halfH) + 1 < wH && val(cx, cy + halfH + 1) > 0.5) ++halfH;
        report(halfW > 4 && std::abs(halfW - halfH) <= 2,
               "a spot is round on a 2:1 frame, not stretched by its aspect",
               std::to_string(halfW) + " x " + std::to_string(halfH) + " px");
    }
}

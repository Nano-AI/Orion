// The `developLinear` kernel: the tone bands and the local adjustments.
//
// One fixture, two subjects (decision #129). Both dispatch `developLinear` with
// the same eight-texture binding list and the same `params::LinearAdjust`, and
// the pair is why they are one file: `testToneBandsWithoutGuide` has to explain
// a mask binding it never samples — "the binding has to exist or every texture
// after it shifts by one, which is silent and total" — and
// `testLocalAdjustments` is the test that samples it.
//
// Moved out of tests_grade.cpp, which was 1,029 lines.

#include "harness.h"

/// The tone bands, with the guided-filter chain switched off.
///
/// The guide chain is seven nodes feeding only the highlight and shadow bands,
/// so it is disabled when both sliders are zero — and a disabled node resolves
/// to the last live producer, which is the color matrix. `developLinear`
/// sampled the two guide bindings regardless, so it read linear RGB as log2
/// luminance and as filter coefficients.
///
/// The offsets were zero so nothing moved directly, and every visual check
/// passed. But the four band weights are normalized into a partition of unity,
/// and two of them came from that garbage — so they sat in the denominator and
/// diluted whites and blacks per pixel, by an amount that varied with the
/// pixel's own color. The bench printed `ok` throughout, because it asserted
/// that a blacks edit changed *something* rather than that it changed by the
/// right amount.
///
/// The invariant: a blacks-only edit must land identically whether the guide
/// chain is running or not.
void testToneBandsWithoutGuide() {
    section("Tone bands with the guide chain off (GPU)");

    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developLinear.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developLinear");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    // The real guide textures are RG32Float; the *resolved* ones, when the
    // chain is off, are the color matrix's RGBA16Float. Both are built here so
    // the test exercises the binding production actually makes.
    auto guideAb = orion::gpu::Texture::create(*device, kW, kH,
                                               orion::gpu::PixelFormat::RG32Float);
    auto maskStub = orion::gpu::Texture::create(*device, kW, kH,
                                                orion::gpu::PixelFormat::R16Float);
    auto guideRaw = orion::gpu::Texture::create(*device, kW, kH,
                                                orion::gpu::PixelFormat::RG32Float);

    // A luminance ramp spanning every band center, from below blacks (-5.5 EV)
    // to above whites (+5.5 EV). Slightly tinted, because the garbage `ab` is
    // the pixel's own red and green — a neutral frame would understate it.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    std::vector<float> lumaEv(std::size_t(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            const double ev = -7.0 + 14.0 * (x / double(kW - 1));
            const double base = 0.18 * std::pow(2.0, ev);
            const double tint = 0.7 + 0.6 * (y / double(kH - 1));
            input[i * 4 + 0] = static_cast<__fp16>(base * tint);
            input[i * 4 + 1] = static_cast<__fp16>(base);
            input[i * 4 + 2] = static_cast<__fp16>(base / tint);
            input[i * 4 + 3] = 1;
            // Rec.2020 luma of what was written, which is what the shader's own
            // fallback computes.
            lumaEv[i] = static_cast<float>(
                0.2627 * double(input[i * 4 + 0]) +
                0.6780 * double(input[i * 4 + 1]) +
                0.0593 * double(input[i * 4 + 2]));
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(float) / 2);

    // A guide that is deliberately the identity: a = 1, b = 0, and raw = the
    // pixel's own log2 luminance. Then the guided estimate *is* the pixel, so a
    // correct disabled path has to agree with it exactly. Any other guide would
    // only prove the two paths differ, which is not the question.
    std::vector<float> ab(std::size_t(kW) * kH * 2);
    std::vector<float> raw(std::size_t(kW) * kH * 2);
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        ab[i * 2 + 0] = 1.0f;
        ab[i * 2 + 1] = 0.0f;
        raw[i * 2 + 0] = std::log2(std::max(lumaEv[i], 1e-6f));
        raw[i * 2 + 1] = 0.0f;
    }
    guideAb->upload(ab.data(), std::size_t(kW) * 2 * sizeof(float));
    guideRaw->upload(raw.data(), std::size_t(kW) * 2 * sizeof(float));

    orion::pipe::params::LinearAdjust la{};
    la.size[0] = kW; la.size[1] = kH;
    la.guideSize[0] = kW; la.guideSize[1] = kH;

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    // `live` picks which textures land in the guide slots: the real ones, or
    // the color matrix output that `Pipeline::resolve` substitutes when the
    // chain is disabled.
    const auto run = [&](bool live, std::vector<__fp16>& o) {
        orion::gpu::CommandBuffer cb(*device);
        // The kernel gained a mask input. It is never sampled here — maskActive
        // stays zero — but the binding has to exist or every texture after it
        // shifts by one, which is silent and total.
        cb.dispatch(*kernel,
                    {src.get(),
                     live ? guideAb.get() : src.get(),
                     live ? guideRaw.get() : src.get(),
                     maskStub.get(), maskStub.get(),
                     maskStub.get(), maskStub.get(),
                     dst.get()},
                    &la, sizeof la, kW, kH);
        cb.commitAndWait();
        dst->download(o.data(), std::size_t(kW) * 4 * sizeof(float) / 2, kW, kH);
    };

    // Green carries the untinted luminance, so it is what the deltas are read
    // from. In EV, because that is the unit the bands are defined in.
    const auto greenEvDelta = [&](const std::vector<__fp16>& o, std::size_t i) {
        const double after = std::max(double(o[i * 4 + 1]), 1e-9);
        const double before = std::max(double(input[i * 4 + 1]), 1e-9);
        return std::log2(after / before);
    };

    // Nothing set is a pass-through down both paths, or the fallback is not a
    // fallback. This kernel had no GPU test at all before now.
    std::vector<__fp16> idle(std::size_t(kW) * kH * 4);
    la.guideEnabled = 0.0f;
    run(false, idle);
    double worstIdle = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        worstIdle = std::max(worstIdle, std::abs(greenEvDelta(idle, i)));
    }
    report(worstIdle < 0.01, "no adjustment is a pass-through with the guide off",
           "worst " + std::to_string(worstIdle) + " EV");

    // ── The invariant ───────────────────────────────────────────────────
    la.blacks = -1.0f;

    std::vector<__fp16> off(std::size_t(kW) * kH * 4);
    la.guideEnabled = 0.0f;
    run(false, off);                 // guide disabled, bindings resolved past it

    std::vector<__fp16> on(std::size_t(kW) * kH * 4);
    la.guideEnabled = 1.0f;
    run(true, on);                   // guide live, and equal to the pixel

    double worstGap = 0.0, worstAt = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        const double gap = std::abs(greenEvDelta(off, i) - greenEvDelta(on, i));
        if (gap > worstGap) { worstGap = gap; worstAt = greenEvDelta(on, i); }
    }
    report(worstGap < 0.02,
           "a blacks edit lands the same whether the guide chain runs or not",
           "worst " + std::to_string(worstGap) + " EV, against a delta of " +
           std::to_string(worstAt));

    // ── And the test has to be able to fail ─────────────────────────────
    //
    // The shipped behavior: the flag set, but the bindings still resolved back
    // to the color matrix. If this passed, the check above would be measuring
    // nothing.
    std::vector<__fp16> shipped(std::size_t(kW) * kH * 4);
    la.guideEnabled = 1.0f;
    run(false, shipped);

    // The darkest patches are where blacks does its work, so that is where the
    // dilution shows. Compare the strongest correct delta against what the
    // garbage weights allowed.
    double bestCorrect = 0.0, atSameSpot = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        const double d = std::abs(greenEvDelta(on, i));
        if (d > bestCorrect) { bestCorrect = d; atSameSpot = std::abs(greenEvDelta(shipped, i)); }
    }
    report(bestCorrect > 1.0, "blacks at -1 is worth more than a stop somewhere",
           std::to_string(bestCorrect) + " EV");
    report(atSameSpot < 0.75 * bestCorrect,
           "the old binding measurably diluted it, so this check can fail",
           "correct " + std::to_string(bestCorrect) + " EV, shipped " +
           std::to_string(atSameSpot) + " EV");
    std::printf("  blacks -1 at its strongest: %.3f EV correct, %.3f EV as shipped\n",
                bestCorrect, atSameSpot);

    // Whites, the same way. It reads the pixel too, so it was diluted too.
    la.blacks = 0.0f;
    la.whites = 1.0f;
    la.guideEnabled = 0.0f;
    run(false, off);
    la.guideEnabled = 1.0f;
    run(true, on);
    double worstWhites = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        worstWhites = std::max(worstWhites,
                               std::abs(greenEvDelta(off, i) - greenEvDelta(on, i)));
    }
    report(worstWhites < 0.02, "and so does a whites edit",
           "worst " + std::to_string(worstWhites) + " EV");
}

// Local adjustments beyond exposure — research/masking.md §2b.
//
// §2's rule is that the coverage scales the *parameter*, not the result, and
// §2b's test for whether an adjustment can be local at all is whether it is a
// function of the pixel alone. These are the four that pass it.
//
// ⚠ Asserted against exact numbers rather than magnitudes. Every one of these
// is a small addition to a node that already runs, and "it moved" is what let a
// blacks slider ship delivering 39% of its effect.
void testLocalAdjustments() {
    section("Local adjustments (GPU)");

    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 64, kH = 8;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developLinear.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developLinear");

    using orion::gpu::PixelFormat;
    auto src  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto dst  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto mask = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);

    // A tinted mid-gray everywhere, so saturation and a color cast both have
    // something to act on. Constant, because what is being measured is the
    // coverage ramp rather than a response to the picture.
    constexpr double kR = 0.22, kG = 0.18, kB = 0.14;
    {
        std::vector<__fp16> in(std::size_t(kW) * kH * 4);
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
            in[i * 4 + 0] = __fp16(kR); in[i * 4 + 1] = __fp16(kG);
            in[i * 4 + 2] = __fp16(kB); in[i * 4 + 3] = __fp16(1.0f);
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    }
    // Coverage ramps 0..1 across x, so one run measures the whole ramp.
    {
        std::vector<__fp16> a(std::size_t(kW) * kH);
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x)
                a[std::size_t(y) * kW + x] = __fp16(float(x) / float(kW - 1));
        mask->upload(a.data(), std::size_t(kW) * sizeof(__fp16));
    }

    params::LinearAdjust la{};
    la.size[0] = kW; la.size[1] = kH;
    la.guideSize[0] = kW; la.guideSize[1] = kH;
    la.maskActive = 1.0f;
    // One layer, reading coverage slot 0.
    la.layerCount = 1;
    la.layerMask[0] = 0;

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    const auto run = [&]() {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), src.get(), src.get(),
                              mask.get(), mask.get(), mask.get(), mask.get(),
                              dst.get()},
                    &la, sizeof la, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
    };
    const auto px = [&](int x, int c) {
        return double(out[(std::size_t(kH / 2) * kW + std::size_t(x)) * 4 + std::size_t(c)]);
    };
    const auto luma = [&](int x) {
        return 0.2627 * px(x, 0) + 0.6780 * px(x, 1) + 0.0593 * px(x, 2);
    };
    const double baseLuma = 0.2627 * kR + 0.6780 * kG + 0.0593 * kB;

    // ── ⚠ Zero coverage is bit-identical to no local adjustment ───────────
    //
    // The load-bearing one. Every check below says "it moved"; this says it
    // moved *only where the mask is*. Column 0 has alpha 0.
    {
        la.layerContrast[0] = 0.7f; la.layerSaturation[0] = -0.8f;
        la.layerWarmth[0] = 0.6f; la.layerTint[0] = -0.4f;
        run();
        const bool same = std::abs(px(0, 0) - kR) < 2e-3
                       && std::abs(px(0, 1) - kG) < 2e-3
                       && std::abs(px(0, 2) - kB) < 2e-3;
        report(same, "all four leave a pixel with no coverage exactly as it was",
               std::to_string(px(0, 0)) + ", " + std::to_string(px(0, 1)) + ", "
             + std::to_string(px(0, 2)));
        la = params::LinearAdjust{};
        la.size[0] = kW; la.size[1] = kH;
        la.guideSize[0] = kW; la.guideSize[1] = kH;
        la.maskActive = 1.0f;
        la.layerCount = 1;
        la.layerMask[0] = 0;
    }

    // ── Contrast pivots where the display transform pivots ────────────────
    //
    // A gain on the pixel's distance from -2.5 in log2, so the answer at full
    // coverage is exactly 2^((ev + 2.5) * k) times the input. The tinted gray
    // sits above the pivot, so a positive contrast brightens it.
    {
        la.layerContrast[0] = 0.5f;
        run();
        const double ev = std::log2(baseLuma);
        const double want = baseLuma * std::exp2((ev + 2.5) * 0.5);
        report(std::abs(luma(kW - 1) - want) / want < 0.02,
               "contrast is a gain on distance from the display transform's pivot",
               std::to_string(luma(kW - 1)) + " against " + std::to_string(want));

        // Half coverage gives half the gain in the exponent, which is what
        // "the alpha scales the parameter" means for this control.
        const double half = baseLuma * std::exp2((ev + 2.5) * 0.5 * 0.5);
        const int mid = int(kW) / 2;
        report(std::abs(luma(mid) - half) / half < 0.03,
               "and half coverage applies half of it, in the exponent",
               std::to_string(luma(mid)) + " against " + std::to_string(half));
        la.layerContrast[0] = 0.0f;
    }

    // ── Saturation goes to the pixel's own luminance ──────────────────────
    {
        la.layerSaturation[0] = -1.0f;
        run();
        const int x = int(kW) - 1;
        const double spread = std::max({px(x, 0), px(x, 1), px(x, 2)})
                            - std::min({px(x, 0), px(x, 1), px(x, 2)});
        report(spread < 2e-3, "full negative saturation reaches neutral",
               std::to_string(spread));
        report(std::abs(luma(x) - baseLuma) / baseLuma < 0.01,
               "and does it at the pixel's own luminance, not at gray",
               std::to_string(luma(x)) + " against " + std::to_string(baseLuma));
        la.layerSaturation[0] = 0.0f;
    }

    // ── ⚠ The color cast moves color and not exposure ───────────────────
    //
    // The channels are renormalized on luminance afterwards, or Warmth would
    // double as a brightness slider and the two controls would fight over the
    // same pixels. The first version of the shader read the luminance on both
    // sides of the cast from the same already-cast color, so the ratio was one
    // and the line did nothing — this is the check that would have caught it.
    {
        la.layerWarmth[0] = 1.0f;
        run();
        const int x = int(kW) - 1;
        report(std::abs(luma(x) - baseLuma) / baseLuma < 0.01,
               "a warm cast at full coverage leaves the luminance where it was",
               std::to_string(luma(x)) + " against " + std::to_string(baseLuma));
        report(px(x, 0) / px(x, 2) > (kR / kB) * 1.4,
               "while moving red against blue",
               std::to_string(px(x, 0) / px(x, 2)) + " against "
             + std::to_string(kR / kB));

        // And it is signed.
        la.layerWarmth[0] = -1.0f;
        run();
        report(px(x, 0) / px(x, 2) < (kR / kB) * 0.75,
               "and the other way when it is negative",
               std::to_string(px(x, 0) / px(x, 2)));
        la.layerWarmth[0] = 0.0f;
    }

    // ── Tint is the green axis, and independent of warmth ─────────────────
    {
        la.layerTint[0] = 1.0f;
        run();
        const int x = int(kW) - 1;
        report(px(x, 1) / (px(x, 0) + px(x, 2)) > (kG / (kR + kB)) * 1.15,
               "tint moves green against the magenta axis",
               std::to_string(px(x, 1) / (px(x, 0) + px(x, 2))));
        report(std::abs(luma(x) - baseLuma) / baseLuma < 0.01,
               "and holds the luminance too",
               std::to_string(luma(x)));
    }
}

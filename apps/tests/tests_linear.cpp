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

// For `kBaselineExposureEv` — the constant `kEvWhites` is derived from.
#include "pipe/DevelopInternal.h"

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
        la.layerHighlights[0] = 0.8f; la.layerShadows[0] = -0.7f;
        la.layerWhites[0] = 0.5f; la.layerBlacks[0] = -0.6f;
        run();
        const bool same = std::abs(px(0, 0) - kR) < 2e-3
                       && std::abs(px(0, 1) - kG) < 2e-3
                       && std::abs(px(0, 2) - kB) < 2e-3;
        report(same, "all eight leave a pixel with no coverage exactly as it was",
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
        la.layerTint[0] = 0.0f;
    }

    // ── The tone bands, per layer and closed-form ─────────────────────────
    //
    // `guideEnabled` is zero here, so the shader's guideEv falls back to the
    // pixel's own log2 luminance and the whole deltaEv is computable below by
    // the published construction (research/deep-research-2026-07-27.md §3) —
    // a different order of operations from the kernel's, deliberately.
    {
        la.layerShadows[0]    = 1.0f;
        la.layerHighlights[0] = 0.4f;
        la.layerWhites[0]     = -0.3f;
        la.layerBlacks[0]     = 0.2f;
        run();
        const double ev = std::log2(baseLuma / 0.18);
        const auto w = [&](double center) {
            const double d = (ev - center) / 1.6;
            return std::exp(-0.5 * d * d);
        };
        // ⚠ The whites center mirrors `kEvWhites` in ops/tone_ops.slang, which
        // is `kBaselineExposureEv` - log2(0.18) and not the research table's
        // +5.5 — decision #221. A closed form that re-derives the shader by a
        // different route still has to carry the same constants.
        constexpr double kEvWhitesMirror = 3.673931;

        // ⚠ **The one thing that makes the number above safe to duplicate.**
        // `kEvWhites` is *derived* from `kBaselineExposureEv`, and it is written
        // out longhand in two places — the shader, which cannot include a C++
        // header, and the line below. `kBaselineExposureEv` is itself a fit
        // (#46, #190) and is exactly the kind of constant a later session
        // remeasures. Move it without this and the whites band silently slides
        // off the white anchor, which is the defect #221 closed, restored in
        // full and just as invisible: the bench probe keeps passing on any
        // frame with highlights, because the band is still *near* the data.
        //
        // So the derivation is asserted rather than trusted. This fails the
        // moment the baseline moves, and it names the two files to fix.
        report(std::abs(kEvWhitesMirror
                        - (orion::pipe::kBaselineExposureEv - std::log2(0.18))) < 1e-4,
               "kEvWhites still sits on the white anchor kBaselineExposureEv puts it at",
               "kBaselineExposureEv = "
               + std::to_string(orion::pipe::kBaselineExposureEv)
               + " wants kEvWhites "
               + std::to_string(orion::pipe::kBaselineExposureEv - std::log2(0.18))
               + ", but ops/tone_ops.slang and this file both say "
               + std::to_string(kEvWhitesMirror));

        const double wB = w(-5.5), wS = w(-2.5), wH = w(2.5), wW = w(kEvWhitesMirror);
        const double total = wB + wS + wH + wW + 1e-6;
        const double delta = ((wB * 0.2) + (wS * 1.0) + (wH * 0.4)
                              + (wW * -0.3)) * 2.0 / total;

        const double want = baseLuma * std::exp2(delta);
        report(std::abs(luma(int(kW) - 1) - want) / want < 0.02,
               "the four bands sum into one exponent at full coverage",
               std::to_string(luma(int(kW) - 1)) + " against "
             + std::to_string(want));

        // The alpha scales the parameter, and deltaEv is linear in each — so
        // half coverage is exactly half the exponent, not a blend of frames.
        const double half = baseLuma * std::exp2(delta * 0.5);
        const int mid = int(kW) / 2;
        report(std::abs(luma(mid) - half) / half < 0.03,
               "and half coverage halves the exponent",
               std::to_string(luma(mid)) + " against " + std::to_string(half));
    }
}

/// Show mask paints the layer being edited — not the union it used to paint.
///
/// The union drowned the mask under the cursor in every other mask's red, so
/// the overlay now reads exactly one layer's coverage, chosen by
/// `maskOverlayLayer`. Two layers on two disjoint coverages: the tint must
/// land on the chosen one and leave the other bit-exact, whichever is chosen.
void testOverlayPaintsTheSelectedLayer() {
    section("Mask overlay (GPU)");

    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 64, kH = 16;

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
    auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto maskL = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto maskR = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);

    constexpr double kR = 0.22, kG = 0.18, kB = 0.14;
    {
        std::vector<__fp16> in(std::size_t(kW) * kH * 4);
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
            in[i * 4 + 0] = __fp16(kR); in[i * 4 + 1] = __fp16(kG);
            in[i * 4 + 2] = __fp16(kB); in[i * 4 + 3] = __fp16(1.0f);
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    }
    // Disjoint coverages: slot 0 owns the left half, slot 1 the right.
    {
        std::vector<__fp16> left(std::size_t(kW) * kH), right(left.size());
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                left[std::size_t(y) * kW + x]  = __fp16(x < kW / 2 ? 1.0f : 0.0f);
                right[std::size_t(y) * kW + x] = __fp16(x < kW / 2 ? 0.0f : 1.0f);
            }
        maskL->upload(left.data(), std::size_t(kW) * sizeof(__fp16));
        maskR->upload(right.data(), std::size_t(kW) * sizeof(__fp16));
    }

    params::LinearAdjust la{};
    la.size[0] = kW; la.size[1] = kH;
    la.guideSize[0] = kW; la.guideSize[1] = kH;
    la.maskActive = 1.0f;
    la.layerCount = 2;
    la.layerMask[0] = 0;
    la.layerMask[1] = 1;
    la.maskOverlay = 1.0f;

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    const auto run = [&]() {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), src.get(), src.get(),
                              maskL.get(), maskR.get(), maskL.get(), maskL.get(),
                              maskL.get(), maskL.get(), maskL.get(), maskL.get(),
                              dst.get()},
                    &la, sizeof la, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
    };
    const auto px = [&](int x, int c) {
        return double(out[(std::size_t(kH / 2) * kW + std::size_t(x)) * 4
                          + std::size_t(c)]);
    };
    // The overlay is the only thing acting here, so "tinted" is a red/green
    // ratio well above the input's, and "untouched" is bit-level equality.
    const auto tinted = [&](int x) { return px(x, 0) / px(x, 1) > (kR / kG) * 1.5; };
    const auto untouched = [&](int x) {
        return std::abs(px(x, 0) - kR) < 2e-3 && std::abs(px(x, 1) - kG) < 2e-3
            && std::abs(px(x, 2) - kB) < 2e-3;
    };
    const int xl = int(kW) / 4, xr = int(kW) * 3 / 4;

    la.maskOverlayLayer = 0;
    run();
    report(tinted(xl) && untouched(xr),
           "editing layer 0, the overlay paints its coverage and no other",
           std::to_string(px(xl, 0) / px(xl, 1)) + " left, right "
         + std::to_string(px(xr, 0)));

    la.maskOverlayLayer = 1;
    run();
    report(tinted(xr) && untouched(xl),
           "editing layer 1, the paint moves with the selection",
           std::to_string(px(xr, 0) / px(xr, 1)) + " right, left "
         + std::to_string(px(xl, 0)));

    // An index past the stack is clamped to the last layer, not read off the
    // end of the table.
    la.maskOverlayLayer = 7;
    run();
    report(tinted(xr) && untouched(xl),
           "an out-of-range selection clamps to the last layer",
           std::to_string(px(xr, 0) / px(xr, 1)));
}

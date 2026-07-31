// Hue/sat maps, colour grading, tone bands, local adjustments, lens auto-scale.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

void testHueSatMapGpu() {
    section("Camera profile: HueSatMap (GPU)");

    using namespace orion::pipe;

    // The matrices, before any pixel touches them. A composed round trip that
    // is not the identity would show up as a global tint no amount of table
    // fitting could remove.
    {
        const auto m = huesat::multiply(huesat::rec2020ToProPhoto(),
                                        huesat::proPhotoToRec2020());
        double worst = 0.0;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(double(m[r * 3 + c]) - (r == c ? 1.0 : 0.0)));
        checkNear(worst, 0.0, 1e-5, "Rec.2020 -> ProPhoto -> Rec.2020 is the identity");

        // Published matrices are rounded, so white lands on white to about a
        // part in ten thousand. Anything looser would be a transcription error.
        const auto toPro = huesat::rec2020ToProPhoto();
        for (int r = 0; r < 3; ++r) {
            const double sum = double(toPro[r * 3 + 0]) + toPro[r * 3 + 1] + toPro[r * 3 + 2];
            checkNear(sum, 1.0, 1e-3, "the working white maps to ProPhoto white");
        }
    }

    // The table's own structure, which the spec constrains.
    {
        const auto table = huesat::buildTable(huesat::blueSky());
        bool neutralClean = true, valueScaleOne = true;
        for (int h = 0; h < huesat::kHueDivisions; ++h) {
            const std::size_t i = std::size_t(h) * huesat::kSatDivisions * 4;
            if (table[i + 0] != 0.0f || table[i + 1] != 1.0f || table[i + 2] != 1.0f)
                neutralClean = false;
            for (int s = 0; s < huesat::kSatDivisions; ++s)
                if (table[i + std::size_t(s) * 4 + 2] != 1.0f) valueScaleOne = false;
        }
        report(neutralClean, "every zero-saturation entry is (0, 1, 1)");
        report(valueScaleOne, "no entry scales value — this stage moves chroma only");

        // Red is outside the window and must be untouched; blue at full
        // saturation must carry the whole fit.
        const auto entry = [&](int hueDeg, int sat) {
            const int h = (hueDeg * huesat::kHueDivisions / 360) % huesat::kHueDivisions;
            const std::size_t i = (std::size_t(h) * huesat::kSatDivisions + sat) * 4;
            return std::array<float, 3>{table[i], table[i + 1], table[i + 2]};
        };
        const auto red = entry(0, huesat::kSatDivisions - 1);
        checkNear(red[0], 0.0, 1e-6, "red is outside the corrected window");
        checkNear(red[1], 1.0, 1e-6, "red's saturation is untouched");
        const auto blue = entry(252, huesat::kSatDivisions - 1);
        checkNear(blue[0], huesat::blueSky().hueShiftDeg, 0.35,
                  "the blue centre carries the fitted rotation");
    }

    constexpr std::uint32_t kW = 64, kH = 4;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/hueSatMap.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "hueSatMap");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto tex = orion::gpu::Texture::create(*device, huesat::kSatDivisions,
                                           huesat::kHueDivisions,
                                           orion::gpu::PixelFormat::RGBA32Float);

    params::HueSat hs{};
    {
        const auto toPro   = huesat::rec2020ToProPhoto();
        const auto fromPro = huesat::proPhotoToRec2020();
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                hs.toProPhoto[r][c]   = toPro[r * 3 + c];
                hs.fromProPhoto[r][c] = fromPro[r * 3 + c];
            }
        hs.size[0] = kW; hs.size[1] = kH;
        hs.hueDivisions = huesat::kHueDivisions;
        hs.satDivisions = huesat::kSatDivisions;
    }

    // Row 0 a neutral ramp, row 1 sky blue, row 2 foliage green, row 3 skin.
    // The three colours are ordinary linear Rec.2020 values; what matters is
    // that only one of them is in the corrected hue region.
    const double colours[4][3] = {
        {0.0, 0.0, 0.0},                 // filled per column below
        {0.09, 0.13, 0.30},              // saturated blue, the sky's shape
        {0.06, 0.11, 0.03},              // foliage
        {0.34, 0.22, 0.17},              // skin
    };

    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t x = 0; x < kW; ++x) {
        const double level = 0.004 * std::pow(250.0, x / double(kW - 1));   // 0.004 .. 1
        for (std::uint32_t y = 0; y < kH; ++y) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const double v = (y == 0) ? level : colours[y][c] * level * 3.0;
                input[i + c] = static_cast<__fp16>(v);
            }
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(float) / 2);

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    const auto run = [&](const std::vector<float>& table) {
        tex->upload(table.data(), huesat::kSatDivisions * 4 * sizeof(float));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), tex.get(), dst.get()}, &hs, sizeof hs, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(float) / 2, kW, kH);
    };
    const auto pixel = [&](const std::vector<__fp16>& o, std::uint32_t x, std::uint32_t y) {
        const std::size_t i = (std::size_t(y) * kW + x) * 4;
        return std::array<double, 3>{double(o[i]), double(o[i + 1]), double(o[i + 2])};
    };

    // 1. An identity table is a no-op — through both matrices and HSV.
    {
        huesat::Correction none{};
        none.satScale = 1.0f;
        run(huesat::buildTable(none));

        double worst = 0.0;
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                const auto a = pixel(input, x, y), b = pixel(out, x, y);
                for (int c = 0; c < 3; ++c)
                    worst = std::max(worst, std::abs(a[c] - b[c]) / std::max(1e-4, a[c]));
            }
        // Half float carries about three decimal digits, and the round trip is
        // four matrix multiplies plus HSV and back.
        checkNear(worst, 0.0, 6e-3, "an identity table leaves every pixel where it was");
    }

    // 2. The fitted table, on a neutral: still neutral.
    {
        run(huesat::buildTable(huesat::blueSky()));

        double worstTint = 0.0;
        for (std::uint32_t x = 1; x < kW; ++x) {
            const auto p = pixel(out, x, 0);
            const double mean = (p[0] + p[1] + p[2]) / 3.0;
            for (int c = 0; c < 3; ++c)
                worstTint = std::max(worstTint, std::abs(p[c] - mean) / std::max(1e-4, mean));
        }
        checkNear(worstTint, 0.0, 6e-3, "a grey ramp comes through grey at every level");

        // 3. Blue moves, and the other hues do not.
        const std::uint32_t mid = kW / 2;
        const auto blueIn  = pixel(input, mid, 1), blueOut  = pixel(out, mid, 1);
        const auto leafIn  = pixel(input, mid, 2), leafOut  = pixel(out, mid, 2);
        const auto skinIn  = pixel(input, mid, 3), skinOut  = pixel(out, mid, 3);

        const auto rb = [](const std::array<double, 3>& p) { return p[0] / p[2]; };
        report(rb(blueOut) < rb(blueIn) * 0.9,
               "the correction takes red out of a saturated blue",
               std::to_string(rb(blueIn)) + " -> " + std::to_string(rb(blueOut)));

        const auto same = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
            double worst = 0.0;
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(a[c] - b[c]) / std::max(1e-4, a[c]));
            return worst;
        };
        checkNear(same(leafIn, leafOut), 0.0, 6e-3, "foliage is not in the corrected window");
        checkNear(same(skinIn, skinOut), 0.0, 6e-3, "skin is not in the corrected window");
    }
}

void testColorGradeGpu() {
    section("Colour grading (GPU)");

    constexpr std::uint32_t kW = 64, kH = 8;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/colorGrade.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "colorGrade");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // A neutral wedge across the zones the shader partitions on: scene-linear
    // luminance from deep shadow to well past the highlight knee at 1.0.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    std::vector<double> level(kW);
    for (std::uint32_t x = 0; x < kW; ++x) {
        level[x] = 0.002 * std::pow(2000.0, x / double(kW - 1));   // 0.002 .. 4
        for (std::uint32_t y = 0; y < kH; ++y) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            for (int c = 0; c < 3; ++c) input[i + c] = static_cast<__fp16>(level[x]);
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(float) / 2);

    orion::pipe::params::Grade g{};
    g.size[0] = kW; g.size[1] = kH;

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    const auto run = [&]() {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &g, sizeof g, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(float) / 2, kW, kH);
    };

    // Chroma *relative* to the pixel, because that is what the grade now
    // produces: the offset is scaled by luminance, so a wheel is a constant
    // chromaticity shift at every exposure rather than a constant additive one
    // whose authority fell as one over the level.
    const auto chromaAt = [&](const std::vector<__fp16>& o, std::uint32_t x) {
        const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
        const double r = o[i + 0], gg = o[i + 1], b = o[i + 2];
        const double mean = (r + gg + b) / 3.0;
        if (mean < 1e-6) return 0.0;
        return (std::abs(r - mean) + std::abs(gg - mean) + std::abs(b - mean)) / 3.0 / mean;
    };

    // Every wheel centred is the identity, exactly. The node is disabled in
    // that state, but a disabled node passes its input through — so if this
    // were not an identity, enabling grading would visibly jump.
    run();
    double worstIdentity = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH * 4; ++i) {
        worstIdentity = std::max(worstIdentity,
                                 std::abs(double(out[i]) - double(input[i])));
    }
    report(worstIdentity < 1e-3, "every wheel centred is the identity",
           "worst " + std::to_string(worstIdentity));

    // Columns at the zone centres, in EV relative to middle gray. The zones
    // are Gaussian bands on log2(Y/0.18) centred at -2.5 / 0 / +2.5, the same
    // partition-of-unity construction the tone controls use.
    const auto columnAtEv = [&](double ev) {
        const double want = 0.18 * std::pow(2.0, ev);
        std::uint32_t best = 0;
        double bestErr = 1e30;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double err = std::abs(std::log2(level[x] / want));
            if (err < bestErr) { bestErr = err; best = x; }
        }
        return best;
    };
    const std::uint32_t atShadow    = columnAtEv(-2.5);
    const std::uint32_t atMidtone   = columnAtEv(0.0);
    const std::uint32_t atHighlight = columnAtEv(2.5);

    // A shadow push has to tint the shadows and leave the highlights alone.
    // That is the whole claim a three-way grade makes, and the claim the linear
    // partition could not keep: at Y = 0.0/0.5/1.0 middle gray weighed 0.70
    // shadows, so the shadow wheel graded most of a normal photograph.
    orion::pipe::DevelopPipeline::gradeOffsets(-0.6f, -0.6f, g.shadow);
    run();

    report(chromaAt(out, atShadow) > 0.05, "the shadow wheel tints its own zone",
           "relative chroma " + std::to_string(chromaAt(out, atShadow)));
    report(chromaAt(out, atHighlight) < 0.1 * chromaAt(out, atShadow),
           "and leaves the highlight zone alone",
           "shadow " + std::to_string(chromaAt(out, atShadow)) + " highlight " +
           std::to_string(chromaAt(out, atHighlight)));

    // ── Scale invariance: the property the whole rewrite is about ─────────
    //
    // A wheel must be worth the same thing at every exposure. The offset used
    // to be an additive constant in unbounded scene-linear, so what it was
    // worth relative to the pixel fell as one over the level — which is why the
    // highlight wheel measured -0.0000 chroma on a lifted frame while the
    // shadow wheel was strong enough to clip channels to black.
    //
    // Same wheel, same zone weight, two levels three stops apart: the relative
    // tint has to match.
    for (float* zone : {g.shadow, g.midtone, g.highlight}) {
        zone[0] = zone[1] = zone[2] = 0.0f;
    }
    orion::pipe::DevelopPipeline::gradeOffsets(0.6f, 0.4f, g.shadow);
    orion::pipe::DevelopPipeline::gradeOffsets(0.6f, 0.4f, g.midtone);
    orion::pipe::DevelopPipeline::gradeOffsets(0.6f, 0.4f, g.highlight);
    run();
    // All three zones carry the same push, so the weights sum to one and every
    // level gets the identical correction. Any level dependence left is the
    // bug this replaced.
    const double lo = chromaAt(out, columnAtEv(-3.0));
    const double hi = chromaAt(out, columnAtEv(3.0));
    report(lo > 0.05 && std::abs(lo - hi) < 0.02 * lo + 1e-4,
           "a wheel is worth the same at every level",
           "-3 EV " + std::to_string(lo) + "  +3 EV " + std::to_string(hi));
    std::printf("  same wheel at -3 EV: %.4f relative chroma, at +3 EV: %.4f\n", lo, hi);

    // ── Zero-sum survives, including in deep shadow ──────────────────────
    //
    // A wheel is a colour control, not a brightness one, so the mean of the
    // three channels must not move. It used to, below about a fiftieth of
    // middle gray: the offset was a constant larger than the pixel, the
    // negative channels stuck at the shader's zero clamp, the sum stopped
    // cancelling and the wheel *brightened* what it was tinting — measured at
    // +29% on a 0.0096-linear patch. Scaling the offset by luminance makes that
    // unrepresentable rather than merely unlikely, so this is checked across
    // the whole wedge rather than at one convenient level.
    double worstDrift = 0.0;
    std::uint32_t worstAt = 0;
    for (std::uint32_t x = 0; x < kW; ++x) {
        const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
        const double mean = (double(out[i]) + double(out[i + 1]) +
                             double(out[i + 2])) / 3.0;
        const double drift = std::abs(mean / level[x] - 1.0);
        if (drift > worstDrift) { worstDrift = drift; worstAt = x; }
    }
    report(worstDrift < 0.02,
           "and does not change brightness anywhere on the wedge",
           "worst " + std::to_string(100.0 * worstDrift) + "% at " +
           std::to_string(level[worstAt]) + " linear");

    // The slope is the component that is *meant* to change brightness, and it
    // has to be read where its own zone has the weight.
    for (float* zone : {g.shadow, g.midtone, g.highlight}) {
        zone[0] = zone[1] = zone[2] = 0.0f;
    }
    g.shadow[3] = 0.5f;
    run();
    const std::size_t si = (std::size_t(kH / 2) * kW + atShadow) * 4;
    const double lifted = (double(out[si]) + double(out[si + 1]) +
                           double(out[si + 2])) / 3.0;
    report(lifted > level[atShadow] * 1.2,
           "the zone's luminance control does lift it",
           std::to_string(level[atShadow]) + " -> " + std::to_string(lifted));

    // And leaves the zone at the other end of the wedge alone.
    const std::size_t hi_i = (std::size_t(kH / 2) * kW + atHighlight) * 4;
    const double untouched = (double(out[hi_i]) + double(out[hi_i + 1]) +
                              double(out[hi_i + 2])) / 3.0;
    report(untouched < level[atHighlight] * 1.03,
           "and leaves the far zone's brightness alone",
           std::to_string(level[atHighlight]) + " -> " + std::to_string(untouched));
    g.shadow[3] = 0.0f;
}

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

    // A tinted mid-grey everywhere, so saturation and a colour cast both have
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
    // coverage is exactly 2^((ev + 2.5) * k) times the input. The tinted grey
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
               "and does it at the pixel's own luminance, not at grey",
               std::to_string(luma(x)) + " against " + std::to_string(baseLuma));
        la.layerSaturation[0] = 0.0f;
    }

    // ── ⚠ The colour cast moves colour and not exposure ───────────────────
    //
    // The channels are renormalised on luminance afterwards, or Warmth would
    // double as a brightness slider and the two controls would fight over the
    // same pixels. The first version of the shader read the luminance on both
    // sides of the cast from the same already-cast colour, so the ratio was one
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


/// The zoom that keeps a lens correction inside the frame.
///
/// Host maths, so it is checked exhaustively rather than at the four points a
/// GPU test can afford to read.
void testLensAutoScale() {
    section("Lens autoscale");

    using orion::pipe::lens::autoScale;

    // A real frame, and the shipped coefficient range: the sliders run -1..1
    // into k₁ = ±0.35 and ca = ±0.003.
    constexpr std::uint32_t kW = 6024, kH = 4024;

    checkNear(autoScale(kW, kH, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f), 1.0, 0.0,
              "no correction is exactly no zoom");
    checkNear(autoScale(kW, kH, 0.5f, 0.5f, 0.0f, 0.35f, 0.0f, 0.0f, 0.0f), 1.0, 0.0,
              "pincushion pulls inward, so it needs no zoom");
    checkNear(autoScale(kW, kH, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.003f, 0.003f), 1.0, 0.0,
              "a fringe correction that shrinks red and blue needs no zoom");

    const float barrel = autoScale(kW, kH, 0.5f, 0.5f, 0.0f, -0.35f, 0.0f, 0.0f, 0.0f);
    report(barrel < 1.0f, "full barrel correction needs a zoom",
           "scale " + std::to_string(barrel));

    // The invariant, recomputed here rather than trusted: at the returned
    // scale, no pixel on the frame's perimeter fetches outside it. Written out
    // independently of the implementation, because a bug shared between the
    // function and its test is not caught by either.
    const auto worstOverreach = [&](float k1, float caR, float caB, float s) {
        const double w = kW, h = kH;
        const double cx = 0.5 * w, cy = 0.5 * h;
        const double rNorm = 0.5 * std::sqrt(w * w + h * h);
        double worst = 0.0;
        const auto probeAt = [&](double dx, double dy) {
            const double px = dx * s, py = dy * s;
            const double ru = std::sqrt(px * px + py * py) / rNorm;
            const double r2 = ru * ru;
            const double m = ((1.0 - k1) + k1 * r2) *
                std::max({1.0, 1.0 - caR * r2, 1.0 - caB * r2});
            const double fx = cx + px * m - 0.5, fy = cy + py * m - 0.5;
            worst = std::max({worst, -fx, fx - (w - 1.0), -fy, fy - (h - 1.0)});
        };
        // Denser than the implementation walks, so a coarse perimeter would
        // show up as an overreach here.
        for (int i = 0; i <= 977; ++i) {
            const double t = double(i) / 977.0;
            const double x = 0.5 + t * (w - 1.0);
            const double y = 0.5 + t * (h - 1.0);
            probeAt(x - cx, 0.5 - cy);
            probeAt(x - cx, h - 0.5 - cy);
            probeAt(0.5 - cx, y - cy);
            probeAt(w - 0.5 - cx, y - cy);
        }
        return worst;
    };

    report(worstOverreach(-0.35f, 0.0f, 0.0f, barrel) < 0.01,
           "at the returned scale nothing fetches outside the frame",
           "overreach " + std::to_string(worstOverreach(-0.35f, 0.0f, 0.0f, barrel)));

    // Uncorrected must land exactly on the last texel, not a hair past it —
    // that half texel is the difference between "no zoom" and 0.9999.
    report(worstOverreach(0.0f, 0.0f, 0.0f, 1.0f) < 1e-6,
           "an uncorrected frame reaches exactly its own edge");

    // Every step of the slider, both signs, with fringe at its extremes.
    // A sign this function got wrong anywhere would leave a band on screen.
    int violations = 0, needless = 0;
    for (int i = -100; i <= 100; ++i) {
        const float k1 = i * 0.0035f;
        for (const float ca : {-0.003f, 0.0f, 0.003f}) {
            const float s = autoScale(kW, kH, 0.5f, 0.5f, 0.0f, k1, 0.0f, ca, -ca);
            if (worstOverreach(k1, ca, -ca, s) > 0.01) ++violations;
            // And not slack: a scale that could be larger is frame thrown away.
            if (s < 0.9999f && worstOverreach(k1, ca, -ca, s * 1.002f) < 0.01) ++needless;
        }
    }
    report(violations == 0, "no coefficient in range leaves the frame",
           std::to_string(violations) + " of 603");
    report(needless == 0, "and none zooms further than it has to",
           std::to_string(needless) + " of 603");

    // An off-center optical axis reaches further on one side. Real lens
    // profiles carry one, so the maths must not assume the middle.
    const float offset = autoScale(kW, kH, 0.44f, 0.53f, 0.0f, -0.35f, 0.0f, 0.0f, 0.0f);
    report(offset < barrel, "an off-center axis needs more zoom, not the same",
           "centered " + std::to_string(barrel) + " off-center " +
           std::to_string(offset));

    // A portrait frame is the same maths with the axes swapped; it has been
    // the thing that was wrong before.
    const float portrait = autoScale(kH, kW, 0.5f, 0.5f, 0.0f, -0.35f, 0.0f, 0.0f, 0.0f);
    checkNear(portrait, barrel, 1e-4, "portrait and landscape zoom the same");
}

/// The graph's output must actually carry more than eight bits.
///
/// Changing a texture format is the kind of edit that looks done the moment it
/// compiles. This renders a gradient fine enough that eight bits could not
/// represent it and checks the steps survive — if the format quietly fell back,
/// adjacent outputs would land on the same value.

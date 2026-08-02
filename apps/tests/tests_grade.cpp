// Hue/sat maps, color grading, lens auto-scale.
//
// Split out of main.cpp 2026-07-31; see harness.h. The tone bands and the local
// adjustments went to tests_linear.cpp on 2026-08-02 (decision #129): they are
// one `developLinear` fixture between them, and nothing here shares it.

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
                  "the blue center carries the fitted rotation");
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
    // The three colors are ordinary linear Rec.2020 values; what matters is
    // that only one of them is in the corrected hue region.
    const double colors[4][3] = {
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
                const double v = (y == 0) ? level : colors[y][c] * level * 3.0;
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
        checkNear(worstTint, 0.0, 6e-3, "a gray ramp comes through gray at every level");

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
    section("Color grading (GPU)");

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

    // Every wheel centerd is the identity, exactly. The node is disabled in
    // that state, but a disabled node passes its input through — so if this
    // were not an identity, enabling grading would visibly jump.
    run();
    double worstIdentity = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH * 4; ++i) {
        worstIdentity = std::max(worstIdentity,
                                 std::abs(double(out[i]) - double(input[i])));
    }
    report(worstIdentity < 1e-3, "every wheel centerd is the identity",
           "worst " + std::to_string(worstIdentity));

    // Columns at the zone centers, in EV relative to middle gray. The zones
    // are Gaussian bands on log2(Y/0.18) centerd at -2.5 / 0 / +2.5, the same
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
    // A wheel is a color control, not a brightness one, so the mean of the
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

    // ── Balance: where the split between the three zones sits (#101) ──────
    //
    // Every assertion above ran with `g.balance` at its zero-initialized zero,
    // so they are themselves the regression on Balance being centred. What
    // follows pins the thing they cannot see: that centred means *exactly* the
    // old -2.5 / 0 / +2.5, and that off-centre moves the zones the right way,
    // by the right amount, without ever reordering them.
    //
    // The weights are read straight out of the render rather than inferred.
    // With every offset zero and one zone's slope at 1, the kernel computes
    // `out = in * (1 + w)` for that zone alone — so `out / in - 1` is the
    // weight the shader actually used, not a proxy for it. `in` is read back
    // from the uploaded half-float rather than from `level`, because the two
    // differ by up to a part in two thousand and that is the size of the
    // effect being measured.
    const auto zoneWeights = [&](int zone, float balance) {
        for (float* z : {g.shadow, g.midtone, g.highlight}) {
            z[0] = z[1] = z[2] = 0.0f;
            z[3] = 0.0f;
        }
        (zone == 0 ? g.shadow : zone == 1 ? g.midtone : g.highlight)[3] = 1.0f;
        g.balance = balance;
        run();
        std::vector<double> w(kW);
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
            w[x] = double(out[i]) / double(input[i]) - 1.0;
        }
        return w;
    };
    const auto evOf = [&](std::uint32_t x) {
        const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
        return std::log2(double(input[i]) / 0.18);
    };

    // The partition this file documents, on the host, with the centres written
    // out. A shift of `-balance * 1.25` EV, positive toward the highlights.
    const auto predicted = [](double ev, double balance) {
        const double shift = -balance * 1.25;
        const double center[3] = {-2.5 + shift, shift, 2.5 + shift};
        double raw[3], total = 1e-6;
        for (int i = 0; i < 3; ++i) {
            const double d = (ev - center[i]) / 1.6;
            raw[i] = std::exp(-0.5 * d * d);
            total += raw[i];
        }
        return std::array<double, 3>{raw[0] / total, raw[1] / total,
                                     raw[2] / total};
    };

    // ⚠ **Balance centred is the zones this shader has always had.** This is
    // the check that stops a new control quietly rebasing every baseline in the
    // repository: if the centres moved by a twentieth of a stop, or if zero on
    // the slider stopped meaning zero shift, the bench pins and the repro
    // expectations would all still be *green* and all be measuring a different
    // picture. Nudging `kEvShadow` by 0.05, or `kBalanceEv`'s neutral point,
    // fails here and nowhere else.
    {
        double worst = 0.0;
        for (int zone = 0; zone < 3; ++zone) {
            const auto w = zoneWeights(zone, 0.0f);
            for (std::uint32_t x = 0; x < kW; ++x)
                worst = std::max(worst, std::abs(w[x] - predicted(evOf(x), 0.0)[zone]));
        }
        report(worst < 3e-3,
               "Balance centred is the old fixed -2.5 / 0 / +2.5 EV partition",
               "worst weight error " + std::to_string(worst));
    }

    // And off-centre is the same partition, translated. Checked against the
    // analytic form rather than against "it moved", because "it moved" is
    // satisfied by a control that moves the zones by any amount in any pattern.
    {
        double worst = 0.0;
        for (float b : {-1.0f, -0.5f, 0.5f, 1.0f})
            for (int zone = 0; zone < 3; ++zone) {
                const auto w = zoneWeights(zone, b);
                for (std::uint32_t x = 0; x < kW; ++x)
                    worst = std::max(worst,
                                     std::abs(w[x] - predicted(evOf(x), b)[zone]));
            }
        report(worst < 3e-3, "and off-centre is that partition translated",
               "worst weight error " + std::to_string(worst));
    }

    // The shadow zone's half-weight point — the EV where the shadow wheel stops
    // owning the majority of a pixel, which is the "split point" the control is
    // named for. It has to slide monotonically and by the documented distance.
    const auto halfPointEv = [&](const std::vector<double>& w) {
        for (std::uint32_t x = 1; x < kW; ++x)
            if (w[x] <= 0.5 && w[x - 1] > 0.5) {
                const double t = (w[x - 1] - 0.5) / (w[x - 1] - w[x]);
                return evOf(x - 1) + t * (evOf(x) - evOf(x - 1));
            }
        return 1e30;   // never crossed — the wedge no longer spans the zones
    };
    {
        const float b[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
        double split[5];
        for (int i = 0; i < 5; ++i) split[i] = halfPointEv(zoneWeights(0, b[i]));

        bool monotone = true;
        for (int i = 1; i < 5; ++i)
            if (!(split[i] < split[i - 1] - 0.4)) monotone = false;
        report(monotone,
               "the split point slides monotonically, shadows to highlights",
               "-1 " + std::to_string(split[0]) + "  0 " + std::to_string(split[2])
               + "  +1 " + std::to_string(split[4]) + " EV");

        // Full travel is 2 x kBalanceEv. A control whose slider ran to a
        // different EV than its comment claims fails here.
        report(std::abs((split[0] - split[4]) - 2.5) < 0.15,
               "and full travel is 2.5 EV, the zone spacing",
               std::to_string(split[0] - split[4]) + " EV end to end");
        std::printf("  split point: %.2f EV at Balance -1, %.2f at 0, %.2f at +1\n",
                    split[0], split[2], split[4]);
    }

    // The zones stay ordered and stay apart at every setting.
    //
    // ⚠ Measured as the two **crossovers** — where shadow and midtone carry a
    // pixel equally, and where midtone and highlight do — and not as where each
    // zone's weight peaks. The weights are normalized, so the shadow weight
    // falls monotonically across the whole wedge and the highlight weight
    // rises: their maxima are at the ends of the test image no matter where the
    // centres are, and a check on them would be green for every possible
    // Balance. It was written that way first.
    //
    // The crossovers are the ordering. A rigid shift keeps them exactly the
    // zone spacing apart wherever it puts them; a Balance that re-spaced the
    // centres instead — `kEvShadow + shift, kEvMidtone, kEvHighlight - shift` —
    // squeezes them to 1.25 EV at full deflection and, with a larger constant,
    // to zero, which is two wheels fighting over one zone.
    {
        const auto crossoverEv = [&](const std::vector<double>& lo,
                                     const std::vector<double>& hi) {
            for (std::uint32_t x = 1; x < kW; ++x)
                if (lo[x] <= hi[x] && lo[x - 1] > hi[x - 1]) {
                    const double a = lo[x - 1] - hi[x - 1], b = lo[x] - hi[x];
                    const double t = a / (a - b);
                    return evOf(x - 1) + t * (evOf(x) - evOf(x - 1));
                }
            return 1e30;
        };
        bool ordered = true;
        double worstGap = 1e30;
        for (float b : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            const auto ws = zoneWeights(0, b);
            const auto wm = zoneWeights(1, b);
            const auto wh = zoneWeights(2, b);
            const double sm = crossoverEv(ws, wm);
            const double mh = crossoverEv(wm, wh);
            if (!(sm < mh)) ordered = false;
            worstGap = std::min(worstGap, mh - sm);
        }
        report(ordered && std::abs(worstGap - 2.5) < 0.15,
               "and the two crossovers stay ordered and one zone apart",
               "closest " + std::to_string(worstGap) + " EV");
    }

    // ⚠ **Balance costs nothing when the wheels are centred.** Not "nearly
    // nothing" — the render is bit-for-bit the input, because every offset is
    // zero and every slope is one whatever the weights are. The host relies on
    // exactly this to refuse to switch the node on for a Balance drag, so a
    // Balance that touched luminance would make that refusal a silent bug
    // rather than an optimisation.
    {
        for (float* z : {g.shadow, g.midtone, g.highlight}) {
            z[0] = z[1] = z[2] = 0.0f;
            z[3] = 0.0f;
        }
        double worst = 0.0;
        for (float b : {-1.0f, 1.0f}) {
            g.balance = b;
            run();
            for (std::size_t i = 0; i < std::size_t(kW) * kH * 4; ++i)
                worst = std::max(worst,
                                 std::abs(double(out[i]) - double(input[i])));
        }
        report(worst == 0.0,
               "with the wheels centred, any Balance is the exact identity",
               "worst " + std::to_string(worst));
    }
    g.balance = 0.0f;
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

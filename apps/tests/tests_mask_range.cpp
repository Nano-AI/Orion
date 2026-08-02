// Masks that measure the pixels: the luminance band and the color band.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

void testMaskRangeGpu() {
    section("Range masks (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/maskComponent.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "maskComponent");

    auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    // The stroke lives in a texture now rather than in the parameter block —
    // see mask_component.slang. One texel per dab, row-major.
    auto dabTex = orion::gpu::Texture::create(*device, orion::pipe::params::kDabStride,
                                              orion::pipe::params::kDabRows,
                                              PixelFormat::RGBA32Float);
    // One box per run of 64 dabs — research/brush-acceleration.md. Bound
    // whatever the kind, like the matte beside it.
    auto dabBoundsTex = orion::gpu::Texture::create(
        *device, orion::pipe::params::kMaxDabBlocks, 1, PixelFormat::RGBA32Float);
    // ⚠ Through the product's own `buildDabBounds`, never a copy of it. A box
    // computed a second way here would be a stand-in with its own bugs, and the
    // one thing this must not do is check the kernel against a private
    // reimplementation of the thing the kernel is being checked for.
    const auto uploadDabBounds = [&](const std::vector<float>& texels) {
        std::vector<float> bounds(
            std::size_t(orion::pipe::params::kMaxDabBlocks) * 4, 0.0f);
        orion::pipe::params::buildDabBounds(
            texels.data(), orion::pipe::params::kMaxDabs, bounds.data());
        dabBoundsTex->upload(
            bounds.data(),
            std::size_t(orion::pipe::params::kMaxDabBlocks) * 4 * sizeof(float));
    };
    const auto setDabs = [&](const std::vector<std::pair<float, float>>& pts) {
        std::vector<float> texels(std::size_t(orion::pipe::params::kDabStride)
                                  * orion::pipe::params::kDabRows * 4, 0.0f);
        for (std::size_t i = 0; i < pts.size(); ++i) {
            texels[i * 4 + 0] = pts[i].first;
            texels[i * 4 + 1] = pts[i].second;
        }
        dabTex->upload(texels.data(),
                       std::size_t(orion::pipe::params::kDabStride) * 4 * sizeof(float));
        uploadDabBounds(texels);
    };
    std::vector<std::pair<float, float>> dabList;
    const auto setDabsAt = [&](int i, float x, float y) {
        if (int(dabList.size()) <= i) dabList.resize(std::size_t(i) + 1, {0.0f, 0.0f});
        dabList[std::size_t(i)] = {x, y};
        setDabs(dabList);
    };

    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));

    // A neutral ramp across x, one stop per column band: column x carries
    // luminance 2^(x/8 - 4), so the frame spans -4 to +4 stops.
    const auto evAt = [&](int x) { return double(x) / 8.0 - 4.0; };
    {
        std::vector<__fp16> ref(std::size_t(kW) * kH * 4, __fp16(0.0f));
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                // Neutral, so Rec.2020's coefficients sum to one and the
                // luminance is exactly the channel value — the test is about
                // the band, not about the luma weights.
                const float v = float(std::exp2(evAt(int(x))));
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                ref[i + 0] = __fp16(v); ref[i + 1] = __fp16(v);
                ref[i + 2] = __fp16(v); ref[i + 3] = __fp16(1.0f);
            }
        }
        reference->upload(ref.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    }

    const auto run = [&](const params::MaskComponent& m) {
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dabTex.get(), dabBoundsTex.get(), dst.get()},
                    &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x) {
        return double(a[std::size_t(kH / 2) * kW + std::size_t(x)]);
    };

    params::MaskComponent base{};
    base.size[0] = kW; base.size[1] = kH;
    base.kind = 5;
    base.rangeLo = -2.0f; base.rangeHi = 1.0f; base.rangeSoft = 0.5f;

    // ── The band sits where the stops say, not where the linear values do ──
    //
    // ⚠ This is the assertion that separates a log band from a linear one, and
    // the two are not subtly different — they are different by orders of
    // magnitude. The band -2..+1 stops is luminance 0.25..2.0; its midpoint in
    // *stops* is -0.5 (luminance 0.707), while the midpoint of the same
    // interval in *linear* light is 1.125, which is +0.17 stops. A linear
    // implementation puts the plateau's center 0.67 stops to the right of where
    // this checks it.
    {
        const auto got = run(base);

        // Full inside, well clear of both ramps.
        const int inside = int(std::lround((-0.5 + 4.0) * 8.0));
        report(at(got, inside) > 0.99, "the band is full in its middle",
               std::to_string(at(got, inside)));

        // Zero outside, well clear of both ramps.
        const int below = int(std::lround((-3.5 + 4.0) * 8.0));
        const int above = int(std::lround((2.5 + 4.0) * 8.0));
        report(at(got, below) < 0.01 && at(got, above) < 0.01,
               "and zero outside it at both ends",
               std::to_string(at(got, below)) + ", " + std::to_string(at(got, above)));

        // The edges are where the stops put them: half coverage half a ramp in.
        const int loHalf = int(std::lround((-2.0 + 0.25 + 4.0) * 8.0));
        const int hiHalf = int(std::lround((1.0 - 0.25 + 4.0) * 8.0));
        report(std::abs(at(got, loHalf) - 0.5) < 0.06 &&
               std::abs(at(got, hiHalf) - 0.5) < 0.06,
               "with each edge at half coverage half a ramp inside it",
               std::to_string(at(got, loHalf)) + ", " + std::to_string(at(got, hiHalf)));

        // ⚠ A band in linear light would have its plateau centerd here instead.
        // Asserting the difference rather than trusting the comment.
        const int linearMid = int(std::lround((std::log2(1.125) + 4.0) * 8.0));
        report(std::abs(linearMid - inside) > 4,
               "and the log midpoint is nowhere near the linear one",
               "stops " + std::to_string(inside) + " vs linear "
               + std::to_string(linearMid));
    }

    // ── The two edges are independent ─────────────────────────────────────
    //
    // Pushing one past the frame's range turns the band into a one-sided
    // selection, which is how "the highlights" and "the shadows" are expressed
    // without a second control.
    {
        auto highs = base;
        highs.rangeLo = 0.0f; highs.rangeHi = 99.0f;
        const auto got = run(highs);
        report(at(got, kW - 1) > 0.99 && at(got, 0) < 0.01,
               "a high edge past the frame's range makes it a highlight selection",
               std::to_string(at(got, 0)) + " -> " + std::to_string(at(got, kW - 1)));

        auto lows = base;
        lows.rangeLo = -99.0f; lows.rangeHi = 0.0f;
        const auto shadow = run(lows);
        report(at(shadow, 0) > 0.99 && at(shadow, kW - 1) < 0.01,
               "and a low edge past it makes a shadow selection",
               std::to_string(at(shadow, 0)) + " -> " + std::to_string(at(shadow, kW - 1)));
    }

    // ── Monotone, and smooth enough to be C² ──────────────────────────────
    //
    // The falloff is shared with every other mask, so what is checked here is
    // that the range branch actually uses it: a linear ramp would be monotone
    // too, but its second difference jumps at the ends of the ramp instead of
    // going to zero.
    {
        auto lows = base;
        lows.rangeLo = -99.0f; lows.rangeHi = 0.0f; lows.rangeSoft = 2.0f;
        const auto got = run(lows);

        bool monotone = true;
        for (std::uint32_t x = 1; x < kW; ++x) {
            if (at(got, int(x)) > at(got, int(x - 1)) + 1e-3) { monotone = false; break; }
        }
        report(monotone, "a single edge is monotone across the whole frame");

        // ⚠ At the *foot* of its own ramp smootherstep is flat to second
        // order; a straight line is not. The first version of this check
        // computed the ramp's position from a constant instead of from the
        // band actually being run — with rangeLo at -99 there is no ramp within
        // sixty columns of where it sampled, so it measured a flat plateau and
        // passed for any falloff whatever. A linear-ramp mutation survived it.
        //
        // The edge here is `fall = smootherstep((rangeHi - ev) / soft)` with
        // rangeHi = 0 and soft = 2, so it is full at -2 stops and zero at 0 —
        // the ramp spans [-2, 0] and its foot is at 0, not at +2. Getting that
        // wrong is what let the linear-falloff mutation through the first time.
        const int rampFoot = int(std::lround((0.0 + 4.0) * 8.0));
        const double drop = at(got, rampFoot - 2) - at(got, rampFoot);
        report(drop < 0.02 && drop >= 0,
               "and it approaches the foot of its ramp flat, which a straight "
               "line would not",
               std::to_string(drop));

        // The same at the ramp's midpoint, where smootherstep is steepest and a
        // straight line is not — the pair together is what pins the shape
        // rather than merely the endpoints.
        const int rampMid = int(std::lround((-1.0 + 4.0) * 8.0));
        const double steep = at(got, rampMid - 2) - at(got, rampMid + 2);
        report(steep > 0.25, "and steepest in the middle of it",
               std::to_string(steep));
    }

    // ── A band narrow enough for its two ramps to overlap ─────────────────
    //
    // ⚠ Added because a mutation survived. With a wide band the two edges never
    // both sit part-way, so `rise * fall` and `rise + fall - 1` agree
    // everywhere and the product could be replaced by the sum unnoticed. They
    // differ only where both edges are partial — a thin luminance slice, which
    // is exactly what someone reaches for to isolate a narrow tone.
    {
        auto narrow = base;
        narrow.rangeLo = -0.5f; narrow.rangeHi = 0.5f; narrow.rangeSoft = 1.0f;
        const auto got = run(narrow);

        // At the band's center each edge is exactly half its ramp in, so
        // smootherstep gives 0.5 apiece: the product is 0.25 and the
        // sum-minus-one is 0.
        const int center = int(std::lround((0.0 + 4.0) * 8.0));
        const double peak = at(got, center);
        report(peak > 0.15 && peak < 0.4,
               "overlapping edges multiply rather than add, so a narrow band "
               "is partial everywhere instead of empty",
               std::to_string(peak));
    }

    // ── Invert and compose reach kind 5 too ───────────────────────────────
    {
        auto inv = base;
        inv.invert = 1;
        const auto plain = run(base);
        const auto flipped = run(inv);
        double worst = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            worst = std::max(worst, std::abs((1.0 - at(plain, int(x)))
                                             - at(flipped, int(x))));
        }
        report(worst < 2e-3, "invert reaches a range component",
               "worst " + std::to_string(worst));
    }
}

// A color range mask — research/masking.md §4c.
//
// A band on chromaticity rather than on brightness. Checked against an
// independent CPU model of the metric rather than against magnitudes, for the
// same reason the guided filter is: a shader that is wrong by a factor still
// moves the picture, and "it did something" is what let a blacks slider ship
// delivering 39% of its effect.
//
// ⚠ The load-bearing property is not the falloff, which is shared with every
// other kind. It is that the metric is **exactly invariant under exposure**,
// because that is the whole reason lightness is excluded and the reason this
// works at all on a scene-linear, unbounded input.
void testMaskColorGpu() {
    section("Color range masks (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/maskComponent.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "maskComponent");

    auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);
    auto dabTex = orion::gpu::Texture::create(*device, params::kDabStride,
                                              params::kDabRows, PixelFormat::RGBA32Float);
    // One box per run of 64 dabs — research/brush-acceleration.md.
    auto dabBoundsTex = orion::gpu::Texture::create(
        *device, params::kMaxDabBlocks, 1, PixelFormat::RGBA32Float);
    // ⚠ Through the product's own `buildDabBounds`, never a copy of it.
    const auto uploadDabBounds = [&](const std::vector<float>& texels) {
        std::vector<float> bounds(std::size_t(params::kMaxDabBlocks) * 4, 0.0f);
        params::buildDabBounds(texels.data(), params::kMaxDabs, bounds.data());
        dabBoundsTex->upload(bounds.data(),
                             std::size_t(params::kMaxDabBlocks) * 4 * sizeof(float));
    };
    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));

    // ── The metric, on the CPU ────────────────────────────────────────────
    //
    // The same numbers the shader carries, written out independently. This is
    // the oracle: asserting the GPU against a magnitude would pass on a shader
    // that had, say, dropped the division by L — which is precisely the term
    // the whole design rests on.
    const auto chroma = [](double r, double g, double b) {
        r = std::max(r, 0.0); g = std::max(g, 0.0); b = std::max(b, 0.0);
        const double l = 0.6166884417 * r + 0.3601590705 * g + 0.0230433072 * b;
        const double m = 0.2651401962 * r + 0.6358564847 * g + 0.0990302685 * b;
        const double s = 0.1001506451 * r + 0.2040043234 * g + 0.6963246874 * b;
        const double l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
        const double L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_;
        const double A = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_;
        const double B = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_;
        const double d = std::max(L, 0.1);
        return std::pair<double, double>{A / d, B / d};
    };
    const auto distance = [&](std::array<double, 3> p, std::array<double, 3> q) {
        const auto a = chroma(p[0], p[1], p[2]);
        const auto b = chroma(q[0], q[1], q[2]);
        return std::hypot(a.first - b.first, a.second - b.second);
    };

    // Six ordinary photographic colors, one per column band, and four
    // exposures down the rows — 1/4, 1, 4 and 64 stops apart in brightness.
    // The exposures are what make the invariance checkable at all.
    const std::array<std::array<double, 3>, 6> colors{{
        {0.05, 0.09, 0.22},   // blue sky
        {0.55, 0.42, 0.03},   // yellow car
        {0.09, 0.09, 0.10},   // gray tarmac
        {0.35, 0.03, 0.02},   // red
        {0.06, 0.12, 0.03},   // foliage
        {0.40, 0.24, 0.18},   // skin
    }};
    const std::array<double, 4> exposures{0.25, 1.0, 4.0, 64.0};

    const auto bandFor = [&](int x) { return std::min(5, x * 6 / int(kW)); };
    const auto rowFor   = [&](int y) { return std::min(3, y * 4 / int(kH)); };

    // ⚠ A lambda rather than a one-off, because the floor check below replaces
    // the reference with deep-shadow pixels and every later case reads it. The
    // first version did not put it back, and the *invert* check — the last one
    // in the function — then ran against a frame of near-black and passed for
    // the wrong reason. A shared fixture that one case mutates is a fixture
    // every later case is quietly testing something else against.
    const auto uploadColors = [&]() {
        std::vector<__fp16> ref(std::size_t(kW) * kH * 4, __fp16(0.0f));
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const auto& c = colors[std::size_t(bandFor(int(x)))];
                const double k = exposures[std::size_t(rowFor(int(y)))];
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                ref[i + 0] = __fp16(c[0] * k);
                ref[i + 1] = __fp16(c[1] * k);
                ref[i + 2] = __fp16(c[2] * k);
                ref[i + 3] = __fp16(1.0f);
            }
        }
        reference->upload(ref.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    };
    uploadColors();

    const auto run = [&](const params::MaskComponent& m) {
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dabTex.get(), dabBoundsTex.get(), dst.get()},
                    &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return double(a[std::size_t(y) * kW + std::size_t(x)]);
    };
    // The center of band `b` at exposure row `r`.
    const auto sampleX = [&](int b) { return int((b * 2 + 1) * int(kW) / 12); };
    const auto sampleY = [&](int r) { return int((r * 2 + 1) * int(kH) / 8); };

    params::MaskComponent base{};
    base.size[0] = kW; base.size[1] = kH;
    base.kind = 6;
    base.colorSoft = 0.02f;

    const auto target = [&](params::MaskComponent& m, std::array<double, 3> c) {
        m.colorR = float(c[0]); m.colorG = float(c[1]); m.colorB = float(c[2]);
    };

    // ── ⚠ Exposure invariance, which is the whole design ──────────────────
    //
    // The same shade at 1/4, 1, 4 and 64 times the light must give *identical*
    // coverage. This is not a tolerance being generous — Oklab's nonlinearity
    // is a pure cube root, so a/L and b/L are algebraically invariant under a
    // multiply, and the only error here is half-float storage.
    //
    // A version measuring a and b themselves, or CIELAB's L*a*b* against any
    // reference white, fails this at every row but one. So does anything that
    // sneaks lightness into the distance.
    {
        params::MaskComponent m = base;
        target(m, colors[0]);            // the blue sky, at ×1
        m.colorTol = 0.05f;
        const auto got = run(m);

        double lo = 1.0, hi = 0.0;
        for (int r = 0; r < 4; ++r) {
            const double v = at(got, sampleX(0), sampleY(r));
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        report(lo > 0.999 && hi - lo < 2e-3,
               "the same color selects identically across 8 stops of exposure",
               "min " + std::to_string(lo) + ", spread " + std::to_string(hi - lo));

        // And the target was picked at one exposure while the pixels sit at
        // four, so this also says the *target* conversion is invariant.
        params::MaskComponent bright = m;
        target(bright, {colors[0][0] * 64, colors[0][1] * 64, colors[0][2] * 64});
        const auto got2 = run(bright);
        double worst = 0.0;
        for (int r = 0; r < 4; ++r) {
            worst = std::max(worst, std::abs(at(got, sampleX(0), sampleY(r))
                                           - at(got2, sampleX(0), sampleY(r))));
        }
        report(worst < 2e-3,
               "and picking that color off a brighter pixel gives the same mask",
               std::to_string(worst));
    }

    // ── The tolerance is a radius in the metric, checked against the model ──
    //
    // Every band, against the CPU distance: inside `tol` it is full, beyond
    // `tol + soft` it is nothing, and the classification is the model's rather
    // than a hand-written list of which colors are near the sky.
    {
        params::MaskComponent m = base;
        target(m, colors[0]);
        m.colorTol = 0.25f; m.colorSoft = 0.02f;
        const auto got = run(m);

        int agreed = 0, tested = 0;
        for (int b = 0; b < 6; ++b) {
            const double d = distance(colors[std::size_t(b)], colors[0]);
            const double v = at(got, sampleX(b), sampleY(1));
            if (d < m.colorTol - m.colorSoft) { ++tested; agreed += (v > 0.999); }
            else if (d > m.colorTol + m.colorSoft * 2) { ++tested; agreed += (v < 1e-3); }
        }
        report(tested >= 5 && agreed == tested,
               "each color is inside or outside the radius as the model says",
               std::to_string(agreed) + " of " + std::to_string(tested));

        // The discriminating pair: tarmac and skin are the closest two at
        // 0.126, so a tolerance between them separates colors a coarser check
        // could not tell apart.
        params::MaskComponent fine = base;
        target(fine, colors[2]);          // tarmac
        fine.colorTol = 0.08f; fine.colorSoft = 0.01f;
        const auto tight = run(fine);
        report(at(tight, sampleX(2), sampleY(1)) > 0.999 &&
               at(tight, sampleX(5), sampleY(1)) < 1e-3,
               "and the closest pair in the frame is still separable",
               std::to_string(at(tight, sampleX(5), sampleY(1))));
    }

    // ── The ramp is the shared smootherstep, and it is one-sided ──────────
    //
    // A color band is a disc around the target, not an interval, so there is
    // no far edge to open. Half coverage lands exactly at `tol + soft/2`.
    {
        params::MaskComponent m = base;
        target(m, colors[0]);
        m.colorSoft = 0.30f;

        // Pick the tolerance so the sky-to-foliage distance sits mid-ramp.
        const double d = distance(colors[4], colors[0]);
        m.colorTol = float(d - m.colorSoft * 0.5);
        const auto got = run(m);
        report(std::abs(at(got, sampleX(4), sampleY(2)) - 0.5) < 0.02,
               "half coverage lands where the model puts the ramp's midpoint",
               std::to_string(at(got, sampleX(4), sampleY(2))));
    }

    // ── Every neutral is one color, at every brightness ──────────────────
    //
    // Neutrals collapse to the origin because a = b = 0 for them, so a gray
    // target selects gray however light or dark. The residual is asserted as a
    // bound rather than assumed away: the composed matrix's rows sum to
    // 0.99989, 1.00003 and 1.00048 rather than exactly one, because Ottosson's
    // published M1 is fitted and rounded, so a neutral lands at about 1.2e-4
    // from the origin instead of on it. research/masking.md §4c.
    {
        const auto n = chroma(0.5, 0.5, 0.5);
        const double residual = std::hypot(n.first, n.second);
        report(residual < 1e-3,
               "a neutral lands on the origin to within the matrix's own rounding",
               std::to_string(residual));

        params::MaskComponent m = base;
        target(m, {0.18, 0.18, 0.18});
        m.colorTol = 0.02f; m.colorSoft = 0.005f;
        const auto got = run(m);
        // Tarmac is very nearly neutral; the saturated bands are not.
        report(at(got, sampleX(2), sampleY(0)) > 0.5 &&
               at(got, sampleX(2), sampleY(3)) > 0.5,
               "a gray target selects a near-neutral at both ends of the exposure range",
               std::to_string(at(got, sampleX(2), sampleY(0))) + ", " +
               std::to_string(at(got, sampleX(2), sampleY(3))));
        report(at(got, sampleX(3), sampleY(1)) < 1e-3,
               "and does not reach a saturated red",
               std::to_string(at(got, sampleX(3), sampleY(1))));
    }

    // ── ⚠ The floor on L, without which the shadows fill with noise ───────
    //
    // The metric divides by L, so as a pixel goes to black the ratio goes to
    // infinity and two nearly-black pixels a code apart in one channel land
    // arbitrarily far apart. The floor at L = 0.1 — a linear luminance of 1e-3,
    // about seven and a half stops below middle gray — pulls everything below
    // it toward the origin instead.
    //
    // Checked as the property that matters: two deep-shadow pixels of very
    // different hue must not be flung to opposite ends of the metric, or a
    // color mask speckles through every shadow in the frame.
    {
        // The two hues, at a level where the metric is still scale free.
        const double open = distance({0.4, 0.1, 0.1}, {0.1, 0.1, 0.4});

        // ⚠ The first version of this check asserted the wrong magnitude and
        // failed against a correct shader. It put the deep pixels at 2e-4,
        // where L is 0.09 — *barely* under the floor — and then demanded a
        // fourfold suppression the floor cannot deliver there. The floor's
        // effect is not a step, it is the ratio L/0.1, so how far under it you
        // go is the whole question. Fifth time in this file's history that a
        // first-draft assertion measured something other than its claim.
        const double deep = 1e-6;
        const double far = distance({deep * 4, deep, deep}, {deep, deep, deep * 4});
        report(far < open * 0.2,
               "two deep-shadow pixels of opposite hue collapse toward each other",
               std::to_string(far) + " against " + std::to_string(open));

        // Stronger than a bound, because the floor's behavior is predictable:
        // below it the ratio is scaled by L/0.1 exactly, so the suppression is
        // a number this test can name rather than a direction it can hope for.
        // (Not identical, because the two endpoints have slightly different L.)
        const double lDeep = 0.0126;          // L of the first deep pixel
        report(std::abs(far / open - lDeep / 0.1) < 0.02,
               "and by the factor the floor predicts, not merely by some factor",
               std::to_string(far / open) + " against " + std::to_string(lDeep / 0.1));

        // And it does not reach up into ordinary shadow detail. This pair sits
        // at L = 0.31, three times the floor, so it is untouched.
        const double lit = distance({0.02, 0.03, 0.08}, {0.08, 0.03, 0.02});
        report(lit > open * 0.85,
               "while a normal shadow still separates by color",
               std::to_string(lit / open));

        // ⚠ **On the GPU, not on the model.** Everything above compares the CPU
        // oracle against itself: the oracle carries the same floor, so deleting
        // the shader's floor entirely left all of it green. That mutation
        // survived, and it is the same shape as the matte test's clamp — a
        // check that cannot tell the code under test from its own stand-in.
        //
        // The property, stated so only the shader can satisfy it: with the
        // floor, two deep-shadow pixels of opposite hue sit close enough that a
        // tight band around one covers the other. Without it they are as far
        // apart as a saturated red is from a saturated blue, and the band
        // covers one and not the other — which on a photograph is a color mask
        // speckling through every shadow in the frame.
        {
            const double d = 1e-6;
            const std::array<std::array<double, 3>, 2> shadows{{
                {d * 4, d, d}, {d, d, d * 4},
            }};
            std::vector<__fp16> ref(std::size_t(kW) * kH * 4, __fp16(0.0f));
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    const auto& c = shadows[x < kW / 2 ? 0 : 1];
                    const std::size_t i = (std::size_t(y) * kW + x) * 4;
                    ref[i + 0] = __fp16(c[0]); ref[i + 1] = __fp16(c[1]);
                    ref[i + 2] = __fp16(c[2]); ref[i + 3] = __fp16(1.0f);
                }
            }
            reference->upload(ref.data(), std::size_t(kW) * 4 * sizeof(__fp16));

            params::MaskComponent m = base;
            m.colorR = float(shadows[0][0]);
            m.colorG = float(shadows[0][1]);
            m.colorB = float(shadows[0][2]);
            m.colorTol = 0.10f; m.colorSoft = 0.01f;
            const auto got = run(m);
            report(at(got, int(kW) / 4, int(kH) / 2) > 0.999 &&
                   at(got, int(kW) * 3 / 4, int(kH) / 2) > 0.999,
                   "the shader's own floor holds two deep-shadow hues together",
                   std::to_string(at(got, int(kW) * 3 / 4, int(kH) / 2)));
            uploadColors();
        }
    }

    // ── Invert and compose reach kind 6 like every other kind ─────────────
    //
    // The fourth dead control this codebase found was invert not reaching the
    // brush, because a new kind was added past the line that applied it.
    {
        params::MaskComponent m = base;
        target(m, colors[0]);
        m.colorTol = 0.05f;
        m.invert = 1;
        const auto got = run(m);
        report(at(got, sampleX(0), sampleY(1)) < 1e-3 &&
               at(got, sampleX(3), sampleY(1)) > 0.999,
               "invert applies to a color band",
               std::to_string(at(got, sampleX(0), sampleY(1))));
    }
}


// A raster mask component — research/masking.md §5, the shape a segmentation
// matte arrives in.
//
// Vision's own output cannot be unit-tested: it is a model whose result moves
// between OS releases, and "did it find the subject" is not a property this
// suite can assert. What *can* be pinned is everything between the matte and
// the picture, which is where the silent failures live — a half-texel offset in
// the lift, a live rectangle ignored, invert or compose skipping the new kind
// the way they once skipped the brush.

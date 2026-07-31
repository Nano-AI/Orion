// Raster mattes and the guided refinement that snaps them to an edge.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

void testMaskMatteGpu() {
    section("Matte masks (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 64, kH = 64;
    constexpr std::uint32_t kM = 8;          // the matte texture's allocation

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
    auto matte = orion::gpu::Texture::create(*device, kM, kM, PixelFormat::R16Float);
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

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
    std::vector<std::pair<float, float>> dabList;
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
    const auto setDabsAt = [&](int i, float x, float y) {
        if (int(dabList.size()) <= i) dabList.resize(std::size_t(i) + 1, {0.0f, 0.0f});
        dabList[std::size_t(i)] = {x, y};
        setDabs(dabList);
    };
    (void)setDabs; (void)setDabsAt;

    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));

    // The matte texture is allocated for the largest matte a producer might
    // hand over, and a smaller one lands in its top-left corner — exactly as
    // DevelopPipeline::setMaskMatte does it. `fill` writes junk outside the live
    // rectangle on purpose: if the kernel ever reads past `matteSize`, that junk
    // is what tells us.
    const auto upload = [&](int lw, int lh, const std::vector<float>& live,
                            float outside) {
        std::vector<__fp16> tex(std::size_t(kM) * kM, __fp16(outside));
        for (int y = 0; y < lh; ++y) {
            for (int x = 0; x < lw; ++x) {
                tex[std::size_t(y) * kM + std::size_t(x)] =
                    __fp16(live[std::size_t(y) * std::size_t(lw) + std::size_t(x)]);
            }
        }
        matte->upload(tex.data(), std::size_t(kM) * sizeof(__fp16));
    };

    const auto run = [&](const params::MaskComponent& m) {
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dabTex.get(), dabBoundsTex.get(), dst.get()}, &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return double(a[std::size_t(y) * kW + std::size_t(x)]);
    };

    params::MaskComponent base{};
    base.size[0] = kW; base.size[1] = kH;
    base.kind = 4;
    base.matteSize[0] = 2; base.matteSize[1] = 1;

    // ── The half-texel convention, which is the whole risk ────────────────
    //
    // A 2 x 1 matte of [0, 1] is a ramp whose answer is analytic under the
    // stated convention — samples at pixel *centres*, clamped at the edge
    // texels. The texel centres sit at normalized x = 0.25 and 0.75, so the
    // output is flat 0 left of 0.25, flat 1 right of 0.75, and linear between,
    // passing through exactly 0.5 at the middle.
    //
    // Every plausible off-by-half convention breaks one of those three: drop
    // the -0.5 and the ramp shifts a quarter of the frame; sample at texel
    // corners and the midpoint stops being 0.5; forget the clamp and the ends
    // wrap or extrapolate.
    {
        upload(2, 1, {0.0f, 1.0f}, 0.75f);
        const auto got = run(base);

        // Mid-frame, by symmetry, exactly half.
        const double mid = 0.5 * (at(got, 31, 32) + at(got, 32, 32));
        report(std::abs(mid - 0.5) < 2e-3,
               "a matte is lifted about its texel centres, so a two-texel ramp "
               "is exactly half way across",
               std::to_string(mid));

        // Flat outside the texel centres, at both ends.
        report(at(got, 0, 32) < 2e-3 && at(got, 15, 32) < 2e-3,
               "flat at the value of the first texel left of its centre",
               std::to_string(at(got, 0, 32)) + ", " + std::to_string(at(got, 15, 32)));
        report(at(got, 48, 32) > 1.0 - 2e-3 && at(got, 63, 32) > 1.0 - 2e-3,
               "and flat at the last texel's value right of its centre",
               std::to_string(at(got, 48, 32)) + ", " + std::to_string(at(got, 63, 32)));

        // A quarter of the way from one centre to the other is a quarter up.
        const double q = at(got, 24, 32);
        report(std::abs(q - 0.25) < 0.02, "and linear between them",
               std::to_string(q));

        // ⚠ Nothing outside the live rectangle leaks in. The texture is 8 x 8
        // and holds 0.75 everywhere the 2 x 1 matte does not; if any of that
        // were read, the ramp's ends could not be 0 and 1.
        report(at(got, 0, 0) < 2e-3 && at(got, 63, 63) > 1.0 - 2e-3,
               "and the rows past the live rectangle are never sampled",
               std::to_string(at(got, 0, 0)) + ", " + std::to_string(at(got, 63, 63)));
    }

    // ── The edge clamp, with a first texel that is not zero ───────────────
    //
    // ⚠ Added because a mutation survived. The ramp above starts at 0, and an
    // out-of-bounds texture read on Metal also returns 0 — so deleting the
    // clamp on the sample coordinate changed nothing anywhere the test looked,
    // and the suite reported a clean run on a kernel that reads outside its
    // matte. A first texel of 0.25 makes the two answers differ: clamped gives
    // 0.25 at the left edge, unclamped blends it toward the out-of-bounds zero
    // and gives about 0.13.
    {
        upload(2, 1, {0.25f, 1.0f}, 0.0f);
        const auto got = run(base);
        report(std::abs(at(got, 0, 32) - 0.25) < 3e-3,
               "the sample coordinate is clamped to the edge texel, so the "
               "border is the matte's own value and not the void past it",
               std::to_string(at(got, 0, 32)));
    }

    // ── A matte with no live rectangle contributes nothing ────────────────
    //
    // Zero means "no matte uploaded", and a component in that state has to
    // behave like a component switched off — not sample an empty texture and
    // not cover the frame.
    {
        upload(2, 1, {0.0f, 1.0f}, 1.0f);
        auto m = base;
        m.matteSize[0] = 0; m.matteSize[1] = 0;
        const auto got = run(m);
        double worst = 0.0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                worst = std::max(worst, at(got, int(x), int(y)));
            }
        }
        report(worst < 1e-4, "an empty matte covers nothing at all",
               std::to_string(worst));
    }

    // ── Invert and compose reach the new kind ─────────────────────────────
    //
    // ⚠ This is the check that exists because of history. When the brush was
    // added, invert was held by the gradient node and the brush node discarded
    // its output, so invert silently did nothing to a stroke — a dead control
    // nobody noticed for a session. A fourth kind is exactly the shape of that
    // mistake repeating.
    {
        upload(2, 1, {0.0f, 1.0f}, 0.0f);
        const auto plain = run(base);
        auto inv = base;
        inv.invert = 1;
        const auto inverted = run(inv);

        double worst = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            worst = std::max(worst, std::abs((1.0 - at(plain, int(x), 32))
                                             - at(inverted, int(x), 32)));
        }
        report(worst < 2e-3, "invert reaches a matte component",
               "worst " + std::to_string(worst));
    }
    {
        // Subtract a matte from full coverage: the fold's input is 1 here
        // rather than the usual 0, so what is measured is the op and not the
        // component alone.
        upload(2, 1, {0.0f, 1.0f}, 0.0f);
        const std::vector<__fp16> ones(std::size_t(kW) * kH, __fp16(1.0f));
        auto m = base;
        m.compose = 1;   // subtract

        src->upload(ones.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dabTex.get(), dabBoundsTex.get(), dst.get()}, &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> got(std::size_t(kW) * kH);
        dst->download(got.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);

        // alpha1 * (1 - alpha2), with alpha1 = 1: the matte's complement.
        report(at(got, 0, 32) > 1.0 - 2e-3 && at(got, 63, 32) < 2e-3,
               "and subtract composes it like any other kind",
               std::to_string(at(got, 0, 32)) + ", " + std::to_string(at(got, 63, 32)));
    }

    // ── A two-dimensional matte, to catch a transposed lift ───────────────
    //
    // Everything above runs along x, so a kernel that swapped the two axes
    // would pass all of it. This one cannot: the matte varies only in y.
    {
        upload(1, 2, {0.0f, 1.0f}, 0.0f);
        auto m = base;
        m.matteSize[0] = 1; m.matteSize[1] = 2;
        const auto got = run(m);
        report(at(got, 32, 0) < 2e-3 && at(got, 32, 63) > 1.0 - 2e-3 &&
               std::abs(0.5 * (at(got, 32, 31) + at(got, 32, 32)) - 0.5) < 2e-3,
               "a matte that varies in y is lifted in y, not in x",
               std::to_string(at(got, 32, 0)) + " -> " + std::to_string(at(got, 32, 63)));
    }
}

// Guided feathering of the mask group — research/masking.md §4.
//
// He, Sun & Tang's guided filter with the mask as input and the photograph as
// guide. The whole seven-node chain is driven here exactly as DevelopPipeline
// wires it, because the wiring is most of what can go wrong: three kernels,
// two of them the existing box blurs, and a bilinear lift between two grids.
//
// ⚠ **The synthetic frame is chosen so the answers are identities, not
// magnitudes.** A test that only asked "did the boundary move" would pass on a
// plain blur; the case that separates the two is the flat guide, where the
// correct answer is that the boundary does *not* move.
void testMaskRefineGpu() {
    section("Guided mask refinement (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;

    // Wide and short: the interesting structure is one vertical edge, and the
    // rows are identical, so height buys nothing but time.
    constexpr std::uint32_t kW = 512, kH = 64;
    constexpr int kScale = 4;
    constexpr std::uint32_t kGW = kW / kScale, kGH = kH / kScale;
    constexpr int kRadiusFull = 60;
    constexpr int kRadius = kRadiusFull / kScale;   // 15, as the pipeline does
    constexpr float kEps = 0.01f;

    constexpr int kGuideEdge = 256;   // where the photograph's edge is
    constexpr int kMaskEdge  = 296;   // where the mask was placed — 40px off

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    const std::string dir = std::string(ORION_SHADER_DIR) + "/";
    auto libPrep  = orion::gpu::Library::createFromFile(*device, dir + "maskGuidePrep.metallib");
    auto libAb    = orion::gpu::Library::createFromFile(*device, dir + "maskGuideAb.metallib");
    auto libApply = orion::gpu::Library::createFromFile(*device, dir + "maskGuideApply.metallib");
    auto libBox   = orion::gpu::Library::createFromFile(*device, dir + "boxBlur.metallib");
    auto libBox4  = orion::gpu::Library::createFromFile(*device, dir + "boxBlur4.metallib");

    auto kPrep  = orion::gpu::Kernel::create(*device, *libPrep,  "maskGuidePrep");
    auto kAb    = orion::gpu::Kernel::create(*device, *libAb,    "maskGuideAb");
    auto kApply = orion::gpu::Kernel::create(*device, *libApply, "maskGuideApply");
    auto kBox   = orion::gpu::Kernel::create(*device, *libBox,   "boxBlur");
    auto kBox4  = orion::gpu::Kernel::create(*device, *libBox4,  "boxBlur4");

    auto texGuide = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RG32Float);
    auto texMask  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto texM0    = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RGBA32Float);
    auto texM1    = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RGBA32Float);
    auto texM2    = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RGBA32Float);
    auto texAb0   = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RG32Float);
    auto texAb1   = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RG32Float);
    auto texAb2   = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RG32Float);
    auto texOut   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);

    // The guide as `guide_prep.slang` writes it: (l, l*l) with l = log2 luma.
    const auto uploadGuide = [&](const std::function<float(int)>& logLumaAt) {
        std::vector<float> g(std::size_t(kW) * kH * 2);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const float l = logLumaAt(int(x));
                g[(std::size_t(y) * kW + x) * 2 + 0] = l;
                g[(std::size_t(y) * kW + x) * 2 + 1] = l * l;
            }
        }
        texGuide->upload(g.data(), std::size_t(kW) * 2 * sizeof(float));
    };

    const auto uploadMask = [&](const std::function<float(int)>& alphaAt) {
        std::vector<__fp16> m(std::size_t(kW) * kH);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                m[std::size_t(y) * kW + x] = __fp16(alphaAt(int(x)));
            }
        }
        texMask->upload(m.data(), std::size_t(kW) * sizeof(__fp16));
    };

    // The chain, in the order DevelopPipeline adds the nodes.
    const auto refine = [&](float strength) {
        params::MaskGuidePrep mp{};
        mp.outSize[0] = kGW; mp.outSize[1] = kGH;
        mp.inSize[0]  = kW;  mp.inSize[1]  = kH;
        mp.scale = kScale;

        params::BoxBlur bh{{kGW, kGH}, kRadius, 1};
        params::BoxBlur bv{{kGW, kGH}, kRadius, 0};
        params::MaskGuideAb mab{{kGW, kGH}, kEps, 0.0f};

        params::MaskGuideApply mga{};
        mga.size[0] = kW; mga.size[1] = kH;
        mga.coeffSize[0] = kGW; mga.coeffSize[1] = kGH;
        mga.strength = strength;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kPrep, {texGuide.get(), texMask.get(), texM0.get()},
                    &mp, sizeof mp, kGW, kGH);
        cb.dispatch(*kBox4, {texM0.get(), texM1.get()}, &bh, sizeof bh, kGW, kGH);
        cb.dispatch(*kBox4, {texM1.get(), texM2.get()}, &bv, sizeof bv, kGW, kGH);
        cb.dispatch(*kAb,   {texM2.get(), texAb0.get()}, &mab, sizeof mab, kGW, kGH);
        cb.dispatch(*kBox,  {texAb0.get(), texAb1.get()}, &bh, sizeof bh, kGW, kGH);
        cb.dispatch(*kBox,  {texAb1.get(), texAb2.get()}, &bv, sizeof bv, kGW, kGH);
        cb.dispatch(*kApply, {texMask.get(), texAb2.get(), texGuide.get(), texOut.get()},
                    &mga, sizeof mga, kW, kH);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(kW) * kH);
        texOut->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& v, int x) {
        return double(v[std::size_t(kH / 2) * kW + x]);
    };

    const auto stepGuide = [&](int x) { return x < kGuideEdge ? 5.0f : 8.0f; };
    const auto stepMask  = [&](int x) { return x < kMaskEdge  ? 0.0f : 1.0f; };

    // The filter computed directly, one dimension, no subsampling.
    //
    // ⚠ This is the oracle, and it is why the test is worth having. Every
    // assertion below could have been written as a magnitude — "the boundary
    // moved", "the far side stayed clear" — and every one of those passes on a
    // plain blur or on a chain wired half right. Against a reference the answer
    // is a number the algorithm owes, and the subsampled GPU chain has to
    // reproduce it.
    //
    // Same radius, same epsilon, same clamp-to-edge windows the box passes use.
    // The rows of the synthetic frame are identical, so one row is the whole
    // problem.
    const auto exact = [&](const std::function<float(int)>& gAt,
                           const std::function<float(int)>& mAt) {
        std::vector<double> I(kW), pv(kW), a(kW), b(kW), q(kW);
        for (std::uint32_t x = 0; x < kW; ++x) { I[x] = gAt(int(x)); pv[x] = mAt(int(x)); }

        const auto mean = [&](const std::vector<double>& v, int at) {
            double sum = 0.0; int n = 0;
            for (int t = at - kRadiusFull; t <= at + kRadiusFull; ++t) {
                sum += v[std::size_t(std::clamp(t, 0, int(kW) - 1))]; ++n;
            }
            return sum / n;
        };
        std::vector<double> II(kW), IP(kW);
        for (std::uint32_t x = 0; x < kW; ++x) { II[x] = I[x] * I[x]; IP[x] = I[x] * pv[x]; }

        for (std::uint32_t x = 0; x < kW; ++x) {
            const double mI = mean(I, int(x)), mII = mean(II, int(x));
            const double mP = mean(pv, int(x)), mIP = mean(IP, int(x));
            const double var = std::max(mII - mI * mI, 0.0);
            a[x] = (mIP - mI * mP) / (var + kEps);
            b[x] = mP - a[x] * mI;
        }
        for (std::uint32_t x = 0; x < kW; ++x) {
            q[x] = std::clamp(mean(a, int(x)) * I[x] + mean(b, int(x)), 0.0, 1.0);
        }
        return q;
    };

    const auto crossing = [&](const std::vector<__fp16>& v) {
        for (std::uint32_t x = 1; x < kW; ++x) {
            if (at(v, int(x - 1)) < 0.5 && at(v, int(x)) >= 0.5) return int(x);
        }
        return -1;
    };

    // ── A constant mask is returned unchanged, corners included ───────────
    //
    // cov(I, p) is zero for constant p whatever the guide does, so a = 0 and
    // b = mean(p): the output is the constant back. It holds at the frame's
    // corners only if both box passes normalise by the window they actually
    // read, which is the one thing a separable blur most often gets wrong.
    {
        uploadGuide(stepGuide);
        uploadMask([](int) { return 0.5f; });
        const auto got = refine(1.0f);

        double worst = 0.0;
        int worstAt = 0;
        for (std::uint32_t y : {0u, kH / 2, kH - 1}) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const double d = std::abs(double(got[std::size_t(y) * kW + x]) - 0.5);
                if (d > worst) { worst = d; worstAt = int(x); }
            }
        }
        report(worst < 2e-3, "a constant mask survives refinement, corners included",
               "worst " + std::to_string(worst) + " at x=" + std::to_string(worstAt));
    }

    // ── Strength zero is the identity, bit for bit ────────────────────────
    {
        uploadGuide(stepGuide);
        uploadMask(stepMask);
        const auto got = refine(0.0f);
        bool same = true;
        for (std::uint32_t x = 0; x < kW; ++x) {
            if (at(got, int(x)) != double(stepMask(int(x)))) { same = false; break; }
        }
        report(same, "strength 0 returns the placed mask unchanged");
    }

    // ── The chain reproduces the filter, to a hundredth of coverage ───────
    int crossWithEdge = -1;
    {
        uploadGuide(stepGuide);
        uploadMask(stepMask);
        const auto got = refine(1.0f);
        const auto want = exact([&](int x) { return stepGuide(x); },
                                [&](int x) { return stepMask(x); });

        double worst = 0.0;
        int worstAt = 0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double d = std::abs(at(got, int(x)) - want[x]);
            if (d > worst) { worst = d; worstAt = int(x); }
        }
        // The subsampled chain is not the exact filter and is not meant to be —
        // He & Sun's whole argument is that a and b vary slowly enough to be
        // sampled coarsely. Worst disagreement lands at the discontinuity,
        // where s = 4 smears the lift over four pixels.
        report(worst < 0.02,
               "the subsampled chain reproduces the exact filter",
               "worst " + std::to_string(worst) + " at x=" + std::to_string(worstAt));

        crossWithEdge = crossing(got);

        // ⚠ **The matte is discontinuous at the photograph's edge**, and that
        // is the claim of guided feathering. Left of the edge it is identically
        // zero — a cancellation of a, b and the full-resolution reconstruction
        // that nothing but a correct chain produces — and at the edge it jumps.
        //
        // The jump has a closed form for this geometry. With the mask placed d
        // past an edge and a window of radius r,
        //
        //     q(edge+) = 1 - (d/2r)(1 + ln(2r/d))
        //
        // which at d = 40, r = 60 is 1 - (1/3)(1 + ln 3) = 0.3005. The
        // reference above computes 0.3058 and the GPU 0.3145; all three agree,
        // and the point of quoting the closed form is that it depends on r, so
        // it dies if the radius is ever quietly changed.
        report(at(got, kGuideEdge - 1) < 0.03 && at(got, kGuideEdge) > 0.27,
               "the matte is zero up to the photograph's edge and jumps there",
               "q[255] " + std::to_string(at(got, kGuideEdge - 1)) +
               ", q[256] " + std::to_string(at(got, kGuideEdge)));

        report(at(got, 430) > 0.98, "and reaches full coverage well inside",
               std::to_string(at(got, 430)));
    }

    // ── The control: with no edge to snap to, nothing is attracted ────────
    //
    // ⚠ The case that separates guided feathering from a blur. A flat guide has
    // var(I) = 0 and cov(I, p) = 0, so a = 0 and q collapses to the local mean
    // of the mask — blurred, but still centred where it was placed, and with no
    // discontinuity anywhere. Without this, every assertion above is also
    // satisfied by a box blur of the mask.
    {
        uploadGuide([](int) { return 6.0f; });
        uploadMask(stepMask);
        const auto got = refine(1.0f);
        const int cross = crossing(got);

        report(cross >= 0 && std::abs(cross - kMaskEdge) < 4,
               "a flat guide leaves the boundary where it was placed",
               "crossing " + std::to_string(cross) +
               ", placed " + std::to_string(kMaskEdge));

        double jump = 0.0;
        for (int x = kGuideEdge - 4; x <= kGuideEdge + 4; ++x) {
            jump = std::max(jump, at(got, x + 1) - at(got, x));
        }
        report(jump < 0.02,
               "and puts no step where the other guide had its edge",
               std::to_string(jump));

        report(crossWithEdge >= 0 && cross - crossWithEdge > 8,
               "so the edge is what moved the boundary, by a measurable amount",
               std::to_string(cross - crossWithEdge) + " px");
    }

    // ── The filter is affine in the mask, so the complement is symmetric ──
    //
    // a and b are linear in p, so refine(1 - p) = 1 - refine(p) exactly. Free
    // to check and it catches an asymmetry anywhere in the chain — a stray
    // saturate, a sign lost in the covariance — that the step cases would read
    // as merely a slightly different boundary.
    {
        uploadGuide(stepGuide);
        uploadMask(stepMask);
        const auto direct = refine(1.0f);
        uploadMask([&](int x) { return 1.0f - stepMask(x); });
        const auto complement = refine(1.0f);

        double worst = 0.0;
        int worstAt = 0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double d = std::abs((1.0 - at(direct, int(x))) - at(complement, int(x)));
            if (d > worst) { worst = d; worstAt = int(x); }
        }
        report(worst < 3e-3, "and refining the complement gives the complement",
               "worst " + std::to_string(worst) + " at x=" + std::to_string(worstAt));
    }

    // ── Epsilon decides which edges count, and both wrong answers are here ─
    //
    // The constant is Orion's own (UNSOURCED.md §20), so what is pinned is the
    // behaviour it was chosen for, measured as the height of the jump at the
    // edge. Reference values from the model above:
    //
    //     half-stop edge, eps = 0.01 (Orion)          jump 0.233
    //     half-stop edge, eps = 0.04 (recovery chain) jump 0.149
    //     tenth-stop edge, eps = 0.01 (Orion)         jump 0.047
    //     tenth-stop edge, eps = 1e-6 (the paper)     jump 0.306
    //
    // So the two thresholds below fail in opposite directions: reusing the
    // recovery chain's constant loses the half-stop edge, and copying the
    // paper's makes the matte snap to a tenth-stop one — which on a photograph
    // is shadow noise.
    {
        const auto jumpAtEdge = [&](const std::vector<__fp16>& v) {
            double j = 0.0;
            for (int x = kGuideEdge - 4; x <= kGuideEdge + 4; ++x) {
                j = std::max(j, at(v, x + 1) - at(v, x));
            }
            return j;
        };

        uploadMask(stepMask);
        uploadGuide([&](int x) { return x < kGuideEdge ? 5.0f : 5.5f; });
        const double strong = jumpAtEdge(refine(1.0f));
        uploadGuide([&](int x) { return x < kGuideEdge ? 5.0f : 5.1f; });
        const double faint = jumpAtEdge(refine(1.0f));

        report(strong > 0.20, "a half-stop edge is followed",
               "jump " + std::to_string(strong));
        report(faint < 0.10, "and a tenth-stop one is treated as texture",
               "jump " + std::to_string(faint));
    }
}


// Mask primitives, the brush, and how components compose.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

void testMaskGpu() {
    section("Masks (GPU)");

    using orion::gpu::PixelFormat;
    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    const auto load = [&](const char* entry) {
        auto lib = orion::gpu::Library::createFromFile(
            *device, std::string(ORION_SHADER_DIR) + "/" + entry + ".metallib");
        auto k = orion::gpu::Kernel::create(*device, *lib, entry);
        return std::pair{std::move(lib), std::move(k)};
    };

    auto kMask = load("maskComponent");
    auto src = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by kinds 1-3. The kernel takes a matte for kind 4
    // (research/masking.md §5) and the binding has to be present either way.
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by every kind except 5, which reads a luminance out of
    // it (research/masking.md §4b). The binding has to be present regardless.
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    // The fold's identity, which mask:base writes in the product. Every check
    // here runs a single component over it, where add-from-zero is the
    // component's own coverage — so all of the numbers pinned before the merge
    // must reproduce exactly.
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

    const auto run = [&](const orion::pipe::params::MaskComponent& m) {
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kMask.second, {src.get(), reference.get(), matte.get(), dabTex.get(), dabBoundsTex.get(), &scratchAccum(*device, kW, kH), dst.get()}, &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return float(a[std::size_t(y) * kW + x]);
    };

    // ── Linear ────────────────────────────────────────────────────────────
    {
        orion::pipe::params::MaskComponent m{};
        m.size[0] = kW; m.size[1] = kH;
        m.kind = 1;
        // Left edge to right edge, through the shipping derivation rather
        // than a second copy of it: centre 0.5, angle 0, length 1.
        {
            const auto r = orion::pipe::mask::ramp(0.5f, 0.5f, 0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) m.rampNum[k] = r.num[k];
        }
        const auto a = run(m);

        report(at(a, 0, 32) < 0.01f && at(a, kW - 1, 32) > 0.99f,
               "a linear gradient is zero at Zero and full at Full",
               std::to_string(at(a, 0, 32)) + " … " + std::to_string(at(a, kW - 1, 32)));

        bool monotone = true;
        for (std::uint32_t x = 1; x < kW; ++x) {
            if (at(a, int(x), 32) < at(a, int(x) - 1, 32) - 1e-3f) monotone = false;
        }
        report(monotone, "and rises monotonically between them", "");

        // Perpendicular to the gradient direction nothing may change — that is
        // what makes it a *linear* gradient rather than a smear.
        double worstRow = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            worstRow = std::max(worstRow,
                                std::abs(double(at(a, int(x), 4)) - double(at(a, int(x), 60))));
        }
        report(worstRow < 2e-3, "and is constant along its own perpendicular",
               "worst " + std::to_string(worstRow));

        // The midpoint of a smootherstep is a half, exactly. Cheap check that
        // the falloff is the one that was intended.
        report(std::abs(at(a, kW / 2, 32) - 0.5f) < 0.02f,
               "the falloff is symmetric about its midpoint",
               std::to_string(at(a, kW / 2, 32)));

        m.invert = 1;
        const auto inv = run(m);
        double worstInv = 0.0;
        for (std::uint32_t i = 0; i < kW * kH; ++i) {
            worstInv = std::max(worstInv,
                                std::abs(1.0 - double(a[i]) - double(inv[i])));
        }
        report(worstInv < 2e-3, "and inverting is exactly one minus it",
               "worst " + std::to_string(worstInv));
    }

    // ── Radial ────────────────────────────────────────────────────────────
    {
        orion::pipe::params::MaskComponent m{};
        m.size[0] = kW; m.size[1] = kH;
        m.kind = 2;
        // On a pixel center, not on 0.5. With 64 pixels, 0.5 falls *between*
        // 31 and 32, so "twelve pixels either side" is not equidistant — and on
        // the steepest part of the feather that half-pixel is worth a quarter
        // of the alpha range. The asymmetry was in the test, not the shader.
        m.center[0] = 32.5f / 64.0f; m.center[1] = 32.5f / 64.0f;
        m.semi[0] = 0.25f; m.semi[1] = 0.25f;
        m.feather = 0.4f;
        m.roundness = 2.0f;
        const auto a = run(m);

        report(at(a, 32, 32) > 0.99f, "a radial gradient is full at its center",
               std::to_string(at(a, 32, 32)));
        report(at(a, 0, 0) < 0.01f, "and zero well outside its boundary",
               std::to_string(at(a, 0, 0)));

        // Circular: equal radii means the alpha depends only on distance, so
        // the four cardinal points at one radius must agree.
        const float right = at(a, 32 + 12, 32), left = at(a, 32 - 12, 32);
        const float down  = at(a, 32, 32 + 12), up   = at(a, 32, 32 - 12);
        const double spread = std::max({std::abs(right - left), std::abs(right - down),
                                        std::abs(right - up)});
        report(spread < 2e-3, "and with equal radii depends only on distance",
               "spread " + std::to_string(spread));
    }

    // ── The alpha scales the parameter, not the result ────────────────────
    //
    // research/masking.md section 2 calls this the part most likely to be got
    // wrong, and the two candidates differ measurably: at alpha 0.5 with a
    // one-stop local exposure, scaling the parameter gives 2^0.5 = 1.414, while
    // blending two rendered results gives (1 + 2)/2 = 1.5. Six per cent apart,
    // and only one of them is a smooth multiplicative ramp in linear light.
    {
        auto kLinear = load("developLinear");

        auto src   = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::RGBA16Float);
        auto guide = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::RG32Float);
        auto mask  = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::R16Float);
        auto out   = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * 4);
        for (std::uint32_t x = 0; x < kW; ++x) {
            for (int c = 0; c < 3; ++c) in[x * 4 + c] = __fp16(0.25f);
            in[x * 4 + 3] = __fp16(1.0f);
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        // A ramp of coverage across the row.
        std::vector<__fp16> alpha(kW);
        for (std::uint32_t x = 0; x < kW; ++x) alpha[x] = __fp16(float(x) / float(kW - 1));
        mask->upload(alpha.data(), std::size_t(kW) * sizeof(__fp16));

        std::vector<float> guideData(std::size_t(kW) * 2, 0.0f);
        guide->upload(guideData.data(), std::size_t(kW) * 2 * sizeof(float));

        orion::pipe::params::LinearAdjust la{};
        la.size[0] = kW; la.size[1] = 1;
        la.guideEnabled = 0.0f;
        la.layerExposureEv[0] = 1.0f;
        la.maskActive = 1.0f;
        // One layer reading coverage slot 0. The kernel binds eight slots
        // because a layer's coverage cannot be a node picked per render.
        la.layerCount = 1;
        la.layerMask[0] = 0;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kLinear.second,
                    {src.get(), guide.get(), guide.get(),
                     mask.get(), mask.get(), mask.get(), mask.get(),
                     mask.get(), mask.get(), mask.get(), mask.get(), out.get()},
                    &la, sizeof la, kW, 1);
        cb.commitAndWait();

        std::vector<__fp16> got(in.size());
        out->download(got.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, 1);

        double worstParam = 0.0, worstAgainstBlend = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double a = double(x) / double(kW - 1);
            const double parameter = 0.25 * std::exp2(a * 1.0);   // what it should be
            const double blended   = 0.25 * (1.0 - a) + 0.5 * a;  // what it must not be
            const double v = double(got[x * 4]);
            worstParam = std::max(worstParam, std::abs(v - parameter));
            worstAgainstBlend = std::max(worstAgainstBlend, std::abs(v - blended));
        }
        report(worstParam < 2e-3,
               "a masked exposure scales the parameter: alpha 0.5 gives 2^0.5",
               "worst " + std::to_string(worstParam));
        report(worstAgainstBlend > 0.005,
               "and is measurably not a blend of two rendered results",
               "differs by " + std::to_string(worstAgainstBlend));

        // Coverage zero has to be the untouched pixel, or every mask would put
        // a faint edit over the whole frame.
        report(std::abs(double(got[0]) - 0.25) < 1e-3,
               "and zero coverage leaves the pixel exactly alone",
               std::to_string(double(got[0])));
    }
}

/// Brush dabs — a stroke as a list of centers. research/masking.md §1.
void testMaskBrushGpu() {
    section("Brush masks (GPU)");

    using orion::gpu::PixelFormat;
    constexpr std::uint32_t kW = 128, kH = 128;

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

    auto src = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by kinds 1-3. The kernel takes a matte for kind 4
    // (research/masking.md §5) and the binding has to be present either way.
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by every kind except 5, which reads a luminance out of
    // it (research/masking.md §4b). The binding has to be present regardless.
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    // A fresh stroke folds over the group's identity, so `src` is zeroed before
    // each run unless a check is deliberately chaining passes.
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
    src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));

    const auto run = [&](const orion::pipe::params::MaskComponent& b) {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dabTex.get(), dabBoundsTex.get(), &scratchAccum(*device, kW, kH), dst.get()}, &b, sizeof b, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return double(a[std::size_t(y) * kW + x]);
    };

    orion::pipe::params::MaskComponent base{};
    base.size[0] = kW; base.size[1] = kH;
    // ⚠ Set explicitly. A zero-initialized `MaskComponent` leaves `dabStride`
    // at zero, and the kernel's `max(stride, 1)` then puts dab 1 on row 1 of
    // the texture, where nothing was written — so a two-dab check silently
    // measured one dab. The pipeline always sets it; a test that forgets is
    // testing something else.
    base.kind = 3;
    base.dabStride = orion::pipe::params::kDabStride;
    // 0.2 of the 128-pixel test frame. The nib is in frame pixels now,
    // so the checks below that compute an expected falloff from a
    // normalized distance stay valid on this square frame.
    base.nibPx = 0.2f * float(kW);
    base.flow = 1.0f;
    base.hardness = 0.5f;

    // ── One dab, against the falloff computed here ────────────────────────
    //
    // Not "the center is bright and the outside is dark" — that passes on any
    // blob. The dab has to be the *stated* function, so it is checked against
    // smootherstep evaluated independently, at radii either side of where the
    // ramp starts.
    {
        auto b = base;
        b.count = 1;
        setDabsAt(0, 0.5f, 0.5f);
        const auto a = run(b);

        const auto falloff = [](double t) {
            const double x = std::clamp(t, 0.0, 1.0);
            return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
        };
        const auto want = [&](double d) {
            const double h = 0.5;
            return 1.0 - falloff((d - h) / (1.0 - h));
        };

        double worst = 0;
        for (int px = 64; px < 64 + 25; ++px) {
            // Distance from the center in units of the radius.
            const double d = ((px + 0.5) / double(kW) - 0.5) / 0.2;
            if (d >= 1.0) break;
            worst = std::max(worst, std::abs(at(a, px, 64) - want(d)));
        }
        report(worst < 2e-3, "a dab is smootherstep in the radius, not merely round",
               "worst " + std::to_string(worst));

        report(std::abs(at(a, 64, 64) - 1.0) < 1e-3,
               "full flow reaches full coverage at the center");
        report(at(a, 4, 4) == 0.0,
               "and lays down exactly nothing outside its radius",
               std::to_string(at(a, 4, 4)));
    }

    // ── The one most likely to be got wrong ───────────────────────────────
    //
    // Dabs compose source-over, not additively. Two dabs at full flow on the
    // same spot must still be full coverage; adding would drive alpha past 1
    // and clip, which reads as the brush getting stronger the longer a hand
    // hovers. The check that catches it is a *partial* flow, where the two
    // rules give measurably different answers: over gives 0.5 + 0.5·0.5 =
    // 0.75, addition gives 1.0.
    {
        auto b = base;
        b.flow = 0.5f;
        b.count = 2;
        setDabsAt(0, 0.5f, 0.5f);
        setDabsAt(1, 0.5f, 0.5f);
        const auto a = run(b);
        report(std::abs(at(a, 64, 64) - 0.75) < 2e-3,
               "two half-flow dabs compose source-over, not by addition",
               std::to_string(at(a, 64, 64)));
    }

    // ── Chaining, which is what makes a long stroke possible ──────────────
    //
    // A stroke longer than one pass accumulates into the alpha it is handed.
    // If `accumulate` were ignored, a long stroke would silently keep only its
    // last 256 dabs — and would still look like a stroke.
    {
        auto b = base;
        b.flow = 0.5f;
        b.count = 1;
        setDabsAt(0, 0.5f, 0.5f);
        const auto first = run(b);

        // Feed the result back in and lay the same dab again.
        src->upload(first.data(), std::size_t(kW) * sizeof(__fp16));
        b.accumulate = 1;
        const auto second = run(b);

        report(std::abs(at(second, 64, 64) - 0.75) < 2e-3,
               "a second pass builds on the first rather than replacing it",
               std::to_string(at(second, 64, 64)));

        // And a pass that lays nothing must leave the stroke untouched, or
        // every chained dispatch would erode what came before.
        //
        // ⚠️ `src` has to be fed the *second* result first. The first version
        // of this check forgot to, so it compared the pass-through of `first`
        // against `second` and failed — reporting a shader bug that was a
        // missing upload in the test.
        src->upload(second.data(), std::size_t(kW) * sizeof(__fp16));
        b.count = 0;
        const auto empty = run(b);

        // Every pixel, not one. A long stroke is several components chained
        // nose to tail, each continuing the alpha it is handed — so a
        // continuation pass that lays nothing must be exactly the identity, or
        // every chained dispatch would erode the stroke before it. Checking the
        // center pixel alone would not notice: the center is where a brush
        // writes most confidently and an edge is where an off-by-one shows up.
        std::size_t differing = 0;
        double worst = 0;
        for (std::size_t i = 0; i < empty.size(); ++i) {
            const double d = std::abs(double(empty[i]) - double(second[i]));
            if (d != 0.0) ++differing;
            worst = std::max(worst, d);
        }
        report(differing == 0,
               "an empty pass leaves the stroke bit-identical, every pixel",
               std::to_string(differing) + " of " + std::to_string(empty.size())
                   + " differ, worst " + std::to_string(worst));
    }

    // ── Flow zero is exactly nothing ──────────────────────────────────────
    {
        // The chaining check above left its stroke in `src`; this one starts a
        // fresh component over the group's identity.
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        auto b = base;
        b.flow = 0.0f;
        b.count = 8;
        std::vector<std::pair<float, float>> pts;
        for (int i = 0; i < 8; ++i) {
            pts.push_back({0.3f + 0.05f * float(i), 0.5f});
        }
        setDabs(pts);
        const auto a = run(b);
        double worst = 0;
        for (std::size_t i = 0; i < a.size(); ++i) worst = std::max(worst, double(a[i]));
        report(worst == 0.0, "flow zero lays down exactly nothing",
               std::to_string(worst));
    }

    // ── Why R16F and not R8, with the flow it actually starts to matter at ─
    //
    // The format claim is load-bearing, so it gets a number rather than an
    // assertion of principle.
    //
    // ⚠️ The first version of this check used flow 0.03 and claimed banding.
    // It does not band there and cannot: source-over moves alpha by about
    // 0.02 per dab, which is five to seven whole 8-bit codes, so all forty
    // steps resolve at eight bits and the test was demonstrating nothing while
    // reading like proof. The threshold is where one dab moves alpha less than
    // one code — flow · (1 − alpha) < 1/255, so flow below about 0.0039.
    //
    // 0.002 is below it and is an ordinary airbrush flow. There, consecutive
    // dabs land on the same 8-bit code and the buildup quantises; R16F still
    // resolves every one.
    {
        auto b = base;
        b.flow = 0.002f;
        b.count = 1;
        setDabsAt(0, 0.5f, 0.5f);

        std::vector<__fp16> zero(std::size_t(kW) * kH, __fp16(0.0f));
        src->upload(zero.data(), std::size_t(kW) * sizeof(__fp16));
        b.accumulate = 1;

        std::set<int> levels8, levels16;
        double alpha = 0;
        for (int pass = 0; pass < 40; ++pass) {
            const auto a = run(b);
            src->upload(a.data(), std::size_t(kW) * sizeof(__fp16));
            alpha = at(a, 64, 64);
            levels16.insert(int(std::lround(alpha * 65535.0)));
            levels8.insert(int(std::lround(alpha * 255.0)));
        }
        report(levels16.size() == 40 && levels8.size() * 4 < levels16.size() * 3,
               "40 airbrush dabs resolve in R16F and quantise at 8 bits",
               std::to_string(levels16.size()) + " distinct in 16-bit, "
                   + std::to_string(levels8.size()) + " at 8-bit");
        // 1 - 0.998^40, computed independently of the shader.
        report(std::abs(alpha - (1.0 - std::pow(0.998, 40.0))) < 2e-3,
               "and the buildup is the source-over series, not something near it",
               std::to_string(alpha));
    }
}

/// Mask groups — components folding into one coverage. research/masking.md §6.
void testMaskCompositeGpu() {
    section("Mask groups (GPU)");

    using orion::gpu::PixelFormat;
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

    auto src = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by kinds 1-3. The kernel takes a matte for kind 4
    // (research/masking.md §5) and the binding has to be present either way.
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by every kind except 5, which reads a luminance out of
    // it (research/masking.md §4b). The binding has to be present regardless.
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
        for (std::size_t k = 0; k < pts.size(); ++k) {
            texels[k * 4 + 0] = pts[k].first;
            texels[k * 4 + 1] = pts[k].second;
        }
        dabTex->upload(texels.data(),
                       std::size_t(orion::pipe::params::kDabStride) * 4 * sizeof(float));
        uploadDabBounds(texels);
    };
    const auto setDabsAt = [&](int k, float x, float y) {
        if (int(dabList.size()) <= k) dabList.resize(std::size_t(k) + 1, {0.0f, 0.0f});
        dabList[std::size_t(k)] = {x, y};
        setDabs(dabList);
    };
    (void)setDabs; (void)setDabsAt;

    // Chains a list of components exactly as DevelopPipeline does: the fold
    // starts from zero and each pass reads the one before it.
    const auto fold = [&](const std::vector<orion::pipe::params::MaskComponent>& parts) {
        std::vector<__fp16> alpha(std::size_t(kW) * kH, __fp16(0.0f));
        for (const auto& p : parts) {
            src->upload(alpha.data(), std::size_t(kW) * sizeof(__fp16));
            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dabTex.get(), dabBoundsTex.get(), &scratchAccum(*device, kW, kH), dst.get()}, &p, sizeof p, kW, kH);
            cb.commitAndWait();
            dst->download(alpha.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        }
        return alpha;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return double(a[std::size_t(y) * kW + x]);
    };

    // Two radials, deliberately overlapping and deliberately *partial* in the
    // overlap — the ops differ measurably only at fractional coverage, so full
    // coverage would pass on the wrong algebra. Wide feather keeps the region
    // between the centers in both falloffs.
    orion::pipe::params::MaskComponent A{};
    A.size[0] = kW; A.size[1] = kH;
    A.kind = 2;
    A.center[0] = 0.375f; A.center[1] = 0.5f;
    A.semi[0] = 0.35f; A.semi[1] = 0.35f;
    A.feather = 0.9f; A.roundness = 2.0f;

    auto B = A;
    B.center[0] = 0.625f;

    // Each alone, for reference — the algebra below is checked against what the
    // kernel actually produces for the parts, not against a reimplementation of
    // the falloff.
    const auto a = fold({A});
    const auto b = fold({B});

    // A probe row through both centers, all in partial coverage.
    const int y = 32;

    {
        auto B2 = B; B2.compose = 0;    // add
        const auto got = fold({A, B2});
        double worstMax = 0.0, vsScreen = 0.0;
        for (int x = 8; x < 56; ++x) {
            const double va = at(a, x, y), vb = at(b, x, y);
            worstMax = std::max(worstMax,
                                std::abs(at(got, x, y) - std::max(va, vb)));
            // Screen is the research's own listed alternative, so it is the
            // failure shape to rule out by name: smoother accumulation, but a
            // visible strengthening where two components overlap.
            vsScreen = std::max(vsScreen,
                                std::abs(at(got, x, y) - (va + vb - va * vb)));
        }
        report(worstMax < 2e-3, "add is max: no buildup where components overlap",
               "worst " + std::to_string(worstMax));
        report(vsScreen > 0.05, "and is measurably not screen",
               "differs by " + std::to_string(vsScreen));
    }

    {
        auto B2 = B; B2.compose = 1;    // subtract
        const auto got = fold({A, B2});
        double worst = 0.0;
        for (int x = 8; x < 56; ++x) {
            const double want = at(a, x, y) * (1.0 - at(b, x, y));
            worst = std::max(worst, std::abs(at(got, x, y) - want));
        }
        report(worst < 2e-3, "subtract is alpha1 * (1 - alpha2)",
               "worst " + std::to_string(worst));
    }

    {
        auto B2 = B; B2.compose = 2;    // intersect
        const auto got = fold({A, B2});
        double worst = 0.0;
        for (int x = 8; x < 56; ++x) {
            const double want = at(a, x, y) * at(b, x, y);
            worst = std::max(worst, std::abs(at(got, x, y) - want));
        }
        report(worst < 2e-3, "intersect is alpha1 * alpha2",
               "worst " + std::to_string(worst));
    }

    // ── Order sensitivity, which the research states rather than implies ────
    //
    // Components fold left in listed order: A then subtract-B masks A's region
    // away from B; B-subtract then add-A is A alone, because subtracting from
    // the fold's zero start is zero. If a reorder ever stops changing the
    // answer, the fold has quietly become a merge.
    {
        auto Bsub = B; Bsub.compose = 1;
        const auto ab = fold({A, Bsub});
        const auto ba = fold({Bsub, A});
        double biggest = 0.0;
        for (int x = 8; x < 56; ++x) {
            biggest = std::max(biggest,
                               std::abs(at(ab, x, y) - at(ba, x, y)));
        }
        report(biggest > 0.05, "add then subtract is order-sensitive, as stated",
               "differs by " + std::to_string(biggest));
    }

    // ── Invert reaches the brush now ─────────────────────────────────────────
    //
    // It never did before: the gradient node held the invert, the brush node
    // discarded that node's output, so inverting a brush mask did nothing —
    // the fourth dead control of the class found in session 2026-07-29n. Pin
    // the fix: an inverted dab is full coverage far away and dips at the dab.
    {
        orion::pipe::params::MaskComponent s{};
        s.size[0] = kW; s.size[1] = kH;
        s.kind = 3;
        s.dabStride = orion::pipe::params::kDabStride;
        s.invert = 1;
        s.nibPx = 0.2f * float(kW); s.flow = 1.0f; s.hardness = 0.5f;
        s.count = 1;
        setDabsAt(0, 0.5f, 0.5f);
        const auto got = fold({s});
        report(std::abs(at(got, 4, 4) - 1.0) < 1e-3 &&
               at(got, 32, 32) < 1e-3,
               "inverting a brush component inverts the stroke",
               std::to_string(at(got, 4, 4)) + " far, "
                   + std::to_string(at(got, 32, 32)) + " under the dab");
    }

    // ── A subtracted brush erases from a gradient ───────────────────────────
    //
    // The shape §6 exists for: a sky gradient with the treeline painted back
    // out. Under the dab the gradient must fall by exactly its own coverage
    // times the dab's; away from the dab it must not move at all.
    {
        orion::pipe::params::MaskComponent g{};
        g.size[0] = kW; g.size[1] = kH;
        g.kind = 1;
        {
            const auto r = orion::pipe::mask::ramp(0.5f, 0.5f, 0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) g.rampNum[k] = r.num[k];
        }

        orion::pipe::params::MaskComponent s{};
        s.size[0] = kW; s.size[1] = kH;
        s.kind = 3;
        s.dabStride = orion::pipe::params::kDabStride;
        s.compose = 1;   // subtract
        s.nibPx = 0.15f * float(kW); s.flow = 1.0f; s.hardness = 0.5f;
        s.count = 1;
        setDabsAt(0, 0.75f, 0.5f);

        const auto grad = fold({g});
        const auto stroke = fold({[&]{ auto t = s; t.compose = 0; return t; }()});
        const auto got = fold({g, s});

        const int dabX = 48;
        const double want = at(grad, dabX, y) * (1.0 - at(stroke, dabX, y));
        report(std::abs(at(got, dabX, y) - want) < 2e-3,
               "a subtracted stroke erases the gradient under it",
               std::to_string(at(got, dabX, y)) + " vs " + std::to_string(want));
        report(std::abs(at(got, 8, y) - at(grad, 8, y)) < 1e-3,
               "and leaves it exactly alone elsewhere",
               std::to_string(std::abs(at(got, 8, y) - at(grad, 8, y))));
    }

    // ── A radial mask at an angle, rendered where it is stored ──────────────
    //
    // The kernel evaluates the ellipse directly at the frame pixel — no
    // geometry, the matte's own contract. What this pins is the *struct*: the
    // display matrix's nine floats left `MaskComponentParams`, every field
    // after them moved, and a mis-agreed offset draws a plausible ellipse in
    // the wrong place, which no maths-only test can see. The reference is the
    // same superellipse transcribed on the CPU, at an angle and with a
    // non-default roundness so no field of the block is left unread.
    {
        const float cx = 0.42f, cy = 0.48f, ang = 0.5f;
        const float rx = 0.34f, ry = 0.26f;

        orion::pipe::params::MaskComponent m{};
        m.size[0] = kW; m.size[1] = kH;
        m.kind = 2;
        m.center[0] = cx; m.center[1] = cy;
        m.semi[0]   = rx; m.semi[1]   = ry;
        m.angle     = ang;
        m.feather   = 0.30f;
        m.roundness = 3.0f;
        m.startsLayer = 1;
        const auto got = fold({m});

        const auto coverage = [&](float qx, float qy) {
            const float c = std::cos(ang), s = std::sin(ang);
            const float ex = qx - cx, ey = qy - cy;
            const float u = ( c * ex + s * ey) / rx;
            const float v = (-s * ex + c * ey) / ry;
            const float n = 3.0f;
            const float r = std::pow(std::pow(std::abs(u), n)
                                     + std::pow(std::abs(v), n), 1.0f / n);
            const float f = 0.30f;
            const float t = std::clamp((r - (1.0f - f)) / f, 0.0f, 1.0f);
            return 1.0f - t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        };

        double worst = 0.0;
        for (std::uint32_t py = 0; py < kH; ++py) {
            for (std::uint32_t px = 0; px < kW; ++px) {
                const float qx = (float(px) + 0.5f) / float(kW);
                const float qy = (float(py) + 0.5f) / float(kH);
                worst = std::max(worst,
                    std::abs(double(coverage(qx, qy))
                             - double(at(got, int(px), int(py)))));
            }
        }
        // R16Float carries about three decimal digits, and the reference is a
        // different order of operations, so this is a tolerance on the format
        // rather than on the mathematics.
        report(worst < 3e-3,
               "a radial mask renders the stored frame-space ellipse exactly",
               "worst " + std::to_string(worst));
    }
}

/// A mask has to stay on its subject when the picture is turned or cropped.
/// A brush stroke has to go through the same transform a gradient's center does.
///
/// It did not. `DevelopPipeline` ran the gradient center through
/// `mask::toFrame` and copied the dab centers straight from displayed
/// coordinates into the shader, so a stroke ignored the crop and the rotation —
/// and because a portrait file carries an EXIF quarter turn, a stroke on one
/// landed mirrored and ninety degrees off with the rotate control untouched.
///
/// The gradients being right is what hid it, so the check is written as the
/// gradients' own invariant applied to a *stroke*: transform the points, and
/// they must sit where the same transform puts a gradient center placed at each
/// of them. If the two ever disagree again, a mask's shapes have stopped
/// agreeing about where the picture is.

/// The layer table is rebuilt when a layer break moves — the unlink bug.
///
/// `applyTone` resolves layers from runs of `startsLayer` flags and pushes the
/// table only when `linearMoved` says the linear params moved. That comparison
/// named every field the pushed struct depends on except the flags themselves —
/// so splitting a row re-rendered its coverage (the fold restarts from zero)
/// while the linear pass kept last frame's table, and layer 0's grade was
/// applied through the new row's coverage only. In the app: unlink a mask and
/// the previous mask's edits vanish until its eye button forces a push.
///
/// ⚠ Two pushes on one pipeline, deliberately: a fresh `apply` takes the
/// `first` branch and cannot see a stale guard. The reorder push at the end is
/// the same defect arriving through `maskmove`, which swaps rows wholesale.
void testLayerBreakRefreshesTheLayerTable() {
    section("Layer break staleness (GPU)");

    namespace pipe = orion::pipe;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    // A flat frame: every comparison below is a region against the unmasked
    // base, and a gradient would put the answer in the geometry instead.
    orion::raw::BayerImage img;
    img.width = 96;
    img.height = 96;
    img.samples.assign(std::size_t(96) * 96, 1200);
    img.filters = 0x94949494u;             // RGGB
    img.white = 4095;
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};

    std::unique_ptr<pipe::DevelopPipeline> dev;
    try {
        dev = std::make_unique<pipe::DevelopPipeline>(
            *device, std::string(ORION_SHADER_DIR), img);
    } catch (const std::exception& e) {
        report(false, "develop graph builds", e.what());
        return;
    }

    const std::uint32_t w = dev->outputWidth(), h = dev->outputHeight();
    std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
    // Mean of the green channel over a 5×5 patch at displayed (fx, fy).
    const auto patch = [&](float fx, float fy) {
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        const int cx = int(fx * float(w)), cy = int(fy * float(h));
        double sum = 0.0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                sum += double(px[(std::size_t(cy + dy) * w + std::size_t(cx + dx))
                               * 4 + 1]);
        return sum / 25.0;
    };

    // Two radials that do not overlap: one over the left, one over the right,
    // linked into a single run carrying layer 0's -2 EV.
    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};
    adj.maskCount = 2;
    adj.layers[0].exposureEv = -2.0f;
    for (int i = 0; i < 2; ++i) {
        auto& c = adj.maskComponents[std::size_t(i)];
        c.kind = 2;
        c.center[0] = (i == 0) ? 0.25f : 0.75f;
        c.center[1] = 0.5f;
        c.radius[0] = c.radius[1] = 0.16f;
        c.feather = 0.3f;
    }

    dev->apply(adj);
    dev->render();
    const double base  = patch(0.5f, 0.06f);
    const double left  = patch(0.25f, 0.5f);
    const double right = patch(0.75f, 0.5f);
    report(left < base * 0.8 && right < base * 0.8,
           "linked, one run darkens both regions",
           "base " + std::to_string(base) + " left " + std::to_string(left) +
           " right " + std::to_string(right));

    // The unlink. The split row's fold restarts from zero and the table must
    // follow: layer 0 is now the left radial alone, and the right radial is a
    // new layer with no adjustments.
    adj.maskComponents[1].startsLayer = true;
    dev->apply(adj);
    dev->render();
    const double leftSplit  = patch(0.25f, 0.5f);
    const double rightSplit = patch(0.75f, 0.5f);
    report(leftSplit < base * 0.8,
           "after the split, the first mask keeps its grade",
           "base " + std::to_string(base) + " left " + std::to_string(leftSplit));
    report(rightSplit > base * 0.95,
           "and the split-off mask starts from no adjustments",
           "base " + std::to_string(base) + " right " + std::to_string(rightSplit));

    // The reorder. `maskmove` swaps rows wholesale, so the flags travel with
    // them: after the swap the second row no longer starts a layer, the two
    // fold back into one run, and both regions carry layer 0's grade again.
    std::swap(adj.maskComponents[0], adj.maskComponents[1]);
    dev->apply(adj);
    dev->render();
    const double leftSwap  = patch(0.25f, 0.5f);
    const double rightSwap = patch(0.75f, 0.5f);
    report(leftSwap < base * 0.8 && rightSwap < base * 0.8,
           "a reorder that moves the break re-folds the run",
           "base " + std::to_string(base) + " left " + std::to_string(leftSwap) +
           " right " + std::to_string(rightSwap));

    // A layer's shadows alone must enable the guide chain. The dead-chain
    // hazard (#113, #119): every global path was checked at zero here, so a
    // predicate that forgot the layers would leave the six nodes disabled and
    // the local band quietly reading the pixel's own EV — roughly right,
    // everywhere, and never what was asked for.
    adj.layers[1].shadows = 0.5f;
    dev->apply(adj);
    dev->render();
    int guides = 0;
    for (const auto& t : dev->graph().lastRun())
        if (t.executed && t.name.rfind("guide:", 0) == 0) ++guides;
    report(guides >= 6, "a layer's shadows alone enables the guide chain",
           std::to_string(guides) + " guide nodes ran");
}

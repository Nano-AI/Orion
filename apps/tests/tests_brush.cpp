// The brush's dab list at length, and the prefix that says how much of it is
// already on the GPU.
//
// Split out of main.cpp 2026-07-31; see harness.h. Bayer decimation went to
// tests_color.cpp and spot removal to tests_spot.cpp on 2026-08-02 (decision
// #129) — neither shared this file's fixture, which is the `maskComponent`
// kernel with a dab texture and a bounds texture beside it.

#include "harness.h"

// ⚠ A stroke longer than a constant block can carry.
//
// This is the oldest carried-forward gap in planning/STATUS.md: everything past
// 256 dabs was dropped, with a warning on stderr that a photographer painting a
// long stroke would never see. The note beside it said the fix was more nodes,
// chained through the kernel's `accumulate` flag. It was not — the cap came
// from Metal's four-kilobyte limit on `setBytes`, and moving the stroke into an
// auxiliary texture removes it without a chain, a spare component or a second
// code path.
//
// What this checks is the far end of a long stroke, which is exactly what used
// to go missing.
/// Per-block dab rejection changes the speed and nothing else.
/// research/brush-acceleration.md, after Clark (1976).
///
/// ⚠ **The oracle is the same kernel with the boxes widened to the whole
/// plane.** That disables every rejection exactly — no block can be skipped —
/// so it is the unaccelerated loop, running the same arithmetic in the same
/// order, rather than a CPU model of it. A second implementation would be a
/// stand-in with its own bugs, and this is a claim about bit-identity, which is
/// the one claim a stand-in cannot support.
///
/// ⚠ And the third render is why this test is not circular. If the kernel
/// ignored `dabBounds` altogether, the first two would agree trivially and the
/// suite would report green on a rejection that never happens. Deliberately
/// wrong boxes must therefore *change* the picture.
void testDabBlockRejection() {
    section("Brush dab rejection");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 192, kH = 96;

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
    auto boundsTex = orion::gpu::Texture::create(
        *device, params::kMaxDabBlocks, 1, PixelFormat::RGBA32Float);

    // ⚠ 999, not 1024: a count that is not a multiple of the block size leaves a
    // partial run at the end, and an off-by-one in `min(lo + DAB_BLOCK, count)`
    // would either drop its dabs or read past them into the zero padding — which
    // sits at (0, 0) and would paint a blob in the corner.
    constexpr int kDabs = 999;
    std::vector<float> texels(std::size_t(params::kDabStride)
                              * params::kDabRows * 4, 0.0f);
    for (int i = 0; i < kDabs; ++i) {
        const float t = float(i) / float(kDabs - 1);
        // A self-crossing figure that revisits its own path, so blocks overlap
        // each other and a pixel is genuinely covered by several runs — the
        // case where order decides the answer.
        const float x = 0.5f + 0.42f * std::sin(t * 11.0f);
        const float y = 0.5f + 0.42f * std::sin(t * 7.0f) * std::cos(t * 3.0f);
        texels[std::size_t(i) * 4 + 0] = x;
        texels[std::size_t(i) * 4 + 1] = y;
        // ⚠ Mixed paint and erase, interleaved. Source-over and destination-out
        // do not commute, so a scheme that visited the runs in any other order
        // — or merged them — lands somewhere else entirely. Every seventh dab
        // rubs out, which puts erases inside runs rather than only between them.
        texels[std::size_t(i) * 4 + 2] = (i % 7 == 0) ? 1.0f : 0.0f;
    }
    // ⚠ A few centers off the frame on purpose. Their boxes stick out past the
    // edge, and a rejection written as containment in the picture rather than as
    // the per-dab test's own comparison would trim them.
    for (int i = 0; i < 8; ++i) {
        texels[std::size_t(kDabs - 1 - i) * 4 + 0] = (i % 2) ? -0.05f : 1.05f;
    }
    dabTex->upload(texels.data(),
                   std::size_t(params::kDabStride) * 4 * sizeof(float));

    params::MaskComponent m{};
    m.size[0] = kW; m.size[1] = kH;
    m.kind = 3;
    m.count = kDabs;
    m.dabStride = params::kDabStride;
    m.flow = 0.35f;      // well under 1, so the buildup series is exercised
    m.hardness = 0.5f;   // a soft nib, so the mid-values are real
    m.nibPx = 9.0f;

    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));
    const auto renderWith = [&](const std::vector<float>& bounds) {
        boundsTex->upload(bounds.data(),
                          std::size_t(params::kMaxDabBlocks) * 4 * sizeof(float));
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(),
                              dabTex.get(), boundsTex.get(), &scratchAccum(*device, kW, kH), dst.get()},
                    &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };

    // The oracle: no box can reject, so every dab is visited.
    std::vector<float> wide(std::size_t(params::kMaxDabBlocks) * 4, 0.0f);
    for (int b = 0; b < params::kMaxDabBlocks; ++b) {
        wide[std::size_t(b) * 4 + 0] = -1e9f;
        wide[std::size_t(b) * 4 + 1] = -1e9f;
        wide[std::size_t(b) * 4 + 2] =  1e9f;
        wide[std::size_t(b) * 4 + 3] =  1e9f;
    }
    const auto unaccelerated = renderWith(wide);

    std::vector<float> real(std::size_t(params::kMaxDabBlocks) * 4, 0.0f);
    params::buildDabBounds(texels.data(), kDabs, real.data());
    const auto accelerated = renderWith(real);

    int differing = 0;
    float worst = 0.0f;
    for (std::size_t i = 0; i < unaccelerated.size(); ++i) {
        const float a = float(unaccelerated[i]), b = float(accelerated[i]);
        if (a != b) { ++differing; worst = std::max(worst, std::abs(a - b)); }
    }
    report(differing == 0,
           "999 mixed paint and erase dabs render bit-identically with rejection on",
           differing == 0 ? ""
                          : std::to_string(differing) + " texels differ, worst "
                                + std::to_string(worst));

    // The positive control. Boxes far from every dab must reject everything, so
    // the frame comes back empty — which is only true if the kernel reads them.
    std::vector<float> nowhere(std::size_t(params::kMaxDabBlocks) * 4, 0.0f);
    for (int b = 0; b < params::kMaxDabBlocks; ++b) {
        nowhere[std::size_t(b) * 4 + 0] = 50.0f;
        nowhere[std::size_t(b) * 4 + 1] = 50.0f;
        nowhere[std::size_t(b) * 4 + 2] = 51.0f;
        nowhere[std::size_t(b) * 4 + 3] = 51.0f;
    }
    const auto rejected = renderWith(nowhere);
    float mostCoverage = 0.0f, paintedBefore = 0.0f;
    for (std::size_t i = 0; i < rejected.size(); ++i) {
        mostCoverage = std::max(mostCoverage, float(rejected[i]));
        paintedBefore = std::max(paintedBefore, float(unaccelerated[i]));
    }
    report(paintedBefore > 0.5f,
           "the stroke covers the frame at all, so the check above is not vacuous",
           "peak coverage " + std::to_string(paintedBefore));
    report(mostCoverage == 0.0f,
           "boxes away from the stroke reject every dab, so the rejection is live",
           "peak coverage " + std::to_string(mostCoverage));
}

void testLongBrushStroke() {
    section("Long brush strokes");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 256, kH = 64;

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

    // A stroke straight across the frame, 400 dabs — comfortably past the old
    // 256 and past the end of the texture's first row, which is the other
    // thing that could go wrong now that the list is two-dimensional.
    constexpr int kDabs = 400;
    {
        std::vector<float> texels(std::size_t(params::kDabStride)
                                  * params::kDabRows * 4, 0.0f);
        for (int i = 0; i < kDabs; ++i) {
            texels[std::size_t(i) * 4 + 0] =
                0.02f + 0.96f * float(i) / float(kDabs - 1);
            texels[std::size_t(i) * 4 + 1] = 0.5f;
        }
        dabTex->upload(texels.data(),
                       std::size_t(params::kDabStride) * 4 * sizeof(float));
        uploadDabBounds(texels);
    }

    params::MaskComponent b{};
    b.size[0] = kW; b.size[1] = kH;
    b.kind = 3;
    b.dabStride = params::kDabStride;
    b.count = kDabs;
    b.flow = 1.0f;
    b.hardness = 0.5f;
    b.nibPx = 4.0f;

    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));
    src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dabTex.get(),
                          dabBoundsTex.get(), &scratchAccum(*device, kW, kH), dst.get()}, &b, sizeof b, kW, kH);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(kW) * kH);
    dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
    const auto at = [&](int x) {
        return double(out[std::size_t(kH / 2) * kW + std::size_t(x)]);
    };

    // ⚠ The far end. Dab 255 lands around x = 0.02 + 0.96*255/399 = 0.63, so
    // anything past about column 165 is only covered by dabs the old build
    // threw away.
    report(at(int(0.95 * kW)) > 0.9,
           "the last dab of a 400-dab stroke is painted",
           std::to_string(at(int(0.95 * kW))));
    report(at(int(0.75 * kW)) > 0.9,
           "and so is everything past the old 256-dab cap",
           std::to_string(at(int(0.75 * kW))));

    // The near end is still there — a fix that dropped the *start* instead
    // would satisfy the two checks above.
    report(at(int(0.05 * kW)) > 0.9, "with the start of the stroke intact",
           std::to_string(at(int(0.05 * kW))));

    // And nothing outside it.
    report(at(0) < 0.5 && at(int(kW) - 1) < 0.5,
           "and the frame beyond either end left alone",
           std::to_string(at(0)) + ", " + std::to_string(at(int(kW) - 1)));

    // ⚠ Rows past the first are read correctly. With `count` at 300 the stroke
    // spans two rows of the dab texture, and an index that ignored the stride
    // would wrap back onto row zero and repaint the beginning.
    {
        auto c = b;
        c.count = 300;
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb2(*device);
        cb2.dispatch(*kernel, {src.get(), reference.get(), matte.get(),
                               dabTex.get(), dabBoundsTex.get(), &scratchAccum(*device, kW, kH), dst.get()}, &c, sizeof c, kW, kH);
        cb2.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);

        // Dab 299 sits at 0.02 + 0.96*299/399 = 0.74; dab 300 onward is not
        // drawn, so the stroke must stop there rather than reaching the end.
        report(at(int(0.70 * kW)) > 0.9 && at(int(0.90 * kW)) < 0.5,
               "a stroke spanning two rows of the texture stops where its "
               "count says",
               std::to_string(at(int(0.70 * kW))) + ", "
               + std::to_string(at(int(0.90 * kW))));
    }
}

// Erasing with the brush — the other half of a brush.
//
// A component accumulates every stroke ever laid on it, so the polarity has to
// travel with the *dab* rather than with the stroke. It rides in the aux
// texture's third channel.
//
// ⚠ The property worth pinning is that paint and erase are **exact inverses**.
// Erase is destination-out — `a -= cov*a` — against paint's source-over
// `a += cov*(1-a)`, so painting takes the alpha a fraction of the way to one
// and erasing takes it the same fraction of the way to zero. Subtracting the
// coverage outright would drive the alpha negative wherever a slow hand
// lingered, and the saturate at the end would hide that as a hard hole in a
// soft brush.
void testBrushErase() {
    section("Brush erase (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 128, kH = 32;

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
                                              params::kDabRows,
                                              PixelFormat::RGBA32Float);
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

    /// Uploads a list of (x, erase) dabs, all at mid-height.
    const auto setDabs = [&](const std::vector<std::pair<float, bool>>& pts) {
        std::vector<float> texels(std::size_t(params::kDabStride)
                                  * params::kDabRows * 4, 0.0f);
        for (std::size_t i = 0; i < pts.size(); ++i) {
            texels[i * 4 + 0] = pts[i].first;
            texels[i * 4 + 1] = 0.5f;
            texels[i * 4 + 2] = pts[i].second ? 1.0f : 0.0f;
        }
        dabTex->upload(texels.data(),
                       std::size_t(params::kDabStride) * 4 * sizeof(float));
        uploadDabBounds(texels);
    };

    params::MaskComponent m{};
    m.size[0] = kW; m.size[1] = kH;
    m.kind = 3;
    m.dabStride = params::kDabStride;
    m.hardness = 1.0f;
    m.nibPx = 12.0f;

    const auto run = [&](int count) {
        m.count = count;
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(),
                              dabTex.get(), dabBoundsTex.get(), &scratchAccum(*device, kW, kH), dst.get()}, &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x) {
        return double(a[std::size_t(kH / 2) * kW + std::size_t(x)]);
    };

    // ── A full-flow erase over a full-flow paint clears it ────────────────
    {
        m.flow = 1.0f;
        setDabs({{0.5f, false}});
        const auto painted = run(1);
        report(at(painted, kW / 2) > 0.999, "a dab at full flow covers fully",
               std::to_string(at(painted, kW / 2)));

        setDabs({{0.5f, true}, {0.5f, false}});
        // ⚠ Order matters and this is the wrong way round on purpose: the erase
        // is listed *first*, so it runs against nothing and the paint that
        // follows is what stands. A kernel that applied the erase after — or
        // that ignored order — would leave this empty.
        const auto eraseThenPaint = run(2);
        report(at(eraseThenPaint, kW / 2) > 0.999,
               "dabs apply in listed order, so an erase before a paint is a no-op",
               std::to_string(at(eraseThenPaint, kW / 2)));

        setDabs({{0.5f, false}, {0.5f, true}});
        const auto paintThenErase = run(2);
        report(at(paintThenErase, kW / 2) < 1e-3,
               "and a full erase over a full paint takes it back to nothing",
               std::to_string(at(paintThenErase, kW / 2)));
    }

    // ── ⚠ Paint and erase are exact inverses at partial flow ──────────────
    //
    // Source-over then destination-out at the same flow f leaves f(1-f)... no:
    // paint gives a = f, erase gives a = f - f*f = f(1-f). The point is that it
    // is *reachable and bounded*, never negative, and that repeating the erase
    // converges to zero geometrically rather than jumping there.
    {
        m.flow = 0.5f;
        setDabs({{0.5f, false}});
        const double one = at(run(1), kW / 2);
        report(std::abs(one - 0.5) < 2e-3, "half flow lays down half",
               std::to_string(one));

        setDabs({{0.5f, false}, {0.5f, true}});
        const double erased = at(run(2), kW / 2);
        report(std::abs(erased - 0.25) < 2e-3,
               "and half-erasing it leaves a quarter — destination-out, not a "
               "subtraction",
               std::to_string(erased));

        // Four more erases: geometric decay toward zero, never past it.
        setDabs({{0.5f, false}, {0.5f, true}, {0.5f, true}, {0.5f, true},
                 {0.5f, true}, {0.5f, true}});
        const double faded = at(run(6), kW / 2);
        report(faded > 0.0 && faded < 0.02,
               "repeated erasing decays toward zero and never below it",
               std::to_string(faded));
    }

    // ── Erasing only where it was applied ─────────────────────────────────
    //
    // The two-sided half: an erase is a dab, so it has the nib's extent and
    // nothing outside it may move.
    {
        m.flow = 1.0f;
        setDabs({{0.25f, false}, {0.75f, false}});
        const auto both = run(2);
        setDabs({{0.25f, false}, {0.75f, false}, {0.75f, true}});
        const auto oneGone = run(3);
        report(at(oneGone, kW / 4) > 0.999 && at(both, kW / 4) > 0.999,
               "erasing one dab leaves the other exactly as it was",
               std::to_string(at(oneGone, kW / 4)));
        report(at(oneGone, kW * 3 / 4) < 1e-3,
               "and removes the one it was aimed at",
               std::to_string(at(oneGone, kW * 3 / 4)));
    }
}


// ── The incremental brush predicate ──────────────────────────────────────────
//
// `research/brush-acceleration.md` and ROADMAP's "Incremental brush
// accumulation", session one. Painting appends dabs and every appended dab
// re-runs the loop over every dab laid so far, so a stroke costs
// `Σ over blocks of (pixels in that block's box) × 64` per pointer event and
// **the block count is what grows**: 49 → 294 dabs is 2.65 → 34.88 ms on the
// full graph. The fix is to keep the coverage and composite only the new dabs,
// and the question that has to be answered before any of that is *how much of
// this stroke did I already draw*.
//
// ⚠ **The predicate is shipped a session before the accumulator, deliberately.**
// Its failure mode is not a crash or a smear: it is a stale coverage rendering a
// completely plausible brushstroke. Every screenshot passes. Only a check that
// returns the dab count to a value it has already had, with a different prefix
// behind it, can see the difference.
//
// The unit half. `testBrushPrefixWiring` is the same predicate through the real
// pipeline, where the texels come out of `mask::toFrame` rather than out of this
// file.
void testBrushPrefixPredicate() {
    section("Brush prefix predicate");

    namespace params = orion::pipe::params;

    // The nib as `apply` computes it — one radius for the whole stroke.
    const params::BrushShape shape{3, 4.8f, 1.0f, 0.7f};

    // Texels in the layout the dab texture actually carries: four floats a dab,
    // `(x, y, erasing, 0)`, post-transform.
    const auto line = [](int count) {
        std::vector<float> t(std::size_t(count) * 4, 0.0f);
        for (int i = 0; i < count; ++i) {
            t[std::size_t(i) * 4 + 0] = 0.06f + 0.005f * float(i);
            t[std::size_t(i) * 4 + 1] = 0.06f + 0.004f * float(i);
        }
        return t;
    };
    const auto stored = [](const std::vector<float>& t,
                           const params::BrushShape& s) {
        params::BrushPrefixState p;
        p.texels = t;
        p.count  = int(t.size() / 4);
        p.shape  = s;
        p.live   = true;
        return p;
    };

    // 1. Nothing has been uploaded, so nothing is unchanged. The safe answer,
    //    and the one that makes a first stroke a full evaluation.
    {
        const params::BrushPrefixState cold;
        const auto t = line(12);
        report(params::unchangedPrefix(cold, shape, t.data(), 12) == 0,
               "a component with nothing uploaded has no unchanged prefix");
        report(params::unchangedPrefix(stored(line(80), shape), shape,
                                       nullptr, 80) == 0,
               "and neither does a null stroke");
    }

    // 2. **The append**, which is the only way a real stroke grows: dab spacing
    //    is fixed by the nib, so a longer stroke is a longer path.
    //
    // ⚠ This is the check that a predicate returning 0 forever has to fail. A
    // predicate that never fires is trivially correct and completely useless,
    // and session two would then be a no-op that passes everything.
    {
        const auto prev = stored(line(80), shape);
        const auto next = line(160);
        const int p = params::unchangedPrefix(prev, shape, next.data(), 160);
        report(p == 80,
               "appending 80 dabs leaves the first 80 as an unchanged prefix",
               std::to_string(p));
    }

    // 3. ⚠ **THE TRAP.** Undo three dabs, paint three different ones. The count
    //    is exactly what it was, and the cheap predicate — "the stroke did not
    //    shrink, so what came before it is still there" — hands back all 160 and
    //    the accumulator keeps coverage belonging to dabs the photographer took
    //    back. What renders is a brushstroke. It is just not theirs.
    {
        const auto prev = stored(line(160), shape);
        auto next = line(160);
        for (int i = 157; i < 160; ++i) next[std::size_t(i) * 4 + 1] += 0.02f;
        const int p = params::unchangedPrefix(prev, shape, next.data(), 160);
        report(p == 157,
               "undo three dabs and paint three different ones: the prefix is 157",
               std::to_string(p));
        report(p != 160,
               "and the count, which never moved, is evidence of nothing");
    }

    // 4. The same three centers, flipped from paint to erase. Source-over and
    //    destination-out do not commute, so this is a different picture from the
    //    same coordinates — and the flag rides in the texel's `z`, which is why
    //    the comparison is over all four floats and not over the pair.
    {
        const auto prev = stored(line(160), shape);
        auto next = line(160);
        for (int i = 157; i < 160; ++i) next[std::size_t(i) * 4 + 2] = 1.0f;
        const int p = params::unchangedPrefix(prev, shape, next.data(), 160);
        report(p == 157,
               "flipping three dabs to erase ends the prefix, every center held",
               std::to_string(p));
    }

    // 5. An undo on its own. The survivors are still a prefix, and the answer is
    //    capped by the shorter of the two strokes rather than by the stored one.
    {
        const auto prev = stored(line(160), shape);
        const auto next = line(120);
        const int p = params::unchangedPrefix(prev, shape, next.data(), 120);
        report(p == 120, "undoing forty dabs leaves the surviving 120",
               std::to_string(p));
    }

    // 6. Identical centers are not enough. One radius covers the whole stroke,
    //    so widening the nib re-lays every dab that was already down.
    {
        const auto prev = stored(line(160), shape);
        const auto next = line(160);
        report(params::unchangedPrefix(prev, shape, next.data(), 160) == 160,
               "an untouched stroke is unchanged all the way through");

        params::BrushShape wider = shape;    wider.nibPx *= 2.0f;
        params::BrushShape thinner = shape;  thinner.flow = 0.25f;
        params::BrushShape harder = shape;   harder.hardness = 1.0f;
        params::BrushShape gradient = shape; gradient.kind = 2;
        report(params::unchangedPrefix(prev, wider, next.data(), 160) == 0,
               "a wider nib re-lays all of it");
        report(params::unchangedPrefix(prev, thinner, next.data(), 160) == 0,
               "and so does the flow");
        report(params::unchangedPrefix(prev, harder, next.data(), 160) == 0,
               "and the hardness");
        report(params::unchangedPrefix(prev, gradient, next.data(), 160) == 0,
               "and a component that has stopped being a brush at all");
    }

    // 7. The finest change a float can carry, in the middle of the stroke. The
    //    coverage it produces is invisible; the prefix is not allowed to be.
    {
        const auto prev = stored(line(160), shape);
        auto next = line(160);
        next[40 * 4] = std::nextafter(next[40 * 4], 1.0f);
        const int p = params::unchangedPrefix(prev, shape, next.data(), 160);
        report(p == 40, "one ulp on dab 40 ends the prefix at 40",
               std::to_string(p));
    }
}

/// The predicate through the real `DevelopPipeline`, where the texels are
/// whatever `mask::toFrame` produced and the strokes arrive the way the app
/// sends them.
///
/// ⚠ The unit test above cannot see the two things this one exists for: that
/// the predicate is fed the **post-transform** texels, and that it is asked at
/// all. `BrushPrefixStat::evaluations` is what makes the second checkable —
/// `apply` skips a component whose edit did not change, so an answer left over
/// from an earlier event reads exactly like a fast path that was taken.
void testBrushPrefixWiring() {
    section("Brush prefix wiring");

    namespace pipe = orion::pipe;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    // A ramp in both axes: a flat patch would be bit-identical under a stroke
    // that landed anywhere at all, which would make the pixel comparison at the
    // end a formality.
    orion::raw::BayerImage img;
    img.width = 96;
    img.height = 96;
    img.samples.resize(std::size_t(96) * 96);
    for (std::uint32_t y = 0; y < 96; ++y) {
        for (std::uint32_t x = 0; x < 96; ++x) {
            img.samples[std::size_t(y) * 96 + x] =
                static_cast<std::uint16_t>(200 + x * 20 + y * 15);
        }
    }
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
    const auto frame = [&] {
        std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        return px;
    };

    // Dab centers in **displayed** coordinates, which is what the app sends and
    // what `apply` runs through `mask::toFrame`. A diagonal, so the transform is
    // exercised in both axes.
    const auto line = [](int count) {
        std::vector<float> xy(std::size_t(count) * 2, 0.0f);
        for (int i = 0; i < count; ++i) {
            xy[std::size_t(i) * 2 + 0] = 0.06f + 0.005f * float(i);
            xy[std::size_t(i) * 2 + 1] = 0.06f + 0.004f * float(i);
        }
        return xy;
    };

    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};
    auto& c = adj.maskComponents[0];
    c.kind = 3;
    c.brushRadius = 0.05f;
    c.brushFlow = 1.0f;
    c.brushHardness = 0.7f;
    adj.maskCount = 1;
    adj.layers[0].exposureEv = 2.0f;

    // ── The first pointer event ────────────────────────────────────────────
    const auto first = line(80);
    dev->setBrushStroke(0, first.data(), nullptr, 80);
    c.brushRevision = 1;
    dev->apply(adj);
    dev->render();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.evaluations == 1 && s.prefix == 0,
               "the first upload has nothing to compare itself against",
               std::to_string(s.evaluations) + " evals, prefix "
               + std::to_string(s.prefix));
    }

    // ── The append, which is what painting does ────────────────────────────
    //
    // ⚠ **The fast path has to be taken, not merely available.** A predicate
    // that answers 0 forever passes every correctness check in this file and
    // makes session two a no-op, so this asserts the number and asserts that the
    // predicate was the thing that produced it.
    const auto appendedStroke = line(160);
    dev->setBrushStroke(0, appendedStroke.data(), nullptr, 160);
    c.brushRevision = 2;
    dev->apply(adj);
    dev->render();
    const auto appended = frame();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.evaluations == 2,
               "the predicate ran on the append rather than leaving its last answer",
               std::to_string(s.evaluations) + " evaluations");
        report(s.prefix == 80 && s.previousCount == 80 && s.count == 160,
               "and 80 of the 160 dabs are the stroke already on the GPU",
               "prefix " + std::to_string(s.prefix) + " of "
               + std::to_string(s.count) + ", was " + std::to_string(s.previousCount));
    }

    // ── ⚠ Undo three dabs, paint three different ones ──────────────────────
    //
    // The count returns to a value it has already had. Nothing about the size of
    // the stroke has changed, and three dabs of it are somewhere else.
    auto repaint = line(160);
    for (int i = 157; i < 160; ++i) repaint[std::size_t(i) * 2 + 1] += 0.02f;
    dev->setBrushStroke(0, repaint.data(), nullptr, 160);
    c.brushRevision = 3;
    dev->apply(adj);
    dev->render();
    const auto repainted = frame();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.count == 160 && s.previousCount == 160,
               "undo three and repaint three: the dab count is exactly where it was",
               std::to_string(s.previousCount) + " -> " + std::to_string(s.count));
        report(s.prefix == 157,
               "and the prefix stops at 157, which is the only thing that can tell",
               std::to_string(s.prefix));
        // ⚠ Without this the case above is a fixture that could not have failed:
        // three dabs moved somewhere the picture cannot see would be a stale
        // accumulator nobody would mind.
        report(repainted != appended,
               "and the repaint is a different photograph, so the stale coverage "
               "would have been wrong on screen");
    }

    // ── Paint, then erase over the same three centers ──────────────────────
    std::vector<float> erase(160, 0.0f);
    for (int i = 157; i < 160; ++i) erase[std::size_t(i)] = 1.0f;
    dev->setBrushStroke(0, repaint.data(), erase.data(), 160);
    c.brushRevision = 4;
    dev->apply(adj);
    dev->render();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.prefix == 157,
               "flipping three dabs from paint to erase ends the prefix at 157, "
               "with every center exactly where it was",
               std::to_string(s.prefix));
        report(frame() != repainted,
               "and destination-out is a different picture from source-over");
    }

    // ── ⚠ The geometry moves and the stroke does not — literally, now ──────
    //
    // `setBrushStroke` is not called and `brushRevision` does not move. The
    // stored dabs are **frame coordinates**, anchored at the gesture, so a
    // straighten changes no center: the stroke is not re-walked, not
    // re-uploaded, and the predicate is not even asked. When storage was
    // display-space this same straighten re-uploaded all 160 dabs and reset
    // the prefix to zero — per tick of the drag — which is both the cost and
    // the mask-slides-off-its-subject bug that frame storage removed.
    adj.straightenDeg = 3.0f;
    dev->apply(adj);
    dev->render();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.evaluations == 4,
               "a straighten no longer touches the stroke at all",
               std::to_string(s.evaluations) + " evaluations");
        report(s.prefix == 157,
               "and the accumulator's record survives the geometry untouched",
               std::to_string(s.prefix));
    }
    adj.straightenDeg = 0.0f;

    // ── ⚠ Session one moves no pixel ───────────────────────────────────────
    //
    // A reload throws the record away — a different photograph through the same
    // graph shares nothing with the stroke that was on it — so the same 160 dabs
    // go up as one upload with no prefix behind them. That render and the one
    // built by appending have to be the same bytes.
    dev->reload(img);
    dev->setBrushStroke(0, appendedStroke.data(), nullptr, 160);
    c.brushRevision = 1;
    dev->apply(adj);
    dev->render();
    const auto whole = frame();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.evaluations == 1 && s.prefix == 0,
               "a reload forgets what was uploaded, stat and texels together",
               std::to_string(s.evaluations) + " evals, prefix "
               + std::to_string(s.prefix));
        report(whole == appended,
               "and a stroke built by appending is bit-identical to the same "
               "stroke uploaded whole");
    }

    // And the same component ceasing to be a brush forgets it too, because the
    // accumulator session two hangs on this will not have survived that.
    c.kind = 2;
    c.brushRevision = 2;
    dev->apply(adj);
    dev->render();
    c.kind = 3;
    dev->apply(adj);
    dev->render();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.prefix == 0,
               "a component that stopped being a brush and came back has no prefix",
               std::to_string(s.prefix));
    }
}


// A luminance range mask — research/masking.md §4b.
//
// A band on what a pixel *is*. Three decisions to pin, and the shader's own
// comments name all three: it reads the reference image, it measures in stops,
// and its two edges are independent.

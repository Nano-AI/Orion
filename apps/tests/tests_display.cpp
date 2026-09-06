// The display transform, driven straight at `developDisplay`.
//
// One fixture, shared: a scene-linear ramp in a one-row RGBA16Float texture, a
// curve LUT, a creative-LUT cube, and a `params::Display` block dispatched over
// them. Both checks here read that same dispatch — output depth reads its
// dynamic range, the LUT checks read the cube it always binds, and the first
// already has to know about the second's texture or every binding after it
// shifts by one. Split out of `tests_effects.cpp` 2026-08-02; see decision #127.
//
// A new check on the display transform belongs here, next to the frame it needs.

#include "harness.h"

void testOutputDepth() {
    section("Output depth");

    constexpr std::uint32_t kN = 512;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developDisplay.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developDisplay");

    auto src = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto lut = orion::gpu::Texture::create(*device, orion::pipe::kCurveResolution,
                                           orion::pipe::kCurveRows,
                                           orion::gpu::PixelFormat::R32Float);
    const auto identity = orion::pipe::buildCurveLut({});
    lut->upload(identity.data(), orion::pipe::kCurveResolution * sizeof(float));

    // A gradient across a narrow slice of scene-linear values, so the *output*
    // steps are far finer than 1/255.
    std::vector<__fp16> input(std::size_t(kN) * 4);
    for (std::uint32_t i = 0; i < kN; ++i) {
        const float v = 0.180f + 0.004f * (float(i) / float(kN - 1));
        input[i * 4 + 0] = static_cast<__fp16>(v);
        input[i * 4 + 1] = static_cast<__fp16>(v);
        input[i * 4 + 2] = static_cast<__fp16>(v);
        input[i * 4 + 3] = 1;
    }
    src->upload(input.data(), std::size_t(kN) * 4 * sizeof(__fp16));

    orion::pipe::params::Display dp{};
    dp.contrast = 1.0f;
    dp.curveIdentity = 1;
    dp.resolution = orion::pipe::kCurveResolution;
    dp.size[0] = kN;
    dp.size[1] = 1;

    orion::gpu::CommandBuffer cb(*device);
    // The display kernel binds the creative LUT after the curve LUT. It is
    // never sampled here — lutSize stays zero — but the binding has to exist or
    // every texture after it shifts by one, which is silent and total.
    auto cubeStub = orion::gpu::Texture::create(*device, 2, 4,
                                                orion::gpu::PixelFormat::RGBA32Float);
    cb.dispatch(*kernel, {src.get(), lut.get(), cubeStub.get(), dst.get()},
                &dp, sizeof dp, kN, 1);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(kN) * 4);
    dst->download(out.data(), std::size_t(kN) * 4 * sizeof(__fp16), kN, 1);

    // How many distinct green values came back. Eight bits across this range
    // could give only a handful.
    std::vector<float> values;
    values.reserve(kN);
    for (std::uint32_t i = 0; i < kN; ++i) values.push_back(float(out[i * 4 + 1]));

    std::sort(values.begin(), values.end());
    const auto last = std::unique(values.begin(), values.end());
    const auto distinct = static_cast<std::size_t>(std::distance(values.begin(), last));

    const double span = double(values.back() - values.front());
    const double eightBitSteps = span * 255.0;

    report(span > 0.0, "the gradient produced a range at all");
    report(distinct > eightBitSteps * 4.0,
           "the output resolves far finer than eight bits could",
           std::to_string(distinct) + " distinct values across "
               + std::to_string(eightBitSteps) + " eight-bit steps");

    // And it must be monotone: a format mismatch shows up as noise, not as a
    // smooth ramp.
    bool monotone = true;
    for (std::uint32_t i = 1; i < kN; ++i) {
        if (float(out[i * 4 + 1]) < float(out[(i - 1) * 4 + 1]) - 1e-5f) {
            monotone = false;
            break;
        }
    }
    report(monotone, "the output ramp is monotone");
}

/// Reading .cube files, and applying them tetrahedrally.
void testCreativeLut() {
    section("Creative LUTs");

    using orion::pipe::parseCube;

    // ── The parser ────────────────────────────────────────────────────────
    {
        // Deliberately awkward: CRLF endings, a comment mid-file, blank lines,
        // a quoted title, mixed-case keywords, and leading whitespace. Every
        // one of these appears in LUTs people actually download.
        const std::string text =
            "# a comment\r\n"
            "TITLE \"Test Look\"\r\n"
            "\r\n"
            "  lut_3d_size 2\r\n"
            "DOMAIN_MIN 0 0 0\r\n"
            "DOMAIN_MAX 1 1 1\r\n"
            "0 0 0\r\n"          // r=0 g=0 b=0
            "1 0 0\r\n"          // r=1 g=0 b=0   <- red varies fastest
            "0 1 0\r\n"
            "1 1 0\r\n"
            "0 0 1\r\n"
            "1 0 1\r\n"
            "0 1 1\r\n"
            "1 1 1\r\n"
            "# and a trailing comment line\r\n";

        const auto r = parseCube(text);
        report(r.ok, "a well-formed 3D cube parses", r.error);
        report(r.lut.size == 2, "LUT_3D_SIZE is read",
               std::to_string(r.lut.size));
        report(r.lut.title == "Test Look", "TITLE loses its quotes", r.lut.title);

        // The ordering claim. Entry 1 is (r=1, g=0, b=0), which is only true if
        // red varies fastest. Getting this backwards swaps red and blue in
        // every LUT the product ever loads, and it would look plausible.
        report(r.ok && r.lut.data.size() == 24 &&
               r.lut.data[3] == 1.0f && r.lut.data[4] == 0.0f && r.lut.data[5] == 0.0f,
               "red varies fastest in the data block",
               r.ok ? std::to_string(r.lut.data[3]) + "," +
                      std::to_string(r.lut.data[4]) + "," +
                      std::to_string(r.lut.data[5]) : r.error);
    }
    {
        // Comments in this format are whole lines, not trailing text
        // (specification section 5.8) — and a look really can be called
        // "Look #3". A mid-line rule truncates that to "Look" and never says
        // so. This is the case that was written wrong first.
        std::string text = "TITLE \"Look #3\"\nLUT_3D_SIZE 2\n";
        for (int i = 0; i < 8; ++i) text += "0 0 0\n";
        const auto r = parseCube(text);
        report(r.ok && r.lut.title == "Look #3",
               "a hash inside a title is part of the title",
               r.ok ? r.lut.title : r.error);
    }
    {
        const auto r = parseCube("LUT_3D_SIZE 2\n0 0 0\n1 1 1\n");
        report(!r.ok, "a short data block is refused", r.error);
        report(r.error.find("needs 8 rows") != std::string::npos,
               "and the message says how many rows were expected", r.error);
    }
    {
        const auto r = parseCube("0 0 0\n1 1 1\n");
        report(!r.ok, "a file with no size keyword is refused", r.error);
    }
    {
        const auto r = parseCube("LUT_3D_SIZE 2\nLUT_1D_SIZE 4\n0 0 0\n");
        report(!r.ok, "declaring both 1D and 3D is refused", r.error);
    }
    {
        const auto r = parseCube("LUT_3D_SIZE 2\n0 0 0\n1 1 nonsense\n");
        report(!r.ok && r.error.find("line 3") != std::string::npos,
               "a bad number is refused, with its line number", r.error);
    }
    {
        // A 1D LUT is a separable 3D LUT; it is lifted onto the grid so there
        // is one code path downstream. Halving red, leaving green and blue.
        const auto r = parseCube("LUT_1D_SIZE 2\n0 0 0\n0.5 1 1\n");
        report(r.ok && r.lut.wasOneDimensional, "a 1D cube is lifted onto the grid",
               r.error);
        if (r.ok) {
            // The grid corner at r=1, g=1, b=1 must read (0.5, 1, 1).
            const int n = r.lut.size;
            const std::size_t last =
                ((static_cast<std::size_t>(n - 1) * n + (n - 1)) * n + (n - 1)) * 3;
            report(std::abs(r.lut.data[last] - 0.5f) < 1e-5f &&
                   std::abs(r.lut.data[last + 1] - 1.0f) < 1e-5f,
                   "and the lift preserves the curve at the grid corners",
                   std::to_string(r.lut.data[last]));
        }
    }

    // ── Tetrahedral, and not trilinear ────────────────────────────────────
    //
    // These two agree on any linear function, so a LUT that does something
    // gentle cannot tell them apart — which is exactly why "it looks right" is
    // not evidence here. This table is zero at every corner except (1,1,1),
    // where the two interpolations disagree enormously: at a sample inside the
    // cell, tetrahedral returns the smallest fractional coordinate and
    // trilinear returns the product of all three.
    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developDisplay.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developDisplay");

    constexpr std::uint32_t kN = 64;
    auto src  = orion::gpu::Texture::create(*device, kN, 1, orion::gpu::PixelFormat::RGBA16Float);
    auto curve = orion::gpu::Texture::create(*device, 256, 4, orion::gpu::PixelFormat::R32Float);
    auto cube = orion::gpu::Texture::create(*device, orion::pipe::kMaxCubeSize,
                                            orion::pipe::kMaxCubeSize * orion::pipe::kMaxCubeSize,
                                            orion::gpu::PixelFormat::RGBA32Float);
    auto dst  = orion::gpu::Texture::create(*device, kN, 1, orion::gpu::PixelFormat::RGBA8Unorm);

    // A scene-linear sweep, so the display values the LUT sees cover the range.
    std::vector<__fp16> in(std::size_t(kN) * 4);
    for (std::uint32_t x = 0; x < kN; ++x) {
        const double v = 0.002 * std::pow(1500.0, x / double(kN - 1));
        in[x * 4 + 0] = __fp16(v);
        in[x * 4 + 1] = __fp16(v * 0.82);
        in[x * 4 + 2] = __fp16(v * 0.65);
        in[x * 4 + 3] = __fp16(1.0f);
    }
    src->upload(in.data(), std::size_t(kN) * 4 * sizeof(float) / 2);

    std::vector<float> curveData(256 * 4, 0.0f);
    curve->upload(curveData.data(), 256 * sizeof(float));

    constexpr int kGrid = 2;
    std::vector<float> grid(std::size_t(orion::pipe::kMaxCubeSize) *
                            orion::pipe::kMaxCubeSize * orion::pipe::kMaxCubeSize * 4, 0.0f);
    // Row is b * grid + g — the LUT's own edge, which is what the shader
    // recomputes; the texture is only wide enough to hold the largest grid.
    const auto gridAt = [&](int r, int g, int b) -> std::size_t {
        const std::size_t row = std::size_t(b) * kGrid + g;
        return (row * orion::pipe::kMaxCubeSize + r) * 4;
    };
    const std::size_t top = gridAt(1, 1, 1);
    grid[top + 0] = grid[top + 1] = grid[top + 2] = 1.0f;
    cube->upload(grid.data(), std::size_t(orion::pipe::kMaxCubeSize) * 4 * sizeof(float));

    const auto run = [&](std::uint32_t lutSize, float strength) {
        orion::pipe::params::Display p{};
        p.contrast = 1.0f;
        p.curveIdentity = 1u;
        p.resolution = 256u;
        p.size[0] = kN; p.size[1] = 1;
        p.dither = 0u;                 // noise would swamp the comparison
        p.lutSize = lutSize;
        p.lutStrength = strength;
        p.lutMin[0] = p.lutMin[1] = p.lutMin[2] = 0.0f;
        p.lutMax[0] = p.lutMax[1] = p.lutMax[2] = 1.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), curve.get(), cube.get(), dst.get()},
                    &p, sizeof p, kN, 1);
        cb.commitAndWait();
        std::vector<std::uint8_t> out(std::size_t(kN) * 4);
        dst->download(out.data(), std::size_t(kN) * 4, kN, 1);
        return out;
    };

    // What the display transform produces without any LUT — the values the
    // lookup is indexed by. Measured rather than predicted, because AgX is not
    // something to re-derive in a test.
    const auto plain = run(0u, 1.0f);
    const auto looked = run(kGrid, 1.0f);

    double worstTetra = 0.0, worstTriBest = 1e30;
    for (std::uint32_t x = 0; x < kN; ++x) {
        const double fr = plain[x * 4 + 0] / 255.0;
        const double fg = plain[x * 4 + 1] / 255.0;
        const double fb = plain[x * 4 + 2] / 255.0;

        // Only c111 is nonzero, so tetrahedral reduces to the smallest of the
        // three fractions and trilinear to their product.
        const double tetra = std::min({fr, fg, fb});
        const double tri   = fr * fg * fb;

        const double got = looked[x * 4 + 0] / 255.0;
        worstTetra = std::max(worstTetra, std::abs(got - tetra));
        worstTriBest = std::min(worstTriBest, std::abs(got - tri));
    }

    report(worstTetra < 0.02,
           "the lookup is tetrahedral, matching the simplex the sample is in",
           "worst " + std::to_string(worstTetra));
    report(worstTriBest > 0.02 || worstTetra < worstTriBest,
           "and is measurably not trilinear, which the two agree would differ",
           "tetrahedral " + std::to_string(worstTetra));

    // Strength zero has to be exactly the untouched picture, so that loading a
    // LUT and dialling it out is not a slightly different image.
    const auto off = run(kGrid, 0.0f);
    int differing = 0;
    for (std::size_t i = 0; i < off.size(); ++i) if (off[i] != plain[i]) ++differing;
    report(differing == 0, "strength zero is byte-identical to no LUT at all",
           std::to_string(differing) + " bytes differ");

    // An identity LUT must be the identity, which is the check that catches the
    // packing being wrong: a transposed or mis-strided grid still looks like a
    // plausible look, and only an identity table makes it obvious.
    std::fill(grid.begin(), grid.end(), 0.0f);
    for (int b = 0; b < kGrid; ++b)
        for (int g = 0; g < kGrid; ++g)
            for (int r = 0; r < kGrid; ++r) {
                const std::size_t o = gridAt(r, g, b);
                grid[o + 0] = float(r); grid[o + 1] = float(g); grid[o + 2] = float(b);
            }
    cube->upload(grid.data(), std::size_t(orion::pipe::kMaxCubeSize) * 4 * sizeof(float));

    const auto identity = run(kGrid, 1.0f);
    int worstId = 0;
    for (std::size_t i = 0; i < identity.size(); ++i) {
        worstId = std::max(worstId, std::abs(int(identity[i]) - int(plain[i])));
    }
    report(worstId <= 1, "an identity LUT leaves every pixel where it was",
           "worst " + std::to_string(worstId) + "/255");
}

/// Middle gray is the anchor, and the toe reaches black where it says it does.
///
/// ⚠ **Written because the display transform's shadow end had no assertion at
/// all.** Shortening AgX's latitude — the fix for a default that rendered the
/// darkest patch of a photograph 1.63x brighter than macOS ImageIO's decode of
/// the same raw — moved every shadow in the program, and all 889 checks passed
/// unchanged. A suite that cannot see a change that large is not covering the
/// thing that changed.
///
/// The first attempt at that fix is what check 1 catches. `agxCurve` is a fit
/// over a normalized axis on which middle gray sits at 10/16.5 = 0.606061, not
/// at the middle; rewriting the range while leaving `(ev - min) / (max - min)`
/// alone slides gray down to 0.498, where the polynomial returns 0.285. Every
/// tone in the picture fell by 1.7 stops. Middle gray in must be middle gray
/// out, whatever the latitude is set to, and that is check 1.
///
/// Check 2 pins the latitude to its declared meaning rather than to a number:
/// `kBlackStops` under gray is black, half a stop above that is not. Change the
/// constant and both move together; delete the two-piece normalization and
/// check 1 fails immediately.
///
/// ⚠ Checks 4-6 dispatch the same walk a **second** time at `contrast = 1.45`,
/// because 1-3 run at 1.0 and the product never does. See the note beside them:
/// the whole washed-out-shadows report of 2026-09-05 was a measurement taken at
/// 1.0 (#220), and nothing here could tell the two apart.
void testAgxLatitudeIsAnAnchoredRescale() {
    section("AgX latitude");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developDisplay.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developDisplay");

    // Middle gray, then a walk down from it in stops. The two either side of
    // `kBlackStops` are the ones that matter; the rest are there so a failure
    // says *where* the curve went wrong rather than only that it did.
    const float kBlackStops = 8.0f;   // must match develop_display.slang
    const std::vector<float> stops{0.0f, -2.0f, -4.0f, -6.0f,
                                   -(kBlackStops - 0.5f), -kBlackStops};
    const auto kN = static_cast<std::uint32_t>(stops.size());

    auto src = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto lut = orion::gpu::Texture::create(*device, orion::pipe::kCurveResolution,
                                           orion::pipe::kCurveRows,
                                           orion::gpu::PixelFormat::R32Float);
    const auto identity = orion::pipe::buildCurveLut({});
    lut->upload(identity.data(), orion::pipe::kCurveResolution * sizeof(float));

    // ⚠ Rec.2020 neutral, not (0.18, 0.18, 0.18) in some other space. The
    // kernel's first act is a rotation into Rec.709 and then the AgX inset, and
    // both preserve neutrals *because every row sums to one* — the property
    // #29's purple cast came from breaking. A gray in, a gray out, so the
    // green channel alone is the whole story.
    std::vector<__fp16> input(std::size_t(kN) * 4);
    for (std::uint32_t i = 0; i < kN; ++i) {
        const float v = 0.18f * std::pow(2.0f, stops[i]);
        input[i * 4 + 0] = static_cast<__fp16>(v);
        input[i * 4 + 1] = static_cast<__fp16>(v);
        input[i * 4 + 2] = static_cast<__fp16>(v);
        input[i * 4 + 3] = 1;
    }
    src->upload(input.data(), std::size_t(kN) * 4 * sizeof(__fp16));

    orion::pipe::params::Display dp{};
    dp.contrast = 1.0f;     // the anchor is a property of the axis, not the slope
    dp.curveIdentity = 1;
    dp.resolution = orion::pipe::kCurveResolution;
    dp.size[0] = kN;
    dp.size[1] = 1;

    orion::gpu::CommandBuffer cb(*device);
    auto cubeStub = orion::gpu::Texture::create(*device, 2, 4,
                                                orion::gpu::PixelFormat::RGBA32Float);
    cb.dispatch(*kernel, {src.get(), lut.get(), cubeStub.get(), dst.get()},
                &dp, sizeof dp, kN, 1);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(kN) * 4);
    dst->download(out.data(), std::size_t(kN) * 4 * sizeof(__fp16), kN, 1);
    const auto green = [&](std::size_t i) { return float(out[i * 4 + 1]); };

    // 1. The anchor. AgX puts 0.18 scene at ~0.5 display by construction, and
    //    that must not depend on how much latitude sits underneath it.
    report(std::fabs(green(0) - 0.497f) < 0.02f,
           "middle gray renders at the sigmoid's own midpoint",
           "got " + std::to_string(green(0)) + ", wanted 0.497 +- 0.02");

    // 2. The latitude means what it is named. Anything at or past the black
    //    stop is black; half a stop inside it is not.
    // Exactly zero, not merely small: the clamp puts the black stop on the
    // normalized axis at 0, where the polynomial's constant term is negative
    // and `saturate` floors it. Half a stop inside, there is still signal —
    // 0.0025, which is a quarter of an eight-bit step and the difference
    // between a toe and a cliff.
    report(green(kN - 1) == 0.0f,
           "the declared black stop renders exactly black",
           std::to_string(kBlackStops) + " stops under gray gave "
               + std::to_string(green(kN - 1)));
    report(green(kN - 2) > 0.0f,
           "and half a stop inside it still carries signal",
           std::to_string(kBlackStops - 0.5f) + " stops under gray gave "
               + std::to_string(green(kN - 2)));

    // 3. Monotone all the way down, which a mis-anchored two-piece axis is not
    //    obliged to be — a discontinuity at the join would show here.
    bool monotone = true;
    for (std::uint32_t i = 1; i < kN; ++i) {
        if (green(i) > green(i - 1) + 1e-4f) { monotone = false; break; }
    }
    report(monotone, "the walk down from gray is monotone");

    // ── The same walk, at the contrast the product actually opens with ──────
    //
    // ⚠ **Everything above runs at `contrast = 1.0`, which nothing ships.**
    // `Engine.contrast` opens at **1.45** (app/Engine.swift), and the slope
    // pivots about gray on the normalized axis, so it multiplies the distance
    // to the black end: the render reaches zero at about **5.5** stops under
    // gray, not at `kBlackStops`. Every assertion this file had was therefore
    // about a shadow end no photograph is ever shown through, and the gap cost
    // a session: a night sky measured off `orion-bench`, whose `Adjustments{}`
    // default is also 1.0, reads 5-10x brighter than macOS ImageIO's decode of
    // the same raw and looks milky — while the *product's* render of that frame
    // matches macOS to within 15%. See decision #220.
    //
    // So this second dispatch pins the shipping toe. It reuses the same source
    // walk; only the slope changes.
    dp.contrast = 1.45f;
    orion::gpu::CommandBuffer cb2(*device);
    cb2.dispatch(*kernel, {src.get(), lut.get(), cubeStub.get(), dst.get()},
                 &dp, sizeof dp, kN, 1);
    cb2.commitAndWait();
    dst->download(out.data(), std::size_t(kN) * 4 * sizeof(__fp16), kN, 1);

    // 4. Gray is still gray. Contrast pivots about `kPivotNorm`, so the slope
    //    must not move the anchor — if this fails, the pivot and the axis have
    //    stopped agreeing and every tone in the program has shifted.
    report(std::fabs(green(0) - 0.497f) < 0.02f,
           "at the shipping contrast middle gray is still the anchor",
           "got " + std::to_string(green(0)) + ", wanted 0.497 +- 0.02");

    // 5. Four stops under gray is where a night sky sits, and it is the one
    //    number that moves whichever way `kBlackStops` is pushed: 0.038 at 10
    //    stops (the AgX reference latitude, which renders that sky milky) and
    //    0.0002 at 6 (which crushes it to nothing). 0.0175 is 8.
    report(std::fabs(green(2) - 0.0175f) < 0.006f,
           "four stops under gray renders at the measured shipping value",
           "got " + std::to_string(green(2)) + ", wanted 0.0175 +- 0.006");

    // 6. And the toe closes well inside the declared latitude. At 10 stops this
    //    reads 0.0075 instead — a lifted floor is exactly what "washed out" is.
    report(green(3) == 0.0f,
           "six stops under gray is already black at the shipping contrast",
           "got " + std::to_string(green(3)));
}

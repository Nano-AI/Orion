// Output depth, highlight halo, local Laplacian clarity, dehaze, creative LUTs.
//
// Split out of main.cpp 2026-07-31; see harness.h.

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
    dp.pivot = 0.18f;
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

/// A bright highlight on a dark, color-cast background.
///
/// The case that produced a purple halo around every light in a night shot.
/// The fit that recovers a clipped channel has to be anchored on pixels near
/// the saturation boundary — Masood et al.'s Ω. Anchored on the general
/// neighborhood instead, which around a small light at night is nearly all
/// dark background, it learns the background's channel balance and then
/// extrapolates it out to highlight brightness, which is a long way past any
/// data it saw.
void testHighlightHaloGpu() {
    section("Highlight halo (GPU)");

    constexpr std::uint32_t kW = 192, kH = 192;
    constexpr float kClipR = 2.0f, kClipG = 1.0f, kClipB = 1.5f;
    constexpr float kGamma = 0.97f;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/highlightRecover.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "highlightRecover");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // A neutral lamp with a smooth falloff, on a dark background that is
    // strongly warm — a sodium-lit street, which is what teaches the fit a
    // channel balance that has nothing to do with the lamp.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    const double cx = kW / 2.0, cy = kH / 2.0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            const double d = std::hypot(double(x) - cx, double(y) - cy);

            // A warm lamp, which is what a real one is: red reaches its stop
            // before green, and green before blue, so there is a ring around
            // the white core where only some channels have clipped. That ring
            // is where the halo appeared.
            const double lamp = std::exp(-(d * d) / (2.0 * 16.0 * 16.0));
            const double back = 0.04;

            const double r = 3.2 * lamp * kClipR + back * kClipR * 1.9;
            const double g = 2.4 * lamp * kClipG + back * kClipG * 1.0;
            const double b = 1.7 * lamp * kClipB + back * kClipB * 0.30;

            input[i + 0] = static_cast<__fp16>(std::min(r, double(kClipR)));
            input[i + 1] = static_cast<__fp16>(std::min(g, double(kClipG)));
            input[i + 2] = static_cast<__fp16>(std::min(b, double(kClipB)));
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    orion::pipe::params::Highlights hl{};
    hl.size[0] = kW; hl.size[1] = kH;
    hl.clipR = kClipR; hl.clipG = kClipG; hl.clipB = kClipB;
    hl.gamma = kGamma;
    hl.strength = 1.0f;

    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), dst.get()}, &hl, sizeof hl, kW, kH);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

    // Magenta is red and blue running ahead of green, measured against each
    // channel's own clipping level so the white balance is divided back out.
    const auto magentaShift = [&](const std::vector<__fp16>& img, std::size_t i) {
        const double r = double(img[i * 4 + 0]) / kClipR;
        const double g = double(img[i * 4 + 1]) / kClipG;
        const double b = double(img[i * 4 + 2]) / kClipB;
        return (r + b) * 0.5 - g;
    };

    double worstBefore = 0.0, worstAfter = 0.0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            worstBefore = std::max(worstBefore, magentaShift(input, i));
            worstAfter = std::max(worstAfter, magentaShift(out, i));
        }
    }

    // The lamp is neutral, so nothing anywhere should come back appreciably
    // more magenta than it went in.
    std::printf("  magenta shift: before %.4f, after %.4f\n",
                worstBefore, worstAfter);
    report(worstAfter <= worstBefore + 0.01,
           "recovery does not tint a highlight magenta",
           "before " + std::to_string(worstBefore) + ", after "
               + std::to_string(worstAfter));
}

/// Harmonic fill of a clipped region — research/highlight-reconstruction.md.
///
/// Rouf, Lau & Heidrich (PROCAMS 2012) §3.2 solve grad^2 rho = 0 over the
/// clipped region with its own rim as a Dirichlet condition. `hl_pull.slang`
/// and `hl_push.slang` are that solver, as the pull-push interpolant of Gortler
/// et al. (SIGGRAPH 1996) §3.5.1.
///
/// ⚠ The check that carries the argument for building this at all is the last
/// one: a blown region wider than `highlights.slang`'s 12-pixel window, where
/// the shipping recovery returns its input untouched — correctly, since it has
/// no valid neighbour to fit against — and the fill reaches across it.
void testHighlightFillGpu() {
    section("Harmonic highlight fill (GPU)");

    namespace hf = orion::pipe::hlfill;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto pullLib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/hlPull.metallib");
    auto pullK = orion::gpu::Kernel::create(*device, *pullLib, "hlPull");
    auto pushLib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/hlPush.metallib");
    auto pushK = orion::gpu::Kernel::create(*device, *pushLib, "hlPush");

    // Run the pyramid on the GPU exactly as a graph would unroll it, and hand
    // back level zero. `cap` truncates the pyramid, which is the knob the reach
    // checks below turn: a pyramid too short to reach across a hole leaves its
    // middle with no information at all.
    const auto runGpuCapped = [&](const hf::Level& in, int cap) {
        const int levels = std::min(cap, hf::levelsFor(in.width, in.height));

        std::vector<int> lw(levels), lh(levels);
        lw[0] = in.width; lh[0] = in.height;
        for (int l = 1; l < levels; ++l) {
            lw[l] = std::max(1, (lw[l - 1] + 1) / 2);
            lh[l] = std::max(1, (lh[l - 1] + 1) / 2);
        }

        std::vector<std::unique_ptr<orion::gpu::Texture>> pulled, pushed;
        for (int l = 0; l < levels; ++l) {
            pulled.push_back(orion::gpu::Texture::create(
                *device, std::uint32_t(lw[l]), std::uint32_t(lh[l]),
                orion::gpu::PixelFormat::RGBA32Float));
            pushed.push_back(orion::gpu::Texture::create(
                *device, std::uint32_t(lw[l]), std::uint32_t(lh[l]),
                orion::gpu::PixelFormat::RGBA32Float));
        }

        // Level zero of the pull chain is the input itself.
        std::vector<float> flat(std::size_t(in.width) * in.height * 4);
        for (std::size_t i = 0; i < in.texels.size(); ++i) {
            flat[i * 4 + 0] = in.texels[i].v[0];
            flat[i * 4 + 1] = in.texels[i].v[1];
            flat[i * 4 + 2] = in.texels[i].v[2];
            flat[i * 4 + 3] = in.texels[i].w;
        }
        pulled[0]->upload(flat.data(), std::size_t(in.width) * 4 * sizeof(float));

        orion::gpu::CommandBuffer cb(*device);
        for (int l = 1; l < levels; ++l) {
            orion::pipe::params::HlPull pp{};
            pp.outSize[0] = std::uint32_t(lw[l]);
            pp.outSize[1] = std::uint32_t(lh[l]);
            pp.inSize[0]  = std::uint32_t(lw[l - 1]);
            pp.inSize[1]  = std::uint32_t(lh[l - 1]);
            cb.dispatch(*pullK, {pulled[l - 1].get(), pulled[l].get()}, &pp, sizeof pp,
                        std::uint32_t(lw[l]), std::uint32_t(lh[l]));
        }

        // The coarsest level has nothing below it to blend in, so its push is
        // its pull. Copying it through the push kernel against itself would be
        // wrong; the graph would simply start the descent one level up.
        for (int l = levels - 2; l >= 0; --l) {
            const orion::gpu::Texture* below =
                (l == levels - 2) ? pulled[l + 1].get() : pushed[l + 1].get();
            orion::pipe::params::HlPush pp{};
            pp.size[0]       = std::uint32_t(lw[l]);
            pp.size[1]       = std::uint32_t(lh[l]);
            pp.coarseSize[0] = std::uint32_t(lw[l + 1]);
            pp.coarseSize[1] = std::uint32_t(lh[l + 1]);
            cb.dispatch(*pushK, {pulled[l].get(), below, pushed[l].get()}, &pp,
                        sizeof pp, std::uint32_t(lw[l]), std::uint32_t(lh[l]));
        }
        cb.commitAndWait();

        std::vector<float> out(std::size_t(in.width) * in.height * 4);
        pushed[0]->download(out.data(), std::size_t(in.width) * 4 * sizeof(float),
                            std::uint32_t(in.width), std::uint32_t(in.height));

        hf::Level res;
        res.width = in.width; res.height = in.height;
        res.texels.resize(in.texels.size());
        for (std::size_t i = 0; i < res.texels.size(); ++i) {
            res.texels[i].v[0] = out[i * 4 + 0];
            res.texels[i].v[1] = out[i * 4 + 1];
            res.texels[i].v[2] = out[i * 4 + 2];
            res.texels[i].w    = out[i * 4 + 3];
        }
        return res;
    };

    const auto runGpu = [&](const hf::Level& in) {
        return runGpuCapped(in, 1 << 20);
    };

    const auto colorAt = [](const hf::Level& l, int x, int y, int k) {
        const hf::Sample& s = l.at(x, y);
        return s.v[k] / std::max(s.w, 1e-8f);
    };

    // ── 1. A constant rim fills with that constant, exactly ──────────────
    //
    // The strongest single statement about a harmonic function: with constant
    // Dirichlet data the solution is that constant. Any weighting error, any
    // lost normalization, any half-texel drift in either kernel shows here.
    {
        constexpr int kN = 96, kHole = 60;
        hf::Level in;
        in.width = kN; in.height = kN;
        in.texels.assign(std::size_t(kN) * kN, hf::Sample{});
        const float c[3] = {0.62f, 0.41f, 0.17f};
        const int lo = (kN - kHole) / 2, hi = lo + kHole;
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (x >= lo && x < hi && y >= lo && y < hi) continue;   // the hole
                hf::Sample& s = in.at(x, y);
                for (int k = 0; k < 3; ++k) s.v[k] = c[k];
                s.w = 1.0f;
            }
        }

        const hf::Level gpu = runGpu(in);

        double worst = 0.0, minW = 1e9;
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                for (int k = 0; k < 3; ++k) {
                    worst = std::max(worst, std::fabs(double(colorAt(gpu, x, y, k)) - c[k]));
                }
                minW = std::min(minW, double(gpu.at(x, y).w));
            }
        }
        std::printf("  constant rim, %dx%d hole: worst color %.3e, min weight %.4f\n",
                    kHole, kHole, worst, minW);
        report(worst < 1e-5, "a constant rim fills with that constant",
               "worst " + std::to_string(worst));

        // ⚠ The weight after a push is a *confidence*, not a coverage flag, and
        // it does not reach one — the pull caps it at one and then averages it
        // down again over a neighbourhood that is part hole. What must hold is
        // that it is never zero: a zero-weight pixel has no color at all, and
        // v/w is not a number. Asserting w == 1 here was the first draft and it
        // failed at 0.913, which is the interpolant behaving correctly.
        report(minW > 0.0, "the fill reaches every pixel (no zero weight)",
               "min weight " + std::to_string(minW));

        // The same fixture with the pyramid cut off before it can span the
        // hole. This is the mutation that turns the reach checks red, run as a
        // check rather than left as a remark: 4 levels see 8 pixels of the 60
        // the hole is wide, so its middle stays untouched and unresolved.
        const hf::Level shortPyramid = runGpuCapped(in, 4);
        const int mx = kN / 2, my = kN / 2;
        std::printf("  truncated to 4 levels: centre weight %.4f\n",
                    double(shortPyramid.at(mx, my).w));
        report(shortPyramid.at(mx, my).w == 0.0f,
               "a pyramid too short to span the hole leaves its middle unresolved",
               "centre weight " + std::to_string(shortPyramid.at(mx, my).w));
    }

    // ── 2. The GPU agrees with the CPU twin, and the maximum principle ───
    //
    // An irregular rim carrying real variation, so the two implementations are
    // compared on something that exercises the interpolation rather than a
    // constant either could get right by accident.
    {
        constexpr int kN = 128;
        hf::Level in;
        in.width = kN; in.height = kN;
        in.texels.assign(std::size_t(kN) * kN, hf::Sample{});

        double loRim[3] = {1e9, 1e9, 1e9}, hiRim[3] = {-1e9, -1e9, -1e9};
        const double cx = kN / 2.0, cy = kN / 2.0;
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                const double d = std::hypot(double(x) - cx, double(y) - cy);
                if (d < 34.0) continue;   // a round hole, off-center rim variation
                hf::Sample& s = in.at(x, y);
                const double t = std::atan2(double(y) - cy, double(x) - cx);
                const double f[3] = {0.50 + 0.30 * std::cos(t),
                                     0.40 + 0.20 * std::sin(2.0 * t),
                                     0.25 + 0.15 * std::cos(3.0 * t)};
                for (int k = 0; k < 3; ++k) {
                    s.v[k] = float(f[k]);
                    loRim[k] = std::min(loRim[k], f[k]);
                    hiRim[k] = std::max(hiRim[k], f[k]);
                }
                s.w = 1.0f;
            }
        }

        const hf::Level gpu = runGpu(in);
        const hf::Level cpu = hf::pullPush(in);

        double worst = 0.0;
        for (std::size_t i = 0; i < gpu.texels.size(); ++i) {
            for (int k = 0; k < 3; ++k) {
                worst = std::max(worst, std::fabs(double(gpu.texels[i].v[k]) -
                                                  double(cpu.texels[i].v[k])));
            }
            worst = std::max(worst, std::fabs(double(gpu.texels[i].w) -
                                              double(cpu.texels[i].w)));
        }
        std::printf("  GPU vs CPU twin: worst %.3e\n", worst);
        report(worst < 2e-6, "the shader and its host twin agree",
               "worst " + std::to_string(worst));

        // Every value in the pyramid is a convex combination of known inputs,
        // so nothing may leave the rim's own range. This is what says the fill
        // cannot invent a color — the guard `highlights.slang` needs three
        // explicit clamps to get.
        bool inRange = true;
        double overshoot = 0.0;
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                for (int k = 0; k < 3; ++k) {
                    const double v = colorAt(gpu, x, y, k);
                    overshoot = std::max(overshoot,
                                         std::max(v - hiRim[k], loRim[k] - v));
                    if (v < loRim[k] - 1e-5 || v > hiRim[k] + 1e-5) inRange = false;
                }
            }
        }
        std::printf("  maximum principle: worst excursion %.3e\n", overshoot);
        report(inRange, "the fill stays inside the range of its own rim",
               "excursion " + std::to_string(overshoot));

        // And how far the shipping solver sits from the harmonic answer it
        // approximates. Printed every run, because Rouf et al. use a multigrid
        // solve and this does not — the deviation is the price of the pyramid
        // and it should be a number, not a memory.
        const hf::Level truth = hf::harmonic(in, 1e-6f, 40000);
        double dev = 0.0, span = 0.0;
        for (int k = 0; k < 3; ++k) span = std::max(span, hiRim[k] - loRim[k]);
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (in.at(x, y).w > 0.0f) continue;
                for (int k = 0; k < 3; ++k) {
                    dev = std::max(dev, std::fabs(double(colorAt(gpu, x, y, k)) -
                                                  double(truth.at(x, y).v[k])));
                }
            }
        }
        std::printf("  vs Gauss-Seidel harmonic truth: worst %.4f (%.1f%% of rim span %.3f)\n",
                    dev, 100.0 * dev / span, span);
        report(dev < 0.10 * span,
               "pull-push tracks the harmonic solution within 10% of rim span",
               "deviation " + std::to_string(dev) + " of span " + std::to_string(span));

        // ── What solving it small costs ──────────────────────────────────
        //
        // ⚠ This sweep is the reason the chain is 34 nodes and 41 MiB rather
        // than the +25 nodes and ~516 MB ROADMAP costed it at. The question is
        // not whether a smaller solve is cheaper — it obviously is — but
        // whether it is *the same answer*, and that is a measurement, so it is
        // taken here against the same Gauss-Seidel reference the full-resolution
        // solver is judged by. Printed for every factor, every run.
        //
        // The hole is 68 px across, so at scale 4 it is 17 coarse texels: the
        // same coarse width a 140 px blown lamp has on a real frame at the same
        // factor. What makes this legitimate is that rho is harmonic — it has no
        // detail to lose — and the only place it moves quickly is the rim, which
        // the apply pass reads at full resolution.
        const auto deviationAtScale = [&](int scale) {
            const hf::Level small = hf::pullPushScaled(in, scale);
            double worstHere = 0.0;
            for (int y = 0; y < kN; ++y) {
                for (int x = 0; x < kN; ++x) {
                    if (in.at(x, y).w > 0.0f) continue;
                    for (int k = 0; k < 3; ++k) {
                        worstHere = std::max(worstHere,
                                             std::fabs(double(colorAt(small, x, y, k)) -
                                                       double(truth.at(x, y).v[k])));
                    }
                }
            }
            return worstHere;
        };

        double devAtScale[5] = {};
        for (int i = 0; i < 5; ++i) {
            const int scale = 1 << i;
            devAtScale[i] = deviationAtScale(scale);
            std::printf("  solved at 1/%-2d (%3d x %3d, hole %2d texels): worst %.4f "
                        "(%.1f%% of rim span)\n",
                        scale, (kN + scale - 1) / scale, (kN + scale - 1) / scale,
                        68 / scale, devAtScale[i], 100.0 * devAtScale[i] / span);
        }

        // The claim the graph is built on, stated so it can fail: at the factor
        // the chain actually runs, subsampling costs less than the pyramid
        // already costs against the same reference. If that stops being true the
        // chain has no argument for running small and this check says so.
        report(devAtScale[2] < 2.0 * devAtScale[0],
               "solving at 1/4 costs less than the pull-push approximation itself",
               "1/4 " + std::to_string(devAtScale[2]) + " vs full "
                   + std::to_string(devAtScale[0]));

        // And the other end, because a check with no failing side is decoration:
        // at 1/16 the hole is four coarse texels across and the rim is no longer
        // resolved. This is what says the factor is a real choice and not a free
        // one.
        report(devAtScale[4] > 1.5 * devAtScale[2],
               "and 1/16 is measurably worse, so the factor is not free",
               "1/16 " + std::to_string(devAtScale[4]) + " vs 1/4 "
                   + std::to_string(devAtScale[2]));
    }

    // ── 3. The reach the window fit does not have ────────────────────────
    //
    // ⚠ This is the check that justifies the feature, so it is built to be able
    // to fail. A blown disc 140 pixels across on a warm background: every
    // channel sits on the common clip inside it, which is what `linearize`
    // guarantees (decision #29), so `highlightRecover` takes its count == 3
    // branch and returns the input — a flat neutral plate with no shape and no
    // hue. The fill carries the rim's color across the whole disc.
    {
        constexpr std::uint32_t kN = 256;
        constexpr float kClip = 1.0f;
        constexpr double kR = 70.0;          // 140 px across, vs a 12 px window
        const double cx = kN / 2.0, cy = kN / 2.0;

        // Warm rim, as a sodium lamp's surround is.
        const double rim[3] = {0.90, 0.55, 0.22};

        std::vector<__fp16> img(std::size_t(kN) * kN * 4);
        hf::Level in;
        in.width = int(kN); in.height = int(kN);
        in.texels.assign(std::size_t(kN) * kN, hf::Sample{});

        for (std::uint32_t y = 0; y < kN; ++y) {
            for (std::uint32_t x = 0; x < kN; ++x) {
                const std::size_t i = std::size_t(y) * kN + x;
                const double d = std::hypot(double(x) - cx, double(y) - cy);
                const bool blown = d < kR;

                double c[3];
                for (int k = 0; k < 3; ++k) c[k] = blown ? kClip : rim[k];

                for (int k = 0; k < 3; ++k) img[i * 4 + k] = static_cast<__fp16>(c[k]);
                img[i * 4 + 3] = 1;

                hf::Sample& s = in.texels[i];
                if (!blown) {
                    for (int k = 0; k < 3; ++k) s.v[k] = float(rim[k]);
                    s.w = 1.0f;
                }
            }
        }

        // What the shipping recovery does with it.
        auto hlLib = orion::gpu::Library::createFromFile(
            *device, std::string(ORION_SHADER_DIR) + "/highlightRecover.metallib");
        auto hlK = orion::gpu::Kernel::create(*device, *hlLib, "highlightRecover");
        auto src = orion::gpu::Texture::create(*device, kN, kN,
                                               orion::gpu::PixelFormat::RGBA16Float);
        auto dst = orion::gpu::Texture::create(*device, kN, kN,
                                               orion::gpu::PixelFormat::RGBA16Float);
        src->upload(img.data(), std::size_t(kN) * 4 * sizeof(__fp16));

        orion::pipe::params::Highlights hl{};
        hl.size[0] = kN; hl.size[1] = kN;
        hl.clipR = hl.clipG = hl.clipB = kClip;
        hl.gamma = 0.97f;
        hl.strength = 1.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*hlK, {src.get(), dst.get()}, &hl, sizeof hl, kN, kN);
        cb.commitAndWait();

        std::vector<__fp16> rec(std::size_t(kN) * kN * 4);
        dst->download(rec.data(), std::size_t(kN) * 4 * sizeof(__fp16), kN, kN);

        // Chromaticity at the centre of the disc, as far from the rim as a
        // pixel in this frame can be.
        const std::size_t mid = std::size_t(kN / 2) * kN + kN / 2;
        const double recR = double(rec[mid * 4 + 0]);
        const double recG = double(rec[mid * 4 + 1]);
        const double recB = double(rec[mid * 4 + 2]);
        const double recRB = recR / std::max(recB, 1e-6);

        const hf::Level filled = runGpu(in);
        const double fr = colorAt(filled, int(kN) / 2, int(kN) / 2, 0);
        const double fg = colorAt(filled, int(kN) / 2, int(kN) / 2, 1);
        const double fb = colorAt(filled, int(kN) / 2, int(kN) / 2, 2);
        const double fillRB = fr / std::max(fb, 1e-6);
        const double rimRB  = rim[0] / rim[2];

        std::printf("  core R/B — input %.3f, highlightRecover %.3f, fill %.3f, rim %.3f\n",
                    1.0, recRB, fillRB, rimRB);

        // The window fit declines: 70 px from the rim there is no valid pixel
        // inside a 12 px window, so it returns its input unchanged. Stated as
        // an assertion rather than a remark, because if the shipping node ever
        // *does* reach this far, this whole feature needs re-arguing.
        report(std::fabs(recRB - 1.0) < 1e-3 && std::fabs(recG - kClip) < 1e-3,
               "the window fit leaves a 140 px blown core untouched",
               "R/B " + std::to_string(recRB));

        report(std::fabs(fillRB - rimRB) < 0.02,
               "the fill carries the rim's color to the core of the same region",
               "fill R/B " + std::to_string(fillRB) + " vs rim "
                   + std::to_string(rimRB));

        report(fr > fg && fg > fb,
               "the filled core is warm, in the rim's own channel order",
               std::to_string(fr) + " " + std::to_string(fg) + " " + std::to_string(fb));
    }
}

/// The fill in the develop graph, on a frame the shipping node cannot help.
///
/// ⚠ `testHighlightFillGpu` above drives the kernels directly, which proves the
/// solver and proves nothing about the wiring. Every bug this project has
/// shipped in a *correct* kernel lived in the wiring: a node left running at
/// zero strength, a parameter block pushed with the wrong texture's size, a
/// binding shifted by one. So this one builds the real `DevelopPipeline` and
/// reads what comes out of it.
///
/// The fixture is a night frame in miniature, and it is built so the existing
/// recovery **visibly fails** on it. A lamp 96 pixels across, saturated in every
/// channel — so under decision #29 it arrives at `highlights.slang` as
/// (clip, clip, clip) and its `count == 3` branch is a literal identity — inside
/// a warm annulus where only red is clipped, on a dark warm background. The
/// annulus is what the window fit is for and it handles it. The core is 48
/// pixels from the nearest valid pixel, four times its 12-pixel reach, and it
/// comes back exactly as neutral as it went in.
void testHighlightFillWiring() {
    section("Harmonic highlight fill (wiring)");

    namespace pipe = orion::pipe;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    constexpr std::uint32_t kN = 256;
    constexpr double kCore = 48.0;      // fully blown out to here
    constexpr double kRim  = 64.0;      // partially clipped out to here

    // With black 0, white 4095 and camMul (2.0, 1.0, 1.5), `whiteClipFor` is the
    // lowest of the three post-balance levels, which is green's 1.0. A channel
    // therefore clips at the sensor count where its own gain takes it to 1.0:
    // red at 2048, blue at 2730, green only at 4095.
    //
    //   4095 -> (1.00, 1.00, 1.00)   every channel at the ceiling: Omega^inter
    //   2500 -> (1.00, 0.61, 0.92)   red clipped, green and blue still valid
    //    300 -> (0.15, 0.07, 0.11)   dark, warm, and not evidence about a lamp
    orion::raw::BayerImage img;
    img.width = kN;
    img.height = kN;
    img.samples.resize(std::size_t(kN) * kN);
    img.filters = 0x94949494u;             // RGGB
    img.white = 4095;
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};

    const double cx = kN / 2.0, cy = kN / 2.0;
    for (std::uint32_t y = 0; y < kN; ++y) {
        for (std::uint32_t x = 0; x < kN; ++x) {
            const double d = std::hypot(double(x) - cx, double(y) - cy);
            const std::uint16_t v = (d < kCore) ? 4095 : (d < kRim ? 2500 : 300);
            img.samples[std::size_t(y) * kN + x] = v;
        }
    }

    std::unique_ptr<pipe::DevelopPipeline> dev;
    try {
        dev = std::make_unique<pipe::DevelopPipeline>(
            *device, std::string(ORION_SHADER_DIR), img);
    } catch (const std::exception& e) {
        report(false, "develop graph builds with the fill in it", e.what());
        return;
    }

    /// How many `hl:` nodes dispatched on the last render.
    const auto fillRan = [&] {
        int n = 0;
        for (const auto& t : dev->graph().lastRun()) {
            if (t.executed && t.name.rfind("hl:", 0) == 0) ++n;
        }
        return n;
    };

    // Scene-linear Rec.2020, straight out of the camera profile and before any
    // user adjustment — which is where a hue is a hue. The 8-bit tail would not
    // do: a blown core lands on white through the display transform whether or
    // not it has a color, so the check would pass on a dead node.
    std::vector<__fp16> ref(std::size_t(kN) * kN * 4);
    const auto reference = [&] {
        dev->referenceImage().download(ref.data(), std::size_t(kN) * 4 * sizeof(__fp16),
                                       kN, kN);
    };

    /// (r, g) chromaticity, which is what "the same color, whatever its
    /// brightness" means here.
    const auto chroma = [&](std::size_t i, double out[2]) {
        const double r = double(ref[i * 4 + 0]);
        const double g = double(ref[i * 4 + 1]);
        const double b = double(ref[i * 4 + 2]);
        const double s = std::max(r + g + b, 1e-6);
        out[0] = r / s;
        out[1] = g / s;
    };
    const auto distance = [](const double a[2], const double b[2]) {
        return std::hypot(a[0] - b[0], a[1] - b[1]);
    };

    const std::size_t centre = std::size_t(kN / 2) * kN + kN / 2;
    const std::size_t corner = std::size_t(4) * kN + 4;
    const std::size_t onRim  = std::size_t(kN / 2) * kN + kN / 2 + 56;   // in the annulus

    pipe::Adjustments adj{};
    adj.wb = dev->asShotWhiteBalance();

    // ── 1. Off is off ────────────────────────────────────────────────────
    adj.highlightRecovery = 0.0f;
    dev->apply(adj);
    dev->render();
    report(fillRan() == 0, "no fill node runs at highlightRecovery 0",
           std::to_string(fillRan()) + " ran");

    reference();
    double coreOff[2], rimOff[2], cornerOff[2];
    chroma(centre, coreOff);
    chroma(onRim,  rimOff);
    chroma(corner, cornerOff);
    const double cornerOffValue = double(ref[corner * 4 + 1]);

    // ── 2. On is on ──────────────────────────────────────────────────────
    adj.highlightRecovery = 0.8f;
    dev->apply(adj);
    dev->render();
    const int ranOn = fillRan();
    report(ranOn >= 12, "the whole chain runs when the slider is up",
           std::to_string(ranOn) + " fill nodes");

    reference();
    double coreOn[2], cornerOn[2];
    chroma(centre, coreOn);
    chroma(corner, cornerOn);

    const double offGap = distance(coreOff, rimOff);
    const double onGap  = distance(coreOn,  rimOff);

    std::printf("  core chromaticity  off (%.4f, %.4f)  on (%.4f, %.4f)  "
                "rim (%.4f, %.4f)\n",
                coreOff[0], coreOff[1], coreOn[0], coreOn[1], rimOff[0], rimOff[1]);
    std::printf("  distance from the rim's own color: off %.4f, on %.4f\n",
                offGap, onGap);

    // ⚠ The check the feature exists for. The shipping recovery is *on* in the
    // second run too — one control drives both — so what this measures is the
    // fill, since the window fit's count == 3 branch cannot move this pixel and
    // `testHighlightFillGpu` asserts separately that it does not.
    report(offGap > 0.02,
           "the blown core does not have the rim's color to begin with",
           "distance " + std::to_string(offGap));
    report(onGap < 0.25 * offGap,
           "the fill carries the rim's color into the core, through the graph",
           "off " + std::to_string(offGap) + " -> on " + std::to_string(onGap));

    // ── 3. And it touches nothing else ───────────────────────────────────
    //
    // A corner pixel has no clipped channel and is below the shoulder, so it is
    // neither in the hole nor evidence about it. The apply pass must hand it
    // back unchanged — bit for bit, since it takes an early return.
    report(distance(cornerOff, cornerOn) < 1e-6 &&
               std::fabs(cornerOffValue - double(ref[corner * 4 + 1])) < 1e-9,
           "an unclipped pixel is returned untouched",
           "delta " + std::to_string(distance(cornerOff, cornerOn)));

    // ── 4. Back to zero is back to where it started ──────────────────────
    adj.highlightRecovery = 0.0f;
    dev->apply(adj);
    dev->render();
    report(fillRan() == 0, "and the chain switches off again");

    reference();
    double coreAgain[2];
    chroma(centre, coreAgain);
    report(distance(coreAgain, coreOff) < 1e-6,
           "the core is back to the color it had before the slider moved",
           "delta " + std::to_string(distance(coreAgain, coreOff)));
}

/// Local Laplacian clarity, against Paris et al.'s own algorithm.
///
/// The GPU runs Aubry et al.'s approximation: eight remapped pyramids and a
/// linear interpolation between the two that bracket each coefficient. The
/// reference in pipe/LocalLaplacian.h runs Algorithm 1 literally — one full
/// pyramid per output coefficient, no discretization at all. The difference
/// between them *is* the approximation error, and the paper states its own
/// accuracy as "above 30 dB", so that is the gate.
///
/// This is the check that says the shader implements the published filter
/// rather than a different filter that happens to look sharper. Nothing else
/// here can tell those apart.
void testLocalLaplacianGpu() {
    section("Local Laplacian clarity (GPU)");

    namespace llf = orion::pipe::llf;
    using orion::gpu::PixelFormat;

    constexpr int kW = 48, kH = 32;
    constexpr int kLevels = llf::kPyramidLevels;
    constexpr int kStacks = (llf::kGammaLevels + 3) / 4;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    const auto kernelNamed = [&](const char* entry) {
        auto lib = orion::gpu::Library::createFromFile(
            *device, std::string(ORION_SHADER_DIR) + "/" + entry + ".metallib");
        auto k = orion::gpu::Kernel::create(*device, *lib, entry);
        // The library has to outlive the kernel; hand both back.
        return std::pair{std::move(lib), std::move(k)};
    };

    auto kDown     = kernelNamed("llfDown");
    auto kRemap    = kernelNamed("llfRemapDown");
    auto kDown4    = kernelNamed("llfDownPacked");
    auto kColl     = kernelNamed("llfCollapse");
    auto kColl0    = kernelNamed("llfCollapse0");

    // Level sizes, exactly as DevelopPipeline computes them.
    int lw[kLevels], lh[kLevels];
    for (int l = 0; l < kLevels; ++l) {
        lw[l] = (l == 0) ? kW : std::max(1, (lw[l - 1] + 1) / 2);
        lh[l] = (l == 0) ? kH : std::max(1, (lh[l - 1] + 1) / 2);
    }

    // A frame with all three things the filter has to tell apart: a step edge
    // far larger than sigmaR, a fine ripple far smaller than it, and a smooth
    // ramp that is neither.
    llf::Plane input{kW, kH, std::vector<float>(std::size_t(kW) * kH)};
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const double ramp   = 0.25 + 0.30 * (double(x) / (kW - 1));
            const double step   = (x >= kW / 2) ? 0.30 : 0.0;
            const double ripple = 0.012 * std::sin(x * 1.7) * std::cos(y * 2.3);
            input.v[std::size_t(y) * kW + x] =
                float(std::clamp(ramp + step + ripple, 0.0, 1.0));
        }
    }

    auto luma = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    std::vector<__fp16> half(std::size_t(kW) * kH);
    for (std::size_t i = 0; i < half.size(); ++i) half[i] = __fp16(input.v[i]);
    luma->upload(half.data(), std::size_t(kW) * sizeof(__fp16));

    // Every intermediate the chain needs.
    std::vector<std::unique_ptr<orion::gpu::Texture>> gauss(kLevels), out(kLevels);
    std::vector<std::array<std::unique_ptr<orion::gpu::Texture>, kStacks>> pack(kLevels);
    for (int l = 1; l < kLevels; ++l) {
        gauss[l] = orion::gpu::Texture::create(*device, lw[l], lh[l], PixelFormat::R16Float);
        for (int st = 0; st < kStacks; ++st) {
            pack[l][st] = orion::gpu::Texture::create(*device, lw[l], lh[l],
                                                      PixelFormat::RGBA16Float);
        }
    }
    for (int l = 0; l < kLevels - 1; ++l) {
        out[l] = orion::gpu::Texture::create(*device, lw[l], lh[l], PixelFormat::R16Float);
    }

    std::vector<float> gpu(std::size_t(kW) * kH);

    const auto runChain = [&](float alpha) {
        orion::gpu::CommandBuffer cb(*device);

        for (int l = 1; l < kLevels; ++l) {
            orion::pipe::params::LlfDown d{{std::uint32_t(lw[l]), std::uint32_t(lh[l])},
                                           {std::uint32_t(lw[l - 1]), std::uint32_t(lh[l - 1])}};
            cb.dispatch(*kDown.second,
                        {l == 1 ? luma.get() : gauss[l - 1].get(), gauss[l].get()},
                        &d, sizeof d, lw[l], lh[l]);
        }

        for (int l = 1; l < kLevels; ++l) {
            for (int st = 0; st < kStacks; ++st) {
                if (l == 1) {
                    orion::pipe::params::LlfRemap r{};
                    r.outSize[0] = std::uint32_t(lw[1]); r.outSize[1] = std::uint32_t(lh[1]);
                    r.inSize[0]  = kW;                   r.inSize[1]  = kH;
                    r.gamma0    = float(st * 4) * llf::kGammaStep;
                    r.gammaStep = llf::kGammaStep;
                    r.sigmaR    = llf::kSigmaR;
                    r.alpha     = alpha;
                    r.noiseLo   = llf::kNoiseLo;
                    r.noiseHi   = llf::kNoiseHi;
                    cb.dispatch(*kRemap.second, {luma.get(), pack[1][st].get()},
                                &r, sizeof r, lw[1], lh[1]);
                } else {
                    orion::pipe::params::LlfDown d{
                        {std::uint32_t(lw[l]), std::uint32_t(lh[l])},
                        {std::uint32_t(lw[l - 1]), std::uint32_t(lh[l - 1])}};
                    cb.dispatch(*kDown4.second,
                                {pack[l - 1][st].get(), pack[l][st].get()},
                                &d, sizeof d, lw[l], lh[l]);
                }
            }
        }

        for (int l = kLevels - 2; l >= 1; --l) {
            orion::pipe::params::LlfCollapse c{};
            c.size[0] = std::uint32_t(lw[l]);           c.size[1] = std::uint32_t(lh[l]);
            c.coarseSize[0] = std::uint32_t(lw[l + 1]); c.coarseSize[1] = std::uint32_t(lh[l + 1]);
            c.gammaStep  = llf::kGammaStep;
            c.gammaCount = llf::kGammaLevels;
            // The coarsest level is the residual: the input's own pyramid.
            const orion::gpu::Texture* coarseOut =
                (l + 1 == kLevels - 1) ? gauss[l + 1].get() : out[l + 1].get();
            cb.dispatch(*kColl.second,
                        {gauss[l].get(),
                         pack[l][0].get(), pack[l][1].get(),
                         pack[l][2].get(), pack[l][3].get(),
                         pack[l + 1][0].get(), pack[l + 1][1].get(),
                         pack[l + 1][2].get(), pack[l + 1][3].get(),
                         coarseOut, out[l].get()},
                        &c, sizeof c, lw[l], lh[l]);
        }

        orion::pipe::params::LlfCollapse0 c0{};
        c0.size[0] = kW;                       c0.size[1] = kH;
        c0.coarseSize[0] = std::uint32_t(lw[1]); c0.coarseSize[1] = std::uint32_t(lh[1]);
        c0.gammaStep  = llf::kGammaStep;
        c0.gammaCount = llf::kGammaLevels;
        c0.sigmaR     = llf::kSigmaR;
        c0.alpha      = alpha;
        c0.noiseLo    = llf::kNoiseLo;
        c0.noiseHi    = llf::kNoiseHi;
        cb.dispatch(*kColl0.second,
                    {luma.get(),
                     pack[1][0].get(), pack[1][1].get(),
                     pack[1][2].get(), pack[1][3].get(),
                     (kLevels - 1 == 1) ? gauss[1].get() : out[1].get(),
                     out[0].get()},
                    &c0, sizeof c0, kW, kH);

        cb.commitAndWait();

        std::vector<__fp16> got(std::size_t(kW) * kH);
        out[0]->download(got.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        for (std::size_t i = 0; i < got.size(); ++i) gpu[i] = float(got[i]);
    };

    const auto psnrAgainst = [&](const std::vector<float>& a,
                                 const std::vector<float>& b) {
        double mse = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double d = double(a[i]) - double(b[i]);
            mse += d * d;
        }
        mse /= double(a.size());
        // Signal peak is 1: the plane is normalized log luminance.
        return (mse <= 0.0) ? 99.0 : 10.0 * std::log10(1.0 / mse);
    };

    // ── alpha = 1 is the identity, and proves the pyramid round-trips ──────
    //
    // With fd(D) = D the remapping is exactly r(i) = i, so the whole chain
    // reduces to "analyze into a Laplacian pyramid, collapse it again". That
    // has to return the input to the precision of the storage — and if the
    // downsample and the expand disagree even slightly, this is where it shows.
    // Every other check in this function would still pass with a subtly wrong
    // expand operator, because both branches would share the mistake.
    runChain(1.0f);
    double worst = 0.0;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
        worst = std::max(worst, std::abs(double(gpu[i]) - double(input.v[i])));
    }
    report(worst < 2e-3, "alpha = 1 collapses back to the input",
           "worst " + std::to_string(worst));

    // ── The approximation, against the published algorithm ────────────────
    for (const float clarity : {0.5f, 1.0f, -1.0f}) {
        const float alpha = llf::alphaForClarity(clarity);
        runChain(alpha);
        const llf::Plane want = llf::reference(input, kLevels, llf::kSigmaR, alpha,
                                               llf::kNoiseLo, llf::kNoiseHi);

        // First: is the shader running the algorithm it claims to? Same
        // discretization, same everything, on the CPU. A gap here is a bug in
        // a kernel and nothing to do with the approximation.
        const llf::Plane same = llf::referenceFast(input, kLevels, llf::kGammaLevels,
                                                   llf::kSigmaR, alpha,
                                                   llf::kNoiseLo, llf::kNoiseHi);
        double worstShader = 0.0;
        for (std::size_t i = 0; i < gpu.size(); ++i) {
            worstShader = std::max(worstShader,
                                   std::abs(double(gpu[i]) - double(same.v[i])));
        }
        report(worstShader < 5e-3,
               "clarity " + std::to_string(clarity) +
                   ": the shaders run Aubry et al.'s algorithm",
               "worst " + std::to_string(worstShader));

        // Second: how much does the discretization itself cost, and does it
        // converge? If it does not, the sampling argument is not what is
        // limiting the accuracy and more gamma levels would be wasted money.
        for (const int n : {8, 16, 32}) {
            const llf::Plane approx = llf::referenceFast(input, kLevels, n, llf::kSigmaR,
                                                         alpha, llf::kNoiseLo, llf::kNoiseHi);
            double sum = 0.0, worstN = 0.0;
            for (std::size_t i = 0; i < approx.v.size(); ++i) {
                const double d = std::abs(double(approx.v[i]) - double(want.v[i]));
                sum += d; worstN = std::max(worstN, d);
            }
            std::printf("      N = %2d  mean %.4f EV  max %.4f EV\n", n,
                        sum / double(approx.v.size()) * llf::kWindowEv,
                        worstN * llf::kWindowEv);
        }
        const double db = psnrAgainst(gpu, want.v);
        // dB alone is misleading here: the plane is normalized over a twelve
        // stop window, so a respectable PSNR can still be a visible error in
        // EV. Print the distribution as well — if the error is a handful of
        // pixels at the step edge that is a different fact about the filter
        // than a drift across the whole frame.
        std::vector<double> err(gpu.size());
        for (std::size_t i = 0; i < gpu.size(); ++i) {
            err[i] = std::abs(double(gpu[i]) - double(want.v[i])) * llf::kWindowEv;
        }
        std::sort(err.begin(), err.end());
        std::printf("  clarity %+.2f  alpha %.3f  %.2f dB   median %.4f EV  "
                    "p99 %.4f EV  max %.4f EV\n",
                    clarity, alpha, db, err[err.size() / 2],
                    err[err.size() * 99 / 100], err.back());
        report(db > 30.0,
               "clarity " + std::to_string(clarity) + ": matches Paris et al. "
               "Algorithm 1 above 30 dB",
               std::to_string(db) + " dB");
    }

    // ── A step edge is an edge, and no halo of our own making ─────────────
    //
    // The step here is 0.30 and sigmaR is 0.143, so Eq. 2 with fe the identity
    // leaves it alone by construction. A clarity slider that moves it is a
    // tone control wearing the wrong name.
    //
    // Overshoot is measured against the *exact* algorithm rather than against
    // the input's range, and the difference matters. Increasing local contrast
    // legitimately pushes an edge profile past the values that were there —
    // that is what it is for, and the paper's own filter does it. What the
    // paper claims is that it does not ring, so the question worth asking is
    // whether the approximation overshoots more than the algorithm it
    // approximates. That is a question about this code; the other is a
    // question about the 2011 paper.
    const float strong = llf::alphaForClarity(1.0f);
    runChain(strong);
    const llf::Plane exact = llf::reference(input, kLevels, llf::kSigmaR, strong,
                                            llf::kNoiseLo, llf::kNoiseHi);

    double worstEdge = 0.0;
    for (int y = 0; y < kH; ++y) {
        for (int x = kW / 2 - 4; x < kW / 2 + 4; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            worstEdge = std::max(worstEdge, std::abs(double(gpu[i]) - double(input.v[i])));
        }
    }
    report(worstEdge < 0.06, "a step far larger than sigmaR survives clarity",
           "worst " + std::to_string(worstEdge));

    const auto range = [](const std::vector<float>& v) {
        return std::pair{double(*std::min_element(v.begin(), v.end())),
                         double(*std::max_element(v.begin(), v.end()))};
    };
    const auto [inLo, inHi]       = range(input.v);
    const auto [exactLo, exactHi] = range(exact.v);
    const auto [gpuLo, gpuHi]     = range(gpu);

    std::printf("  range: input [%.3f %.3f]  exact [%.3f %.3f]  gpu [%.3f %.3f]\n",
                inLo, inHi, exactLo, exactHi, gpuLo, gpuHi);

    report(gpuHi <= exactHi + 0.02 && gpuLo >= exactLo - 0.02,
           "the approximation adds no overshoot the exact filter does not",
           "gpu [" + std::to_string(gpuLo) + " " + std::to_string(gpuHi) +
               "] against exact [" + std::to_string(exactLo) + " " +
               std::to_string(exactHi) + "]");

    // ── The noise term is doing something ─────────────────────────────────
    //
    // Paris et al. section 5.2 blends fd back to the identity below 1% of the
    // range. Without it the lowest-amplitude signal in the frame gets the
    // largest gain of anything in the picture, which on a photograph is the
    // noise. Compare what the ripple — amplitude 0.012, right in the band the
    // term protects — is worth with the term and with it disabled.
    const float alpha = llf::alphaForClarity(1.0f);
    const float g = 0.5f;
    const float withTerm    = llf::remap(g + 0.004f, g, llf::kSigmaR, alpha,
                                         llf::kNoiseLo, llf::kNoiseHi) - g;
    const float withoutTerm = llf::remap(g + 0.004f, g, llf::kSigmaR, alpha, 0.0f, 0.0f) - g;
    report(withTerm < withoutTerm * 0.35f,
           "the noise term declines to amplify a 0.4% ripple",
           std::to_string(withTerm) + " against " + std::to_string(withoutTerm));

    // Above the upper threshold it must get out of the way entirely.
    const float big = llf::remap(g + 0.10f, g, llf::kSigmaR, alpha,
                                 llf::kNoiseLo, llf::kNoiseHi) - g;
    const float bigRaw = llf::remap(g + 0.10f, g, llf::kSigmaR, alpha, 0.0f, 0.0f) - g;
    report(std::abs(big - bigRaw) < 1e-6f,
           "and leaves real detail to the published power curve",
           std::to_string(big) + " against " + std::to_string(bigRaw));

    // alpha = 1 is the identity point-wise, which is what makes clarity = 0
    // safe to leave switched on if it ever is.
    double worstRemap = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const float v = float(i) / 100.0f;
        worstRemap = std::max(worstRemap,
                              std::abs(double(llf::remap(v, 0.5f, llf::kSigmaR, 1.0f,
                                                         llf::kNoiseLo, llf::kNoiseHi)) - v));
    }
    report(worstRemap < 1e-6, "alpha = 1 is the identity remapping",
           "worst " + std::to_string(worstRemap));
}

/// Dehaze — the three claims that are not obvious by reading.
void testDehazeGpu() {
    section("Dehaze, dark channel prior");

    namespace dh = orion::pipe::dehaze;
    using orion::gpu::PixelFormat;

    // ── The atmospheric light picks haze, not a specular ──────────────────
    //
    // CVPR section 4.4 is two stages: the most haze-opaque pixels by dark
    // channel *first*, then the brightest among those. The paper is explicit
    // that the answer "may not be brightest ones in the whole input image", and
    // this is the case that separates a correct implementation from the obvious
    // wrong one — a specular highlight is the brightest pixel in most frames,
    // and picking it hands the whole recovery a wrong constant.
    {
        std::vector<dh::Candidate> c;
        for (int i = 0; i < 1000; ++i) {
            c.push_back({0.02f, 0.2f, 0.2f, 0.2f});          // ordinary scene
        }
        c.push_back({0.60f, 0.75f, 0.74f, 0.72f});           // haze-opaque sky
        c.push_back({0.61f, 0.70f, 0.69f, 0.68f});           // slightly dimmer haze
        c.push_back({0.01f, 3.00f, 3.00f, 3.00f});           // a specular

        const auto a = dh::airlightFrom(c);
        report(a[0] > 0.6f && a[0] < 0.8f,
               "the atmospheric light is the haze, not the brightest pixel",
               "A.r " + std::to_string(a[0]));
        report(a[0] < 1.0f, "a specular four times brighter is rejected",
               "A.r " + std::to_string(a[0]));
    }

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

    // ── A separable rank filter really is the square patch ────────────────
    //
    // The claim in dehaze_rank.slang is that a 15-tap minimum along each axis
    // *is* the 15 x 15 minimum, not an approximation of it — that is what buys
    // 30 taps instead of 225. Checked against the square patch computed
    // directly, which is the only way to know.
    {
        constexpr int kW = 61, kH = 41;
        auto kRank = load("dehazeRank");

        std::vector<float> src(std::size_t(kW) * kH);
        std::mt19937 rng(20260728);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);
        for (auto& v : src) v = uni(rng);
        // A few hard zeros and ones, so the extremes are exercised and not just
        // the interior of the distribution.
        src[std::size_t(7) * kW + 9] = 0.0f;
        src[std::size_t(30) * kW + 40] = 1.0f;

        auto a = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
        auto b = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
        auto c = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);

        std::vector<__fp16> up(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) up[i] = __fp16(src[i]);
        a->upload(up.data(), std::size_t(kW) * sizeof(__fp16));

        const auto runRank = [&](bool maximum) {
            orion::gpu::CommandBuffer cb(*device);
            orion::pipe::params::DehazeRank h{};
            h.size[0] = kW; h.size[1] = kH;
            h.radius = dh::kPatchRadius; h.horizontal = 1; h.maximum = maximum ? 1 : 0;
            cb.dispatch(*kRank.second, {a.get(), b.get()}, &h, sizeof h, kW, kH);
            orion::pipe::params::DehazeRank v = h;
            v.horizontal = 0;
            cb.dispatch(*kRank.second, {b.get(), c.get()}, &v, sizeof v, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> got(src.size());
            c->download(got.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
            return got;
        };

        for (const bool maximum : {false, true}) {
            const auto got = runRank(maximum);
            double worst = 0.0;
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    float want = src[std::size_t(y) * kW + x];
                    for (int dy = -dh::kPatchRadius; dy <= dh::kPatchRadius; ++dy) {
                        for (int dx = -dh::kPatchRadius; dx <= dh::kPatchRadius; ++dx) {
                            const int sx = std::clamp(x + dx, 0, kW - 1);
                            const int sy = std::clamp(y + dy, 0, kH - 1);
                            const float v = src[std::size_t(sy) * kW + sx];
                            want = maximum ? std::max(want, v) : std::min(want, v);
                        }
                    }
                    worst = std::max(worst,
                        std::abs(double(got[std::size_t(y) * kW + x]) - double(want)));
                }
            }
            report(worst < 1e-3,
                   maximum ? "separable 15-tap max is the 15x15 maximum"
                           : "separable 15-tap min is the 15x15 minimum",
                   "worst " + std::to_string(worst));
        }
    }

    // ── Eq. (16), and the identity at omega = 0 ───────────────────────────
    {
        constexpr int kW = 32, kH = 16;
        auto kRec = load("dehazeRecover");

        auto src    = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
        auto coeffs = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RG32Float);
        auto dst    = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * kH * 4);
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                in[i + 0] = __fp16(0.05 + 0.9 * x / double(kW - 1));
                in[i + 1] = __fp16(0.04 + 0.7 * x / double(kW - 1));
                in[i + 2] = __fp16(0.06 + 0.5 * x / double(kW - 1));
                in[i + 3] = __fp16(1.0f);
            }
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        const float airlight[3] = {0.80f, 0.78f, 0.75f};

        // a = 0 and b = t makes the transmission a constant, which is what lets
        // Eq. (16) be checked as arithmetic rather than against another
        // implementation of the guided filter.
        const auto runWithT = [&](float t) {
            std::vector<float> ab(std::size_t(kW) * kH * 2);
            for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
                ab[i * 2 + 0] = 0.0f;
                ab[i * 2 + 1] = t;
            }
            coeffs->upload(ab.data(), std::size_t(kW) * 2 * sizeof(float));

            orion::pipe::params::DehazeRecover p{};
            p.size[0] = kW; p.size[1] = kH;
            p.coeffSize[0] = kW; p.coeffSize[1] = kH;
            p.t0 = dh::kT0;
            p.lo = orion::pipe::llf::kWindowLoEv;
            p.invRange = 1.0f / orion::pipe::llf::kWindowEv;
            for (int c = 0; c < 3; ++c) p.airlight[c] = airlight[c];

            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kRec.second, {src.get(), coeffs.get(), dst.get()},
                        &p, sizeof p, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> got(in.size());
            dst->download(got.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
            return got;
        };

        // t = 1 is what omega = 0 produces, and Eq. (16) collapses to J = I.
        // The slider's zero is exact because of this, not because the result is
        // blended back afterwards.
        const auto identity = runWithT(1.0f);
        double worstId = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if ((i & 3) == 3) continue;
            worstId = std::max(worstId, std::abs(double(identity[i]) - double(in[i])));
        }
        report(worstId < 2e-3, "transmission of one is exactly the identity",
               "worst " + std::to_string(worstId));

        const float t = 0.55f;
        const auto got = runWithT(t);
        double worstEq = 0.0;
        for (int x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const double want =
                    (double(in[i + c]) - airlight[c]) / t + airlight[c];
                worstEq = std::max(worstEq,
                                   std::abs(double(got[i + c]) - std::max(want, 0.0)));
            }
        }
        report(worstEq < 3e-3, "Eq. (16) recovers the radiance it was given",
               "worst " + std::to_string(worstEq));

        // The floor on t. Below it the division would run away, and the paper
        // puts t0 at 0.1 for exactly that reason.
        const auto floored = runWithT(0.001f);
        const auto atFloor = runWithT(dh::kT0);
        double worstFloor = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            worstFloor = std::max(worstFloor,
                                  std::abs(double(floored[i]) - double(atFloor[i])));
        }
        report(worstFloor < 2e-3, "transmission is floored at t0, not divided by",
               "worst " + std::to_string(worstFloor));
    }
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
        p.pivot = -2.5f;
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

/// Simulated exposure fusion — the maths, before any of it reaches a shader.

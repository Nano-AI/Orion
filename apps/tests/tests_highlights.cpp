// Clipped highlights: the window fit, the harmonic fill, and the fill in the graph.
//
// One fixture in three forms, which is why these three live together: a blown
// light source inside a warm surround, the frame that produced the purple halo.
// `testHighlightHaloGpu` and `testHighlightFillGpu` both build
// `highlightRecover` over it and read the same clipping levels; the third builds
// the real `DevelopPipeline` over the same scene, because a correct kernel says
// nothing about its wiring, and its checks are written against the second's —
// "the window fit leaves a 140 px blown core untouched" is asserted there and
// relied on here. Separating them would leave each half asserting a premise it
// no longer establishes.
//
// Split out of `tests_effects.cpp` 2026-08-02; see decision #127.

#include "harness.h"

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
    constexpr double kRim  = 80.0;      // partially clipped out to here
    // ⚠ **And then a ring that is bright and wholly unclipped**, which the
    // fixture did not have before decision #109 and needed. Every ring here is
    // neutral at the sensor, so the truth about this lamp's color is one number
    // — the white balance gains, 2 : 1 : 1.5 — and only this ring reads it
    // undistorted. The partial ring reads 1.00 : 0.61 : 0.92 because its red hit
    // the ceiling, and piece 2 was handing *that* to the solver as its Dirichlet
    // condition. A test whose target was the partial ring's own color could not
    // see the error, because the error was in the target.
    //
    // ⚠ And the partial ring is **32 pixels wide**, not 16, so that it straddles
    // the window fit's 12-pixel reach. Its outer part is close enough to the
    // clean ring to be recovered from real evidence; its inner part is not, and
    // is declined. Both halves have to exist in one fixture or two of the
    // mutations below cannot be seen: a predicate that overwrites the window
    // fit's own answer needs a pixel the window fit answered.
    constexpr double kClean = 104.0;

    // With black 0, white 4095 and camMul (2.0, 1.0, 1.5), `whiteClipFor` is the
    // lowest of the three post-balance levels, which is green's 1.0. A channel
    // therefore clips at the sensor count where its own gain takes it to 1.0:
    // red at 2048, blue at 2730, green only at 4095.
    //
    //   4095 -> (1.00, 1.00, 1.00)   every channel at the ceiling: Omega^inter
    //   2500 -> (1.00, 0.61, 0.92)   red clipped, green and blue still valid
    //   1900 -> (0.93, 0.46, 0.70)   nothing clipped, bright: the only evidence
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
            const std::uint16_t v = (d < kCore)     ? 4095
                                    : (d < kRim)    ? 2500
                                    : (d < kClean)  ? 1900
                                                    : 300;
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
    const std::size_t onFit  = std::size_t(kN / 2) * kN + kN / 2 + 76;   // annulus, within reach
    const std::size_t onEvid = std::size_t(kN / 2) * kN + kN / 2 + 92;   // in the clean ring

    pipe::Adjustments adj{};
    adj.wb = dev->asShotWhiteBalance();

    // ── 1. Off is off ────────────────────────────────────────────────────
    adj.highlightRecovery = 0.0f;
    dev->apply(adj);
    dev->render();
    report(fillRan() == 0, "no fill node runs at highlightRecovery 0",
           std::to_string(fillRan()) + " ran");

    reference();
    double coreOff[2], rimOff[2], cornerOff[2], truth[2];
    chroma(centre, coreOff);
    chroma(onRim,  rimOff);
    chroma(corner, cornerOff);
    // ⚠ The target, and it is measured rather than assumed: the one ring in this
    // fixture whose every channel is under the ceiling reads the lamp's real
    // color. Both the core and the partial annulus should end up here.
    chroma(onEvid, truth);
    const double cornerOffValue = double(ref[corner * 4 + 1]);
    // The partial ring's green and blue never clipped, so no node in this chain
    // is entitled to move them. Kept to compare against.
    const double rimValidOff[2] = {double(ref[onRim * 4 + 1]),
                                   double(ref[onRim * 4 + 2])};

    // ── 2. On is on ──────────────────────────────────────────────────────
    adj.highlightRecovery = 0.8f;
    dev->apply(adj);
    dev->render();
    const int ranOn = fillRan();
    report(ranOn >= 12, "the whole chain runs when the slider is up",
           std::to_string(ranOn) + " fill nodes");

    reference();
    double coreOn[2], cornerOn[2], rimOn[2];
    chroma(centre, coreOn);
    chroma(corner, cornerOn);
    chroma(onRim,  rimOn);

    const double offGap = distance(coreOff, truth);
    const double onGap  = distance(coreOn,  truth);
    const double rimOffGap = distance(rimOff, truth);
    const double rimOnGap  = distance(rimOn,  truth);

    std::printf("  core chromaticity  off (%.4f, %.4f)  on (%.4f, %.4f)  "
                "truth (%.4f, %.4f)\n",
                coreOff[0], coreOff[1], coreOn[0], coreOn[1], truth[0], truth[1]);
    std::printf("  distance from the lamp's own color — core: off %.4f, on %.4f;"
                "  partial ring: off %.4f, on %.4f\n",
                offGap, onGap, rimOffGap, rimOnGap);

    // ⚠ The check the feature exists for. The shipping recovery is *on* in the
    // second run too — one control drives both — so what this measures is the
    // fill, since the window fit's count == 3 branch cannot move this pixel and
    // `testHighlightFillGpu` asserts separately that it does not.
    report(offGap > 0.02,
           "the blown core does not have the lamp's color to begin with",
           "distance " + std::to_string(offGap));
    report(onGap < 0.25 * offGap,
           "the fill carries the lamp's own color into the core, through the graph",
           "off " + std::to_string(offGap) + " -> on " + std::to_string(onGap));

    // ⚠ And a tighter one, which exists to hold `hl_mask.slang`'s shoulder rule
    // in place. Deleting it — letting the dark background count as evidence
    // about the lamp — leaves the check above passing and fails this one,
    // because the background is then averaged into every coarse texel of the rim
    // and drags the fill further off. That is the whole failure Masood et al.'s
    // Omega exists to prevent, one node over.
    report(onGap < 0.10 * offGap,
           "and the dark background is not treated as evidence about the lamp",
           "ratio " + std::to_string(onGap / offGap));

    // ── 2b. §3.3, and it is a separate claim from the one above ──────────
    //
    // ⚠ **The partial ring is the pixel set piece 2 called evidence and
    // decision #109 does not.** Its red hit the ceiling and nothing in this
    // fixture can recover it — the window fit needs eight wholly-valid samples
    // above the shoulder within twelve pixels, the clean ring is sixteen pixels
    // away at the nearest, and the background is below the shoulder. So it is
    // declined, it stays at 1.00 : 0.61 : 0.92, and before #109 the solver read
    // it as the truth about the lamp.
    //
    // §3.3 puts it right by scaling this pixel's *own* valid green and blue to
    // rho's hue rather than replacing them: f*_k = (rho_k/rho_j)*f_j. Two
    // separate things must therefore hold, and they fail to different mutations
    // — the core taking rho (above), and this ring taking the ratio.
    report(rimOffGap > 0.02,
           "the partial ring does not have the lamp's color either, to begin with",
           "distance " + std::to_string(rimOffGap));
    report(rimOnGap < 0.5 * rimOffGap,
           "§3.3 carries the lamp's color into the partially clipped ring",
           "off " + std::to_string(rimOffGap) + " -> on " + std::to_string(rimOnGap));

    // ⚠ **And this is the check that says it is a *transfer* and not a
    // replacement.** Getting the chromaticity right is satisfied just as well by
    // writing rho straight into the partial ring, which is what the §3.2 branch
    // does one line up — and that would throw away the green and blue this pixel
    // actually measured, which are the only detail a blown region has left. The
    // ratio model exists precisely so those two survive untouched. Bit for bit,
    // because "close" is what a lerp toward rho would also be.
    //
    // ⚠ **Measured across the node itself, in camera RGB, not in the reference
    // image.** This check was written on `referenceImage()` first and failed on
    // correct code: that texture is downstream of the camera matrix, which mixes
    // the channels, so moving red moves Rec.2020's green and blue with it. The
    // claim "only the clipped channel moved" is a claim about `hl_apply`'s own
    // space and has to be read there.
    {
        const auto st = dev->highlightStages();
        std::vector<__fp16> before(std::size_t(kN) * kN * 4), after(before.size());
        st.output->download(before.data(), std::size_t(kN) * 4 * sizeof(__fp16), kN, kN);
        st.filled->download(after.data(), std::size_t(kN) * 4 * sizeof(__fp16), kN, kN);
        report(after[onRim * 4 + 1] == before[onRim * 4 + 1] &&
                   after[onRim * 4 + 2] == before[onRim * 4 + 2],
               "and it moves only the clipped channel — green and blue are untouched",
               "g " + std::to_string(double(before[onRim * 4 + 1])) + " -> " +
                   std::to_string(double(after[onRim * 4 + 1])) + ", b " +
                   std::to_string(double(before[onRim * 4 + 2])) + " -> " +
                   std::to_string(double(after[onRim * 4 + 2])));
        report(double(after[onRim * 4 + 0]) > double(before[onRim * 4 + 0]),
               "while the clipped red is lifted off the ceiling",
               std::to_string(double(before[onRim * 4 + 0])) + " -> " +
                   std::to_string(double(after[onRim * 4 + 0])));

        // ⚠ **Piece 2's rule, as a check rather than as a comment.** The outer
        // part of the same ring is within the window fit's reach of the clean
        // ring, so Masood et al.'s fit answered it from real evidence. This
        // interpolant must not touch that answer — where both are available the
        // measurement wins, and "recovered" is what the whole `rec > raw`
        // predicate exists to detect. Bit for bit, all three channels.
        //
        // ⚠ Two mutations pass every other check in this file and fail here:
        // spelling the predicate as a level test (`c >= limit`, which a lifted
        // channel still satisfies) and letting the mask call the same pixel a
        // hole. Both would have shipped against the 16-pixel fixture this test
        // used before, where the fit recovered nothing at all.
        // ⚠ Printed, because where the window fit's reach ends is the whole
        // premise of this feature and it is easier to trust as a profile than
        // as an argument. Red sits at the ceiling out to 72 px and lifts at 76,
        // which is the 12-pixel reach measured against a ring starting at 80.
        std::printf("  radial red after the window fit:");
        for (int rr = 40; rr <= 100; rr += 4) {
            std::printf(" %d:%.2f", rr,
                        double(before[(std::size_t(kN / 2) * kN + kN / 2 + rr) * 4]));
        }
        std::printf("\n");
        report(after[onFit * 4 + 0] == before[onFit * 4 + 0] &&
                   after[onFit * 4 + 1] == before[onFit * 4 + 1] &&
                   after[onFit * 4 + 2] == before[onFit * 4 + 2],
               "a pixel the window fit recovered is left exactly as it left it",
               std::to_string(double(before[onFit * 4 + 0])) + " -> " +
                   std::to_string(double(after[onFit * 4 + 0])));
        report(double(before[onFit * 4 + 0]) > double(st.clip),
               "and that pixel really was recovered — its red is above the ceiling",
               std::to_string(double(before[onFit * 4 + 0])) + " vs clip " +
                   std::to_string(double(st.clip)));
    }
    (void)rimValidOff;

    // ⚠ A `kMaxGain` check was written here and deleted rather than shipped.
    // Its bound is 2x the ceiling and the ratio model in this fixture lands
    // nowhere near it, so it was green for every value the constant could have
    // taken, including one wired to nothing — the same defect two of piece 3's
    // five mutations found. Whether the clamp binds on a real photograph is a
    // measurement, and it is in `apps/bench` block 3e where a measurement
    // belongs, not an assertion here where it cannot fail.

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

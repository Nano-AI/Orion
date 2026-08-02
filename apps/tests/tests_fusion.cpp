// Exposure fusion, on the CPU and the GPU, and the auto-enhance statistics.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

void testExposureFusionMath() {
    section("Exposure fusion (maths)");

    namespace sef = orion::pipe::sef;

    const float alpha = sef::kAlphaDefault, beta = sef::kBeta;
    sef::Plan p{};
    p.images = 5; p.dark = 0; p.bright = 4;

    // ── The smooth clip ───────────────────────────────────────────────────
    //
    // g is the identity inside the restrained range and decays outside it. The
    // join is where a hand-rolled version goes wrong: a discontinuity there is
    // a visible edge in the simulated image, and a slope discontinuity is a
    // visible edge in the *weight*, which is worse because it moves.
    {
        double worstJoin = 0.0, worstSlope = 0.0;
        bool monotone = true, bounded = true;
        for (int k = 0; k <= p.bright; ++k) {
            const float c = sef::rho(k, p, beta);
            const float half = beta * 0.5f;

            // Continuity of value at both ends of the range.
            worstJoin = std::max(worstJoin,
                std::abs(double(sef::clip(c + half, c, beta, sef::kLambda)) - double(c + half)));
            worstJoin = std::max(worstJoin,
                std::abs(double(sef::clip(c - half, c, beta, sef::kLambda)) - double(c - half)));

            // Continuity of slope: one inside, and the tail's own expression
            // evaluated at the join must also be one.
            worstSlope = std::max(worstSlope,
                std::abs(double(sef::clipSlope(c + half + 1e-6f, c, beta, sef::kLambda)) - 1.0));

            float previous = -1e9f;
            for (int i = -200; i <= 400; ++i) {
                const float t = float(i) / 200.0f;      // deliberately outside [0,1]
                const float g = sef::clip(t, c, beta, sef::kLambda);
                if (g < previous - 1e-6f) monotone = false;
                previous = g;
                // The tail asymptote: |g - center| < beta/2 + lambda.
                if (std::abs(g - c) > half + sef::kLambda + 1e-5f) bounded = false;
            }
        }
        report(worstJoin < 1e-6, "the clip is continuous where its branches meet",
               "worst " + std::to_string(worstJoin));
        report(worstSlope < 1e-3, "and its slope is one on both sides of the join",
               "worst " + std::to_string(worstSlope));
        report(monotone, "the clip is monotone, so it cannot invert tones", "");
        report(bounded, "and is bounded by beta/2 + lambda however far the input goes", "");
    }

    // beta = 1 degenerates to plain exposure fusion — the paper says so, and it
    // is the cheapest check that rho and the clip agree with each other.
    {
        sef::Plan q{}; q.images = 5; q.dark = 0; q.bright = 4;
        double worst = 0.0;
        for (int k = 0; k <= q.bright; ++k) {
            for (int i = 0; i <= 100; ++i) {
                const float t = float(i) / 100.0f;
                worst = std::max(worst, std::abs(double(sef::clip(t, sef::rho(k, q, 1.0f),
                                                                 1.0f, sef::kLambda)) - double(t)));
            }
        }
        report(worst < 1e-6, "beta = 1 makes the clip the identity, as the paper states",
               "worst " + std::to_string(worst));
    }

    // ── The contrast weight is the clip's own derivative ──────────────────
    //
    // Hessel & Morel replace Mertens' Laplacian filter with it, so if this is
    // not actually the derivative there is no contrast measure at all. Checked
    // against a finite difference rather than against itself.
    {
        double worst = 0.0;
        for (int k = 0; k <= p.bright; ++k) {
            const float c = sef::rho(k, p, beta);
            for (int i = 1; i < 200; ++i) {
                const float t = -0.2f + 1.4f * float(i) / 200.0f;
                const float h = 1e-4f;
                if (std::abs(std::abs(t - c) - beta * 0.5f) < 2e-3f) continue;  // skip the join
                const float numeric = (sef::clip(t + h, c, beta, sef::kLambda) -
                                       sef::clip(t - h, c, beta, sef::kLambda)) / (2.0f * h);
                worst = std::max(worst,
                    std::abs(double(numeric) - double(sef::clipSlope(t, c, beta, sef::kLambda))));
            }
        }
        report(worst < 2e-3, "the contrast weight is the clip's actual derivative",
               "worst " + std::to_string(worst));
    }

    // ── The simulated set is solved, not chosen ───────────────────────────
    {
        const auto dark  = sef::planFor(0.05f, alpha, beta);
        const auto light = sef::planFor(0.95f, alpha, beta);
        report(dark.valid() && light.valid(), "a plan is found at both ends of the histogram",
               std::to_string(dark.images) + " and " + std::to_string(light.images));
        // "contrast enhancement is generally needed in the dark parts only" —
        // so a dark frame gets brightened images and a bright one gets darkened.
        report(dark.bright > dark.dark,
               "a dark frame simulates more brightened images than darkened",
               std::to_string(dark.bright) + " vs " + std::to_string(dark.dark));
        report(light.dark >= light.bright,
               "and a bright frame the other way round",
               std::to_string(light.dark) + " vs " + std::to_string(light.bright));

        // The paper reports N = 4, N* = 0 — five images — for its own recommended
        // alpha = 8 and beta = 0.5. Reproducing that is the check that says
        // Algorithm 1 was implemented rather than approximated.
        const auto recommended = sef::planFor(0.0f, 8.0f, 0.5f);
        report(recommended.images == 5 && recommended.bright == 4 && recommended.dark == 0,
               "the paper's own alpha = 8, beta = 0.5 gives its own N = 4, N* = 0",
               std::to_string(recommended.images) + " images, N " +
                   std::to_string(recommended.bright) + ", N* " +
                   std::to_string(recommended.dark));

        // And the count moves with beta, which a hardcoded five could not: the
        // paper tabulates N = 3 at beta = 0.6 and N = 6 at beta = 0.4.
        const auto wide   = sef::planFor(0.0f, 8.0f, 0.6f);
        const auto narrow = sef::planFor(0.0f, 8.0f, 0.4f);
        std::printf("  images by beta: 0.4 -> %d, 0.5 -> %d, 0.6 -> %d\n",
                    narrow.images, recommended.images, wide.images);
        report(wide.images < recommended.images && narrow.images > recommended.images,
               "and the count follows beta, which a hardcoded five could not",
               std::to_string(narrow.images) + " / " + std::to_string(recommended.images) +
                   " / " + std::to_string(wide.images));
    }

    // ── The blend, exactly ────────────────────────────────────────────────
    //
    // On a constant image every Laplacian coefficient is zero and only the
    // residual survives, so the fused value must be the weighted average of the
    // remapped constants — computable here in closed form. If the pyramid,
    // the expand, the weighting or the collapse is wrong in any way that does
    // not cancel, this is where it shows.
    {
        constexpr int kW = 24, kH = 16, kLevels = 4;
        const float t = 0.3f;
        const auto plan = sef::planFor(t, alpha, beta);

        orion::pipe::pyr::Plane flat{kW, kH, std::vector<float>(std::size_t(kW) * kH, t)};
        auto fused = sef::fuseReference(flat, plan, alpha, beta, sef::kSigma, kLevels);

        double sumW = 0.0, sumWV = 0.0;
        for (int k = -plan.dark; k <= plan.bright; ++k) {
            const double w = double(sef::wellExposed(sef::remap(t, k, plan, alpha, beta),
                                                     sef::kSigma)) *
                             double(sef::contrastWeight(t, k, plan, alpha, beta));
            sumW  += w;
            sumWV += w * double(sef::remap(t, k, plan, alpha, beta));
        }
        const double want = sumWV / sumW;

        double worst = 0.0, spread = 0.0;
        for (std::size_t i = 0; i < fused.v.size(); ++i) {
            worst  = std::max(worst,  std::abs(double(fused.v[i]) - want));
            spread = std::max(spread, std::abs(double(fused.v[i]) - double(fused.v[0])));
        }
        report(worst < 2e-4, "a flat field fuses to the weighted average of its remaps",
               "want " + std::to_string(want) + ", worst error " + std::to_string(worst));
        report(spread < 1e-5, "and stays flat — no seam from the pyramid or the borders",
               "spread " + std::to_string(spread));
    }

    // A ramp must not gain a reversal: the whole point is more local contrast,
    // not different tonal ordering.
    {
        constexpr int kW = 64, kH = 8, kLevels = 4;
        orion::pipe::pyr::Plane ramp{kW, kH, std::vector<float>(std::size_t(kW) * kH)};
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                ramp.v[std::size_t(y) * kW + x] = float(x) / float(kW - 1);

        const auto plan = sef::planFor(0.5f, alpha, beta);
        auto fused = sef::fuseReference(ramp, plan, alpha, beta, sef::kSigma, kLevels);

        // Exposure fusion does not *guarantee* monotonicity — it blends
        // Laplacian coefficients with spatially varying weights, and Mertens
        // et al. section 4.1 names a "spurious low frequency brightness change"
        // as a known artifact of exactly this. So the question is not whether
        // reversals exist but how large they are, and whether they grow with
        // the amplification — which is what says they come from the method
        // rather than from a mistake in the blend.
        const std::size_t row = std::size_t(kH / 2) * kW;
        const auto worstDropAt = [&](float a) {
            const auto q = sef::planFor(0.5f, a, beta);
            auto f = sef::fuseReference(ramp, q, a, beta, sef::kSigma, kLevels);
            double worst = 0.0;
            for (int x = 1; x < kW; ++x) {
                worst = std::max(worst, double(f.v[row + x - 1]) - double(f.v[row + x]));
            }
            return worst;
        };
        const double mild = worstDropAt(2.0f);
        const double mid  = worstDropAt(4.0f);
        const double full = worstDropAt(8.0f);
        std::printf("  ramp reversal by alpha: 2 -> %.2e, 4 -> %.2e, 8 -> %.2e\n",
                    mild, mid, full);

        report(mild < full && mid <= full,
               "reversals scale with the amplification, as the method implies",
               std::to_string(mild) + " < " + std::to_string(full));
        // A regression guard on the worst case, not an aspiration. If a change
        // to the blend pushes this up, that is the signal.
        report(full < 0.02, "and stay under two percent at full amplification",
               "worst " + std::to_string(full));
    }

    // ── Robust normalisation ──────────────────────────────────────────────
    {
        orion::pipe::pyr::Plane v{101, 1, std::vector<float>(101)};
        for (int i = 0; i <= 100; ++i) v.v[std::size_t(i)] = 0.2f + 0.006f * float(i);
        sef::robustNormalize(v, sef::kRobustClip);

        const auto mm = std::minmax_element(v.v.begin(), v.v.end());
        report(*mm.first <= 1e-5f && *mm.second >= 1.0f - 1e-5f,
               "robust normalisation stretches to the full range",
               std::to_string(*mm.first) + " … " + std::to_string(*mm.second));

        // The clip is what makes it robust: an outlier must not set the scale.
        orion::pipe::pyr::Plane spike = v;
        for (float& t : spike.v) t = 0.5f;
        spike.v[0] = 100.0f;
        sef::robustNormalize(spike, sef::kRobustClip);
        report(spike.v[1] > 0.0f && spike.v[1] <= 1.0f,
               "and a single wild outlier does not flatten everything else",
               std::to_string(spike.v[1]));
    }
}

/// Exposure fusion on the GPU — does the shader run the maths the tests pinned?
void testExposureFusionGpu() {
    section("Exposure fusion (GPU)");

    namespace sef = orion::pipe::sef;
    using orion::gpu::PixelFormat;

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

    constexpr std::uint32_t kW = 64, kH = 4;
    const auto plan = sef::planFor(0.4f, sef::kAlphaDefault, sef::kBeta);

    // ── The simulated images and weights, against the C++ mirror ──────────
    //
    // ops/fuse_ops.slang and pipe/ExposureFusion.h are two implementations of
    // the same equations, and the CPU one is what every other test in this file
    // measures against. If they disagree, those tests are pinning something the
    // product does not run.
    {
        auto kSplit = load("fuseSplit");
        auto proxy = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
        auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * kH);
        for (std::uint32_t x = 0; x < kW; ++x) {
            for (std::uint32_t y = 0; y < kH; ++y) {
                in[std::size_t(y) * kW + x] = __fp16(float(x) / float(kW - 1));
            }
        }
        proxy->upload(in.data(), std::size_t(kW) * sizeof(__fp16));

        const auto run = [&](int base, bool weights) {
            orion::pipe::params::FuseSplit sp{};
            sp.size[0] = kW; sp.size[1] = kH;
            sp.base = base;
            sp.weights = weights ? 1 : 0;
            sp.plan.images = plan.images; sp.plan.bright = plan.bright;
            sp.plan.dark = plan.dark;     sp.plan.span = plan.span();
            sp.plan.alpha = sef::kAlphaDefault; sp.plan.beta = sef::kBeta;
            sp.plan.lambda = sef::kLambda;      sp.plan.sigma = sef::kSigma;
            sp.epsilon = sef::kWeightEpsilon;

            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kSplit.second, {proxy.get(), dst.get()}, &sp, sizeof sp, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> out(std::size_t(kW) * kH * 4);
            dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
            return out;
        };

        double worstImage = 0.0, worstWeight = 0.0, worstSum = 0.0;
        std::vector<std::vector<__fp16>> images, weights;
        for (int st = 0; st * 4 < plan.images; ++st) {
            images.push_back(run(st * 4, false));
            weights.push_back(run(st * 4, true));
        }

        for (std::uint32_t x = 0; x < kW; ++x) {
            const float t = float(x) / float(kW - 1);

            double total = 0.0;
            for (int k = -plan.dark; k <= plan.bright; ++k) {
                total += double(sef::wellExposed(sef::remap(t, k, plan, sef::kAlphaDefault,
                                                            sef::kBeta), sef::kSigma)) *
                         double(sef::contrastWeight(t, k, plan, sef::kAlphaDefault, sef::kBeta));
            }

            double sum = 0.0;
            for (int index = 0; index < plan.images; ++index) {
                const int k = index - plan.dark;
                const std::size_t st = std::size_t(index / 4);
                const std::size_t at = (std::size_t(1) * kW + x) * 4 + std::size_t(index % 4);

                const double wantImage = sef::remap(t, k, plan, sef::kAlphaDefault, sef::kBeta);
                const double wantWeight =
                    double(sef::wellExposed(sef::remap(t, k, plan, sef::kAlphaDefault,
                                                       sef::kBeta), sef::kSigma)) *
                    double(sef::contrastWeight(t, k, plan, sef::kAlphaDefault, sef::kBeta)) /
                    std::max(total, double(sef::kWeightEpsilon));

                worstImage  = std::max(worstImage,  std::abs(double(images[st][at]) - wantImage));
                worstWeight = std::max(worstWeight, std::abs(double(weights[st][at]) - wantWeight));
                sum += double(weights[st][at]);
            }
            worstSum = std::max(worstSum, std::abs(sum - 1.0));
        }

        report(worstImage < 3e-3, "the shader simulates the same exposures as the reference",
               "worst " + std::to_string(worstImage));
        // Looser than the image comparison on purpose: the weights are
        // normalized to sum to one across the whole set, so the more images the
        // plan asks for the smaller each one is, and RGBA16Float's absolute
        // quantum does not shrink with them. The invariant that actually
        // matters — that they still sum to one — is checked below and is tight.
        report(worstWeight < 5e-3, "and computes the same weights",
               "worst " + std::to_string(worstWeight));
        // Mertens et al. section 3.2 normalizes so the weights sum to one at
        // every pixel. If they do not, the blend is a gain as well as a blend.
        report(worstSum < 5e-3, "the weights sum to one at every pixel",
               "worst " + std::to_string(worstSum));
    }

    // ── Strength zero is the identity, bit for bit ────────────────────────
    //
    // No published parameter of this method degenerates to the identity, which
    // is why the slider is a power on the emitted gain instead. gain^0 = 1, and
    // this is the check that says so rather than assuming it.
    {
        auto kApply = load("fuseApply");
        auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
        auto fused = orion::gpu::Texture::create(*device, 8, 2, PixelFormat::R16Float);
        auto proxy = orion::gpu::Texture::create(*device, 8, 2, PixelFormat::R16Float);
        auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * kH * 4);
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
            in[i * 4 + 0] = __fp16(0.02 + 0.9 * double(i % kW) / double(kW - 1));
            in[i * 4 + 1] = __fp16(0.30);
            in[i * 4 + 2] = __fp16(0.11);
            in[i * 4 + 3] = __fp16(1.0f);
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        // A deliberately violent gain: the fused proxy says every pixel should
        // move a long way, so a strength that is not exactly zero will show.
        std::vector<__fp16> a(16, __fp16(0.9f)), b(16, __fp16(0.1f));
        fused->upload(a.data(), 8 * sizeof(__fp16));
        proxy->upload(b.data(), 8 * sizeof(__fp16));

        const auto run = [&](float strength) {
            orion::pipe::params::FuseApply ap{};
            ap.size[0] = kW; ap.size[1] = kH;
            ap.proxySize[0] = 8; ap.proxySize[1] = 2;
            ap.slope = sef::kProxySlopeEv;
            ap.strength = strength;
            ap.maxGain = sef::kMaxGain;

            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kApply.second, {src.get(), fused.get(), proxy.get(), dst.get()},
                        &ap, sizeof ap, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> out(in.size());
            dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
            return out;
        };

        const auto off = run(0.0f);
        int differing = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if (float(off[i]) != float(in[i])) ++differing;
        }
        report(differing == 0, "strength zero leaves every pixel bit-identical",
               std::to_string(differing) + " of " + std::to_string(in.size()) + " differ");

        const auto on = run(1.0f);
        double lift = 0.0;
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
            lift = std::max(lift, double(on[i * 4]) / std::max(double(in[i * 4]), 1e-6));
        }
        report(lift > 1.5, "and full strength actually lifts", "max gain " + std::to_string(lift));

        // The clamp replaces the paper's global renormalisation, so it has to
        // hold even when the proxy asks for something absurd.
        report(lift <= double(sef::kMaxGain) + 1e-3,
               "the gain is clamped where the paper would have renormalized",
               "max gain " + std::to_string(lift));
    }
}

/// Auto-enhance reads a histogram. This is that reading, without a GPU.
void testAutoEnhanceStats() {
    section("Auto-enhance (statistics)");

    namespace ae = orion::pipe::auto_enhance;
    constexpr std::uint32_t kBins = 256;

    // A flat histogram: the p-th percentile is p, to within a bin.
    {
        std::vector<std::uint32_t> h(kBins, 100u);
        double worst = 0.0;
        for (double f = 0.05; f <= 0.95; f += 0.05) {
            worst = std::max(worst, std::abs(double(ae::percentileOf(h.data(), kBins, f)) - f));
        }
        report(worst < 1.0 / double(kBins) + 1e-6,
               "percentiles of a flat histogram are the fraction itself",
               "worst " + std::to_string(worst));
    }

    // Everything in one bin: every percentile is that bin, whatever is asked.
    {
        std::vector<std::uint32_t> h(kBins, 0u);
        h[64] = 5000u;
        report(std::abs(ae::percentileOf(h.data(), kBins, 0.01f) - 64.0f / 256.0f) < 1e-6 &&
               std::abs(ae::percentileOf(h.data(), kBins, 0.99f) - 64.0f / 256.0f) < 1e-6,
               "a single-valued image reports that value at every percentile", "");
    }

    // The bin's lower edge, not its center. With a coarse histogram the center
    // reports a black point above zero on an image that genuinely contains
    // black, and the correction then lifts a picture that needed nothing.
    {
        std::vector<std::uint32_t> h(kBins, 0u);
        h[0] = 1000u;
        report(ae::percentileOf(h.data(), kBins, 0.5) == 0.0f,
               "an image sitting in bin zero reports exactly zero, not half a bin",
               std::to_string(ae::percentileOf(h.data(), kBins, 0.5)));
    }

    // An empty histogram must not divide by its own total.
    {
        std::vector<std::uint32_t> h(kBins, 0u);
        report(ae::percentileOf(h.data(), kBins, 0.5) == 0.0f,
               "an empty histogram is answered, not divided by", "");
    }

    // Combining is a sum over channels: the bin index is a value, and a value's
    // frequency is how often any channel took it.
    {
        std::vector<std::uint32_t> rgb(std::size_t(kBins) * 3, 0u);
        rgb[10] = 3u;                    // red
        rgb[kBins + 10] = 4u;            // green
        rgb[2 * kBins + 10] = 5u;        // blue
        std::vector<std::uint32_t> out(kBins, 0u);
        ae::combine(rgb.data(), kBins, out.data());
        report(out[10] == 12u, "the three channel histograms combine by summing",
               std::to_string(out[10]));
    }

    // ── The solver ────────────────────────────────────────────────────────
    //
    // Driven against a stand-in for the pipeline rather than the real one, so
    // the controller's behavior is testable without a GPU or a photograph.
    // The stand-in compresses like a display transform does, which is the
    // property that makes a full correction overshoot and the damping
    // necessary.
    {
        const auto renderStub = [](float sceneMedian, float ev) {
            const float lifted = sceneMedian * std::exp2(ev);
            // A soft shoulder — not AgX, but non-linear in the same direction.
            return lifted / (1.0f + lifted);
        };

        bool allConverged = true;
        double worst = 0.0;
        for (const float scene : {0.05f, 0.08f, 0.35f, 1.2f, 4.0f}) {
            ae::Controls c{};
            for (int pass = 0; pass < ae::kMaxPasses; ++pass) {
                ae::Stats st{};
                st.median = renderStub(scene, c.exposureEv);
                st.shadow = st.median * 0.4f;
                st.high   = std::min(1.0f, st.median * 1.8f);
                c = ae::refine(c, st);
            }
            const float finalMedian = renderStub(scene, c.exposureEv);
            const double err = std::abs(double(finalMedian) - double(ae::kMidGray));
            worst = std::max(worst, err);
            if (err > 0.05) allConverged = false;
        }
        report(allConverged,
               "the exposure solver converges on the mid-gray anchor across four stops of scene",
               "worst " + std::to_string(worst));

        // Past its clamp it must saturate, not oscillate. A frame this dark
        // wanted a different photograph, and an automatic control that will
        // move ten stops turns a mistake into a bigger one.
        ae::Controls c{};
        float previous = 0.0f;
        bool settled = true;
        for (int pass = 0; pass < 8; ++pass) {
            ae::Stats st{};
            st.median = renderStub(0.001f, c.exposureEv);
            st.shadow = st.median * 0.4f;
            st.high   = std::min(1.0f, st.median * 1.8f);
            c = ae::refine(c, st);
            if (pass > 0 && c.exposureEv < previous - 1e-6f) settled = false;
            previous = c.exposureEv;
        }
        report(settled && std::abs(c.exposureEv - ae::kMaxExposureEv) < 1e-4f,
               "a frame darker than the clamp saturates at it rather than oscillating",
               std::to_string(c.exposureEv));
    }

    // A picture already against the stops must not be pushed further out —
    // Simplest Color Balance section 1 warns that saturation "can create flat
    // white regions or flat black regions that may look unnatural".
    {
        ae::Controls c{};
        ae::Stats st{};
        st.median = ae::kMidGray;
        st.shadow = 0.1f; st.high = 0.9f;
        st.atFloor = 0.10f; st.atCeiling = 0.10f;    // already clipping hard
        const auto next = ae::refine(c, st);
        report(next.blacks == 0.0f && next.whites == 0.0f,
               "an already-clipping picture has its endpoints left alone",
               std::to_string(next.blacks) + " / " + std::to_string(next.whites));
    }

    // And the published slope cap has to actually bind: a picture that already
    // uses a narrow range would be stretched past smax = 2 without it.
    {
        ae::Controls c{};
        ae::Stats st{};
        st.median = ae::kMidGray;
        st.shadow = 0.45f; st.high = 0.55f;          // span 0.1 -> slope 10
        const auto next = ae::refine(c, st);
        report(next.blacks == 0.0f && next.whites == 0.0f,
               "the slope cap stops a low-contrast frame being stretched tenfold",
               std::to_string(next.blacks) + " / " + std::to_string(next.whites));
    }

    // The look controls respond to the picture, and stay inside their range.
    {
        const auto darkLook   = ae::look({0.05f, 0.4f, 0.08f, 0.0f, 0.0f});
        const auto brightLook = ae::look({0.3f, 0.98f, 0.75f, 0.0f, 0.0f});
        report(darkLook.fusion > brightLook.fusion,
               "a dark frame is given more shadow lift than a bright one",
               std::to_string(darkLook.fusion) + " vs " + std::to_string(brightLook.fusion));
        report(brightLook.fusion == 0.0f,
               "and a frame already above mid gray is given none", "");
        report(darkLook.fusion <= 1.0f && darkLook.clarity <= 1.0f,
               "the look controls stay inside the range the sliders have", "");
    }

    // Clipping is reported, because a picture already against the stops does
    // not want its endpoints pushed further out.
    {
        std::vector<std::uint32_t> h(kBins, 10u);
        h[0] = 500u;
        h[kBins - 1] = 250u;
        const auto s = ae::measure(h.data(), kBins, 0.005);
        const double total = 500.0 + 250.0 + 254.0 * 10.0;
        report(std::abs(double(s.atFloor) - 500.0 / total) < 1e-4 &&
               std::abs(double(s.atCeiling) - 250.0 / total) < 1e-4,
               "measure reports how much is already clipped at each end",
               std::to_string(s.atFloor) + " / " + std::to_string(s.atCeiling));
        report(s.shadow < s.median && s.median < s.high,
               "and the three percentiles come back in order", "");
    }
}

/// Mask primitives, and the thing about them most likely to be got wrong.

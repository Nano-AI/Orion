// Local Laplacian clarity and dehaze — the two local operators, each with the
// synthetic frame its own claim needs.
//
// `testLocalLaplacianGpu` owns a 48x32 plane carrying a step, a ripple and a
// ramp, because the filter's whole job is telling those three apart;
// `testDehazeGpu` owns three small frames, one per claim. Neither reads the
// other's, so they are cheap neighbours — and a new effect's checks land here,
// with the frame that exercises them.
//
// The display transform left for `tests_display.cpp` and the clipped-highlight
// family for `tests_highlights.cpp` on 2026-08-02; see decision #127.

#include "harness.h"

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

/// Simulated exposure fusion — the maths, before any of it reaches a shader.

// Non-local means on luminance — research/high-iso.md §4.
//
// Four things, in the order the research file says they are measured: the
// kernel agrees with the paper's algorithm written out on the CPU; zero is
// byte-identical and leaves the node unrun; it removes flat noise; and it
// keeps a fine periodic texture that the wavelet chain, at the same flat
// noise removal, does not — with both retentions printed, so the gain is a
// figure rather than a claim.

#include "harness.h"
#include "pipe/NonLocalMeans.h"

#include <random>
#include <set>

namespace {

using Frame = std::vector<__fp16>;

constexpr float kScaleNorm[4] = {0.8907f, 0.2007f, 0.0855f, 0.0412f};

float luma2020(float r, float g, float b) {
    return 0.2627f * r + 0.6780f * g + 0.0593f * b;
}

/// Equal R, G and B, so luma is the value itself and the chroma vector is
/// zero: a card that isolates the luminance path.
struct Card {
    std::uint32_t w, h;
    float sigma;
    Frame rgba;

    /// Left half flat, right half a period-4 ripple of amplitude `ripple`,
    /// both at grey `level` with Gaussian noise `sigma`.
    Card(std::uint32_t width, std::uint32_t height, float level, float noise, float ripple)
        : w(width), h(height), sigma(noise), rgba(std::size_t(width) * height * 4) {
        std::mt19937 rng(4242);
        std::normal_distribution<float> g(0.0f, 1.0f);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                float v = level + sigma * g(rng);
                if (x >= w / 2) v += ripple * std::cos(float(x) * 1.5707963f);
                const std::size_t i = (std::size_t(y) * w + x) * 4;
                rgba[i] = rgba[i + 1] = rgba[i + 2] = __fp16(v);
                rgba[i + 3] = __fp16(1.0f);
            }
        }
    }

    [[nodiscard]] float lumaAt(const Frame& f, std::uint32_t x, std::uint32_t y) const {
        const std::size_t i = (std::size_t(y) * w + x) * 4;
        return luma2020(float(f[i]), float(f[i + 1]), float(f[i + 2]));
    }

    /// RMS of luma against its own 3x3 mean over the interior of a column
    /// range, `RegionStats.noise`'s construction.
    [[nodiscard]] float rms3(const Frame& f, std::uint32_t x0, std::uint32_t x1) const {
        double acc = 0.0; std::size_t n = 0;
        for (std::uint32_t y = 1; y + 1 < h; ++y) {
            for (std::uint32_t x = std::max(x0, 1u); x + 1 < std::min(x1, w); ++x) {
                double mean = 0.0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        mean += lumaAt(f, x + std::uint32_t(dx), y + std::uint32_t(dy));
                mean /= 9.0;
                const double d = lumaAt(f, x, y) - mean;
                acc += d * d; ++n;
            }
        }
        return float(std::sqrt(acc / double(std::max<std::size_t>(n, 1))));
    }

    /// The ripple's amplitude in the right half, by projection onto the
    /// cosine it was drawn with. Noise averages out over the half.
    [[nodiscard]] float rippleAmplitude(const Frame& f) const {
        double acc = 0.0; std::size_t n = 0;
        for (std::uint32_t y = 2; y + 2 < h; ++y) {
            for (std::uint32_t x = w / 2 + 2; x + 2 < w; ++x) {
                acc += lumaAt(f, x, y) * std::cos(float(x) * 1.5707963f); ++n;
            }
        }
        return float(2.0 * acc / double(std::max<std::size_t>(n, 1)));
    }

    [[nodiscard]] float mean(const Frame& f) const {
        double acc = 0.0;
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) acc += lumaAt(f, x, y);
        return float(acc / (double(w) * h));
    }

    [[nodiscard]] orion::pipe::pyr::Plane lumaPlane(const Frame& f) const {
        orion::pipe::pyr::Plane p{int(w), int(h), {}};
        p.v.resize(std::size_t(w) * h);
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) p.v[std::size_t(y) * w + x] = lumaAt(f, x, y);
        return p;
    }
};

struct Gpu {
    orion::gpu::Device& device;
    std::unique_ptr<orion::gpu::Library> nlmLib, blurLib, shrinkLib;
    std::unique_ptr<orion::gpu::Kernel> nlm, blur, shrink;

    explicit Gpu(orion::gpu::Device& d) : device(d) {
        const std::string dir = ORION_SHADER_DIR;
        nlmLib = orion::gpu::Library::createFromFile(d, dir + "/nlmLuma.metallib");
        nlm = orion::gpu::Kernel::create(d, *nlmLib, "nlmLuma");
        blurLib = orion::gpu::Library::createFromFile(d, dir + "/atrousBlur.metallib");
        blur = orion::gpu::Kernel::create(d, *blurLib, "atrousBlur");
        shrinkLib = orion::gpu::Library::createFromFile(d, dir + "/atrousShrink.metallib");
        shrink = orion::gpu::Kernel::create(d, *shrinkLib, "atrousShrink");
    }

    std::unique_ptr<orion::gpu::Texture> make(const Card& c) const {
        return orion::gpu::Texture::create(device, c.w, c.h, orion::gpu::PixelFormat::RGBA16Float);
    }

    Frame download(const Card& c, const orion::gpu::Texture& t) const {
        Frame out(std::size_t(c.w) * c.h * 4);
        t.download(out.data(), std::size_t(c.w) * 4 * sizeof(__fp16), c.w, c.h);
        return out;
    }

    Frame runNlm(const Card& c, float k, int radius, int patch) {
        auto in = make(c), out = make(c);
        in->upload(c.rgba.data(), std::size_t(c.w) * 4 * sizeof(__fp16));
        orion::pipe::params::Nlm p{};
        p.size[0] = c.w; p.size[1] = c.h;
        p.noiseA = 0.0f; p.noiseB = c.sigma * c.sigma;
        p.k = k; p.radius = radius; p.patch = patch;
        orion::gpu::CommandBuffer cb(device);
        cb.dispatch(*nlm, {in.get(), out.get()}, &p, sizeof p, c.w, c.h);
        cb.commitAndWait();
        return download(c, *out);
    }

    /// The capture chain's four blurs and four shrinks at luma strength
    /// `beta`, dispatched by hand — the comparison the research file asks for.
    Frame runWavelet(const Card& c, float beta) {
        auto c0 = make(c);
        c0->upload(c.rgba.data(), std::size_t(c.w) * 4 * sizeof(__fp16));
        std::vector<std::unique_ptr<orion::gpu::Texture>> blurs, shrinks;
        for (int j = 0; j < 4; ++j) { blurs.push_back(make(c)); shrinks.push_back(make(c)); }
        for (int j = 0; j < 4; ++j) {
            orion::pipe::params::AtrousBlur bp{};
            bp.size[0] = c.w; bp.size[1] = c.h; bp.step = 1 << j;
            orion::gpu::CommandBuffer cb(device);
            cb.dispatch(*blur, {j == 0 ? c0.get() : blurs[std::size_t(j - 1)].get(),
                                blurs[std::size_t(j)].get()}, &bp, sizeof bp, c.w, c.h);
            cb.commitAndWait();
        }
        for (int j = 3; j >= 0; --j) {
            orion::pipe::params::AtrousShrink sp{};
            sp.size[0] = c.w; sp.size[1] = c.h;
            sp.noiseA = 0.0f; sp.noiseB = c.sigma * c.sigma;
            sp.scaleNorm = kScaleNorm[j]; sp.step = 1 << j;
            sp.strengthLuma = beta; sp.strengthChroma = 0.0f;
            const auto* fine = (j == 0) ? c0.get() : blurs[std::size_t(j - 1)].get();
            const auto* coarse = blurs[std::size_t(j)].get();
            const auto* accum = (j == 3) ? coarse : shrinks[std::size_t(j + 1)].get();
            orion::gpu::CommandBuffer cb(device);
            cb.dispatch(*shrink, {fine, coarse, accum, coarse, shrinks[std::size_t(j)].get()},
                        &sp, sizeof sp, c.w, c.h);
            cb.commitAndWait();
        }
        return download(c, *shrinks[0]);
    }
};

/// A flat mid-grey mosaic with the noise estimator's own construction, so the
/// pipeline fits a model and the chain has something to run on.
orion::raw::BayerImage noisyMosaic(std::uint32_t w, std::uint32_t h) {
    constexpr float kWhite = 16383.0f;
    constexpr double kA = 4.0e-4, kB = 4.0e-5, kSignal = 0.25;
    orion::raw::BayerImage img;
    img.width = w; img.height = h;
    img.filters = 0x94949494u;
    img.white = std::uint16_t(kWhite);
    img.black = {0, 0, 0, 0};
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};
    img.samples.resize(std::size_t(w) * h);
    std::mt19937 rng(777);
    std::normal_distribution<double> g(0.0, 1.0);
    const double sigma = std::sqrt(kA * kSignal + kB);
    for (auto& s : img.samples)
        s = std::uint16_t(std::clamp(kSignal + sigma * g(rng), 0.0, 1.0) * kWhite);
    return img;
}

std::vector<std::uint8_t> outputBytes(const orion::pipe::DevelopPipeline& p) {
    const auto row = p.outputWidth() * orion::gpu::bytesPerPixel(p.output().format());
    std::vector<std::uint8_t> bytes(row * p.outputHeight());
    p.output().download(bytes.data(), row, p.outputWidth(), p.outputHeight());
    return bytes;
}

}  // namespace

void testNlmGpu() {
    section("Non-local means on luminance (GPU)");
    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    Gpu gpu(*device);

    // σ = 0.02 on a 0.25 card is a very noisy frame; the ripple sits at 6σ.
    const Card card(96, 96, 0.25f, 0.02f, 0.12f);
    const float noiseBefore = card.rms3(card.rgba, 0, card.w / 2);
    const float rippleBefore = card.rippleAmplitude(card.rgba);
    report(rippleBefore > 0.1f, "the card carries its ripple", std::to_string(rippleBefore));

    // 1. The kernel is the paper's algorithm: agreement with the CPU reference,
    //    at the shipped window.
    {
        const Frame got = gpu.runNlm(card, 0.4f, orion::pipe::nlm::kRadius, orion::pipe::nlm::kPatch);
        const auto want = orion::pipe::nlm::reference(card.lumaPlane(card.rgba),
                                                      orion::pipe::nlm::kRadius,
                                                      orion::pipe::nlm::kPatch,
                                                      0.0f, card.sigma * card.sigma, 0.4f);
        float worst = 0.0f;
        for (std::uint32_t y = 0; y < card.h; ++y)
            for (std::uint32_t x = 0; x < card.w; ++x)
                worst = std::max(worst, std::fabs(card.lumaAt(got, x, y) -
                                                  want.v[std::size_t(y) * card.w + x]));
        report(worst < 1e-3f, "GPU agrees with the CPU reference within 1e-3",
               "worst " + std::to_string(worst));
        report(std::fabs(card.mean(got) - card.mean(card.rgba)) < 1e-3f,
               "mean shift under 1e-3");
    }

    // 2. Removal and retention at both windows, against the wavelet chain at
    //    matched removal. The shipped window is asserted; the published one
    //    is printed beside it so the trade the research file records stays
    //    measured.
    const int windows[2][2] = {{orion::pipe::nlm::kRadius, orion::pipe::nlm::kPatch},
                               {orion::pipe::nlm::kPublishedRadius, orion::pipe::nlm::kPublishedPatch}};
    for (int wi = 0; wi < 2; ++wi) {
        const int r = windows[wi][0], f = windows[wi][1];
        const Frame got = gpu.runNlm(card, 0.4f, r, f);
        const float noiseAfter = card.rms3(got, 0, card.w / 2);
        const float keep = card.rippleAmplitude(got) / rippleBefore;

        float bestBeta = 0.0f, bestNoise = 1e9f, waveletKeep = 0.0f;
        for (float beta = 0.5f; beta <= 4.01f; beta += 0.5f) {
            const Frame w = gpu.runWavelet(card, beta);
            const float n = card.rms3(w, 0, card.w / 2);
            if (std::fabs(n - noiseAfter) < std::fabs(bestNoise - noiseAfter)) {
                bestNoise = n; bestBeta = beta;
                waveletKeep = card.rippleAmplitude(w) / rippleBefore;
            }
        }
        std::printf("  %dx%d / %dx%d, k 0.4: flat noise %.5f -> %.5f, ripple keeps %.3f;"
                    " wavelet at beta %.1f (noise %.5f) keeps %.3f\n",
                    2 * r + 1, 2 * r + 1, 2 * f + 1, 2 * f + 1,
                    double(noiseBefore), double(noiseAfter), double(keep),
                    double(bestBeta), double(bestNoise), double(waveletKeep));
        if (wi == 0) {
            report(noiseAfter <= 0.5f * noiseBefore, "flat noise falls by at least half at k = 0.4",
                   std::to_string(noiseBefore) + " -> " + std::to_string(noiseAfter));
            report(keep >= 0.8f, "a 6-sigma period-4 ripple keeps at least 80% of its amplitude",
                   std::to_string(keep));
            report(keep > waveletKeep, "and more of it than the wavelet chain at matched removal");
        }
    }

    // 3. Through the pipeline: zero is byte-identical and the node does not run.
    {
        const auto img = noisyMosaic(256, 256);
        report(orion::raw::estimateNoise(img).measured, "the fixture's noise is measured");
        orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, img);
        d.setWideOutput(true);
        orion::pipe::Adjustments adj{};
        d.apply(adj); d.render();
        const auto never = outputBytes(d);
        std::set<std::string> ran;
        for (const auto& n : d.graph().lastRun()) if (n.executed) ran.insert(n.name);
        report(!ran.count("denoise:nlm"), "at 0 the node does not run");

        adj.denoiseNlm = 0.4f;
        d.apply(adj); d.render();
        const auto on = outputBytes(d);
        ran.clear();
        for (const auto& n : d.graph().lastRun()) if (n.executed) ran.insert(n.name);
        report(ran.count("denoise:nlm") == 1, "at 0.4 the node runs");
        report(on != never, "and renders a different frame");

        adj.denoiseNlm = 0.0f;
        d.apply(adj); d.render();
        report(outputBytes(d) == never, "back at 0 the render is byte-identical");
    }
}

// Region-of-interest rendering, issue #22 and decision #364: a region must be
// the same pixels as the same crop of the graph it stands in for.
#include "harness.h"
#include "Engine.h"

namespace {
// A noisy mosaic with edges in it, so demosaic, denoise, sharpening, the lens
// and the straighten all have something to move: rings and a diagonal step.
orion::raw::BayerImage structured(std::uint32_t w, std::uint32_t h) {
    auto img = noisyGrey(w, h, 0.25, 2e-4, 1e-6, 22u);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const double r = std::hypot(double(x) - 0.4 * w, double(y) - 0.55 * h);
            double k = 0.6 + 0.4 * std::sin(r / 9.0);
            if (x + 2 * y > w) k *= 1.8;
            auto& s = img.samples[std::size_t(y) * w + x];
            s = std::uint16_t(std::min(16383.0, double(s) * k));
        }
    }
    return img;
}

std::vector<std::uint8_t> crop(const std::vector<std::uint8_t>& px, std::uint32_t stride,
                               std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> out;
    out.reserve(std::size_t(w) * h * 4);
    for (std::uint32_t r = 0; r < h; ++r) {
        const auto* row = px.data() + (std::size_t(y + r) * stride + x) * 4;
        out.insert(out.end(), row, row + std::size_t(w) * 4);
    }
    return out;
}

/// Largest per-channel difference, in eight-bit steps, and how many differ at all.
std::pair<int, std::size_t> difference(const std::vector<std::uint8_t>& a,
                                       const std::vector<std::uint8_t>& b) {
    int worst = 0;
    std::size_t differing = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const int d = std::abs(int(a[i]) - int(b[i]));
        worst = std::max(worst, d);
        differing += d != 0;
    }
    return {worst, differing};
}
}  // namespace

void testRegionMatchesCropGpu() {
    section("A region is the same crop of the graph it stands in for (#22)");

    // 1536 x 1024 so the quarter graph is exactly 384 x 256 and a 0.75 crop is
    // whole pixels in both — the comparison at step 4 needs both grids exact.
    constexpr std::uint32_t kW = 1536, kH = 1024;
    try {
        orion::Engine engine;
        engine.openDecoded(structured(kW, kH));

        orion::pipe::Adjustments a;
        a.wb = engine.develop().asShotWhiteBalance();
        a.exposureEv = 0.4f;
        a.contrast = 1.3f;
        a.lensDistortion = 0.5f;   // a barrel warp and its autoscale, off-centre in any window
        a.lensCaRed = 0.4f;
        a.straightenDeg = 3.0f;
        a.cropX = 0.125f; a.cropY = 0.125f;
        a.cropW = 0.75f;  a.cropH = 0.75f;
        a.sharpenAmount = 0.8f;
        a.denoiseLuma = 1.0f;
        a.denoiseSmoothness = 0.5f;
        engine.setAdjustments(a);
        engine.render();
        const auto& full = engine.develop();
        const std::uint32_t fw = full.outputWidth(), fh = full.outputHeight();
        report(fw == 1152 && fh == 768, "the full picture is the 0.75 crop",
               std::to_string(fw) + "x" + std::to_string(fh));

        // ── Step 1: a 100% view of an off-centre corner ─────────────────────
        //
        // Tolerance: none beyond one eight-bit step on a handful of pixels. The
        // window's origin is a multiple of 16, so the CFA phase and the 4x4
        // dither phase are the full graph's; the only arithmetic that differs
        // is the lens centre, shifted by the window's origin in float, which
        // can move a bilinear weight by an ulp and a rounding by one step.
        const std::uint32_t rx = 640, ry = 96, rw = 448, rh = 320;
        const orion::Engine::Region view{float(rx) / fw, float(ry) / fh,
                                         float(rw) / fw, float(rh) / fh, rw, rh};
        const double ms = engine.renderRegion(view);
        report(ms >= 0.0 && engine.regionStep() == 1, "a 100% view renders at step 1",
               "step " + std::to_string(engine.regionStep()));
        const auto& reg = engine.regionDevelop();
        report(reg.outputWidth() == rw && reg.outputHeight() == rh, "at the size asked for");
        report(std::uint64_t(reg.width()) * reg.height() * 2 < std::uint64_t(kW) * kH,
               "from a window smaller than half the frame",
               std::to_string(reg.width()) + "x" + std::to_string(reg.height()));
        auto want = crop(pixels(full), fw, rx, ry, rw, rh);
        auto [worst, differing] = difference(pixels(reg), want);
        std::printf("  step 1, %ux%u window: max %d step(s), %zu of %zu channels differ\n",
                    reg.width(), reg.height(), worst, differing, want.size());
        report(worst <= 1 && differing * 1000 < want.size(),
               "every pixel within one step of the full render's crop, under 0.1% off at all",
               "max " + std::to_string(worst) + ", " + std::to_string(differing) + " of " +
                   std::to_string(want.size()) + " channels differ");

        // A drag tick through the same window moves it as it moves the full graph.
        a.exposureEv = 0.9f;
        engine.setAdjustments(a);
        engine.render();
        engine.renderRegion(view);
        want = crop(pixels(full), fw, rx, ry, rw, rh);
        std::tie(worst, differing) = difference(pixels(reg), want);
        report(worst <= 1 && differing * 1000 < want.size(),
               "and still after an exposure tick, without a rebuild",
               "max " + std::to_string(worst));

        // ── Step 4: the whole picture at a quarter, against the preview graph ─
        //
        // Denoise and sharpening off: the preview measures its own noise from
        // its quarter mosaic where the region takes the frame's, on purpose.
        a.sharpenAmount = 0.0f; a.denoiseLuma = 0.0f; a.denoiseSmoothness = 0.0f;
        engine.setAdjustments(a);
        engine.renderPreview();
        const auto& pv = engine.previewDevelop();
        const std::uint32_t pw = pv.outputWidth(), ph = pv.outputHeight();
        const std::uint32_t qx = 32, qy = 16, qw = 160, qh = 112;
        engine.renderRegion({float(qx) / pw, float(qy) / ph, float(qw) / pw, float(qh) / ph, qw, qh});
        report(engine.regionStep() == 4, "a quarter view renders at step 4",
               "step " + std::to_string(engine.regionStep()) + ", preview " +
                   std::to_string(pw) + "x" + std::to_string(ph));
        want = crop(pixels(pv), pw, qx, qy, qw, qh);
        std::tie(worst, differing) = difference(pixels(engine.regionDevelop()), want);
        std::printf("  step 4: max %d step(s), %zu of %zu channels differ\n",
                    worst, differing, want.size());
        report(worst <= 1 && differing * 1000 < want.size(),
               "and matches the same crop of the preview graph",
               "max " + std::to_string(worst) + ", " + std::to_string(differing) + " differ");

        // ── The histogram follows the edit, not the last full render ────────
        //
        // Past fit the canvas shows regions and the full graph waits; the
        // preview, the small whole-frame pass, is what has seen the edit. The
        // sample count says which graph was read: one pixel in 31.
        const auto samples = [&] {
            std::vector<std::uint32_t> bins(64 * 3);
            engine.histogram(bins.data(), 64);
            std::uint64_t n = 0;
            for (int i = 0; i < 64; ++i) n += bins[std::size_t(i)];
            return n;
        };
        const auto sampled = [](std::uint64_t pixels) { return (pixels + 30) / 31; };
        a.exposureEv = 0.5f;
        engine.setAdjustments(a);
        engine.renderRegion(view);
        engine.renderPreview();
        report(samples() == sampled(std::uint64_t(pw) * ph),
               "after a region and the preview, the histogram reads the preview");
        engine.render();
        report(samples() == sampled(std::uint64_t(fw) * fh), "after a full render, the full graph");

        // ── What the region graph refuses ───────────────────────────────────
        a.clarity = 0.3f;
        engine.setAdjustments(a);
        report(engine.renderRegion(view) < 0.0, "clarity reads the whole frame: refused");
    } catch (const std::exception& e) {
        report(false, "region rendering", e.what());
    }
}

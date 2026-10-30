// Region-of-interest rendering, issue #22 and decision #371: a region must be
// the same pixels as the same crop of the graph it stands in for.
#include "harness.h"
#include "Engine.h"

#include <filesystem>

namespace {
// A noisy mosaic with edges in it, so demosaic, denoise, sharpening, the lens
// and the straighten all have something to move: rings and a diagonal step.
orion::raw::BayerImage structured(std::uint32_t w, std::uint32_t h) {
    auto img = noisyGrey(w, h, 0.25, 4e-3, 1e-5, 22u);   // noisy enough that denoise moves pixels
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
        a.denoiseLuma = 3.0f;
        a.denoiseColor = 3.0f;       // Smoothness needs Color: the widest support
        a.denoiseSmoothness = 0.5f;
        a.denoiseNlm = 0.5f;
        engine.setAdjustments(a);
        engine.render();
        const auto& full = engine.develop();
        // Without a measured noise model the denoise chain, the widest support
        // here, stays off and the halo goes untested.
        report(full.noiseProfile().measured, "the frame's noise is measured, so denoise runs");
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
        auto fullPx = pixels(full);   // read first: a region gives the full graph's textures back
        const double ms = engine.renderRegion(view);
        report(ms >= 0.0 && engine.regionStep() == 1, "a 100% view renders at step 1",
               "step " + std::to_string(engine.regionStep()));
        const auto& reg = engine.regionDevelop();
        report(reg.outputWidth() == rw && reg.outputHeight() == rh, "at the size asked for");
        report(std::uint64_t(reg.width()) * reg.height() < std::uint64_t(kW) * kH,
               "from a window smaller than the frame (this one is small; the halo is 192)",
               std::to_string(reg.width()) + "x" + std::to_string(reg.height()));
        const auto& g = full.graph();
        report(g.intermediateBytes() == g.sourceTexture().sizeBytes(),
               "past fit the full graph gives its textures back once a region is shown");
        auto want = crop(fullPx, fw, rx, ry, rw, rh);
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
        fullPx = pixels(full);
        engine.renderRegion(view);
        want = crop(fullPx, fw, rx, ry, rw, rh);
        std::tie(worst, differing) = difference(pixels(reg), want);
        report(worst <= 1 && differing * 1000 < want.size(),
               "and still after an exposure tick, without a rebuild",
               "max " + std::to_string(worst));

        // ── Step 4: the whole picture at a quarter, against the preview graph ─
        //
        // Denoise and sharpening off: the preview measures its own noise from
        // its quarter mosaic where the region takes the frame's, on purpose.
        a.sharpenAmount = 0.0f; a.denoiseLuma = 0.0f; a.denoiseColor = 0.0f;
        a.denoiseSmoothness = 0.0f; a.denoiseNlm = 0.0f;
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

        // Mid-drag at 100%: a region, no preview, the full graph released. The
        // readouts render the small pass themselves rather than read nothing.
        a.exposureEv = 0.6f;
        engine.setAdjustments(a);
        engine.renderRegion(view, true);
        report(samples() == sampled(std::uint64_t(pw) * ph),
               "a region alone, the full graph released: the histogram renders and reads the preview");
        float shownRgb[3] = {-1, -1, -1}, sceneRgb[3] = {-1, -1, -1};
        engine.sampleAt(0.5f, 0.5f, shownRgb, sceneRgb);
        report(shownRgb[0] >= 0.0f && sceneRgb[0] >= 0.0f, "and the eyedropper reads a colour, no throw");

        // An export past fit rebuilds the full graph to read it, then gives it back.
        const auto& fg = full.graph();
        const auto released = fg.intermediateBytes();
        report(released == fg.sourceTexture().sizeBytes(), "past fit the full graph holds only its source");
        const auto out = (std::filesystem::temp_directory_path() / "orion-roi-export.jpg").string();
        engine.exportImage(out, orion::util::ExportOptions{});
        report(std::filesystem::file_size(out) > 0 && fg.intermediateBytes() == released,
               "an export past fit writes the file and leaves the full graph released again",
               std::to_string(fg.intermediateBytes()) + " vs " + std::to_string(released));
        std::filesystem::remove(out);

        // ── Denoise renders at step 1 whatever the zoom ─────────────────────
        // Its supports are fixed in graph pixels; a coarser step would change the look.
        a.denoiseLuma = 1.0f;
        engine.setAdjustments(a);
        engine.renderRegion({float(qx) / pw, float(qy) / ph, float(qw) / pw, float(qh) / ph, qw, qh});
        report(engine.regionStep() == 1, "with denoise on, a quarter view still renders at step 1");
        // Mid-drag that would cost more than the preview: refused, so the drag
        // keeps the preview's cost. At 100% the region is no dearer, and stays.
        report(engine.renderRegion({float(qx) / pw, float(qy) / ph, float(qw) / pw, float(qh) / ph, qw, qh},
                                   true) < 0.0,
               "mid-drag, a denoised quarter view is refused: the preview renders the drag");
        report(engine.renderRegion(view, true) >= 0.0 && engine.regionStep() == 1,
               "mid-drag at 100%, the denoised region still renders");
        a.denoiseLuma = 0.0f;

        // ── What the region graph refuses ───────────────────────────────────
        a.clarity = 0.3f;
        engine.setAdjustments(a);
        report(engine.renderRegion(view) < 0.0, "clarity reads the whole frame: refused");
    } catch (const std::exception& e) {
        report(false, "region rendering", e.what());
    }
}

void testRegionAtFrameEdgesGpu() {
    section("A region at the frame's far edges, turned and keystoned, at step 2 (#22)");

    // Sides that are not multiples of 16 (1544 % 16 = 8, 1036 % 16 = 12; at
    // step 2, 772 and 518), so a window that reaches the bottom-right edge has
    // to run to it rather than stop on the alignment grid.
    constexpr std::uint32_t kW = 1544, kH = 1036;
    try {
        orion::Engine engine;
        engine.openDecoded(structured(kW, kH));
        orion::pipe::Adjustments a;
        a.wb = engine.develop().asShotWhiteBalance();
        a.contrast = 1.3f;
        a.lensDistortion = 0.5f;
        a.sharpenAmount = 0.8f;
        engine.setAdjustments(a);
        engine.render();
        const auto& full = engine.develop();

        // ── Step 1: the bottom-right corner of an uncropped frame ───────────
        {
            const std::uint32_t fw = full.outputWidth(), fh = full.outputHeight();
            const std::uint32_t rw = 400, rh = 300, rx = fw - rw, ry = fh - rh;
            const auto fullPx = pixels(full);
            engine.renderRegion({float(rx) / fw, float(ry) / fh, float(rw) / fw, float(rh) / fh, rw, rh});
            const auto want = crop(fullPx, fw, rx, ry, rw, rh);
            const auto [worst, differing] = difference(pixels(engine.regionDevelop()), want);
            std::printf("  step 1 corner: max %d, %zu of %zu differ\n", worst, differing, want.size());
            report(engine.regionStep() == 1 && worst <= 1 && differing * 1000 < want.size(),
                   "the bottom-right corner at 100% is the full render's, last rows and columns included",
                   "max " + std::to_string(worst) + ", " + std::to_string(differing) + " differ");
        }

        // ── Step 2: a quarter turn, a keystone, a crop off any 4-pixel grid ─
        //
        // Against the whole frame decimated by two, as the region stands in for
        // it. The dither is applied before the geometry node, in frame pixels,
        // and the window's origin is a multiple of 16 of them whatever the crop
        // or the turn — so its 4x4 phase holds here as at step 1.
        a.rotateQuarters = 1;
        a.perspectiveVertical = 0.4f;
        a.cropX = 0.125f; a.cropY = 0.125f;   // 129.5 and 193 frame pixels
        a.cropW = 0.75f;  a.cropH = 0.75f;
        engine.setAdjustments(a);
        engine.render();
        orion::pipe::DevelopPipeline half(engine.device(), ORION_SHADER_DIR,
                                          orion::raw::decimate(structured(kW, kH), 2));
        half.setGridStep(2.0f);
        half.setNoiseProfile(full.noiseProfile());
        half.apply(a);
        half.render();
        const std::uint32_t hw = half.outputWidth(), hh = half.outputHeight();
        const std::uint32_t qw = 200, qh = 150, qx = hw - qw, qy = hh - qh;
        engine.renderRegion({float(qx) / hw, float(qy) / hh, float(qw) / hw, float(qh) / hh, qw, qh});
        const auto want = crop(pixels(half), hw, qx, qy, qw, qh);
        const auto [worst, differing] = difference(pixels(engine.regionDevelop()), want);
        std::printf("  step 2 turned corner: max %d, %zu of %zu differ (step %d)\n",
                    worst, differing, want.size(), engine.regionStep());
        report(engine.regionStep() == 2 && worst <= 1 && differing * 1000 < want.size(),
               "the turned, keystoned bottom-right corner at step 2 is the half graph's",
               "step " + std::to_string(engine.regionStep()) + ", max " + std::to_string(worst));
    } catch (const std::exception& e) {
        report(false, "region at the frame's edges", e.what());
    }
}

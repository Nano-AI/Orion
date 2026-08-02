// The film grain plate: determinism, statistics, and the mip chain the preview
// depends on. research/film-grain.md, decision #81.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"
#include "pipe/GrainPlate.h"

namespace grain = orion::pipe::grain;

namespace {

/// Mean and standard deviation of one level of the stacked plate.
struct LevelStats {
    double mean = 0.0;
    double sd = 0.0;
};

LevelStats levelStats(const std::vector<float>& plate, int level) {
    const int n = grain::kPlateSize >> level;
    const std::size_t row = static_cast<std::size_t>(grain::levelOffset(level));
    double sum = 0.0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            sum += plate[(row + y) * grain::kPlateSize + x];
    const double mean = sum / (double(n) * n);
    double var = 0.0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double d = plate[(row + y) * grain::kPlateSize + x] - mean;
            var += d * d;
        }
    }
    return {mean, std::sqrt(var / (double(n) * n))};
}

}  // namespace

void testGrainPlate() {
    section("Film grain plate");

    const auto plate = grain::buildPlate();
    report(plate.size() == std::size_t(grain::kPlateSize) * grain::kPlateHeight,
           "the plate is the size the shader will index");

    // ⚠ The whole export-matches-preview argument rests on this: a level must
    // start exactly where the shader's closed form says it does. The shader
    // recomputes `levelOffset` from the same expression, so a mismatch here is
    // a level read from the wrong rows — plausible noise, wrong field.
    report(grain::levelOffset(0) == 0, "level 0 starts at row 0");
    report(grain::levelOffset(1) == grain::kPlateSize, "level 1 starts below it");
    report(grain::levelOffset(2) == grain::kPlateSize + grain::kPlateSize / 2,
           "and level 2 below that");
    report(grain::levelOffset(grain::kPlateLevels - 1) + 1 <= grain::kPlateHeight,
           "the whole chain fits in the texture",
           "needs " + std::to_string(grain::levelOffset(grain::kPlateLevels - 1) + 1));

    // Level 0 is the calibrated one: `amount` is defined against unit variance,
    // so if this drifts every Amount setting silently means something else.
    const auto l0 = levelStats(plate, 0);
    checkNear(l0.mean, 0.0, 1e-6, "level 0 is zero mean");
    checkNear(l0.sd, 1.0, 1e-5, "level 0 has unit standard deviation");

    // ⚠ **The falling standard deviation down the chain is the point, not a
    // defect.** A preview pixel covering sixteen frame pixels must show the
    // variance sixteen average to. If every level were renormalized to 1.0 the
    // 1/16 preview would look exactly as grainy as the full render — which is
    // the failure the plate exists to prevent, and it would pass any check that
    // only asserted "the levels are noise".
    const auto l1 = levelStats(plate, 1);
    const auto l2 = levelStats(plate, 2);
    report(l1.sd < l0.sd * 0.95, "level 1 is calmer than level 0",
           std::to_string(l1.sd) + " against " + std::to_string(l0.sd));
    report(l2.sd < l1.sd * 0.95, "and level 2 calmer than level 1",
           std::to_string(l2.sd) + " against " + std::to_string(l1.sd));

    // Each level is still centerd, or averaging would introduce a brightness
    // shift that varies with zoom — grain that lightens the preview.
    checkNear(l1.mean, 0.0, 1e-5, "level 1 is still zero mean");
    checkNear(l2.mean, 0.0, 1e-5, "level 2 is still zero mean");

    // ⚠ Correlated, not white. This is what separates film grain from sensor
    // noise, and it is the reason the plate is blurred at all. A white field
    // has ~zero correlation between neighbors; a grain field does not.
    // Measured on level 0 against its own horizontal neighbor.
    {
        double num = 0.0, den = 0.0;
        const int n = grain::kPlateSize;
        for (int y = 0; y < n; y += 4) {
            for (int x = 0; x < n - 1; ++x) {
                const double a = plate[std::size_t(y) * n + x];
                const double b = plate[std::size_t(y) * n + x + 1];
                num += a * b;
                den += a * a;
            }
        }
        const double corr = den > 0 ? num / den : 0.0;
        report(corr > 0.3, "neighboring texels are correlated — grain has a size",
               "correlation " + std::to_string(corr));
    }

    // ⚠ Determinism, which is the property that keeps export matching preview.
    // Two builds from one seed must agree **bit for bit**, not nearly: the
    // generator is written out precisely because `std::normal_distribution`
    // gives different fields on different standard libraries from the same
    // seed.
    {
        const auto again = grain::buildPlate();
        bool identical = again.size() == plate.size();
        std::size_t differing = 0;
        for (std::size_t i = 0; identical && i < plate.size(); ++i)
            if (again[i] != plate[i]) { ++differing; identical = false; }
        report(identical, "two builds from the same seed are bit-identical",
               identical ? "" : "first difference at texel " + std::to_string(differing));

        const auto other = grain::buildPlate(12345u);
        bool same = true;
        for (std::size_t i = 0; i < 4096 && same; ++i) same = (other[i] == plate[i]);
        report(!same, "and a different seed gives a different field");
    }
}

// ── The kernel ─────────────────────────────────────────────────────────────

void testGrainGpu() {
    section("Film grain (GPU)");

    using orion::gpu::PixelFormat;
    // ⚠ 512 rows, not 64. The field is *correlated* by design, so a band holds far
    // fewer independent samples than it holds pixels — at 64 rows the estimator's
    // own spread was ~9%, which is most of the tolerance the variance-law check
    // wants to spend on the shader being wrong.
    constexpr std::uint32_t kW = 256, kH = 512;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/grain.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "grain");

    auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto plate = orion::gpu::Texture::create(*device, grain::kPlateSize,
                                             grain::kPlateHeight, PixelFormat::R32Float);
    {
        const auto field = grain::buildPlate();
        plate->upload(field.data(), std::size_t(grain::kPlateSize) * sizeof(float));
    }

    // A horizontal ramp from black to white, constant down each column. Every
    // display level appears, which is what lets the variance law be measured
    // *as a function of luminance* rather than as one number.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float v = float(x) / float(kW - 1);
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = input[i + 1] = input[i + 2] = static_cast<__fp16>(v);
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    const auto run = [&](float amount, float grainSize, float gridStep,
                         std::uint32_t dither) {
        orion::pipe::params::Grain p{};
        p.size[0] = kW; p.size[1] = kH;
        p.dither = dither;
        p.amount = amount;
        p.grainSize = grainSize;
        p.gridStep = gridStep;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), plate.get(), dst.get()}, &p, sizeof p, kW, kH);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(kW) * kH * 4);
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
        return out;
    };

    // ⚠ **Amount 0 must be a copy plus the dither this node inherited.** The
    // whole quantisation-boundary move rests on it: `develop_display` stopped
    // adding the Bayer offset and stopped clamping, and if this kernel does not
    // do both, in that order, every render shifts. Checked against the CPU's
    // own arithmetic rather than against "roughly the input", because "roughly"
    // is satisfied by a kernel that has dropped the dither entirely.
    //
    // ⚠ Measured into **RGBA32Float**, not the half format the graph uses. The
    // smallest Bayer offsets are 0.03/255, which is *below* half's ULP near
    // white — so a check at half precision cannot distinguish "the dither is
    // there" from "the dither is gone" for four of the sixteen cells. The
    // format is the instrument here, and picking a coarse one would have made
    // this the sixth check in `repro/README.md` that could not fail.
    {
        auto wide = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA32Float);
        orion::pipe::params::Grain p{};
        p.size[0] = kW; p.size[1] = kH;
        p.dither = 1u; p.amount = 0.0f; p.grainSize = 1.5f; p.gridStep = 1.0f;
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), plate.get(), wide.get()}, &p, sizeof p, kW, kH);
        cb.commitAndWait();
        std::vector<float> out(std::size_t(kW) * kH * 4);
        wide->download(out.data(), std::size_t(kW) * 4 * sizeof(float), kW, kH);
        static const float kBayer[16] = {
             0.0f,  8.0f,  2.0f, 10.0f,
            12.0f,  4.0f, 14.0f,  6.0f,
             3.0f, 11.0f,  1.0f,  9.0f,
            15.0f,  7.0f, 13.0f,  5.0f,
        };
        std::size_t wrong = 0;
        float worst = 0.0f;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                const float in = float(input[i + 1]);
                const int b = int((y & 3u) * 4u + (x & 3u));
                const float want = std::clamp(in + (kBayer[b] / 16.0f - 0.5f) / 255.0f,
                                              0.0f, 1.0f);
                // ⚠ Not `==`. Metal compiles with fast math, so the shader may
                // contract `(k/16 - 0.5)/255` differently from the host. The
                // tolerance is one part in ten million — four orders below the
                // smallest dither offset this has to be able to see.
                const float got = out[i + 1];
                if (std::abs(got - want) > 1e-6f) {
                    ++wrong;
                    worst = std::max(worst, std::abs(got - want));
                }
            }
        }
        report(wrong == 0,
               "at Amount 0 the kernel is a copy plus the Bayer dither",
               wrong == 0 ? "" : std::to_string(wrong) + " samples differ, worst "
                                 + std::to_string(worst));
    }

    // The dither has to be skippable, or the wide export path would carry an
    // eight-bit offset it has no use for.
    {
        const auto out = run(0.0f, 1.5f, 1.0f, 0u);
        std::size_t wrong = 0;
        for (std::size_t i = 0; i < out.size(); i += 4)
            if (float(out[i + 1]) != float(input[i + 1])) ++wrong;
        report(wrong == 0, "with the dither off it is a plain copy",
               std::to_string(wrong) + " samples differ");
    }

    /// Mean signed and mean absolute deviation from the input, over the middle
    /// of the ramp where the variance law is loudest.
    struct Dev { double mean = 0.0, absMean = 0.0, sd = 0.0; };
    const auto deviation = [&](const std::vector<__fp16>& out,
                               std::uint32_t x0, std::uint32_t x1) {
        double sum = 0.0, absSum = 0.0;
        std::size_t n = 0;
        std::vector<double> d;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = x0; x < x1; ++x) {
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                const double e = double(float(out[i + 1])) - double(float(input[i + 1]));
                sum += e; absSum += std::abs(e); ++n; d.push_back(e);
            }
        }
        Dev r;
        r.mean = sum / double(n);
        r.absMean = absSum / double(n);
        double var = 0.0;
        for (double v : d) var += (v - r.mean) * (v - r.mean);
        r.sd = std::sqrt(var / double(n));
        return r;
    };

    constexpr float kAmount = 0.05f;
    const auto grainy = run(kAmount, 1.5f, 1.0f, 0u);

    // ⚠ **Mean absolute, not mean.** Grain is zero-mean by construction, so a
    // check on the mean difference passes just as happily on a kernel that adds
    // nothing at all. This is the assertion that fails when the plate is not
    // bound, when `amount` never reaches the shader, or when the early exit is
    // taken for every pixel.
    {
        const auto mid = deviation(grainy, kW / 4, 3 * kW / 4);
        report(mid.absMean > 0.005,
               "grain reaches the picture — mean |difference| over the midtones",
               std::to_string(mid.absMean));
        report(std::abs(mid.mean) < mid.absMean * 0.15,
               "and it is zero-mean, so it does not shift exposure",
               "mean " + std::to_string(mid.mean) + " against |mean| "
                   + std::to_string(mid.absMean));
    }

    // ── The variance law itself ────────────────────────────────────────────
    //
    // Newson, Delon & Galerne (CGF 36(8), 2017): a Boolean model of mean
    // coverage u has Bernoulli variance u(1-u), so sigma = amount*sqrt(Y(1-Y)) —
    // loudest at mid-gray, silent at both ends.
    //
    // ⚠ Measured **across the whole ramp against the closed form**, not as
    // "the middle is grainier than the ends". The weaker phrasing was what this
    // check started as, and it is satisfied by any curve with a hump in it —
    // including a plain triangle, which would be visibly wrong in the shadows
    // of a night frame and passes an ordering test comfortably.
    //
    // It is also what keeps the `saturate` from becoming a mean shift: where
    // clipping could bite, sigma is already zero.
    {
        constexpr int kBands = 16;
        double worstRel = 0.0;
        int worstBand = -1;
        for (int b = 0; b < kBands; ++b) {
            const auto x0 = std::uint32_t(b) * kW / kBands;
            const auto x1 = std::uint32_t(b + 1) * kW / kBands;
            const auto d = deviation(grainy, x0, x1);
            // The band's mean luminance, which is what the law is a function of.
            const double y = (double(x0) + double(x1) - 1.0) / 2.0 / double(kW - 1);
            const double want = kAmount * std::sqrt(y * (1.0 - y));
            // Relative error is meaningless where the prediction is near zero
            // and the estimator's own noise dominates, so the two end bands are
            // checked by the absolute bound below instead.
            if (want < kAmount * 0.25) continue;
            const double rel = std::abs(d.sd - want) / want;
            if (rel > worstRel) { worstRel = rel; worstBand = b; }
            if (std::getenv("ORION_GRAIN_BANDS"))
                std::printf("    band %2d  Y %.3f  want %.5f  got %.5f  %+.1f%%\n",
                            b, y, want, d.sd, (d.sd - want) / want * 100.0);
        }
        report(worstRel < 0.20,
               "sigma follows amount * sqrt(Y(1-Y)) across the ramp",
               "worst band " + std::to_string(worstBand) + " off by "
                   + std::to_string(worstRel * 100.0) + "%");

        // And it really does go quiet at the ends — the property that makes the
        // clamp harmless. The first column is Y = 0 exactly, where the law says
        // zero and a constant-sigma kernel says `amount`.
        const auto black = deviation(grainy, 0, 1);
        const auto white = deviation(grainy, kW - 1, kW);
        report(black.sd < kAmount * 0.02, "no grain at all on pure black",
               std::to_string(black.sd));
        report(white.sd < kAmount * 0.02, "and none on pure white",
               std::to_string(white.sd));
    }

    // Monochrome. Per-channel noise is what a sensor does; film grain is
    // luminance, and #81 says so explicitly.
    {
        std::size_t split = 0;
        for (std::size_t i = 0; i < grainy.size(); i += 4) {
            const float r = float(grainy[i + 0]) - float(input[i + 0]);
            const float g = float(grainy[i + 1]) - float(input[i + 1]);
            if (std::abs(r - g) > 1e-3f) ++split;
        }
        report(split == 0, "grain is monochrome, not per-channel",
               std::to_string(split) + " samples differ between R and G");
    }

    // ⚠ **The preview and the render must sample one field at two resolutions,
    // not two realisations of it.** `gridStep` is what says so: at 4 the kernel
    // reads further down the chain, where the field has already been averaged,
    // so a preview pixel shows the variance its sixteen frame pixels average
    // to. Without this the 1/16 preview reads an order of magnitude grainier
    // than the picture it previews — and every check above still passes.
    {
        const auto preview = run(kAmount, 1.5f, 4.0f, 0u);
        const auto full = deviation(grainy,  kW / 2 - kW / 32, kW / 2 + kW / 32);
        const auto prev = deviation(preview, kW / 2 - kW / 32, kW / 2 + kW / 32);
        report(prev.sd < full.sd * 0.75,
               "a preview pixel is calmer than a frame pixel — one field, two grids",
               std::to_string(prev.sd) + " against " + std::to_string(full.sd));
        report(prev.sd > full.sd * 0.05,
               "but not silent — it reads a coarser level, not an empty one",
               std::to_string(prev.sd));

        // ⚠ **Calmer is not enough, and this is the check that says so.** The
        // two above pass on a kernel that ignores `gridStep` when addressing
        // the plate and uses it only to pick a mip level — a mutation that does
        // exactly that survived them both. Such a preview shows grain of the
        // right *size* in the wrong *place*: settle the drag and the grain
        // jumps, which is the artifact `degrade-then-refine` exists to prevent
        // and which no measure of loudness can see.
        //
        // So render a real preview: a quarter-size grid at gridStep 4, against
        // the full render box-averaged 4x4. Same field, same place, so they
        // must correlate. A kernel keyed to the output grid instead of the
        // frame correlates at zero.
        {
            constexpr std::uint32_t pW = kW / 4, pH = kH / 4;
            auto pSrc = orion::gpu::Texture::create(*device, pW, pH, PixelFormat::RGBA16Float);
            auto pDst = orion::gpu::Texture::create(*device, pW, pH, PixelFormat::RGBA16Float);
            std::vector<__fp16> flatIn(std::size_t(pW) * pH * 4);
            for (std::size_t i = 0; i < flatIn.size(); i += 4) {
                flatIn[i + 0] = flatIn[i + 1] = flatIn[i + 2] = static_cast<__fp16>(0.5f);
                flatIn[i + 3] = 1;
            }
            pSrc->upload(flatIn.data(), std::size_t(pW) * 4 * sizeof(__fp16));

            orion::pipe::params::Grain pp{};
            pp.size[0] = pW; pp.size[1] = pH;
            pp.dither = 0u; pp.amount = kAmount; pp.grainSize = 4.0f; pp.gridStep = 4.0f;
            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kernel, {pSrc.get(), plate.get(), pDst.get()}, &pp, sizeof pp, pW, pH);
            cb.commitAndWait();
            std::vector<__fp16> pOut(std::size_t(pW) * pH * 4);
            pDst->download(pOut.data(), std::size_t(pW) * 4 * sizeof(__fp16), pW, pH);

            // The same field at full rate, then averaged down the way the eye
            // averages a settled render into a preview-sized area on screen.
            auto fSrc = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
            std::vector<__fp16> fullIn(std::size_t(kW) * kH * 4);
            for (std::size_t i = 0; i < fullIn.size(); i += 4) {
                fullIn[i + 0] = fullIn[i + 1] = fullIn[i + 2] = static_cast<__fp16>(0.5f);
                fullIn[i + 3] = 1;
            }
            fSrc->upload(fullIn.data(), std::size_t(kW) * 4 * sizeof(__fp16));
            orion::pipe::params::Grain fp{};
            fp.size[0] = kW; fp.size[1] = kH;
            fp.dither = 0u; fp.amount = kAmount; fp.grainSize = 4.0f; fp.gridStep = 1.0f;
            orion::gpu::CommandBuffer cb2(*device);
            cb2.dispatch(*kernel, {fSrc.get(), plate.get(), dst.get()}, &fp, sizeof fp, kW, kH);
            cb2.commitAndWait();
            std::vector<__fp16> fOut(std::size_t(kW) * kH * 4);
            dst->download(fOut.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

            double sa = 0.0, sb = 0.0;
            std::vector<double> A, B;
            A.reserve(std::size_t(pW) * pH); B.reserve(std::size_t(pW) * pH);
            for (std::uint32_t y = 0; y < pH; ++y) {
                for (std::uint32_t x = 0; x < pW; ++x) {
                    double acc = 0.0;
                    for (std::uint32_t dy = 0; dy < 4; ++dy)
                        for (std::uint32_t dx = 0; dx < 4; ++dx)
                            acc += double(float(fOut[((std::size_t(y) * 4 + dy) * kW
                                                     + x * 4 + dx) * 4 + 1]));
                    const double a = acc / 16.0;
                    const double b = double(float(pOut[(std::size_t(y) * pW + x) * 4 + 1]));
                    A.push_back(a); B.push_back(b); sa += a; sb += b;
                }
            }
            const double ma = sa / double(A.size()), mb = sb / double(B.size());
            double num = 0.0, da = 0.0, db = 0.0;
            for (std::size_t i = 0; i < A.size(); ++i) {
                num += (A[i] - ma) * (B[i] - mb);
                da  += (A[i] - ma) * (A[i] - ma);
                db  += (B[i] - mb) * (B[i] - mb);
            }
            const double corr = (da > 0 && db > 0) ? num / std::sqrt(da * db) : 0.0;
            report(corr > 0.7,
                   "the preview shows the same grain, in the same place, as the render",
                   "correlation " + std::to_string(corr));
        }
    }

    // ⚠ **The Amount slider must mean the same thing at every Size.** The plate
    // is sampled at a rate set by `grainSize`, and bilinear interpolation of a
    // random field *reduces its variance* — by an amount that depends on the
    // rate. So the naive kernel has a Size control that silently changes
    // strength, which is the kind of coupling a photographer reads as "the
    // grain slider is unpredictable" and never reports as a bug.
    // ⚠ Measured on a **flat mid-gray field, not the ramp**. A 16-column band
    // of the ramp spans only two plate texels at Size 8, so the estimator's own
    // spread there is ~6% — enough to invent a coupling that is not real, or
    // hide one that is. The flat field gives every Size the same few thousand
    // independent texels, which is the only way the sizes are comparable to
    // each other at all.
    {
        auto flat = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
        std::vector<__fp16> mid(std::size_t(kW) * kH * 4);
        for (std::size_t i = 0; i < mid.size(); i += 4) {
            mid[i + 0] = mid[i + 1] = mid[i + 2] = static_cast<__fp16>(0.5f);
            mid[i + 3] = 1;
        }
        flat->upload(mid.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        const auto sigmaAt = [&](float size) {
            orion::pipe::params::Grain p{};
            p.size[0] = kW; p.size[1] = kH;
            p.dither = 0u; p.amount = kAmount; p.grainSize = size; p.gridStep = 1.0f;
            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kernel, {flat.get(), plate.get(), dst.get()}, &p, sizeof p, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> out(std::size_t(kW) * kH * 4);
            dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

            double sum = 0.0;
            for (std::size_t i = 0; i < out.size(); i += 4) sum += double(float(out[i + 1]));
            const double mean = sum / double(out.size() / 4);
            double var = 0.0;
            for (std::size_t i = 0; i < out.size(); i += 4) {
                const double e = double(float(out[i + 1])) - mean;
                var += e * e;
            }
            return std::sqrt(var / double(out.size() / 4));
        };

        // Peak sigma would be `amount * sqrt(0.25)` = amount/2 at mid-gray if
        // the plate were read at its own resolution. It is not: the field is
        // resampled bilinearly at a rate the Size slider sets, and interpolating
        // a correlated random field *reduces its variance*. Measured at
        // kGrainSigmaFactor of nominal, flat across the whole range.
        const double want = kAmount * 0.5 * orion::pipe::params::kGrainSigmaFactor;
        double lo = 1e9, hi = 0.0, worst = 0.0;
        for (float size : {1.2f, 1.5f, 2.0f, 3.0f, 5.0f, 8.0f}) {
            const double got = sigmaAt(size);
            if (std::getenv("ORION_GRAIN_BANDS"))
                std::printf("    size %.1f  sigma %.5f  (%.1f%% of nominal)\n",
                            double(size), got, got / (kAmount * 0.5) * 100.0);
            lo = std::min(lo, got); hi = std::max(hi, got);
            worst = std::max(worst, std::abs(got - want) / want);
        }
        // ⚠ **The invariant a photographer can feel is this one**: turning Size
        // must change how big the grain is and *not* how strong it is. A kernel
        // that resamples without accounting for the loss couples the two, which
        // reads as "the grain slider is unpredictable" and gets reported as
        // taste rather than as a bug.
        report((hi - lo) / lo < 0.04,
               "Size changes how big the grain is, not how strong",
               "strength varies by " + std::to_string((hi - lo) / lo * 100.0)
                   + "% across Size 1.2-8");
        // And the factor itself is pinned, so a change to the plate's blur or to
        // the interpolator cannot silently rescale every existing edit.
        report(worst < 0.04, "and Amount is the peak sigma the factor predicts",
               "worst Size off the prediction by " + std::to_string(worst * 100.0) + "%");
    }

    // Size scales the field rather than regenerating it, so a larger grain is
    // spatially smoother. Measured as the difference between neighbors, which
    // is what "clumpy" means and what a per-pixel hash would fail.
    {
        const auto fine   = run(kAmount, 1.0f, 1.0f, 0u);
        const auto coarse = run(kAmount, 8.0f, 1.0f, 0u);
        const auto roughness = [&](const std::vector<__fp16>& o) {
            double sum = 0.0; std::size_t n = 0;
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = kW / 4; x + 1 < 3 * kW / 4; ++x) {
                    const std::size_t i = (std::size_t(y) * kW + x) * 4;
                    sum += std::abs(double(float(o[i + 4 + 1])) - double(float(o[i + 1])));
                    ++n;
                }
            }
            return sum / double(n);
        };
        report(roughness(coarse) < roughness(fine) * 0.6,
               "a larger grain size is spatially smoother",
               std::to_string(roughness(coarse)) + " against "
                   + std::to_string(roughness(fine)));
    }
}

// ── The wiring, which is a different claim from the kernel ─────────────────
//
// `testGrainGpu` above dispatches `grain` directly with parameters it sets
// itself. It proves the maths and it can never prove the slider reaches it —
// the same kernel-versus-wiring split that let dehaze be deleted with every
// instrument green (STATUS, session `2026-07-31e`).
//
// ⚠ All three checks below are here because all three failed during the
// session that wrote them:
//
//   1. The node ran at Amount 0, because it was added enabled and nothing ever
//      disabled it. A full-resolution pointwise pass on every frame of every
//      drag — the exposure slider went from 3 nodes and 10.6 ms p95 to 4 and
//      **17.0 ms**, past the 16 ms M0 gate, and the app felt it before the
//      bench was next run.
//   2. The node did not run at Amount 0.04, because the retarget pushed
//      parameters from `lastAdj_` — the *previous* frame's values — so it
//      switched the node on and immediately handed it Amount 0. The shader took
//      its early exit. Every test stayed green; only the bench's control probe
//      noticed, and only because it measures the picture.
//   3. Returning to Amount 0 did not restore the original bytes, which would
//      silently rebase every `identical` export baseline in the repository.
//
// None of these is a defect in `grain.slang`. That is the point.
void testGrainWiring() {
    section("Film grain wiring");

    namespace pipe = orion::pipe;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    // A horizontal ramp, constant down every column.
    //
    // ⚠ **Not a flat patch, and the reason is the dither check below.** A flat
    // frame lands on one pre-quantisation value, and whether a sub-LSB offset
    // changes the rounded byte then depends on where that single value happens
    // to sit between two 8-bit levels — so "the dither is applied" and "the
    // dither is gone" can produce the same bytes for an unlucky fixture. A ramp
    // crosses every level boundary somewhere, so the offset is always visible.
    //
    // It also spans the midtones, which the grain checks need: the Boolean
    // model's variance law is `sqrt(Y(1-Y))`, so grain is silent at both ends
    // and a black or blown fixture would measure a working node as a dead one.
    orion::raw::BayerImage img;
    img.width = 64;
    img.height = 64;
    img.samples.resize(std::size_t(64) * 64);
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            img.samples[std::size_t(y) * 64 + x] =
                static_cast<std::uint16_t>(200 + x * 50);
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

    /// Did `develop:grain` dispatch on the last render?
    const auto grainRan = [&] {
        for (const auto& n : dev->graph().lastRun())
            if (n.name == "develop:grain") return n.executed;
        return false;
    };

    /// The visible frame, as bytes. RGBA8Unorm, so this is exactly what the
    /// screen and an 8-bit export get — which is what makes bit-identity a
    /// claim about the product rather than about an intermediate.
    const auto frame = [&] {
        std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        return px;
    };

    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};

    // ── 1. Off means off ───────────────────────────────────────────────────
    adj.grainAmount = 0.0f;
    dev->apply(adj);
    dev->render();
    const auto quiet = frame();
    report(!grainRan(), "the grain node does not run at Amount 0");

    // ── 2. On means on, and the slider is what turns it on ─────────────────
    adj.grainAmount = 0.04f;
    dev->apply(adj);
    dev->render();
    const auto grainy = frame();
    report(grainRan(), "the grain node runs when the Amount slider is up");

    // ⚠ Mean *absolute* difference, not mean. Grain is zero-mean by
    // construction — that is what keeps it from shifting exposure — so a check
    // on average brightness reads zero for a working node and zero for one that
    // was never dispatched.
    double sum = 0.0;
    std::size_t moved = 0;
    for (std::size_t i = 0; i < quiet.size(); ++i) {
        if ((i & 3u) == 3u) continue;                    // alpha
        const double d = std::abs(double(grainy[i]) - double(quiet[i]));
        sum += d;
        if (d > 0.0) ++moved;
    }
    const double mad = sum / double(quiet.size() * 3 / 4);
    report(mad > 0.5, "Amount 0.04 visibly moves the picture",
           "mean |delta| " + std::to_string(mad) + " of 255");
    report(moved > quiet.size() / 8,
           "and it moves most of the frame, not a corner of it",
           std::to_string(moved) + " samples");

    // ── 3. Back to zero is back to the byte ────────────────────────────────
    //
    // The chain retargets when Amount crosses zero: `develop:display` goes from
    // RGBA8Unorm to RGBA16Float and back, and the Bayer dither moves between the
    // two nodes. If either half of that fails to reverse, Amount 0 stops being
    // the picture the repository's `identical` baselines were taken against —
    // and it fails silently, because a re-dithered frame looks fine.
    adj.grainAmount = 0.0f;
    dev->apply(adj);
    dev->render();
    const auto again = frame();
    report(again == quiet,
           "Amount back at 0 is bit-identical to never having touched it");
    report(!grainRan(), "and the node is disabled again");

    // ── 3b. Something still dithers ────────────────────────────────────────
    //
    // ⚠ **The bit-identity above compares the pipeline to itself.** It catches a
    // retarget that fails to reverse; it cannot catch one that comes to rest in
    // the wrong place, because both captures would be equally wrong and equally
    // identical. The dither is the thing that would go missing quietly — grain
    // took it away from `develop:display` and hands it back, and a hand-back
    // that never happens is invisible until someone opens a smooth sky at 200%.
    // The mutation that drops the flag fails exactly this check.
    //
    // The Bayer table is 4x4 and the CFA is 2x2, so on a ramp that is constant
    // down each column two rows tell the two apart:
    //
    //   * rows 4 apart share a dither phase *and* a CFA phase — equal either way
    //   * rows 2 apart share the CFA phase and differ in dither phase — equal
    //     only if nothing dithered
    {
        const auto rowsDiffer = [&](std::uint32_t dy) {
            std::size_t n = 0;
            for (std::uint32_t y = 8; y + dy < h - 8; ++y)
                for (std::uint32_t x = 8; x < w - 8; ++x)
                    for (int c = 0; c < 3; ++c)
                        if (again[(std::size_t(y) * w + x) * 4 + c] !=
                            again[(std::size_t(y + dy) * w + x) * 4 + c]) ++n;
            return n;
        };
        const std::size_t two = rowsDiffer(2), four = rowsDiffer(4);
        report(four == 0,
               "rows a whole dither period apart are identical",
               std::to_string(four) + " differing samples");
        report(two > 0,
               "and rows half a period apart are not — something dithered",
               std::to_string(two) + " differing samples");
    }

    // ⚠ **Two mutations survive this file, and neither is a defect in it.**
    //
    // Leaving `develop:display` on `RGBA16Float` with the grain node disabled
    // passes everything above, and correctly: the offset is added in the
    // shader whatever that node's own format is, and the geometry node still
    // rounds it. What it costs is 194 MB and a doubled write on every frame of
    // every drag — a latency regression, which is the bench's job and not this
    // binary's. It is the regression this whole test exists because of, and the
    // instrument that caught it was `orion-bench` exiting 1.
    //
    // Dithering in *both* nodes at once with the slider up doubles the offset
    // to ±1/255. Real, and not covered here: at Amount 0 the two flags are
    // equal by construction, and at Amount 0.04 the grain is louder than the
    // artifact. Said plainly rather than left as an unexplained green.

    // ── 4. What a drag costs, which is the whole reason for any of this ────
    //
    // ⚠ Counted from a small exposure move on a *warm* graph, never from the
    // renders above. The first render after construction is dirty everywhere
    // and runs twelve nodes; a drag runs three. Comparing one against the other
    // measures which render came first, which is what the first draft of this
    // check did — it failed 2 against 12 and was right to.
    //
    // ⚠ And the grain slider is moved on its own render before each count, not
    // in the same `apply` as the exposure, or the retarget's own reallocation
    // would be inside the number being attributed to the exposure tick.
    const auto dragCost = [&](float ev) {
        adj.exposureEv = ev;
        dev->apply(adj);
        dev->render();
        int n = 0;
        for (const auto& r : dev->graph().lastRun()) if (r.executed) ++n;
        return n;
    };

    const int quietDrag = dragCost(0.1f);

    adj.grainAmount = 0.04f;
    dev->apply(adj);
    dev->render();
    const int grainyDrag = dragCost(0.2f);

    adj.grainAmount = 0.0f;
    dev->apply(adj);
    dev->render();
    const int againDrag = dragCost(0.3f);

    // ⚠ The counts, not just the flags. `grainRan()` going false would still be
    // satisfied by a graph that had left `develop:display` writing float — the
    // eight-bit rounding would move into the geometry node, with no dither, and
    // band every smooth sky with nothing here to see it. These say what the
    // feature costs when it is on and that it costs nothing when it is off.
    report(againDrag == quietDrag,
           "an exposure tick costs the same as before grain existed",
           std::to_string(againDrag) + " nodes against " + std::to_string(quietDrag));
    report(grainyDrag == quietDrag + 1,
           "and exactly one node more with the slider up",
           std::to_string(grainyDrag) + " against " + std::to_string(quietDrag));
}

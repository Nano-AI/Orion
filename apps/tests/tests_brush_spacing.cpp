// How far apart dabs may be laid before a stroke stops being a stroke.
//
// `research/brush-nib.md` is the derivation; this is the measurement that keeps
// it honest against the shader that actually draws.
//
// The short version: a stroke is a row of overlapping discs, so its edge is not
// straight — between two neighbouring dab centres the union dips inward by the
// sagitta of the chord, and that dip is what reads as beading. The nib's
// falloff hides it as long as the dip stays inside the feather, and the feather
// at the hardest the nib is allowed to be is `1 - 0.98 = 0.02` of the radius.
// Equating the two gives a bound on the spacing that is arithmetic rather than
// taste.
//
// ⚠ **Why this is a GPU test and not the algebra restated.** The algebra
// describes the union of hard discs. What the kernel draws is alpha
// accumulated with source-over through a smootherstep falloff, and
// accumulation *fills the dip in* — so the geometry is an upper bound on the
// ripple, not a prediction of it. A test that recomputed the sagitta and
// compared it to itself would pass on a kernel that drew nothing.

#include "harness.h"

#include <cmath>
#include <vector>

namespace {

/// The inward dip, in units of the radius, between two dab centres `k` radii
/// apart. Elementary geometry: with centres at ±k/2 and radius 1, the union
/// reaches only `sqrt(1 - k²/4)` on the perpendicular bisector where a
/// continuous sweep would reach 1.
double sagitta(double k) { return 1.0 - std::sqrt(1.0 - k * k / 4.0); }

}  // namespace

/// The stroke's edge, measured, against the spacing that draws it.
///
/// ⚠ **The positive control is the whole point.** Checks that the shipped
/// spacing is smooth are worthless on their own — a kernel that painted a
/// uniform band would pass every one of them. So the same measurement is run at
/// a spacing four times coarser, and it must come back *rough*: the frame has
/// to be able to produce a ripple bigger than the feather before "no ripple at
/// the shipped spacing" means anything.
void testBrushSpacingRipple() {
    section("Brush spacing — the edge of a stroke");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;

    // ⚠ Sized so both quantities are resolvable in pixels rather than inferred.
    // At the shipped spacing the dip is 0.78% of the radius and the feather is
    // 2%, so a small nib puts both under one pixel and the measurement becomes
    // rounding. At r = 200 they are 1.6 px and 4 px.
    constexpr std::uint32_t kW = 1400, kH = 500;
    constexpr float kRadius = 200.0f;

    // ⚠ **Asked for at 1.0, not at the clamp**, and that is the difference
    // between a test that exercises `dabCoverage` and one that restates it. The
    // claim is about the *hardest edge the nib can draw*, which a photographer
    // reaches by dragging hardness to the top; what they get is whatever the
    // shader clamps to. Naming 0.98 here instead would make the shader's clamp
    // unreachable — the first version of this test did exactly that, and moving
    // the clamp to 0.999 left it green, because `clamp(0.98, 0, 0.999)` is
    // still 0.98 and nothing rendered differently.
    constexpr float kHardness = 1.0f;

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
    auto boundsTex = orion::gpu::Texture::create(
        *device, params::kMaxDabBlocks, 1, PixelFormat::RGBA32Float);

    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));

    // A straight horizontal stroke at spacing `k` radii, and the ripple of its
    // upper edge in pixels. The first and last dab are excluded from the
    // measurement: the ends of a stroke are round by design, and a contour that
    // walked over them would report the cap as roughness.
    // ⚠ **The feather is measured, not computed from a constant.** It is the
    // vertical distance between the 0.1 and 0.9 coverage contours, which is the
    // band the falloff occupies in the frame that was actually drawn. The first
    // version of this test derived it from a copy of the shader's 0.98 living
    // here, and a check whose two sides are both written in this file can only
    // ever compare arithmetic against itself.
    struct Edge { double ripple; double feather; };

    const auto rippleAt = [&](double k, int& dabsOut) -> Edge {
        const double stepPx = k * double(kRadius);
        const double firstPx = 300.0, lastPx = double(kW) - 300.0;

        std::vector<float> texels(std::size_t(params::kDabStride)
                                  * params::kDabRows * 4, 0.0f);
        int n = 0;
        for (double x = firstPx; x <= lastPx && n < params::kDabStride; x += stepPx) {
            texels[std::size_t(n) * 4 + 0] = float(x / double(kW));
            texels[std::size_t(n) * 4 + 1] = 0.5f;
            texels[std::size_t(n) * 4 + 2] = 0.0f;   // paint, not erase
            ++n;
        }
        dabsOut = n;
        dabTex->upload(texels.data(),
                       std::size_t(params::kDabStride) * 4 * sizeof(float));

        std::vector<float> bounds(std::size_t(params::kMaxDabBlocks) * 4, 0.0f);
        params::buildDabBounds(texels.data(), n, bounds.data());
        boundsTex->upload(bounds.data(),
                          std::size_t(params::kMaxDabBlocks) * 4 * sizeof(float));

        params::MaskComponent m{};
        m.size[0] = kW; m.size[1] = kH;
        m.kind = 3;
        m.count = n;
        m.dabStride = params::kDabStride;
        m.flow = 1.0f;              // one pass of paint, so the edge is the nib's
        m.hardness = kHardness;
        m.nibPx = kRadius;

        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(),
                              dabTex.get(), boundsTex.get(),
                              &scratchAccum(*device, kW, kH), dst.get()},
                    &m, sizeof m, kW, kH);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);

        // The half-coverage contour of the upper edge, one sample per column,
        // interpolated between the two rows that straddle it so the answer is
        // not quantised to whole pixels — the dip being measured is under two.
        const int fromX = int(firstPx + stepPx);          // past the first cap
        const int toX   = int(lastPx  - stepPx);          // before the last

        // Where the upper edge crosses a coverage level in this column,
        // interpolated between the two rows that straddle it — the dip being
        // measured is under two pixels, so a whole-pixel answer is noise.
        const auto crossing = [&](int x, double level) -> double {
            for (std::uint32_t y = 1; y < kH; ++y) {
                const double a = double(out[std::size_t(y) * kW + x]);
                const double b = double(out[std::size_t(y - 1) * kW + x]);
                if (a >= level && b < level) {
                    return double(y - 1) + (level - b) / (a - b);
                }
            }
            return -1.0;
        };

        double lo = 1e9, hi = -1e9, featherSum = 0.0;
        int columns = 0, feathered = 0;
        for (int x = fromX; x <= toX; ++x) {
            const double mid = crossing(x, 0.5);
            if (mid < 0.0) continue;
            lo = std::min(lo, mid);
            hi = std::max(hi, mid);
            ++columns;

            // The falloff's own width, in this frame: from where the nib first
            // registers to where it is essentially opaque.
            const double soft = crossing(x, 0.1), hard = crossing(x, 0.9);
            if (soft >= 0.0 && hard >= 0.0) { featherSum += hard - soft; ++feathered; }
        }
        if (columns < (toX - fromX) / 2 || feathered == 0) return {-1.0, -1.0};
        return {hi - lo, featherSum / double(feathered)};
    };

    int nShipped = 0, nCoarse = 0, nVeryCoarse = 0;
    const Edge e025 = rippleAt(0.25, nShipped);
    const Edge e050 = rippleAt(0.50, nCoarse);
    const Edge e100 = rippleAt(1.00, nVeryCoarse);

    const double shipped = e025.ripple, coarse = e050.ripple,
                 veryCoarse = e100.ripple;
    // ⚠ The feather is a property of the nib, not of the spacing, so the three
    // measurements should agree — and the check below says so rather than
    // quietly taking one of them.
    const double featherPx = e025.feather;

    std::printf("  spacing 0.25r: %d dabs, ripple %.2f px (sagitta %.2f)\n",
                nShipped, shipped, sagitta(0.25) * kRadius);
    std::printf("  spacing 0.50r: %d dabs, ripple %.2f px (sagitta %.2f)\n",
                nCoarse, coarse, sagitta(0.50) * kRadius);
    std::printf("  spacing 1.00r: %d dabs, ripple %.2f px (sagitta %.2f)\n",
                nVeryCoarse, veryCoarse, sagitta(1.00) * kRadius);
    std::printf("  feather measured off the frame at hardness %.2f: "
                "%.2f / %.2f / %.2f px\n",
                kHardness, e025.feather, e050.feather, e100.feather);

    report(shipped >= 0.0 && coarse >= 0.0 && veryCoarse >= 0.0,
           "the stroke has an edge to measure at every spacing",
           "ripples " + std::to_string(shipped) + " / "
               + std::to_string(coarse) + " / " + std::to_string(veryCoarse));

    report(std::abs(e050.feather - featherPx) < 1.0
               && std::abs(e100.feather - featherPx) < 1.0,
           "the feather is the nib's and does not move with the spacing",
           std::to_string(e025.feather) + " / " + std::to_string(e050.feather)
               + " / " + std::to_string(e100.feather));

    // ⚠ The premise, asserted rather than assumed: these are three *different*
    // strokes. If the dab walk above produced the same list each time, every
    // check below would pass on one measurement repeated.
    report(nShipped > nCoarse && nCoarse > nVeryCoarse,
           "coarser spacing lays fewer dabs over the same path",
           std::to_string(nShipped) + " / " + std::to_string(nCoarse) + " / "
               + std::to_string(nVeryCoarse));

    report(shipped < coarse && coarse < veryCoarse,
           "the edge gets rougher as the dabs get further apart",
           std::to_string(shipped) + " < " + std::to_string(coarse) + " < "
               + std::to_string(veryCoarse));

    // The bound the research file derives, checked as a bound and not as an
    // equality: accumulation fills the dip in, so the measured ripple must come
    // in *under* the union-of-hard-discs figure.
    report(shipped <= sagitta(0.25) * kRadius + 0.5,
           "the shipped spacing's ripple is inside the geometric bound",
           std::to_string(shipped) + " px against "
               + std::to_string(sagitta(0.25) * kRadius));

    // The claim that matters, and the reason 0.25 is not a taste.
    report(shipped < featherPx,
           "at the shipped spacing the dip stays inside the softest edge the "
           "nib can draw, so it cannot be seen",
           std::to_string(shipped) + " px against a " + std::to_string(featherPx)
               + " px feather");

    // ⚠ And the control. Without this the check above passes on a kernel that
    // draws a straight band and never beads at all.
    report(veryCoarse > featherPx,
           "a spacing of one radius beads outside the feather, so the check "
           "above is not vacuous",
           std::to_string(veryCoarse) + " px against " + std::to_string(featherPx));
}

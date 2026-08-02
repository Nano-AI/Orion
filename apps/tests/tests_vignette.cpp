/*  The creative vignette — research/vignette.md, decision #96.
 *
 *  Two tests, and they cover different failures on purpose.
 *
 *  `testCompositionCircle` is arithmetic with no GPU in it: where the crop
 *  rectangle lands inside the frame the upstream nodes actually render. That
 *  map is the whole reason this control is post-crop rather than a falloff
 *  bolted to the middle of the sensor.
 *
 *  `testCreativeVignetteGpu` drives the real `DevelopPipeline` and asserts what
 *  a photographer would notice: the corners go down, they go down together, the
 *  falloff is shaped by the field angle *without* changing what the corner is
 *  worth, a crop takes the vignette with it, and — the one this file exists to
 *  make un-break-able — that none of it touches the lens correction.
 */

#include "harness.h"

namespace {

namespace pipe = orion::pipe;

/// A flat mosaic in the midtones. Flat on purpose: every spatial variation in
/// the output is then the vignette and nothing else, which is not true of the
/// ramp the grain tests use.
orion::raw::BayerImage flatFrame(std::uint32_t w, std::uint32_t h,
                                 std::uint16_t level) {
    orion::raw::BayerImage img;
    img.width = w;
    img.height = h;
    img.samples.assign(std::size_t(w) * h, level);
    img.filters = 0x94949494u;             // RGGB
    img.white = 4095;
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};
    return img;
}

/// Mean of R, G and B over a block, in 8-bit units.
double blockMean(const std::vector<std::uint8_t>& px, std::uint32_t w,
                 std::uint32_t h, std::uint32_t x0, std::uint32_t y0,
                 std::uint32_t side) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < std::min(y0 + side, h); ++y) {
        for (std::uint32_t x = x0; x < std::min(x0 + side, w); ++x) {
            for (int c = 0; c < 3; ++c) {
                sum += px[(std::size_t(y) * w + x) * 4 + std::size_t(c)];
                ++n;
            }
        }
    }
    return n ? sum / double(n) : 0.0;
}

}  // namespace

void testCompositionCircle() {
    section("The composition's circle");

    using Adj = pipe::Adjustments;
    constexpr std::uint32_t kW = 6000, kH = 4000;

    // ── No crop: the frame is the composition ──────────────────────────────
    {
        Adj a{};
        const auto c = pipe::DevelopPipeline::compositionCircle(a, 0, kW, kH);
        checkNear(c.centerX, 0.5, 1e-6, "an uncropped frame is centred in x");
        checkNear(c.centerY, 0.5, 1e-6, "and in y");
        // Half the frame's diagonal, in units of its height. This is the number
        // the kernel divides by, so getting it wrong scales every vignette.
        const double want = 0.5 * std::sqrt(double(kW) * kW + double(kH) * kH) / kH;
        checkNear(c.radius, want, 1e-6, "the radius is half the diagonal");
    }

    // ── A crop takes the circle with it ────────────────────────────────────
    //
    // The entire point of the control. A vignette that stayed on the frame's
    // centre is the pre-crop vignette Adobe replaced in 2008, and it looks like
    // an off-centre vignette rather than like a defect.
    {
        Adj a{};
        a.cropX = 0.0f; a.cropY = 0.0f; a.cropW = 0.5f; a.cropH = 0.5f;
        const auto c = pipe::DevelopPipeline::compositionCircle(a, 0, kW, kH);
        checkNear(c.centerX, 0.25, 1e-6, "a top-left crop moves the centre in x");
        checkNear(c.centerY, 0.25, 1e-6, "and in y");
        const double want =
            0.5 * std::sqrt(0.25 * double(kW) * kW + 0.25 * double(kH) * kH) / kH;
        checkNear(c.radius, want, 1e-6, "and halves the radius");
    }

    // ── A rotation cannot change a length ──────────────────────────────────
    //
    // The property that lets this be a circle and three numbers instead of a
    // second copy of geometry.slang's inverse.
    {
        Adj full{};
        const auto plain = pipe::DevelopPipeline::compositionCircle(full, 0, kW, kH);

        Adj turned{};
        turned.rotateQuarters = 1;
        const auto q = pipe::DevelopPipeline::compositionCircle(turned, 0, kW, kH);
        checkNear(q.radius, plain.radius, 1e-6,
                  "a quarter turn leaves the radius alone");
        checkNear(q.centerX, 0.5, 1e-6, "and an uncropped frame stays centred");
        checkNear(q.centerY, 0.5, 1e-6, "in both axes");

        Adj tilted{};
        tilted.straightenDeg = 12.0f;
        const auto s = pipe::DevelopPipeline::compositionCircle(tilted, 0, kW, kH);
        checkNear(s.radius, plain.radius, 1e-6,
                  "and a straighten leaves it alone too");
        // The crop is centred and the straighten pivots on the frame's centre,
        // so this centre is a fixed point to within half a pixel. ⚠ The half
        // pixel is real and is `geometry.slang`'s: it rotates in *index* space,
        // where the frame's centre index is half a pixel below the continuous
        // centre the pivot is given in. Mirrored rather than corrected — a
        // circle that agreed with hand arithmetic and disagreed with the kernel
        // would be the worse of the two.
        checkNear(s.centerX, 0.5, 1e-3, "a centred crop is the straighten's pivot");
        checkNear(s.centerY, 0.5, 1e-3, "in both axes");
    }

    // ── The quarter turn goes the right way round ──────────────────────────
    //
    // ⚠ A `switch` with the cases transposed is the failure this catches, and
    // it is invisible on a centred crop — which is every crop anybody checks by
    // hand. On a square frame the arithmetic is exact.
    {
        Adj a{};
        a.rotateQuarters = 1;
        a.cropX = 0.0f; a.cropY = 0.0f; a.cropW = 0.5f; a.cropH = 0.5f;
        const auto c = pipe::DevelopPipeline::compositionCircle(a, 0, 1000, 1000);
        // Top-left of the once-turned frame is the bottom-left of the sensor.
        checkNear(c.centerX, 0.25, 1e-6, "one turn: the crop lands left");
        checkNear(c.centerY, 0.75, 1e-6, "and low");
    }

    // ── An off-centre crop does move when the picture is straightened ──────
    {
        Adj a{};
        a.cropX = 0.0f; a.cropY = 0.0f; a.cropW = 0.5f; a.cropH = 0.5f;
        a.straightenDeg = 90.0f;
        const auto c = pipe::DevelopPipeline::compositionCircle(a, 0, 1000, 1000);
        // 0.751 rather than 0.75, again by the half pixel above: the rectangle's
        // centre sits at index 249.5 and the pivot at 500, so the radius swung
        // is 250.5 rather than 250.
        checkNear(c.centerX, 0.751, 1e-5, "90 degrees swings a corner crop across");
        checkNear(c.centerY, 0.25, 1e-5, "and up");
    }

    // ── The clamps agree with the geometry node's ──────────────────────────
    //
    // A rectangle wider than the frame is clamped identically in both places,
    // or the vignette sits where the crop is not.
    {
        Adj a{};
        a.cropX = -1.0f; a.cropY = 2.0f; a.cropW = 5.0f; a.cropH = 0.0f;
        const auto c = pipe::DevelopPipeline::compositionCircle(a, 0, kW, kH);
        report(c.centerX > 0.0f && c.centerX < 1.0f &&
               c.centerY > 0.0f && c.centerY < 1.0f,
               "a nonsense rectangle still lands inside the frame",
               std::to_string(c.centerX) + ", " + std::to_string(c.centerY));
        report(c.radius > 0.0f, "and keeps a positive radius");
    }
}

void testCreativeVignetteGpu() {
    section("Creative vignette (GPU)");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    // 4:3 rather than square, deliberately: the falloff is a circle in *pixels*
    // and the kernel gets there by scaling x by the frame's aspect. On a square
    // frame that scaling is the identity and a dropped aspect term would pass.
    constexpr std::uint32_t kW = 128, kH = 96;
    const auto img = flatFrame(kW, kH, 1000);

    std::unique_ptr<pipe::DevelopPipeline> dev;
    try {
        dev = std::make_unique<pipe::DevelopPipeline>(
            *device, std::string(ORION_SHADER_DIR), img);
    } catch (const std::exception& e) {
        report(false, "develop graph builds", e.what());
        return;
    }

    const auto ran = [&](const char* name) {
        for (const auto& n : dev->graph().lastRun())
            if (n.name == name) return n.executed;
        return false;
    };

    const auto frame = [&](std::uint32_t& w, std::uint32_t& h) {
        w = dev->outputWidth();
        h = dev->outputHeight();
        std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        return px;
    };

    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};

    std::uint32_t w = 0, h = 0;

    // ── 1. Off is off ──────────────────────────────────────────────────────
    dev->apply(adj);
    dev->render();
    const auto plain = frame(w, h);
    report(!ran("grade + vignette"),
           "the node does not run with the vignette at 0 and the wheels centred");

    constexpr std::uint32_t kBlock = 8;
    const auto corners = [&](const std::vector<std::uint8_t>& px,
                             std::uint32_t pw, std::uint32_t ph) {
        return std::array<double, 4>{
            blockMean(px, pw, ph, 0, 0, kBlock),
            blockMean(px, pw, ph, pw - kBlock, 0, kBlock),
            blockMean(px, pw, ph, 0, ph - kBlock, kBlock),
            blockMean(px, pw, ph, pw - kBlock, ph - kBlock, kBlock)};
    };
    const auto middle = [&](const std::vector<std::uint8_t>& px,
                            std::uint32_t pw, std::uint32_t ph) {
        return blockMean(px, pw, ph, pw / 2 - kBlock / 2, ph / 2 - kBlock / 2,
                         kBlock);
    };

    // ⚠ **The fixture is nearly flat, not exactly flat**, and the corner checks
    // below are differential because of it: RCD has no neighbours to interpolate
    // from at the frame's border, so an untouched corner already reads a couple
    // of 8-bit steps off the middle. Measuring the *drop* from `plain` to the
    // vignetted frame cancels that, where an absolute comparison would be
    // reading the demosaic's edge behaviour and calling it a vignette.
    {
        const auto c = corners(plain, w, h);
        const double m = middle(plain, w, h);
        const double spread =
            *std::max_element(c.begin(), c.end()) - *std::min_element(c.begin(), c.end());
        report(spread < 4.0 && std::abs(c[0] - m) < 4.0,
               "the fixture carries no falloff of its own worth the name",
               "corner spread " + std::to_string(spread));
    }

    // ── 2. Negative darkens the corners, and only the corners ──────────────
    adj.vignetteAmount = -2.0f;
    adj.vignetteFieldAngle = 45.0f;
    dev->apply(adj);
    dev->render();
    const auto dark = frame(w, h);
    report(ran("grade + vignette"), "and it runs when the Amount slider moves");

    {
        const auto c = corners(dark, w, h);
        const double m = middle(dark, w, h);
        const double m0 = middle(plain, w, h);

        report(*std::max_element(c.begin(), c.end()) < m - 10.0,
               "-2 EV puts every corner well below the middle",
               "darkest middle " + std::to_string(m) + ", brightest corner "
               + std::to_string(*std::max_element(c.begin(), c.end())));

        // ⚠ The centre must not move. `amount` is defined as the change *at the
        // corner*; a falloff that also dimmed the middle would be an exposure
        // slider with a gradient on it, and the reset value would no longer
        // return the photograph to where it was.
        report(std::abs(m - m0) < 1.5,
               "and leaves the middle where it was",
               std::to_string(m0) + " -> " + std::to_string(m));

        // Symmetry, measured as the drop each corner took. A circle about the
        // composition's centre drops all four equally; a dropped aspect term,
        // or a centre computed wrongly, does not.
        const auto c0 = corners(plain, w, h);
        std::array<double, 4> drop{};
        for (int i = 0; i < 4; ++i) drop[std::size_t(i)] = c0[std::size_t(i)] - c[std::size_t(i)];
        const double spread =
            *std::max_element(drop.begin(), drop.end())
            - *std::min_element(drop.begin(), drop.end());
        report(spread < 2.0, "all four corners fall by the same amount",
               "drops " + std::to_string(drop[0]) + ", " + std::to_string(drop[1])
               + ", " + std::to_string(drop[2]) + ", " + std::to_string(drop[3]));

        // Monotone outward: the edge midpoints sit between the two.
        const double topEdge = blockMean(dark, w, h, w / 2 - kBlock / 2, 0, kBlock);
        report(m > topEdge && topEdge > c[0],
               "and the falloff is monotone from the middle out",
               std::to_string(m) + " > " + std::to_string(topEdge) + " > "
               + std::to_string(c[0]));

        // ⚠ **The check that bites the aspect term**, and nothing above it
        // does. Dropping the `float2(aspect, 1)` scaling in `vignetteRadius`
        // makes the falloff an ellipse stretched to the frame — still centred,
        // still four equal corners, still monotone — so every other line here
        // passes. What it gets wrong is the *edges*: on this 4:3 frame the side
        // midpoint is 0.8 of the way to the corner and the top midpoint 0.6, so
        // the side must be visibly darker. An ellipse puts both at the same
        // fraction and they come out equal.
        const double sideEdge = blockMean(dark, w, h, 0, h / 2 - kBlock / 2, kBlock);
        const double topDrop = blockMean(plain, w, h, w / 2 - kBlock / 2, 0, kBlock)
                             - topEdge;
        const double sideDrop = blockMean(plain, w, h, 0, h / 2 - kBlock / 2, kBlock)
                              - sideEdge;
        report(sideDrop > topDrop + 5.0,
               "the long edge's midpoint falls further than the short edge's",
               "side " + std::to_string(sideDrop) + " against top "
               + std::to_string(topDrop));
    }

    // ── 3. Positive brightens ──────────────────────────────────────────────
    adj.vignetteAmount = 1.0f;
    dev->apply(adj);
    dev->render();
    {
        const auto px = frame(w, h);
        const auto c = corners(px, w, h);
        const double m = middle(px, w, h);
        report(*std::min_element(c.begin(), c.end()) > m + 5.0,
               "a positive Amount lifts the corners instead",
               "middle " + std::to_string(m) + ", dimmest corner "
               + std::to_string(*std::min_element(c.begin(), c.end())));
    }

    // ── 4. The field angle reshapes the falloff and not the corner ─────────
    //
    // ⚠ This is the check on the cos^4 normalization, and it is the one that
    // makes "Amount is the exposure change at the corner" a fact rather than a
    // sentence in a comment. `vignetteFalloff` divides by `1 - cos^4(thetaMax)`
    // precisely so the corner is worth `amount` at every field angle; delete
    // that division and the corner swings by more than a stop between the two
    // settings below.
    {
        adj.vignetteAmount = -2.0f;

        adj.vignetteFieldAngle = 20.0f;
        dev->apply(adj);
        dev->render();
        const auto narrow = frame(w, h);

        adj.vignetteFieldAngle = 65.0f;
        dev->apply(adj);
        dev->render();
        const auto wide = frame(w, h);

        const double cornerN = blockMean(narrow, w, h, 0, 0, 4);
        const double cornerW = blockMean(wide, w, h, 0, 0, 4);
        report(std::abs(cornerN - cornerW) < 4.0,
               "the corner is worth the same at 20 degrees and at 65",
               std::to_string(cornerN) + " against " + std::to_string(cornerW));

        // Halfway out along the diagonal, where the two shapes must disagree.
        const std::uint32_t hx = w / 4, hy = h / 4;
        const double midN = blockMean(narrow, w, h, hx, hy, kBlock);
        const double midW = blockMean(wide, w, h, hx, hy, kBlock);
        report(midN > midW + 10.0,
               "and a narrow field keeps the falloff out of the middle",
               std::to_string(midN) + " against " + std::to_string(midW));
    }

    // ── 5. It is not the lens correction, in both directions ───────────────
    //
    // ⚠ The reason this file exists in the shape it does. `lensVignette`
    // *removes* a measured falloff before the demosaic; this one adds one after
    // the grade. Wiring the creative control into the lens node would look
    // roughly right on screen and would be wrong about everything — it would
    // run before the crop, before the demosaic, and it would fight a lens
    // profile the moment one loaded.
    //
    // ⚠ Every state below is reached by *changing* the field that owns it.
    // `apply` compares field by field and only re-evaluates a node's enable
    // when something in that node's own list moved (#92), so re-applying the
    // same amount from a different struct leaves the graph exactly as it was —
    // and a check written that way is green whatever the wiring does. The first
    // draft of this section was written that way and passed the mutation.
    {
        pipe::Adjustments zero{};
        zero.wb = pipe::WhiteBalance{};
        dev->apply(zero);
        dev->render();

        pipe::Adjustments only = zero;
        only.vignetteAmount = -1.25f;      // a value no earlier state used
        dev->apply(only);
        dev->render();
        report(!ran("lens"),
               "the creative vignette does not switch the lens correction on");
        report(ran("grade + vignette"), "it is its own node's business");

        pipe::Adjustments lensOnly = zero;
        lensOnly.lensVignette = 1.0f;
        dev->apply(lensOnly);
        dev->render();
        report(ran("lens"), "and the lens correction still runs on its own");
        report(!ran("grade + vignette"),
               "without switching the creative one on");
    }

    // ── 6. Back to zero is back to the byte ────────────────────────────────
    adj.vignetteAmount = 0.0f;
    adj.vignetteFieldAngle = 45.0f;
    dev->apply(adj);
    dev->render();
    {
        std::uint32_t aw = 0, ah = 0;
        const auto again = frame(aw, ah);
        report(again == plain,
               "Amount back at 0 is bit-identical to never having touched it");
        report(!ran("grade + vignette"), "and the node is disabled again");
    }

    // ── 7. What a drag costs ───────────────────────────────────────────────
    //
    // ⚠ Counted warm, never against the first render — a cold graph runs every
    // node and comparing one to the other measures which came first.
    {
        const auto dragCost = [&](float ev) {
            adj.exposureEv = ev;
            dev->apply(adj);
            dev->render();
            int n = 0;
            for (const auto& r : dev->graph().lastRun()) if (r.executed) ++n;
            return n;
        };

        const int quiet = dragCost(0.1f);

        adj.vignetteAmount = -2.0f;
        dev->apply(adj);
        dev->render();
        const int withIt = dragCost(0.2f);

        adj.vignetteAmount = 0.0f;
        dev->apply(adj);
        dev->render();
        const int after = dragCost(0.3f);

        report(after == quiet,
               "an exposure tick costs the same as before the vignette existed",
               std::to_string(after) + " nodes against " + std::to_string(quiet));
        report(withIt == quiet + 1,
               "and exactly one node more with the vignette on",
               std::to_string(withIt) + " against " + std::to_string(quiet));
    }
}

void testVignetteFollowsTheCrop() {
    section("The vignette follows the crop");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    constexpr std::uint32_t kW = 160, kH = 120;
    const auto img = flatFrame(kW, kH, 1000);

    std::unique_ptr<pipe::DevelopPipeline> dev;
    try {
        dev = std::make_unique<pipe::DevelopPipeline>(
            *device, std::string(ORION_SHADER_DIR), img);
    } catch (const std::exception& e) {
        report(false, "develop graph builds", e.what());
        return;
    }

    // A quadrant in the top right, which shares no corner and no axis with the
    // frame's own centre. ⚠ A centred crop cannot tell this apart from a
    // frame-centred vignette, which is exactly why the rectangle is off-centre.
    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};
    adj.cropX = 0.5f; adj.cropY = 0.0f;
    adj.cropW = 0.5f; adj.cropH = 0.5f;

    const std::uint32_t w = [&] {
        dev->apply(adj);
        dev->render();
        return dev->outputWidth();
    }();
    const std::uint32_t h = dev->outputHeight();

    constexpr std::uint32_t kBlock = 6;
    const auto grab = [&] {
        std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        return px;
    };
    const auto cornersOf = [&](const std::vector<std::uint8_t>& px) {
        return std::array<double, 4>{
            blockMean(px, w, h, 0, 0, kBlock),
            blockMean(px, w, h, w - kBlock, 0, kBlock),
            blockMean(px, w, h, 0, h - kBlock, kBlock),
            blockMean(px, w, h, w - kBlock, h - kBlock, kBlock)};
    };

    // The same crop with no vignette, as the control the drops are taken
    // against — the fixture's own border behaviour then cancels.
    const auto plain = grab();
    const auto c0 = cornersOf(plain);

    adj.vignetteAmount = -2.0f;
    dev->apply(adj);
    dev->render();
    const auto px = grab();

    const auto c = cornersOf(px);
    std::array<double, 4> drop{};
    for (int i = 0; i < 4; ++i) drop[std::size_t(i)] = c0[std::size_t(i)] - c[std::size_t(i)];
    const double m = blockMean(px, w, h, w / 2 - kBlock / 2, h / 2 - kBlock / 2,
                               kBlock);

    report(w == kW / 2 && h == kH / 2, "the crop is what came out",
           std::to_string(w) + "x" + std::to_string(h));

    // ⚠ **This pair is the post-crop claim.** A vignette centred on the *frame*
    // would put the cropped picture entirely in one quadrant of the falloff:
    // the corner nearest the frame's middle would be barely touched and the far
    // one heavily, so the four would not agree and the "middle" of the crop
    // would not be the brightest thing in it. Mutating `compositionCircle` to
    // return the frame's centre and full radius fails both lines.
    const double spread =
        *std::max_element(drop.begin(), drop.end())
        - *std::min_element(drop.begin(), drop.end());
    report(spread < 2.0,
           "the cropped picture's four corners fall by the same amount",
           "drops " + std::to_string(drop[0]) + ", " + std::to_string(drop[1])
           + ", " + std::to_string(drop[2]) + ", " + std::to_string(drop[3]));
    report(*std::max_element(c.begin(), c.end()) < m - 10.0,
           "and its own middle is the brightest part of it",
           "middle " + std::to_string(m) + ", brightest corner "
           + std::to_string(*std::max_element(c.begin(), c.end())));
}

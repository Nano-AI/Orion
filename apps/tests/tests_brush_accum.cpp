/*  The incremental brush accumulator — decision #108, session two.
 *
 *  `research/brush-acceleration.md`. Session one shipped the predicate with
 *  nothing reading its answer; this is the half that reads it, and its failure
 *  mode is the one this repository is most afraid of — a stale accumulator
 *  renders a completely plausible brushstroke, every screenshot passes, and the
 *  only thing that can tell is a byte-for-byte comparison against the same
 *  stroke laid whole.
 *
 *  ⚠ **Every check here is a comparison against a full evaluation, plus a
 *  counter proving the fast path was taken.** Either half alone is worthless:
 *  an accumulator that is never continued from passes every pixel comparison in
 *  this file, and a counter with no picture behind it says a fast path ran
 *  without saying it was right.
 */

#include "harness.h"

namespace {

/// A ramp in both axes. A flat patch would be bit-identical under a stroke that
/// landed anywhere at all, which makes every pixel comparison below a
/// formality — the same trap `testBrushPrefixWiring` calls out.
orion::raw::BayerImage rampFrame(std::uint32_t n) {
    orion::raw::BayerImage img;
    img.width = n;
    img.height = n;
    img.samples.resize(std::size_t(n) * n);
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            img.samples[std::size_t(y) * n + x] =
                static_cast<std::uint16_t>(200 + x * 20 + y * 15);
        }
    }
    img.filters = 0x94949494u;             // RGGB
    img.white = 4095;
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};
    return img;
}

/// Dab centers in displayed coordinates, the way the app sends them. A diagonal
/// so `mask::toFrame` is exercised in both axes, and long enough that a stroke
/// crosses several 64-dab blocks — a prefix that never lands mid-block would
/// hide the off-by-a-block error the shader's `max(b * DAB_BLOCK, firstDab)`
/// exists to prevent.
std::vector<float> diagonal(int count, float ox = 0.06f, float oy = 0.06f) {
    std::vector<float> xy(std::size_t(count) * 2, 0.0f);
    for (int i = 0; i < count; ++i) {
        xy[std::size_t(i) * 2 + 0] = ox + 0.0026f * float(i);
        xy[std::size_t(i) * 2 + 1] = oy + 0.0021f * float(i);
    }
    return xy;
}

}  // namespace

void testBrushAccumulator() {
    section("Brush accumulator");

    namespace pipe = orion::pipe;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    const orion::raw::BayerImage img = rampFrame(96);

    std::unique_ptr<pipe::DevelopPipeline> dev;
    try {
        dev = std::make_unique<pipe::DevelopPipeline>(
            *device, std::string(ORION_SHADER_DIR), img);
    } catch (const std::exception& e) {
        report(false, "develop graph builds", e.what());
        return;
    }

    const std::uint32_t w = dev->outputWidth(), h = dev->outputHeight();
    const auto frame = [&] {
        std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        return px;
    };

    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};
    adj.maskCount = 1;
    adj.layers[0].exposureEv = 2.0f;
    {
        auto& c = adj.maskComponents[0];
        c.kind = 3;
        c.brushRadius = 0.05f;
        c.brushFlow = 0.6f;          // < 1, so overlapping dabs build up and the
        c.brushHardness = 0.7f;      // order they are applied in is visible
    }

    int revision = 0;
    const auto paint = [&](int component, const std::vector<float>& xy, int n) {
        dev->setBrushStroke(component, xy.data(), nullptr, n);
        adj.maskComponents[std::size_t(component)].brushRevision = ++revision;
        dev->apply(adj);
        dev->render();
    };

    // ⚠ The reference every comparison below is against: the same stroke, on a
    // graph that has never seen a dab, so `firstDab` is zero and the kernel
    // walks all of it. A reload is what throws the record away.
    const auto laidWhole = [&](const std::vector<float>& xy, int n) {
        dev->reload(img);
        paint(0, xy, n);
        report(dev->brushPrefixStat(0).firstDab == 0,
               "the control really is a full evaluation",
               std::to_string(dev->brushPrefixStat(0).firstDab));
        return frame();
    };

    // ── 1. Nothing is allocated until something is painted ─────────────────
    //
    // ~97 MB at 24 Mpx, and most photographs are never painted on. Registered
    // at 1x1 and grown on the first dab.
    report(dev->brushAccumBytes() == 0,
           "an unpainted photograph has no accumulator",
           std::to_string(dev->brushAccumBytes()) + " bytes");
    report(dev->brushAccumOwner() == -1, "and nobody owns it");

    // ── 2. Appending, and the fast path taken rather than merely offered ────
    const auto whole200 = laidWhole(diagonal(200), 200);

    dev->reload(img);
    paint(0, diagonal(40), 40);
    report(dev->brushAccumBytes() == std::size_t(w) * h * sizeof(float),
           "the first dab grows the accumulator to the frame",
           std::to_string(dev->brushAccumBytes()) + " bytes");
    report(dev->brushAccumOwner() == 0, "and the component that painted owns it");

    // Nine more events, the way a hand actually lays a stroke.
    for (int n = 60; n <= 200; n += 20) {
        const int before = dev->brushPrefixStat(0).count;
        paint(0, diagonal(n), n);
        const auto s = dev->brushPrefixStat(0);
        report(s.firstDab == before,
               "an append continues from the dabs already on the GPU",
               "started at " + std::to_string(s.firstDab) + " of "
               + std::to_string(s.count));
    }
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.firstDab == 180 && s.count == 200,
               "so the last of ten events lays 20 dabs, not 200",
               std::to_string(s.count - s.firstDab) + " of "
               + std::to_string(s.count));
        // ⚠ 180 is not a multiple of 64. The block walk has to start at the
        // block *holding* dab 180 and clamp to 180 inside it; rounding down to
        // the block boundary would composite dabs 128..179 a second time.
        report(s.firstDab % 64 != 0,
               "and the prefix ends mid-block, which is where an off-by-a-block "
               "error shows");
    }
    report(frame() == whole200,
           "a stroke built in ten events is bit-identical to the same stroke "
           "laid whole");

    // ── 3. ⚠ Undo three dabs, paint three different ones ───────────────────
    //
    // The count is exactly what it was. The accumulator holds coverage the
    // photographer took back, and continuing from it renders a brushstroke that
    // is not theirs — in the right place, in the right shape, plausible.
    auto repaint = diagonal(200);
    for (int i = 197; i < 200; ++i) repaint[std::size_t(i) * 2 + 1] += 0.03f;
    const auto repaintedWhole = laidWhole(repaint, 200);

    dev->reload(img);
    for (int n = 40; n <= 200; n += 20) paint(0, diagonal(n), n);
    paint(0, repaint, 200);
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.count == 200 && s.previousCount == 200,
               "undo three and repaint three: the count is exactly where it was",
               std::to_string(s.previousCount) + " -> " + std::to_string(s.count));
        report(s.prefix == 197, "the predicate stops the prefix at 197",
               std::to_string(s.prefix));
        report(s.firstDab == 0,
               "and the accumulator is refused, because it holds 200 dabs and "
               "only 197 of them survived",
               std::to_string(s.firstDab));
    }
    report(frame() == repaintedWhole,
           "so the repaint is the photographer's stroke, not the one they undid");
    report(repaintedWhole != whole200,
           "and those two are different pictures, so the check above could fail");

    // ── 4. ⚠ The node runs again on parameters `apply` did not push ─────────
    //
    // A kernel that accumulates is not idempotent: run it twice on one set of
    // parameters and the dabs in [firstDab, count) go down twice. Nothing in
    // `apply` sees this coming — white balance moves, the reference image behind
    // every mask component changes, and the mask node is dirty again with the
    // parameters it already ran on.
    dev->reload(img);
    paint(0, diagonal(120), 120);
    paint(0, diagonal(200), 200);
    report(dev->brushPrefixStat(0).firstDab == 120,
           "the append took the fast path before the disturbance",
           std::to_string(dev->brushPrefixStat(0).firstDab));
    const int refusalsBefore = dev->brushPrefixStat(0).refusals;

    adj.wb.temperatureK = 4200.0f;    // nothing about the component moves
    dev->apply(adj);
    dev->render();
    const auto afterWb = frame();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.refusals == refusalsBefore + 1,
               "a node about to re-run on parameters this apply did not push is "
               "refused the fast path",
               std::to_string(s.refusals - refusalsBefore) + " refusals");
    }

    dev->reload(img);
    adj.maskComponents[0].brushRevision = ++revision;
    dev->setBrushStroke(0, diagonal(200).data(), nullptr, 200);
    dev->apply(adj);
    dev->render();
    report(afterWb == frame(),
           "and the frame is the stroke laid once, not the last 80 dabs laid twice");
    adj.wb = pipe::WhiteBalance{};

    // ⚠ **And the refusal has to be narrow.** Refusing whenever a brush
    // component has anything in the accumulator at all is also correct, and
    // gives back the whole feature the first time the photographer touches an
    // unrelated slider. What is actually dangerous is a node about to re-run
    // with `firstDab > 0`, which is what the code keys on — so a disturbance
    // arriving while the parameters say zero must leave the accumulator alone.
    dev->reload(img);
    paint(0, diagonal(120), 120);           // firstDab 0: nothing to continue yet
    report(dev->brushPrefixStat(0).firstDab == 0, "a first stroke starts at zero");
    const int narrowBefore = dev->brushPrefixStat(0).refusals;
    adj.wb.temperatureK = 4200.0f;
    dev->apply(adj);
    dev->render();
    adj.wb = pipe::WhiteBalance{};
    dev->apply(adj);
    dev->render();
    report(dev->brushPrefixStat(0).refusals == narrowBefore,
           "a disturbance with nothing to continue from refuses nothing",
           std::to_string(dev->brushPrefixStat(0).refusals - narrowBefore)
           + " refusals");
    paint(0, diagonal(200), 200);
    report(dev->brushPrefixStat(0).firstDab == 120,
           "and the 120 dabs that were rendered are still there to continue from",
           std::to_string(dev->brushPrefixStat(0).firstDab));

    // ── 5. One accumulator, one owner ──────────────────────────────────────
    //
    // Four of these would be ~388 MB at 24 Mpx for a feature only the component
    // under the cursor uses. The cost of the choice is one full evaluation when
    // the photographer moves to a different component, and it must be *correct*
    // as well as slow.
    dev->reload(img);
    adj.maskCount = 2;
    {
        auto& c1 = adj.maskComponents[1];
        c1.kind = 3;
        c1.brushRadius = 0.05f;
        c1.brushFlow = 0.6f;
        c1.brushHardness = 0.7f;
        c1.compose = 0;
    }
    paint(0, diagonal(120), 120);
    paint(0, diagonal(200), 200);
    report(dev->brushAccumOwner() == 0 && dev->brushPrefixStat(0).firstDab == 120,
           "component 0 owns the accumulator and is continuing from it");

    paint(1, diagonal(120, 0.30f, 0.10f), 120);
    report(dev->brushAccumOwner() == 1,
           "painting on another component takes the accumulator away",
           std::to_string(dev->brushAccumOwner()));
    report(dev->brushPrefixStat(1).firstDab == 0,
           "the new owner starts from nothing");
    paint(1, diagonal(200, 0.30f, 0.10f), 200);
    report(dev->brushPrefixStat(1).firstDab == 120,
           "and continues from there",
           std::to_string(dev->brushPrefixStat(1).firstDab));
    const auto twoStrokes = frame();

    // ⚠ The two strokes, both laid whole on a graph that has never accumulated.
    // If the hand-off left component 0's coverage in a texture component 1 then
    // overwrote, this is where it shows.
    dev->reload(img);
    dev->setBrushStroke(0, diagonal(200).data(), nullptr, 200);
    dev->setBrushStroke(1, diagonal(200, 0.30f, 0.10f).data(), nullptr, 200);
    adj.maskComponents[0].brushRevision = ++revision;
    adj.maskComponents[1].brushRevision = ++revision;
    dev->apply(adj);
    dev->render();
    report(twoStrokes == frame(),
           "two components painted in turn are bit-identical to both laid whole");
    report(dev->brushPrefixStat(0).firstDab == 0 &&
           dev->brushPrefixStat(1).firstDab == 0,
           "and that control is a full evaluation of both");

    // ── 5b. ⚠ The hand-off in the direction the graph runs against ─────────
    //
    // This case was found by mutation, and the ordinary hand-off above cannot
    // see it. The mask nodes run in component order, so when the accumulator
    // moves from 0 to 1 the old owner writes *before* the new one and is
    // harmlessly overwritten. Moving it from 1 to 0 reverses that: component 1
    // runs last, and a stale `accumUse` on it lands its own coverage on top of
    // the accumulator component 0 just filled. The next append on component 0
    // then continues from a stroke belonging to a different component — the
    // right shape, the wrong place, entirely plausible.
    //
    // Which is why taking the accumulator away patches the old owner's
    // parameters rather than only recording that ownership moved.
    dev->reload(img);
    paint(1, diagonal(120, 0.30f, 0.10f), 120);
    paint(1, diagonal(200, 0.30f, 0.10f), 200);
    report(dev->brushAccumOwner() == 1 && dev->brushPrefixStat(1).firstDab == 120,
           "component 1 has the accumulator and is continuing from it");

    paint(0, diagonal(120), 120);
    paint(0, diagonal(200), 200);
    report(dev->brushAccumOwner() == 0 && dev->brushPrefixStat(0).firstDab == 120,
           "the accumulator moves back down to component 0",
           std::to_string(dev->brushAccumOwner()) + ", from dab "
           + std::to_string(dev->brushPrefixStat(0).firstDab));
    const auto handedBack = frame();

    dev->reload(img);
    dev->setBrushStroke(0, diagonal(200).data(), nullptr, 200);
    dev->setBrushStroke(1, diagonal(200, 0.30f, 0.10f).data(), nullptr, 200);
    adj.maskComponents[0].brushRevision = ++revision;
    adj.maskComponents[1].brushRevision = ++revision;
    dev->apply(adj);
    dev->render();
    report(handedBack == frame(),
           "and the stroke component 0 continued is its own, not the one "
           "component 1 left in the texture");

    adj.maskCount = 1;
    adj.maskComponents[1] = {};

    // ── 6. ⚠ Many applies, one render — which is what a gesture is ─────────
    //
    // The full graph is handed parameters on every pointer event and rendered
    // once, when the gesture ends. A host that recorded its claim at *push* time
    // would believe the accumulator holds a stroke it was only ever told about,
    // and the catch-up render would start 180 dabs into a texture holding none
    // of them.
    const auto whole260 = laidWhole(diagonal(260), 260);

    dev->reload(img);
    for (int n = 40; n <= 260; n += 20) {
        dev->setBrushStroke(0, diagonal(n).data(), nullptr, n);
        adj.maskComponents[0].brushRevision = ++revision;
        dev->apply(adj);            // no render — the gesture is still going
    }
    dev->render();
    {
        const auto s = dev->brushPrefixStat(0);
        report(s.firstDab == 0,
               "twelve applies and one render start from nothing, because "
               "nothing was ever rendered into the accumulator",
               std::to_string(s.firstDab));
    }
    report(frame() == whole260,
           "and the frame that lands when the gesture ends is the whole stroke");

    // The next event after that catch-up render *is* an append, so the win
    // arrives on the second gesture rather than being lost.
    dev->setBrushStroke(0, diagonal(280).data(), nullptr, 280);
    adj.maskComponents[0].brushRevision = ++revision;
    dev->apply(adj);
    dev->render();
    report(dev->brushPrefixStat(0).firstDab == 260,
           "and the stroke after it continues from the 260 dabs that landed",
           std::to_string(dev->brushPrefixStat(0).firstDab));

    // ── 7. A component that is hidden never runs ───────────────────────────
    //
    // `apply` pushes its parameters and `setEnabled` keeps it from running, so
    // a claim recorded at push time would survive into a render that never
    // happened — and the node would later continue from a texture that was
    // written by whatever came after it.
    dev->reload(img);
    paint(0, diagonal(120), 120);
    adj.maskComponents[0].hidden = true;
    dev->setBrushStroke(0, diagonal(200).data(), nullptr, 200);
    adj.maskComponents[0].brushRevision = ++revision;
    dev->apply(adj);
    dev->render();
    adj.maskComponents[0].hidden = false;
    dev->setBrushStroke(0, diagonal(240).data(), nullptr, 240);
    adj.maskComponents[0].brushRevision = ++revision;
    dev->apply(adj);
    dev->render();
    report(dev->brushPrefixStat(0).firstDab == 0,
           "a stroke pushed while the component was hidden is not in the "
           "accumulator, and is not continued from",
           std::to_string(dev->brushPrefixStat(0).firstDab));
    const auto afterHidden = frame();
    report(afterHidden == laidWhole(diagonal(240), 240),
           "and the frame after it is the whole stroke");

    // ── 8. The 97 MB comes back ────────────────────────────────────────────
    //
    // A row switched from a brush to a gradient, or deleted, must not leave the
    // accumulator resident — that is the objection `ROADMAP.md` raised against
    // one of these per component, and it applies to a stale one just as much.
    dev->reload(img);
    paint(0, diagonal(120), 120);
    paint(0, diagonal(200), 200);
    report(dev->brushAccumBytes() == std::size_t(w) * h * sizeof(float),
           "the accumulator is resident while the brush is");
    adj.maskComponents[0].kind = 2;
    dev->apply(adj);
    dev->render();
    report(dev->brushAccumBytes() == 0,
           "and is given back when the last brush component stops being one",
           std::to_string(dev->brushAccumBytes()) + " bytes");
    report(dev->brushAccumOwner() == -1, "with nobody still owning it");

    // ⚠ And coming back has to start from nothing. A claim that survived the
    // release would continue from a texture that has been reallocated
    // underneath it — the shape of the stroke would be right and the coverage
    // it started from would be whatever the new allocation happened to hold.
    adj.maskComponents[0].kind = 3;
    dev->setBrushStroke(0, diagonal(240).data(), nullptr, 240);
    adj.maskComponents[0].brushRevision = ++revision;
    dev->apply(adj);
    dev->render();
    report(dev->brushPrefixStat(0).firstDab == 0,
           "a component that came back to being a brush continues from nothing",
           std::to_string(dev->brushPrefixStat(0).firstDab));
    report(frame() == laidWhole(diagonal(240), 240),
           "and lays the whole stroke");
}

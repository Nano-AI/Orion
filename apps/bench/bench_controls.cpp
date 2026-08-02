/*  orion-bench — every control does something.
 *
 *  ⚠ **This is the file a new control gets a probe in, and it should be the
 *  only one.** Add a row to `probes[]`: the state the control is judged in,
 *  the control itself, the metric that can actually see it, and a floor set at
 *  half the smallest ratio measured over all three sample frames. Nothing else
 *  in the bench needs to know it exists.
 */
#include "bench.h"

#include "pipe/CubeLut.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace bench {

void controlProbes(Bench& b) {
    auto& develop = b.develop;
    bool& controlsPass = b.controlsPass;

    // ── Every control does something ──────────────────────────────────
    // A slider that silently no-ops is worse than one that is missing, so
    // assert each moves the image before trusting any of it.
    std::printf("Control check (mean luma, identity = %.4f)\n", [&] {
        orion::pipe::Adjustments base;
        base.wb = develop.asShotWhiteBalance();
        develop.apply(base);
        develop.render();
        return meanOf(develop, Metric::Luma);
    }());

    {
        orion::pipe::Adjustments base;
        base.wb = develop.asShotWhiteBalance();

        struct Probe {
            const char* name;
            /// The state the control is judged in. A control is measured
            /// against *this* rendered, not against the identity frame:
            /// comparing a probe that lifts exposure 5.5 EV against an
            /// unlifted baseline reports the exposure lift as the control's
            /// own effect. It flattered the highlight grading wheel by more
            /// than tenfold before this was split out — the same mistake,
            /// in the tool built to catch it.
            void (*context)(orion::pipe::Adjustments&);
            /// The control itself, applied on top of the context.
            void (*set)(orion::pipe::Adjustments&);
            Metric metric = Metric::Luma;
            /// The smallest move that counts, as a fraction of what the
            /// reference control moves on this same frame — exposure +1 EV
            /// for luma, saturation -1 for chroma. A fraction rather than
            /// an absolute, because the absolute depends on the photo and
            /// this bench takes the photo as an argument.
            ///
            /// Each value is half the *smallest* ratio measured across all
            /// three sample frames: a night sky, a lit forecourt, and a
            /// bright daylight cityscape.
            ///
            /// Two frames were not enough, and neither was one. Calibrating
            /// on the night shot alone tripped four probes on the forecourt;
            /// calibrating on both night frames then tripped six on the
            /// daylight one — a bright frame has almost no deep blacks, very
            /// little noise to remove, and few shadows to grade, so those
            /// controls genuinely move less in it. None of that is a
            /// regression, and a floor that cannot tell the difference is
            /// worse than no floor.
            ///
            /// Half the minimum over three very different pictures leaves a
            /// fourth room to differ while a control that loses a third of
            /// its strength still trips.
            /// That margin is the point: `blacks -1` was being diluted to
            /// 39% of its correct effect and printed `ok` for weeks,
            /// because the bar was "moved by more than 2e-4".
            ///
            /// The gate reads `meanAbsDiff` — how far the pixels actually
            /// moved — not the summary metric beside it. A summary can
            /// cancel: a grading wheel rotates hue at constant saturation,
            /// and mean luma, mean chroma and mean saturation each reported
            /// that as almost nothing. The metric is printed for insight
            /// into *what* changed; the movement decides whether it did.
            double least = 0.25;
            /// A known, filed defect. Printed every run with its reason,
            /// never silently skipped, and it does not gate the build.
            /// Nothing else may use this without a reason string.
            const char* waived = nullptr;
            /// Out-of-band state, set on the pipeline before either render.
            ///
            /// Not everything a mask can be is expressible in `Adjustments`.
            /// A brush stroke is a variable-length list of centers and a
            /// matte is a raster, so both are uploaded through their own
            /// calls — which is precisely why neither had a probe: the two
            /// hooks above take an `Adjustments&` and cannot reach them.
            /// Runs for the context render as well as the measured one, so
            /// a probe can put a mask in place and then measure a control
            /// *through* it. research/masking.md §5.
            void (*prepare)(orion::pipe::DevelopPipeline&) = nullptr;
            /// ⚠ Fewest nodes this control must recompute, or 0 for "do not
            /// check". This exists because a control can keep moving the
            /// picture by the right amount while a whole chain under it has
            /// gone dead.
            ///
            /// `highlights` and `shadows` are the case that proved it. They
            /// drive a seven-node guided filter; hardwire `needsGuide` false at
            /// its source and the chain dies, these two fall from ten nodes to
            /// three, the picture still moves plenty (the tone curve alone
            /// does that), and **the whole bench exits 0**. The dedicated
            /// `guide off/on` invariant is blind to it too — it renders the
            /// chain-off frame twice and two identical frames agree perfectly.
            /// Found by mutation in decision #113's split, confirmed again by
            /// #118's, reproduced and fixed here. Ten holds on all three sample
            /// frames; `whites` and `blacks` sit at three, so the gap is
            /// exactly the chain.
            int leastNodes = 0;
        };

        const auto flat = [](orion::pipe::Adjustments&) {};
        // These offsets are what the *user* would dial, and the pipeline
        // now adds a silent +1.2 EV baseline on top (see
        // kBaselineExposureEv). Both were reduced by that amount when the
        // baseline landed, so each probe still runs at the effective
        // exposure it was calibrated at — otherwise every context silently
        // moved 1.2 stops and the grading probes lost their zone.
        const auto lift = [](orion::pipe::Adjustments& a) { a.exposureEv = 4.3f; };
        // A moderate lift, so the frame actually spans the three grading
        // zones. This is a night shot: at base exposure everything sits
        // five stops under middle gray and only the shadow wheel has
        // anything to act on.
        const auto mid  = [](orion::pipe::Adjustments& a) { a.exposureEv = 1.8f; };

        // A creative LUT is a file, not an adjustment, so the probe
        // measures the strength control against a LUT loaded here. Mildly
        // warm and not separable — the cross term is there so the probe
        // exercises the 3D path rather than something a per-channel curve
        // could have done.
        {
            orion::pipe::CubeLut look;
            look.size = 17;
            look.data.resize(std::size_t(17) * 17 * 17 * 3);
            std::size_t w = 0;
            for (int b = 0; b < 17; ++b) {
                for (int g = 0; g < 17; ++g) {
                    for (int r = 0; r < 17; ++r) {
                        const float fr = float(r) / 16.0f;
                        const float fg = float(g) / 16.0f;
                        const float fb = float(b) / 16.0f;
                        look.data[w++] = std::clamp(std::pow(fr, 0.85f) + 0.05f * (fg - fb),
                                                    0.0f, 1.0f);
                        look.data[w++] = fg;
                        look.data[w++] = std::clamp(std::pow(fb, 1.15f), 0.0f, 1.0f);
                    }
                }
            }
            develop.setCreativeLut(look);
        }

        // The look is loaded, so "no look" is the context and the control
        // being measured is the strength slider moving off zero.
        const auto noLook = [](orion::pipe::Adjustments& a) { a.lutStrength = 0.0f; };

        const Probe probes[] = {
            {"exposure +1 EV", flat, [](auto& a) { a.exposureEv = 1.0f; }, Metric::Luma, 0.5},
            {"highlights -1",  lift, [](auto& a) { a.highlights = -1.0f; }, Metric::Luma, 0.237, nullptr, nullptr, 10},
            {"shadows +1",     flat, [](auto& a) { a.shadows = 1.0f; }, Metric::Luma, 0.311, nullptr, nullptr, 10},
            // Whites and blacks are endpoint controls: they move the ends
            // and leave the middle alone, so their means move less than
            // exposure's. The floors are low; the guide-chain pair check
            // under Invariants is what actually pins them.
            {"whites +1",      lift, [](auto& a) { a.whites = 1.0f; }, Metric::Luma, 0.045},
            {"blacks -1",      flat, [](auto& a) { a.blacks = -1.0f; }, Metric::Luma, 0.023},
            {"vibrance +1",    flat, [](auto& a) { a.vibrance = 1.0f; }, Metric::Chroma, 0.068},
            {"saturation -1",  flat, [](auto& a) { a.saturation = -1.0f; }, Metric::Chroma, 0.213},
            {"contrast 1.5",   flat, [](auto& a) { a.contrast = 1.5f; }, Metric::Luma, 0.348},
            {"temp 3000K",     flat, [](auto& a) { a.wb.temperatureK = 3000.0f; }, Metric::Luma, 0.107},
            {"tint +0.5",      flat, [](auto& a) { a.wb.tint += 0.5f; }, Metric::Luma, 0.097},
            // Measured as detail, not as brightness — see Metric::Detail.
            {"sharpen 1.0",    flat, [](auto& a) { a.sharpenAmount = 1.0f; }, Metric::Detail, 0.023},
            // Clarity is a local-contrast filter, so like sharpening it is
            // measured as detail. Mean luma is the wrong instrument twice
            // over here: the filter is built to leave the frame's overall
            // brightness alone.
            // Floors are half the smallest ratio measured over all three
            // sample frames: +1 moved 0.125 / 0.185 / 0.125 of the
            // reference, -1 moved 0.120 / 0.158 / 0.115.
            // Floor is half the smaller ratio over the two frames that
            // have any haze in them: 0.123 and 0.057 of the reference.
            //
            // Waived rather than floored across all three, because dehaze
            // is the one control whose correct output on some frames is no
            // change at all. On the night frame the dark channel is near
            // zero everywhere, the atmospheric light lands on a light
            // source, and Eq. (12) gives t = 1 — the method reporting,
            // correctly, that there is no veil to remove. A floor that
            // failed there would be a floor demanding a filter invent haze.
            // What pins this control is testDehazeGpu, not this line.
            // Half the smallest ratio over the three frames: 0.26, 0.25,
            // 0.22 of the reference.
            {"look 1.0",       noLook, [](auto& a) { a.lutStrength = 1.0f; }, Metric::Chroma, 0.11},
            // Half the smallest ratio over the three frames: 1.15, 2.43,
            // 2.62 of the reference. It moves more than an exposure stop
            // does, which is what a shadow lift at full strength should do.
            // A local exposure through a linear gradient. Half the frame
            // is untouched by construction, so this moves less than a
            // global exposure of the same size — which is the point, and
            // what the floor is calibrated against.
            {"local +2 EV",    flat, [](auto& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 1;
                 c.center[0] = 0.5f; c.center[1] = 0.5f;
                 c.angle = 0.0f;
                 c.length = 0.8f;
                 a.maskCount = 1;
                 a.layers[0].exposureEv = 2.0f;
             }, Metric::Luma, 0.36},
            // The same edit with a radial subtracted from the ramp's full
            // side — the only thing in the product that drives a mask group
            // of more than one component through the real pipeline, so this
            // is what catches the chain being miswired: a second component
            // whose params never arrive, an enable that follows the wrong
            // count, a fold against the wrong input. The compose *algebra*
            // is pinned exactly in orion-tests; what this pins is that the
            // graph delivers it. Moves noticeably less than the probe above
            // by construction, since the hole covers most of where that
            // edit lands.
            {"local +2 EV, hole", flat, [](auto& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 1;
                 c.center[0] = 0.5f; c.center[1] = 0.5f;
                 c.angle = 0.0f;
                 c.length = 0.8f;
                 auto& s = a.maskComponents[1];
                 s.kind = 2;
                 s.compose = 1;      // subtract
                 s.center[0] = 0.75f; s.center[1] = 0.5f;
                 s.radius[0] = 0.3f; s.radius[1] = 0.55f;
                 s.feather = 0.5f;
                 a.maskCount = 2;
                 a.layers[0].exposureEv = 2.0f;
             // Half the smallest ratio over the three frames: 0.47, 0.44,
             // 0.60 of the reference.
             }, Metric::Luma, 0.22},
            // Spot removal — research/spot-removal.md.
            //
            // ⚠ Four large spots, not one dust speck. A real dust spot is a
            // few thousand pixels of twenty-four million and moves a
            // whole-frame mean by nothing measurable — the same dilution
            // the mask-refine probe hit. What this asks is that the graph
            // still delivers the node on a photograph; the *behavior*
            // (heal keeps the destination's tone, clone does not) is pinned
            // exactly by testSpotRemovalGpu on a synthetic frame where the
            // right answer is a number rather than a tolerance.
            //
            // Clone rather than heal, deliberately: heal is designed to
            // leave the local tone alone, so it moves a mean by as little
            // as it can manage. Measuring the operation whose whole purpose
            // is to be invisible would be calibrating a floor against a
            // control working correctly.
            {"spots clone x4", flat, [](auto& a) {
                 a.spotCount = 4;
                 for (int i = 0; i < 4; ++i) {
                     auto& sp = a.spots[std::size_t(i)];
                     sp.destX = 0.20f + 0.20f * float(i);
                     sp.destY = 0.30f;
                     sp.srcX  = 0.20f + 0.20f * float(i);
                     sp.srcY  = 0.75f;
                     sp.radius = 0.10f;
                     sp.feather = 0.4f;
                     sp.heal = false;
                 }
             // Half the smallest ratio over the three frames: 0.46, 0.20,
             // 0.11 of the reference. The spread is what the four discs
             // happen to land on in each photograph, which is the honest
             // reason to calibrate against the smallest.
             }, Metric::Luma, 0.056},
            // A luminance range mask — research/masking.md §4b. A band
            // on the reference image, biased by the global exposure so its
            // numbers mean what is on screen.
            //
            // Everything from half a stop above middle gray up, darkened.
            // On all three frames that is a real part of the picture and
            // not the whole of it, which is the property the floor is
            // calibrated against.
            {"range +2 EV shadows", [](orion::pipe::Adjustments& a) {
                 // ⚠ Judged on a normally exposed frame, not on the raw at
                 // zero. These are night and dusk captures: at exposure 0
                 // almost every pixel sits below middle gray, so a genuine
                 // highlight band selects nothing and the probe reported NO
                 // EFFECT on one of the three. Widening the band until it
                 // moved would have "fixed" it by selecting the whole
                 // picture, which measures nothing about a *band*.
                 a.exposureEv = 2.6f;
             }, [](auto& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 5;
                 // ⚠ A *shadow* band, not a highlight one. A highlight
                 // band measured NO EFFECT on the night frame, and
                 // correctly so — it has almost nothing above middle gray,
                 // the same shape as dehaze finding no haze in a clear
                 // sky. Every photograph has shadows, so this is the end
                 // that exercises the band on all three.
                 c.rangeLo = -99.0f; c.rangeHi = -1.0f; c.rangeSoft = 1.0f;
                 a.maskCount = 1;
                 a.layers[0].exposureEv = 2.0f;
             // Half the smallest ratio over the three frames: 2.15, 3.08,
             // 0.34 of the reference. The spread is the band doing its job —
             // the two dark frames have far more below middle gray than the
             // daylight one, so the same band moves six times as much in
             // them.
             }, Metric::Luma, 0.16},
            // Local adjustments beyond exposure — research/masking.md §2b.
            //
            // ⚠ Measured on **chroma**, not luma, and that is the point of
            // the probe rather than a detail: a color cast that also moved
            // the exposure would be caught by a luma floor and a correct
            // one would not. The shader renormalizes the cast on luminance
            // precisely so it does not move brightness, so a luma probe
            // here would measure zero on a working control.
            {"local grade on a mask", [](orion::pipe::Adjustments& a) {
                 a.exposureEv = 2.6f;
             }, [](auto& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 2;                       // a radial over the middle
                 c.center[0] = 0.5f; c.center[1] = 0.5f;
                 c.radius[0] = 0.35f; c.radius[1] = 0.35f;
                 c.feather = 0.4f;
                 a.maskCount = 1;
                 a.layers[0].warmth = 1.0f;
                 a.layers[0].saturation = 0.8f;
             // Measured 0.159, 0.210 and 0.049 of reference across the
             // three frames; half the smallest, on the same rule as every
             // other probe here.
             //
             // ⚠ The daylight cityscape moves a third of what the two dark
             // frames do, and that is the control working rather than
             // noise: it is already the most saturated of the three, so a
             // cast and a saturation lift over its middle have proportionally
             // less room. A floor set from the night frames alone would trip
             // on it — which is the mistake `DECISIONS.md` #47 records
             // paying for twice already.
             }, Metric::Chroma, 0.024},
            // A color range mask — research/masking.md §4c.
            //
            // ⚠ The target is a **neutral**, and deliberately so. A probe
            // that picked a saturated shade would be measuring whether
            // these three particular photographs happen to contain it —
            // and the sample set is a night sky, a lit forecourt and a
            // daylight cityscape, which share almost no saturated color.
            // Every photograph contains near-neutrals: tarmac, concrete,
            // cloud, shadow. That is the same argument the band above makes
            // for choosing shadows over highlights.
            //
            // ⚠ And the metric ignores lightness, which is what makes a
            // neutral target a *large* selection rather than a token one:
            // it takes every gray in the frame at every brightness. The
            // tolerance is tight — 0.06 against the 0.126 that separates
            // the closest pair the research measured — so this is still a
            // selection and not the whole picture.
            {"color range, neutrals", [](orion::pipe::Adjustments& a) {
                 a.exposureEv = 2.6f;
             }, [](auto& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 6;
                 c.color[0] = c.color[1] = c.color[2] = 0.18f;
                 c.colorTol = 0.06f;
                 c.colorSoft = 0.02f;
                 a.maskCount = 1;
                 a.layers[0].exposureEv = 2.0f;
             // Measured 0.826, 0.226 and 0.221 of reference on the three
             // frames; half the smallest, on the same rule as every other
             // probe here. ⚠ The spread is the probe working rather than
             // noise: the forecourt is concrete, tarmac and a white
             // building, so a neutral target takes most of it, while the
             // night sky and the daylight cityscape are mostly colored.
             // A probe that measured the same on all three would be
             // selecting something that is not about color.
             }, Metric::Luma, 0.11},
            // A raster mask component — research/masking.md §5, the
            // shape a segmentation matte arrives in.
            //
            // ⚠ This probe exists because of the `prepare` hook above. A
            // matte is uploaded out of band, like a brush stroke, so
            // neither of the two `Adjustments&` hooks could reach it —
            // which is exactly why the brush has gone unprobed since it was
            // built. Both are reachable now.
            //
            // A centerd disc of radius 0.25 covers about a fifth of the
            // frame, so a two-stop local exposure through it moves roughly a
            // fifth of what an unmasked one would. That ratio is the point:
            // a matte that silently covered everything, or nothing, would
            // not land near it.
            {"matte +2 EV", flat, [](auto& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 4;
                 a.maskCount = 1;
                 a.layers[0].exposureEv = 2.0f;
             // Half the smallest ratio over the three frames: 0.43, 0.48,
             // 0.35 of the reference.
             }, Metric::Luma, 0.17, nullptr,
             [](orion::pipe::DevelopPipeline& d) {
                 const std::uint32_t mw = d.maxMatteWidth();
                 const std::uint32_t mh = d.maxMatteHeight();
                 std::vector<float> a(std::size_t(mw) * mh, 0.0f);
                 for (std::uint32_t y = 0; y < mh; ++y) {
                     for (std::uint32_t x = 0; x < mw; ++x) {
                         const double u = (x + 0.5) / mw - 0.5;
                         const double v = (y + 0.5) / mh - 0.5;
                         if (u * u + v * v < 0.25 * 0.25) {
                             a[std::size_t(y) * mw + x] = 1.0f;
                         }
                     }
                 }
                 d.setMaskMatte(0, a.data(), int(mw), int(mh));
             }},
            // A brush stroke — research/masking.md §1 and §3.
            //
            // ⚠ **The oldest gap in `STATUS.md` closes here.** The brush has
            // been unprobed since it was built, because a stroke is uploaded
            // out of band — like a matte — and neither `Adjustments&` hook
            // could reach it. The `prepare` hook that the matte probe needed
            // is what makes this one possible; it has existed for several
            // sessions and nothing had used it twice.
            //
            // ⚠ Dabs are in **displayed** coordinates and go through
            // `mask::toFrame` on the way in, so a stroke laid on a diagonal
            // exercises the transform as well as the kernel. A stroke along
            // one axis would still land correctly under a transform that had
            // swapped or dropped a term.
            //
            // A stroke of 120 dabs at radius 0.05 covers a band roughly a
            // tenth of the frame, so a two-stop local exposure through it
            // moves about a tenth of what an unmasked one would — the same
            // shape of ratio the matte probe is calibrated against, and what
            // separates a working stroke from one that covered everything or
            // nothing.
            {"brush +2 EV", flat, [](auto& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 3;
                 c.brushRadius = 0.05f;
                 c.brushFlow = 1.0f;
                 c.brushHardness = 0.7f;
                 // ⚠ The revision has to move or `apply` skips the
                 // component entirely — the trap the header documents and
                 // the reason a caller cannot be trusted to remember it.
                 c.brushRevision = 1;
                 a.maskCount = 1;
                 a.layers[0].exposureEv = 2.0f;
             // Measured 0.205, 0.199 and 0.147 of reference across the
             // three frames; half the smallest, on the rule every probe
             // here follows.
             //
             // ⚠ **And the number this probe existed to find: a 120-dab
             // stroke costs 112-162 ms to render.** The kernel loops every
             // dab at every pixel, rejecting on a bounding square first, so
             // the cost is linear in the stroke's length — a long stroke is
             // not free the way a gradient is. Degrade-then-refine hides it
             // while the hand is moving, because the preview graph has a
             // sixteenth of the pixels; the full render on settle is what
             // this measures. Worth watching if strokes get longer: at the
             // 16,384-dab cap this shape of loop would be unusable, and the
             // answer then is a bounding box per dab-block rather than a
             // faster inner test.
             }, Metric::Luma, 0.073, nullptr,
             [](orion::pipe::DevelopPipeline& d) {
                 // A diagonal, corner to corner, at even spacing.
                 constexpr int kDabs = 120;
                 std::vector<float> xy;
                 xy.reserve(std::size_t(kDabs) * 2);
                 for (int i = 0; i < kDabs; ++i) {
                     const float t = float(i) / float(kDabs - 1);
                     xy.push_back(0.10f + 0.80f * t);
                     xy.push_back(0.15f + 0.70f * t);
                 }
                 d.setBrushStroke(0, xy.data(), nullptr, kDabs);
             }},
            // Guided feathering, research/masking.md §4.
            //
            // ⚠ The context is the *same mask, unrefined*, so what is
            // measured is what the refinement itself does. Against a flat
            // frame this would be reporting the local exposure's effect and
            // would pass with the entire chain disabled.
            //
            // ⚠ **The floor is small, and the reason is structural rather
            // than a weakness in the filter.** Refinement only moves the
            // mask's *boundary*, so at a radius of 60 pixels it can touch a
            // band about 120 pixels wide — one or two percent of a 24 MP
            // frame — and a whole-frame mean divides its effect by the other
            // ninety-eight. A near-binary full-width gradient was tried as a
            // more sensitive shape and measured *lower* (0.008-0.018 of
            // reference), for the same reason. What pins this control is
            // testMaskRefineGpu, which checks the chain against the filter
            // computed directly; this line's job is that the graph still
            // delivers it on a photograph and has not become a no-op.
            //
            // A hard-edged radial across the silver car, deliberately: the
            // filter can only move a boundary onto an edge near it, so a
            // mask placed in open sky would correctly measure nothing and
            // the floor would be demanding the filter invent structure.
            // Half the smallest ratio over the three frames: 0.021, 0.023,
            // 0.016 of the reference.
            {"mask refine 1.0", [](orion::pipe::Adjustments& a) {
                 auto& c = a.maskComponents[0];
                 c.kind = 2;
                 c.center[0] = 0.42f; c.center[1] = 0.55f;
                 c.radius[0] = 0.18f; c.radius[1] = 0.18f;
                 c.feather = 0.02f;
                 a.maskCount = 1;
                 a.layers[0].exposureEv = 2.0f;
             }, [](auto& a) { a.maskRefine = 1.0f; }, Metric::Luma, 0.008},
            {"fusion 1.0",     flat, [](auto& a) { a.fusion = 1.0f; }, Metric::Luma, 0.57},
            {"dehaze 1.0",     flat, [](auto& a) { a.dehaze = 1.0f; }, Metric::Luma, 0.028,
             "a haze-free frame has nothing to remove; t = 1 is the right answer"},
            {"clarity +1",     flat, [](auto& a) { a.clarity = 1.0f; }, Metric::Detail, 0.062},
            {"clarity -1",     flat, [](auto& a) { a.clarity = -1.0f; }, Metric::Detail, 0.055},
            {"denoise 2.0",    flat, [](auto& a) { a.denoiseLuma = 2.0f; }, Metric::Detail, 0.018},
            // ⚠ **Detail, not luma.** Film grain is zero-mean by
            // construction — that is the property that keeps it from
            // shifting exposure — so a probe on mean brightness reads
            // exactly zero for a working grain node and exactly zero for
            // one that was never dispatched. `Metric::Detail` is the mean
            // *absolute* difference between neighbors, which is the one
            // thing grain unambiguously raises.
            // ⚠ A real floor, not a 0.0 that only looks like one. It sat at
            // 0.0 for an afternoon, and 0.0 is what this probe reads when
            // the node was never dispatched — which is what the retarget
            // did when it pushed the previous frame's Amount.
            //
            // Measured 0.127, 0.123 and 0.125 of the exposure reference on
            // `_PIC8220`, `_PIC8095` and `_PIC8148`. That the three agree
            // to a percent is the point: grain's amplitude comes from the
            // slider rather than from the scene, so unlike every filter
            // around it this floor does not have to be set from the frame
            // that shows the effect least. Half of it, which leaves room
            // for the plate to be reseeded without a false red.
            {"grain 0.04",     flat, [](auto& a) { a.grainAmount = 0.04f; }, Metric::Detail, 0.06},
            {"mixer blue lum", flat, [](auto& a) { a.lumShift[5] = -1.0f; }, Metric::Luma, 0.068},
            {"mixer blue sat", flat, [](auto& a) { a.satShift[5] = 1.0f; }, Metric::Chroma, 0.03},
            // Lens. Distortion only resamples, so mean luma barely moves —
            // vignetting is the readable one.
            {"lens vignette",  flat, [](auto& a) { a.lensVignette = 1.0f; }, Metric::Luma, 0.1},
            // Perspective, for the same reason and with the same shape: a
            // homography only *moves* pixels, so mean luma is close to
            // silent and `moved` is the whole signal. research/perspective.md.
            //
            // ⚠ The floor is high because a geometry change is among the
            // loudest things in this table, and it is calibrated across
            // three frames per decision #47: 1.84, 0.98 and 1.43 times the
            // exposure reference on `_PIC8220`, `_PIC8095` and `_PIC8148`.
            // Half the smallest. The spread is nearly two to one and it is
            // the frame's own texture, not the correction — a warp of a
            // smooth night sky moves fewer values than a warp of a detailed
            // forecourt, and a floor set from `_PIC8220` alone would have
            // been red on the other two.
            //
            // ⚠ It is also the only probe here that runs **one node**: the
            // homography lives inside the geometry pass that was already
            // there, so the graph does not grow. A day when this line reads
            // 2 nodes is the day somebody chained a second resample.
            {"perspective 0.6", flat, [](auto& a) { a.perspectiveVertical = 0.6f; },
             Metric::Luma, 0.48},
            // ⚠ The line above **removes** a falloff and this one puts one
            // in. They are different controls in different nodes and the
            // two probes sit together so nobody has to take that on trust:
            // if the creative one were ever wired into the lens correction,
            // this would still pass and `testCreativeVignetteGpu` would go
            // red. Keeping the pair adjacent is the reminder.
            //
            // Luma, and it is unambiguous: a vignette is an exposure change
            // over most of the frame, so unlike the wheels there is no
            // question of a mean cancelling it.
            //
            // Measured against the exposure reference on the three sample
            // frames: **0.79, 1.05 and 0.70**. Half the smallest, on the
            // rule every probe here follows.
            //
            // ⚠ Those three do *not* agree the way grain's do, and the
            // reason is worth writing down rather than averaging away.
            // Grain's amplitude is defined in display units, so it is the
            // same wherever the scene sits; a vignette is defined in stops
            // of scene-linear light, and what two stops down is worth on
            // screen depends on where the corner started on AgX's curve.
            // The night frame's corners are already low on the toe, where
            // the curve is shallow and a stop buys less.
            {"vignette -2 EV", flat, [](auto& a) { a.vignetteAmount = -2.0f; }, Metric::Luma, 0.35},

            // ── Three-way grading ────────────────────────────────────
            // The newest node in the graph, and it had neither a probe
            // here nor a GPU test. It has both now, and they say the
            // shadow wheel works and the other two barely reach.
            {"grade shadows",  mid, [](auto& a) { a.gradeShadow[0] = -0.6f; a.gradeShadow[1] = -0.6f; }, Metric::Saturation, 0.01},
            {"grade midtones", mid, [](auto& a) { a.gradeMidtone[0] = 0.6f; a.gradeMidtone[1] = -0.6f; }, Metric::Saturation, 0.043},
            {"grade highlights", mid, [](auto& a) { a.gradeHighlight[0] = 0.6f; a.gradeHighlight[1] = 0.4f; }, Metric::Saturation, 0.008},
        };

        // Measured first, checked second: the floors are ratios against the
        // reference controls, and those are probes like any other.
        struct Result { double value, delta, moved, ms; int nodes; };
        std::vector<Result> results;
        results.reserve(std::size(probes));

        for (const auto& probe : probes) {
            if (probe.prepare) probe.prepare(develop);
            auto ctx = base;
            probe.context(ctx);
            develop.apply(ctx);
            develop.render();
            const double against = meanOf(develop, probe.metric);
            const auto before = output16(develop, develop.outputWidth(),
                                         develop.outputHeight());

            auto a = ctx;
            probe.set(a);
            develop.apply(a);
            const double ms = develop.render();
            const double value = meanOf(develop, probe.metric);
            const auto after = output16(develop, develop.outputWidth(),
                                        develop.outputHeight());

            int nodes = 0;
            for (const auto& n : develop.graph().lastRun()) if (n.executed) ++nodes;
            results.push_back({value, value - against,
                               meanAbsDiff(before, after), ms, nodes});
        }

        // The reference each floor is a fraction of.
        double refMoved = 0.0;
        for (std::size_t i = 0; i < std::size(probes); ++i) {
            if (std::string(probes[i].name) == "exposure +1 EV")
                refMoved = results[i].moved;
        }

        int waivedHere = 0;
        for (std::size_t i = 0; i < std::size(probes); ++i) {
            const auto& p = probes[i];
            const auto& r = results[i];
            const double floor = p.least * refMoved;
            const bool moved = r.moved > 2e-4;
            const bool enough = r.moved >= floor;

            // ⚠ A control can move the picture by the right amount with a
            // whole chain under it dead. See `leastNodes`.
            const bool wholeChain = p.leastNodes == 0 || r.nodes >= p.leastNodes;

            const char* verdict = !moved      ? "NO EFFECT"
                                : !enough     ? "TOO WEAK"
                                : !wholeChain ? "CHAIN MISSING"
                                              : "ok";
            if ((!moved || !enough || !wholeChain) && !p.waived) controlsPass = false;

            std::printf("  %-18s moved %.4f  %-6s %+.4f  %6.2f ms  %2d nodes  %-9s",
                        p.name, r.moved,
                        p.metric == Metric::Chroma ? "chroma"
                          : p.metric == Metric::Detail ? "detail"
                          : p.metric == Metric::Saturation ? "sat" : "luma",
                        r.delta, r.ms, r.nodes, verdict);
            // The floor prints on every line, passing or not. A threshold
            // nobody can see is a threshold nobody maintains.
            std::printf(" [>= %.4f]", floor);
            if (p.leastNodes) std::printf(" [>= %d nodes]", p.leastNodes);
            if (p.waived && (!moved || !enough)) {
                std::printf("  WAIVED: %s", p.waived);
                ++waivedHere;
            }
            std::printf("\n");
        }

        // ⚠ A waiver that only appears mid-table is a waiver nobody reads.
        //
        // Measured 2026-07-31: with dehaze disabled at the host in one line,
        // this bench exited 0 and both test suites passed — the control was
        // gone from the product and every instrument said fine. The waiver
        // itself is right (on a frame with no veil the correct output is no
        // change, and a floor there would demand a filter invent haze); what
        // was wrong is that it excused the control on *every* frame while
        // saying so in one line among thirty.
        //
        // The wiring is pinned by `repro/dehaze-reaches-the-picture.txt`
        // now. This line exists so the next waiver cannot be quiet.
        if (waivedHere > 0) {
            std::printf("  ⚠ %d control%s waived on this frame — not pinned "
                        "here. See repro/ for what covers them.\n",
                        waivedHere, waivedHere == 1 ? "" : "s");
        }
        develop.apply(base);
        develop.render();
    }
}

}  // namespace bench

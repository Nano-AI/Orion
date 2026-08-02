/*  orion-bench — the features composed, and auto-enhance.
 *
 *  Each M3 feature is verified alone elsewhere. These two ask what happens when
 *  they run together, and whether the solver that drives them converged.
 *
 *  ⚠ `composed` measures the creative LUT that `controlProbes` loaded, so it
 *  has to run after it.
 */
#include "bench.h"

#include "pipe/AutoEnhance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace bench {

void composed(Bench& b) {
    auto& develop = b.develop;
    bool& controlsPass = b.controlsPass;

    // ── Everything at once ────────────────────────────────────────────
    //
    // Each M3 feature is verified alone. Nothing verified them *composed*,
    // and they are exactly the kind that interact: dehaze divides by a
    // transmission, exposure fusion divides one proxy luminance by another,
    // clarity raises a normalized amplitude to a fractional power, and the
    // creative LUT indexes a grid with whatever comes out. A NaN from any
    // one of them is invisible on screen — it renders as a black or white
    // pixel — and propagates through everything downstream of it.
    {
        std::printf("\nAll M3 features together\n");

        orion::pipe::Adjustments all;
        all.wb = develop.asShotWhiteBalance();
        all.dehaze  = 1.0f;
        all.clarity = 1.0f;
        all.fusion  = 1.0f;
        all.lutStrength = 1.0f;      // the look loaded for the probes above
        all.exposureEv = 0.75f;      // and a tone move underneath them
        all.blacks = -0.3f;
        all.whites = 0.4f;

        develop.apply(all);
        const double ms = develop.render();

        int nodes = 0;
        for (const auto& n : develop.graph().lastRun()) if (n.executed) ++nodes;

        const std::uint32_t w = develop.outputWidth();
        const std::uint32_t h = develop.outputHeight();
        const auto pixels = output16(develop, w, h);

        // The output is 16-bit unsigned, so a NaN cannot survive as a NaN —
        // it has already been converted, and what it leaves behind is a
        // pixel pinned hard at one end. Count those instead, and compare
        // against the same count with the four features off: a photograph
        // legitimately contains black and white pixels, and the question is
        // whether these features *added* them.
        const auto extremes = [&](const std::vector<std::uint16_t>& p) {
            std::size_t n = 0;
            for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
                const bool low  = p[i * 4] == 0 && p[i * 4 + 1] == 0 && p[i * 4 + 2] == 0;
                const bool high = p[i * 4] == 65535 && p[i * 4 + 1] == 65535 &&
                                  p[i * 4 + 2] == 65535;
                if (low || high) ++n;
            }
            return double(n) / double(std::size_t(w) * h);
        };
        const double withAll = extremes(pixels);

        orion::pipe::Adjustments toneOnly = all;
        toneOnly.dehaze = toneOnly.clarity = toneOnly.fusion = 0.0f;
        toneOnly.lutStrength = 0.0f;
        develop.apply(toneOnly);
        develop.render();
        const double withoutAll = extremes(output16(develop, w, h));

        std::printf("  %d nodes, %.2f ms\n", nodes, ms);
        std::printf("  pinned pixels: %.4f%% with the four on, %.4f%% with them off\n",
                    withAll * 100.0, withoutAll * 100.0);

        // Composed, they may legitimately clip more than the tone controls
        // alone — dehaze darkens and fusion lifts. What they may not do is
        // produce a frame that is largely pinned, which is what arithmetic
        // gone wrong looks like once it has been converted to integers.
        const bool sane = withAll < 0.05;
        std::printf("  composes cleanly: %s\n", sane ? "yes" : "NO");
        if (!sane) controlsPass = false;

        orion::pipe::Adjustments restore;
        restore.wb = develop.asShotWhiteBalance();
        develop.apply(restore);
        develop.render();
    }
}

void autoEnhance(Bench& b) {
    auto& develop = b.develop;
    bool& controlsPass = b.controlsPass;

    // ── Auto-enhance, checked by outcome ──────────────────────────────
    //
    // Everything else about auto-enhance is tested against a stand-in for
    // the pipeline. This is the only check that runs the real one, on a
    // real photograph, and it asks the only question that matters: did the
    // median land where it was aimed?
    //
    // Not a magnitude probe with a floor, because "it moved" is not the
    // claim. The claim is that it converged.
    {
        std::printf("\nAuto-enhance\n");
        namespace ae = orion::pipe::auto_enhance;
        constexpr std::uint32_t kBins = 256;

        const auto measure = [&](const orion::pipe::Adjustments& a) {
            develop.apply(a);
            develop.render();
            const std::uint32_t w = develop.outputWidth();
            const std::uint32_t h = develop.outputHeight();
            const auto pixels = output16(develop, w, h);

            std::vector<std::uint32_t> bins(kBins, 0u);
            // The same prime stride the engine's histogram uses, for the
            // same reason: a picket fence must not alias into a peak.
            constexpr std::size_t kStride = 31;
            for (std::size_t i = 0; i < std::size_t(w) * h; i += kStride) {
                for (int c = 0; c < 3; ++c) {
                    const float v = float(pixels[i * 4 + c]) / 65535.0f;
                    ++bins[std::min<std::uint32_t>(kBins - 1,
                          std::uint32_t(std::clamp(v, 0.0f, 1.0f) * float(kBins)))];
                }
            }
            return ae::measure(bins.data(), kBins, ae::kClipPerSide);
        };

        orion::pipe::Adjustments a;
        a.wb = develop.asShotWhiteBalance();

        const ae::Stats before = measure(a);
        const ae::Controls look = ae::look(before);
        a.fusion = look.fusion;
        a.clarity = look.clarity;

        // ⚠ **The solve starts two stops the wrong way on purpose, and
        //    that is the whole of this check.**
        //
        //    Started from rest, the solver may never be consulted: the
        //    look's fusion and clarity are set from the *first*
        //    measurement, and on a frame they happen to carry across, the
        //    median is already inside `kSettled` when the loop first looks
        //    and it breaks before calling `refine` once. `_PIC8220` is such
        //    a frame — it lands at 0.4570 against a 0.4610 anchor with
        //    exposure, blacks and whites all at zero — so mutation M11
        //    (#118) could make `refine` return its argument unchanged and
        //    this line still printed `yes`. It went red on `_PIC8095`. A
        //    check that depends on which photograph was passed is not a
        //    check, and "run it on the other file" is not a fix.
        //
        //    So displace the start away from the anchor, in whichever
        //    direction that is for this frame. Two stops is far outside
        //    `kSettled` under any transfer function, the response is
        //    monotonic, so the first measurement is off-anchor on *every*
        //    frame and the loop cannot reach the exit without `refine`
        //    having moved something. It is also the stronger claim: the
        //    solver is a fixed-point iteration, so where it lands must not
        //    depend on where it was started.
        ae::Controls c{};
        {
            const ae::Stats rest = measure(a);
            c.exposureEv = rest.median < ae::kMidGray ? -2.0f : 2.0f;
        }
        const float startEv = c.exposureEv;

        // Mirrors Engine::autoEnhance: stop when the median arrives rather
        // than after a fixed count, or this probe measures a different
        // solver from the one the product runs.
        int refines = 0;
        float startMedian = 0.0f;
        for (int pass = 0; pass < ae::kMaxPasses; ++pass) {
            a.exposureEv = c.exposureEv;
            a.blacks     = c.blacks;
            a.whites     = c.whites;
            const auto st = measure(a);
            if (pass == 0) startMedian = st.median;
            if (std::abs(st.median - ae::kMidGray) < ae::kSettled) break;
            c = ae::refine(c, st);
            ++refines;
        }
        a.exposureEv = c.exposureEv;
        a.blacks = c.blacks; a.whites = c.whites;
        const ae::Stats after = measure(a);

        const double err = std::abs(double(after.median) - double(ae::kMidGray));
        std::printf("  median %.4f -> %.4f  (anchor %.4f, off by %.4f)\n",
                    before.median, after.median, ae::kMidGray, err);
        std::printf("  solved from %+.2f EV: median %.4f -> %.4f in %d passes\n",
                    startEv, startMedian, after.median, refines);
        std::printf("  set: exposure %+.2f EV, blacks %+.2f, whites %+.2f, "
                    "lift %.2f, clarity %.2f\n",
                    a.exposureEv, a.blacks, a.whites, a.fusion, a.clarity);

        // A frame needing more than the clamp allows cannot reach the
        // anchor, and should not be failed for it — but it must have gone
        // as far as it is permitted to.
        const bool clamped = std::abs(std::abs(a.exposureEv) - ae::kMaxExposureEv) < 1e-3f;
        const bool landed = err < 0.05 || clamped;
        // And it must have got there by solving. Displaced two stops, a
        // solver that does nothing cannot arrive, and one that arrives
        // without iterating did not exist.
        const bool iterated = refines >= 1;
        std::printf("  converged: %s%s%s\n", landed && iterated ? "yes" : "NO",
                    clamped ? " (at the exposure clamp)" : "",
                    iterated ? "" : " (SOLVER NEVER RAN)");
        if (!landed || !iterated) controlsPass = false;

        // Put the graph back where the sections after this expect it.
        orion::pipe::Adjustments restore;
        restore.wb = develop.asShotWhiteBalance();
        develop.apply(restore);
        develop.render();
    }
}

}  // namespace bench

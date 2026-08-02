/*  orion-bench — invariants, not magnitudes.
 *
 *  ⚠ **This is the file a new node gets an invalidation check in.** The probes
 *  in `bench_controls.cpp` ask "did this move the image, and by roughly the
 *  amount it should". These ask something exact, and each exists because the
 *  loose version of it passed while the code was wrong.
 *
 *  A check belonging to a feature big enough to own a file lives there instead
 *  — the highlight fill in `bench_highlights.cpp`, the brush in
 *  `bench_brush.cpp`. This file is the home for the rest.
 */
#include "bench.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bench {

void invariants(Bench& b) {
    auto& develop = b.develop;
    bool& invariantsPass = b.invariantsPass;
    const int ran = b.cleanNodes;

    // ── Invariants, not magnitudes ────────────────────────────────────
    //
    // The probes above ask "did this move the image, and by roughly the
    // amount it should". That is a net, not a proof. These two ask
    // something exact, and each one exists because the loose version of it
    // passed while the code was wrong.
    std::printf("\nInvariants\n");
    orion::pipe::Adjustments base;
    base.wb = develop.asShotWhiteBalance();

    // 1. A disabled guide chain must not change what the endpoint
    //    controls do.
    //
    //    The guided filter is seven nodes feeding only highlights and
    //    shadows, so it is switched off when both are zero — and a
    //    disabled node resolves to the last live producer, so the two
    //    guide bindings then carried the color matrix's linear RGB.
    //    The shader read it as log2 luminance and as filter
    //    coefficients. The offsets were zero so nothing moved
    //    directly, but the four band weights normalize to a partition
    //    of unity and two of them came from that garbage — diluting
    //    whites and blacks per pixel by an amount that varied with the
    //    pixel's own color. `whites +1` was moving mean luma +0.1105
    //    when it should move +0.0064: an endpoint control acting as a
    //    second exposure slider, on every frame, printing `ok`.
    //
    //    A shadow slider at 1e-6 turns the chain on and is worth
    //    2e-6 EV, so the two runs must agree to the last digit.
    const auto endpointPair = [&](const char* name,
                                  void (*set)(orion::pipe::Adjustments&)) {
        auto off = base;
        set(off);
        develop.apply(off);
        develop.render();
        const double a = meanOf(develop, Metric::Luma);

        auto on = off;
        on.shadows = 1e-6f;
        develop.apply(on);
        develop.render();
        const double b = meanOf(develop, Metric::Luma);

        const bool ok = std::abs(a - b) < 5e-4;
        if (!ok) invariantsPass = false;
        std::printf("  %-24s guide off %.4f  guide on %.4f  (%+.5f)  %s\n",
                    name, a, b, b - a, ok ? "ok" : "DILUTED");
    };
    endpointPair("blacks -1, guide off/on",
                 [](auto& a) { a.blacks = -1.0f; });
    endpointPair("whites +1, guide off/on",
                 [](auto& a) { a.exposureEv = 5.5f; a.whites = 1.0f; });

    // 2. Lens corrections must not cost incremental invalidation.
    //
    //    `correctingLens` was a condition for re-pushing the lens
    //    params, and it is true whenever a slider is *nonzero* rather
    //    than when one *changed*. setParams dirties everything
    //    downstream, so a vignette left on turned every exposure tick
    //    into seven full-resolution passes instead of three. No
    //    latency number caught it because every latency number was
    //    taken with the lens sliders at zero — the state that stops
    //    being normal the moment a lens database lands.
    auto lens = base;
    lens.lensVignette = 0.5f;
    lens.lensDistortion = -0.4f;
    develop.apply(lens);
    develop.render();

    std::vector<double> lensDrag;
    int lensNodes = 0;
    for (int i = 0; i < 12; ++i) {
        lens.exposureEv = -1.5f + 3.0f * i / 11.0f;
        develop.apply(lens);
        lensDrag.push_back(develop.render());
        lensNodes = 0;
        for (const auto& n : develop.graph().lastRun())
            if (n.executed) ++lensNodes;
    }
    std::sort(lensDrag.begin(), lensDrag.end());
    const bool lensOk = lensNodes == ran;
    if (!lensOk) invariantsPass = false;
    std::printf("  %-24s %d nodes, %.2f ms median  (clean: %d nodes)  %s\n",
                "exposure drag, lens on", lensNodes,
                lensDrag[lensDrag.size() / 2], ran,
                lensOk ? "ok" : "LENS DIRTIES THE GRAPH");

    // 3. The dehaze slider must not re-run the dark-channel chain.
    //
    //    Same shape as 2, and it cost twice as much. Only omega moves
    //    with the slider; the dark channel, the six rank passes and the
    //    candidate pooling are functions of the frame's size, the
    //    paper's constants and A. They were re-pushed on every tick,
    //    and `setParams` dirties the whole downstream subgraph whether
    //    or not the bytes changed — so a drag paid for nine extra
    //    nodes, six of them full-resolution rank passes over 24 MP.
    //
    //    Asserted by *name*, not by a total. A count alone would be
    //    satisfied by any nine nodes, and the whole point is which ones.
    //    A wall-clock threshold would be worse still: this machine has
    //    measured the same binary at 8.97 and 44.53 ms p95 within an
    //    hour, and node identity does not care.
    static const char* const sliderIndependent[] = {
        "dehaze:channel min", "dehaze:channel min/A",
        "dehaze:dark h", "dehaze:dark v", "dehaze:candidates",
        "dehaze:min h", "dehaze:min v", "dehaze:max h", "dehaze:max v",
    };

    auto haze = base;
    haze.dehaze = 0.3f;
    develop.apply(haze);
    develop.render();   // pays for the chain, and settles A

    // ⚠ The **worst** tick, not the last one. Reporting the last would
    // pass a chain that redid itself eleven times out of twelve, which
    // is the whole cost back for a green line.
    int hazeNodes = 0, hazeStale = 0;
    std::string firstStale;
    for (int i = 0; i < 12; ++i) {
        haze.dehaze = 0.3f + 0.6f * static_cast<float>(i) / 11.0f;
        develop.apply(haze);
        develop.render();
        int ranHere = 0, staleHere = 0;
        for (const auto& n : develop.graph().lastRun()) {
            if (!n.executed) continue;
            ++ranHere;
            for (const char* s : sliderIndependent) {
                if (n.name != s) continue;
                ++staleHere;
                if (firstStale.empty()) firstStale = n.name;
            }
        }
        hazeNodes = std::max(hazeNodes, ranHere);
        hazeStale = std::max(hazeStale, staleHere);
    }
    const bool hazeOk = hazeStale == 0;
    if (!hazeOk) invariantsPass = false;
    const std::string staleDetail =
        firstStale.empty() ? std::string{} : " (" + firstStale + " ...)";
    std::printf("  %-24s %d nodes, %d of them slider-independent%s  %s\n",
                "dehaze drag", hazeNodes, hazeStale, staleDetail.c_str(),
                hazeOk ? "ok" : "DEHAZE REDOES THE DARK CHANNEL");

    develop.apply(base);
    develop.render();

    // 3b. A Balance drag on an ungraded photograph must run nothing.
    //
    //     Balance slides the three zone centres, and with every wheel
    //     centred those weights multiply an all-zero offset and a slope
    //     of one — the kernel would be an exact identity over the whole
    //     frame. Decision #82 is a node run at zero strength and #92 is
    //     a block re-pushed for a value nothing reads; this control is
    //     both of them at once if it is wired the obvious way.
    //
    //     By name, like 3. `grade + vignette` is the node, and it is
    //     full-resolution.
    auto bal = base;
    int balRuns = 0, balNodes = 0;
    for (int i = 0; i < 8; ++i) {
        bal.gradeBalance = -1.0f + 2.0f * static_cast<float>(i) / 7.0f;
        develop.apply(bal);
        develop.render();
        int ranHere = 0;
        for (const auto& n : develop.graph().lastRun()) {
            if (!n.executed) continue;
            ++ranHere;
            if (n.name == "grade + vignette") ++balRuns;
        }
        balNodes = std::max(balNodes, ranHere);
    }
    const bool balOk = balRuns == 0;
    if (!balOk) invariantsPass = false;
    std::printf("  %-24s %d nodes, grade ran %d times  %s\n",
                "balance with no grade", balNodes, balRuns,
                balOk ? "ok" : "BALANCE RUNS THE GRADE FOR NOTHING");

    // And the mirror of it: with a wheel off centre, Balance is a real
    // control and the node *must* re-run. A guard that switched the
    // grade off for Balance entirely would satisfy the line above and
    // silently break the feature, so the two are asserted together.
    auto balOn = base;
    balOn.gradeShadow[0] = -0.6f;
    balOn.gradeShadow[1] = -0.6f;
    develop.apply(balOn);
    develop.render();
    int balOnRuns = 0;
    for (int i = 0; i < 4; ++i) {
        balOn.gradeBalance = -0.8f + 0.5f * static_cast<float>(i);
        develop.apply(balOn);
        develop.render();
        for (const auto& n : develop.graph().lastRun())
            if (n.executed && n.name == "grade + vignette") ++balOnRuns;
    }
    const bool balOnOk = balOnRuns == 4;
    if (!balOnOk) invariantsPass = false;
    std::printf("  %-24s grade ran %d of 4 ticks  %s\n",
                "balance with a grade", balOnRuns,
                balOnOk ? "ok" : "BALANCE DOES NOT REACH THE GRADE");

    develop.apply(base);
    develop.render();
}

void pathAgreement(Bench& b) {
    auto& develop = b.develop;
    bool& invariantsPass = b.invariantsPass;
    orion::pipe::Adjustments base;
    base.wb = develop.asShotWhiteBalance();

    // 4. The screen path and the export path must agree.
    //
    //    They are different formats now — eight bits for the screen,
    //    sixteen around an export — and a stale parameter push after the
    //    mode switch, a format that did not actually change, or a wrong
    //    dither would all show up here and nowhere else. The bound is one
    //    eight-bit step: that is the whole difference the narrow path is
    //    allowed to make.
    //
    //    ⚠ **Two statistics, because one of them cannot see the dither.**
    //    The frame means below catch a *biased* narrow path — a dither
    //    with a DC term, a missing push, the wrong node quantising. They
    //    are nearly blind to a wrong dither *magnitude*, because an
    //    ordered dither is zero-mean by construction: mutation M9a (#118)
    //    multiplied `bayerOffset` by forty and this line stayed green at
    //    0.00313 against its 0.00392 bound, the gap coming only from
    //    `saturate` clipping the ends off a dither eighty times too wide.
    //    A magnitude error is not a shift, it is a *spread*, so the gate
    //    is the mean per-pixel deviation between the two buffers.
    //
    //    That bound is the same claim as the one beside it, made per
    //    pixel. By construction the narrow path is `round(v + d)` with
    //    |d| <= 0.5/255, then a resample and a second round — so the
    //    honest deviation is a fraction of a code and scales linearly
    //    with the dither's amplitude. Nothing here is a wall clock and
    //    nothing is a sampled threshold: an ordered dither is a fixed
    //    table, so this number is the same on every run of a given frame.
    auto look = base;
    look.exposureEv = 1.4f;
    look.blacks = -0.5f;
    look.straightenDeg = 3.0f;   // make the geometry node resample

    develop.setWideOutput(false);
    develop.apply(look);
    develop.render();
    const std::uint32_t w = develop.outputWidth();
    const std::uint32_t h = develop.outputHeight();
    const auto screenPixels = output16(develop, w, h);
    const double screenLuma = meanOf(develop, Metric::Luma);
    const double screenChroma = meanOf(develop, Metric::Chroma);

    develop.setWideOutput(true);
    develop.apply(look);
    develop.render();
    const auto exportPixels = output16(develop, w, h);
    const double exportLuma = meanOf(develop, Metric::Luma);
    const double exportChroma = meanOf(develop, Metric::Chroma);
    develop.setWideOutput(false);

    const double lumaGap = std::abs(screenLuma - exportLuma);
    const double chromaGap = std::abs(screenChroma - exportChroma);

    // The spread. `meanAbsDiff` is the one instrument here that cannot
    // cancel, which is exactly the property the means lack.
    const double spread = meanAbsDiff(screenPixels, exportPixels);
    // And the worst pixel, printed rather than asserted: it is the number
    // that says *how* the spread went wrong, and a max over 24 MP is a
    // reasonable thing to read and a poor thing to gate on.
    double worst = 0.0;
    for (std::size_t i = 0; i + 3 < screenPixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            worst = std::max(worst, std::abs(double(screenPixels[i + c]) -
                                             double(exportPixels[i + c])));
        }
    }
    worst /= 65535.0;

    const double kStep = 1.0 / 255.0;
    const bool agree = lumaGap < kStep && chromaGap < kStep && spread < kStep;
    if (!agree) invariantsPass = false;
    std::printf("  %-24s screen %.5f  export %.5f  (%+.5f luma, %+.5f chroma)  %s\n",
                "screen vs export path", screenLuma, exportLuma,
                screenLuma - exportLuma, screenChroma - exportChroma,
                agree ? "ok" : "PATHS DISAGREE");
    std::printf("  %-24s %.5f mean per pixel, %.5f worst  [< %.5f]  %s\n",
                "  dither spread", spread, worst, kStep,
                spread < kStep ? "ok" : "DITHER MAGNITUDE");

    develop.apply(base);
    develop.render();
}

}  // namespace bench

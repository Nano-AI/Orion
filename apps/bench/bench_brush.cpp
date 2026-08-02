/*  orion-bench — the brush.
 *
 *  Both halves: the accumulation invariant (#108), asserted by the dab index
 *  the kernel started its loop at rather than by a millisecond, and the
 *  node-by-node stroke profiles that located the slope in the first place.
 */
#include "bench.h"

#include "Engine.h"   // kPreviewScale — the profiles build the preview graph

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bench {

void brushAccumulation(Bench& b) {
    auto& develop = b.develop;
    bool& invariantsPass = b.invariantsPass;

    // 3d. Appending to a stroke must cost the appended dabs, not all of
    //     them — and a stroke whose head moved must cost all of them.
    //
    //     ⚠ **By name and by count, never in milliseconds.** The M0
    //     gate on this machine ranges 8.9 to 31.5 ms across eight runs
    //     of one unchanged binary, so a speed claim asserted as a
    //     threshold would be a coin toss. What is asserted here is
    //     `firstDab`: the dab the kernel started its loop at, which is
    //     a fact about the dispatch rather than about the GPU's clock.
    //
    //     ⚠ **And the refusal is asserted beside it.** An accumulator
    //     that continues from *anything* is fast and wrong: undo three
    //     dabs, paint three different ones, and the coverage the
    //     photographer took back is still on the GPU. Decision #108,
    //     research/brush-acceleration.md.
    {
        const auto brushAdj = [&](unsigned revision) {
            orion::pipe::Adjustments a;
            a.wb = develop.asShotWhiteBalance();
            auto& c = a.maskComponents[0];
            c.kind = 3;
            c.brushRadius = 0.02f;
            c.brushFlow = 1.0f;
            c.brushHardness = 0.7f;
            c.brushRevision = revision;
            a.maskCount = 1;
            a.layers[0].exposureEv = 2.0f;
            return a;
        };
        // The `paint` verb's own stroke: 49 dabs a line, each line
        // spanning the frame, one line below the last. Identical
        // geometry to `appendedStroke` further down this file, and it
        // has to be.
        //
        // ⚠ **Decision #98, which this nearly repeated.** The first
        // version of this probe drew lines 0.018 of the frame wide. Dab
        // count and block count grew exactly as they do here, and the
        // cost did not — because what the kernel pays is
        // `Σ over blocks of (pixels in that block's box) × 64`, and a
        // stroke of small extent has small boxes whatever its length.
        // It reported an append at 294 dabs costing 0.5 ms more than a
        // full re-lay, which is a number about the fixture. A dab is
        // spaced by the nib, so a longer stroke is a longer *path*.
        const auto strokeOf = [](int dabs) {
            std::vector<float> xy(std::size_t(dabs) * 2, 0.0f);
            for (int i = 0; i < dabs; ++i) {
                const float t = float(i % 49) / 48.0f;
                xy[std::size_t(i) * 2 + 0] = 0.02f + 0.96f * t;
                xy[std::size_t(i) * 2 + 1] = 0.05f + 0.10f * float(i / 49);
            }
            return xy;
        };

        unsigned rev = 400;
        const auto lay = [&](orion::pipe::DevelopPipeline& d,
                             const std::vector<float>& xy) {
            const int n = int(xy.size() / 2);
            d.setBrushStroke(0, xy.data(), nullptr, n);
            d.apply(brushAdj(++rev));
            return d.render();
        };

        // `mask:0` alone, profiled, because the whole render carries
        // ~14 ms of `develop:linear`, `develop:display` and `geometry`
        // that are identical in both columns and swamp the difference.
        // The cost being measured is one node's.
        const auto maskMs = [&](orion::pipe::DevelopPipeline& d) {
            d.graph().setProfiling(true);
            d.render();
            d.graph().setProfiling(false);
            for (const auto& t : d.graph().lastRun()) {
                if (t.name == "mask:0" && t.executed) return t.ms;
            }
            return 0.0;
        };

        std::printf("\nBrush accumulation (#108)\n");
        std::printf("  %-6s %-26s %-26s %s\n", "held",
                    "mask:0, append 49 (ms)", "mask:0, head moved (ms)",
                    "firstDab");

        bool accumOk = true;
        for (int held : {49, 294}) {
            constexpr int kReps = 8;
            std::vector<double> fast, slow;
            int sawFirstDab = -1, sawRefused = -1;
            // ⚠ Interleaved rep by rep, not two blocks. This machine's
            // GPU swings better than three to one on an identical
            // binary under load, so two blocks a minute apart compare
            // the load average.
            for (int rep = 0; rep < kReps; ++rep) {
                // A. Append. The head is untouched, so the accumulator
                //    holds it and the kernel starts at `held`.
                lay(develop, strokeOf(held));
                const auto grown = strokeOf(held + 49);
                develop.setBrushStroke(0, grown.data(), nullptr, held + 49);
                develop.apply(brushAdj(++rev));
                fast.push_back(maskMs(develop));
                sawFirstDab = develop.brushPrefixStat(0).firstDab;

                // B. The same append with one dab of the head moved —
                //    undo and repaint. Same dab count, same host work,
                //    same blocks, same boxes. The accumulator must be
                //    refused, and the kernel walks all of it.
                lay(develop, strokeOf(held));
                auto moved = strokeOf(held + 49);
                moved[std::size_t(held / 2) * 2 + 1] += 0.03f;
                develop.setBrushStroke(0, moved.data(), nullptr,
                                       held + 49);
                develop.apply(brushAdj(++rev));
                slow.push_back(maskMs(develop));
                sawRefused = develop.brushPrefixStat(0).firstDab;
            }
            const Stats f = summarise(fast);
            const Stats s = summarise(slow);
            const bool ok = sawFirstDab == held && sawRefused == 0;
            if (!ok) { accumOk = false; invariantsPass = false; }
            std::printf("  %-6d %7.2f (p95 %6.2f)        %7.2f (p95 %6.2f)"
                        "        %d then %d  %s\n",
                        held, f.median, f.p95, s.median, s.p95,
                        sawFirstDab, sawRefused,
                        ok ? "ok" : "THE BRUSH RE-LAYS THE WHOLE STROKE");
        }
        if (accumOk) {
            std::printf("  both columns lay %d dabs and do the same host "
                        "work; only the kernel's starting index differs\n",
                        49);
        }

        orion::pipe::Adjustments clear;
        clear.wb = develop.asShotWhiteBalance();
        develop.setBrushStroke(0, nullptr, nullptr, 0);
        develop.apply(clear);
        develop.render();
    }
}

void brushProfiles(Bench& b) {
    auto& develop = b.develop;
    auto* device = &b.device;   // `*device`, as the preview construction reads it
    const auto& img = b.img;

    // ── Where a brush stroke actually goes ────────────────────────
    //
    // ⚠ This exists because the host side had been eliminated as the
    // cause and the slope did not move. Painting costs 0.2 ms an event
    // at 49 dabs and 1.5 at 490 — linear, forever — and on 2026-08-01
    // every host-side O(N) *in the Swift layer* was removed (the
    // per-event re-flatten, the `DevelopState` copy, the autosave
    // compare) with no change to the slope.
    //
    // Profiled at two stroke lengths on purpose: a single length gives
    // a ranking, and a ranking cannot tell a node that is merely
    // expensive from one that is expensive *in the number of dabs*.
    //
    // ⚠ **Two stroke shapes, and the difference between them is the
    // finding.** The first version of this section grew the dab count by
    // *subdividing a stroke of fixed extent* — the same sine wave, more
    // samples along it. Under that shape decision #80's boxes are
    // perfect: sixteen times the dabs is sixteen times the blocks, each
    // box a sixteenth the size, and the product — which is what the
    // kernel actually pays — does not move. It measured flat, and flat
    // was read as "the mask kernel is not the cost".
    //
    // No hand makes that stroke. Painting *appends*: the dab spacing is
    // fixed by the nib, so more dabs is a longer path over more of the
    // picture, and each new block of 64 arrives with a box the same size
    // as the last one. The block count grows and the boxes do not
    // shrink, so the product is linear — which is the slope.
    //
    // `refined` is the old shape, kept because it is the evidence that
    // #80 works. `appended` is what repeated `paint` verbs lay into one
    // component — 49 dabs a line, one line per stroke — which is the
    // scenario that reports 0.2 ms an event at 49 dabs rising to 0.7 at
    // 294. A component accumulates every stroke ever laid on it, so
    // more than one stroke is the ordinary case and not a contrived one.
    //
    // ⚠ And on both graphs, with the host cost beside the GPU one. The
    // paint the app measures runs on the *preview* graph, and a GPU node
    // timer cannot see a host loop at all.
    {
        // Fixed extent, more samples along it. Boxes shrink with the
        // block count; this is the shape that measures flat.
        const auto refinedStroke = [](int dabs) {
            std::vector<float> xy;
            xy.reserve(std::size_t(dabs) * 2);
            for (int i = 0; i < dabs; ++i) {
                const float t = float(i) / float(std::max(dabs - 1, 1));
                xy.push_back(0.05f + 0.90f * t);
                xy.push_back(0.50f + 0.25f * std::sin(t * 6.283f * 3.0f));
            }
            return xy;
        };

        /// `lines` strokes across the frame, 49 dabs each, one every
        /// tenth of the height — what the `paint` verb lays.
        constexpr int kDabsPerLine = 49;
        const auto appendedStroke = [](int lines) {
            std::vector<float> xy;
            xy.reserve(std::size_t(lines) * kDabsPerLine * 2);
            for (int l = 0; l < lines; ++l) {
                for (int i = 0; i < kDabsPerLine; ++i) {
                    const float t = float(i) / float(kDabsPerLine - 1);
                    xy.push_back(0.02f + 0.96f * t);
                    xy.push_back(0.05f + 0.10f * float(l));
                }
            }
            return xy;
        };
        const auto brushAdjustments = [&](int revision) {
            orion::pipe::Adjustments base;
            base.wb = develop.asShotWhiteBalance();
            auto& c = base.maskComponents[0];
            c.kind = 3;
            c.brushRadius = 0.02f;
            c.brushFlow = 1.0f;
            c.brushHardness = 0.7f;
            c.brushRevision = revision;
            base.maskCount = 1;
            base.layers[0].exposureEv = 2.0f;
            return base;
        };

        // The preview graph, built exactly as `Engine::openRaw` builds
        // it — same decimation, same grid step. Allowed to fail for the
        // same reason the engine's is: a machine short of room can still
        // report the full graph's numbers.
        std::unique_ptr<orion::pipe::DevelopPipeline> preview;
        try {
            preview = std::make_unique<orion::pipe::DevelopPipeline>(
                *device, ORION_SHADER_DIR,
                orion::raw::decimate(img, orion::Engine::kPreviewScale));
            preview->setGridStep(float(orion::Engine::kPreviewScale));
        } catch (const std::exception&) {
            preview.reset();
        }

        const auto profileStroke = [&](orion::pipe::DevelopPipeline& d,
                                       const char* label,
                                       const std::vector<float>& xy) {
            const int dabs = int(xy.size() / 2);
            const auto base = brushAdjustments(1);
            const std::vector<float> erase(std::size_t(dabs), 0.0f);

            d.setBrushStroke(0, xy.data(), erase.data(), dabs);
            d.apply(base);
            d.render();

            // Move the revision so the component is re-evaluated, which
            // is what a dab being appended does.
            d.apply(brushAdjustments(2));

            d.graph().setProfiling(true);
            d.render();
            d.graph().setProfiling(false);

            std::vector<std::pair<double, std::string>> ran;
            double sum = 0.0;
            for (const auto& t : d.graph().lastRun()) {
                if (!t.executed) continue;
                ran.emplace_back(t.ms, t.name);
                sum += t.ms;
            }
            std::sort(ran.begin(), ran.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });

            std::printf("\n%s (%d dabs), node by node\n", label, dabs);
            std::printf("  %zu nodes ran, %.2f ms serialized\n", ran.size(), sum);
            for (std::size_t i = 0; i < ran.size() && i < 5; ++i) {
                std::printf("    %-22s %6.2f ms  %4.1f%%\n", ran[i].second.c_str(),
                            ran[i].first, 100.0 * ran[i].first / std::max(sum, 1e-9));
            }
        };
        // Refined: the boxes shrink with the block count, so this pair
        // is flat — decision #80 doing its job.
        profileStroke(develop,  "Brush refined, full graph",  refinedStroke(60));
        profileStroke(develop,  "Brush refined, full graph",  refinedStroke(960));
        // Appended: the boxes keep their size, so this pair is not.
        profileStroke(develop,  "Brush appended, full graph", appendedStroke(1));
        profileStroke(develop,  "Brush appended, full graph", appendedStroke(6));
        if (preview) {
            profileStroke(*preview, "Brush refined, preview graph",
                          refinedStroke(60));
            profileStroke(*preview, "Brush refined, preview graph",
                          refinedStroke(960));
            profileStroke(*preview, "Brush appended, preview graph",
                          appendedStroke(1));
            profileStroke(*preview, "Brush appended, preview graph",
                          appendedStroke(6));
        }

        // ── One appended dab, host and GPU, side by side ───────────
        //
        // What `MaskOverlay` does on a pointer event: hand both
        // pipelines the whole stroke again, `apply` both, render the
        // preview. Timed as three columns so a slope can be attributed
        // rather than guessed at.
        //
        // ⚠ Interleaved between the two stroke lengths, not run in two
        // blocks. This machine's GPU swings better than three to one on
        // an identical binary under load, so two blocks minutes apart
        // compare the load average and not the code.
        if (preview) {
            constexpr int kReps = 24;
            std::vector<double> setMs[2], applyMs[2], renderMs[2];
            std::vector<float> xy[2], erase[2];
            int lengths[2] = {0, 0};
            // The `paint` verb's own stroke lengths, one line and six,
            // so this table and the scenario report the same case.
            for (int k = 0; k < 2; ++k) {
                xy[k] = appendedStroke(k == 0 ? 1 : 6);
                lengths[k] = int(xy[k].size() / 2);
                erase[k].assign(std::size_t(lengths[k]), 0.0f);
            }

            int revision = 8;
            for (int rep = 0; rep < kReps; ++rep) {
                for (int k = 0; k < 2; ++k) {
                    const auto adjs = brushAdjustments(++revision);

                    const auto tSet = Clock::now();
                    develop.setBrushStroke(0, xy[k].data(), erase[k].data(),
                                           lengths[k]);
                    preview->setBrushStroke(0, xy[k].data(), erase[k].data(),
                                            lengths[k]);
                    setMs[k].push_back(msSince(tSet));

                    const auto tApply = Clock::now();
                    develop.apply(adjs);
                    preview->apply(adjs);
                    applyMs[k].push_back(msSince(tApply));

                    renderMs[k].push_back(preview->render());
                }
            }

            std::printf("\nOne pointer event of paint, host and GPU\n");
            std::printf("  %-6s %-22s %-22s %s\n", "dabs",
                        "setBrushStroke x2", "apply x2 (host)",
                        "preview render (GPU)");
            for (int k = 0; k < 2; ++k) {
                const Stats st = summarise(setMs[k]);
                const Stats ap = summarise(applyMs[k]);
                const Stats re = summarise(renderMs[k]);
                std::printf("  %-6d %6.3f ms (p95 %5.3f)  %6.3f ms (p95 %5.3f)  "
                            "%6.2f ms (p95 %5.2f)\n",
                            lengths[k], st.median, st.p95, ap.median, ap.p95,
                            re.median, re.p95);
            }
        }

        // Put the graph back: the sections after this expect no mask.
        orion::pipe::Adjustments clear;
        clear.wb = develop.asShotWhiteBalance();
        develop.setBrushStroke(0, nullptr, nullptr, 0);
        develop.apply(clear);
        develop.render();
    }
}

}  // namespace bench

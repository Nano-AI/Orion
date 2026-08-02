/*  orion-bench — the harmonic highlight fill, and the census beneath it.
 *
 *  Both halves of one feature: the four invariants saying the twenty-four node
 *  chain switches off to nothing and does not re-run for a slider it cannot
 *  read, and the clip-set census saying how much of a real photograph it is
 *  even for. The census gates one thing and measures everything else.
 */
#include "bench.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bench {

void highlightFill(Bench& b) {
    auto& develop = b.develop;
    bool& invariantsPass = b.invariantsPass;
    const int ran = b.cleanNodes;
    orion::pipe::Adjustments base;
    base.wb = develop.asShotWhiteBalance();

    // 3c. The harmonic highlight fill must disable to nothing, and must
    //     not re-run while a slider it does not depend on moves.
    //
    //     Twenty-four nodes hang off `highlightRecovery`, which is off
    //     by default — so the expensive state here is the *on* one, and
    //     both states are asserted. Decisions #82 and #92 are the two
    //     ways this goes wrong and they are different: #82 is a node
    //     left running at zero strength, #92 is a parameter block
    //     re-pushed for a value nothing reads, which dirties everything
    //     downstream whether or not the bytes changed.
    //
    //     ⚠ By name, like the dehaze check above and for its reason: a
    //     total would be satisfied by any twenty-four nodes, and which
    //     ones is the entire question. The third check is the one that
    //     stops the other two being decoration — a chain that never ran
    //     at all would pass both.
    {
        const auto sweep = [&](orion::pipe::Adjustments a, int& worstRan,
                               int& worstFill, std::string& firstFill) {
            worstRan = 0;
            worstFill = 0;
            for (int i = 0; i < 12; ++i) {
                a.exposureEv = -1.5f + 3.0f * static_cast<float>(i) / 11.0f;
                develop.apply(a);
                develop.render();
                int ranHere = 0, fillHere = 0;
                for (const auto& n : develop.graph().lastRun()) {
                    if (!n.executed) continue;
                    ++ranHere;
                    if (n.name.rfind("hl:", 0) != 0) continue;
                    ++fillHere;
                    if (firstFill.empty()) firstFill = n.name;
                }
                worstRan = std::max(worstRan, ranHere);
                worstFill = std::max(worstFill, fillHere);
            }
        };

        const auto fillNodesLastRun = [&] {
            int n = 0;
            for (const auto& t : develop.graph().lastRun()) {
                if (t.executed && t.name.rfind("hl:", 0) == 0) ++n;
            }
            return n;
        };

        // Turning it on must actually cost the chain, once.
        auto on = base;
        on.highlightRecovery = 0.8f;
        develop.apply(on);
        develop.render();
        const int builtFill = fillNodesLastRun();
        const bool builtOk = builtFill >= 20;
        if (!builtOk) invariantsPass = false;
        std::printf("  %-24s %d fill nodes ran  %s\n",
                    "highlights on, once", builtFill,
                    builtOk ? "ok" : "THE CHAIN NEVER RAN");

        // ⚠ **The full render, not the drag, and that distinction is not
        // cosmetic.** This check was written on the drag first, and the
        // mutation that leaves `filling` permanently true passed it: the
        // fill sits upstream of exposure, so once it has run it stays
        // cached and an exposure tick never touches it either way. A
        // chain running on every photograph opened, forever, for nothing.
        // Switching the control off dirties the subgraph, so the render
        // below is where a node that ignores the control shows up.
        auto off = base;
        off.highlightRecovery = 0.0f;
        develop.apply(off);
        develop.render();
        const int offFullFill = fillNodesLastRun();
        const bool offFullOk = offFullFill == 0;
        if (!offFullOk) invariantsPass = false;
        std::printf("  %-24s %d fill nodes ran on a full render  %s\n",
                    "highlights off", offFullFill,
                    offFullOk ? "ok" : "THE FILL RUNS WHEN IT IS OFF");

        int offRan = 0, offFill = 0;
        std::string offName;
        sweep(off, offRan, offFill, offName);

        const bool offOk = offFill == 0 && offRan == ran;
        if (!offOk) invariantsPass = false;
        std::printf("  %-24s %d nodes, %d of them the fill%s  (clean: %d)  %s\n",
                    "exposure drag, fill off", offRan, offFill,
                    offName.empty() ? "" : (" (" + offName + " ...)").c_str(),
                    ran, offOk ? "ok" : "THE FILL RUNS WHEN IT IS OFF");

        develop.apply(on);
        develop.render();

        int onRan = 0, onFill = 0;
        std::string onName;
        sweep(on, onRan, onFill, onName);

        const bool onOk = onFill == 0 && onRan == ran;
        if (!onOk) invariantsPass = false;
        std::printf("  %-24s %d nodes, %d of them the fill%s  (clean: %d)  %s\n",
                    "exposure drag, fill on", onRan, onFill,
                    onName.empty() ? "" : (" (" + onName + " ...)").c_str(),
                    ran, onOk ? "ok" : "AN EXPOSURE TICK REDOES THE PYRAMID");

        develop.apply(base);
        develop.render();
    }
}

void highlightCensus(Bench& b) {
    auto& develop = b.develop;
    bool& invariantsPass = b.invariantsPass;
    orion::pipe::Adjustments base;
    base.wb = develop.asShotWhiteBalance();

    // 3e. The clip-set census — how much of this frame is the region
    //     Rouf et al.'s §3.3 would newly serve.
    //
    //     ⚠ Placed after block 4 rather than beside 3c on purpose:
    //     another block (3d) lands at 3c's anchor in the same hour, and
    //     adjacent patches merge where interleaved ones do not.
    //
    //     ⚠ **This is a measurement, not an invariant.** It gates
    //     nothing. It exists because ROADMAP costed piece 4 at +23
    //     nodes and ~30 MiB without anyone asking how many pixels it
    //     would reach, and piece 3's own note that "a blown lamp's
    //     partial-clip annulus can be a pixel or two wide" was written
    //     from a synthetic fixture rather than from a photograph.
    //
    //     §3.3 transfers detail into a clipped channel from an
    //     unclipped one. Its domain is therefore Omega^union minus
    //     Omega^intersection — a pixel with at least one channel
    //     clipped and at least one still valid. `highlights.slang`
    //     already serves that set by Masood et al.'s per-pixel
    //     cross-channel fit, so what §3.3 would *add* is the part of it
    //     the window fit hands back untouched: no valid samples within
    //     `kRadius`, or a fit it refuses to extrapolate.
    //
    //     So this counts three things on the node's own two sides,
    //     against the ceiling the node itself was given:
    //       - how big Omega^union \ Omega^intersection is,
    //       - how much of it comes back bit-identical, and
    //       - how far its pixels sit from evidence, in Chebyshev
    //         pixels, which is the metric `kRadius`'s square window
    //         bounds.
    {
        auto full = base;
        full.highlightRecovery = 1.0f;
        develop.apply(full);
        develop.render();

        const auto st = develop.highlightStages();
        const std::uint32_t w = develop.width(), h = develop.height();
        const std::size_t n = std::size_t(w) * h;

        std::vector<__fp16> in(n * 4), out(n * 4), got(n * 4);
        st.input->download(in.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        st.output->download(out.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        st.filled->download(got.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);

        const float limit    = st.clip * st.gamma;
        const float shoulder = st.clip * 0.35f;   // highlights.slang's kShoulder

        // 0 = evidence (wholly valid and bright enough to inform a
        // fit), 1 = partial clip, 2 = Omega^intersection, 3 = dark and
        // valid, which is neither.
        std::vector<std::uint8_t> cls(n);
        std::size_t nInter = 0, nPartial = 0, nUntouched = 0;
        double worstMove = 0.0;

        for (std::size_t i = 0; i < n; ++i) {
            const float r = float(in[i * 4 + 0]);
            const float g = float(in[i * 4 + 1]);
            const float b = float(in[i * 4 + 2]);
            const int count = (r >= limit) + (g >= limit) + (b >= limit);
            if (count == 3) { cls[i] = 2; ++nInter; continue; }
            if (count == 0) {
                cls[i] = (std::max(r, std::max(g, b)) >= shoulder) ? 0 : 3;
                continue;
            }
            cls[i] = 1;
            ++nPartial;
            const bool same = float(out[i * 4 + 0]) == r &&
                              float(out[i * 4 + 1]) == g &&
                              float(out[i * 4 + 2]) == b;
            if (same) ++nUntouched;
            else {
                for (int c = 0; c < 3; ++c) {
                    worstMove = std::max(worstMove,
                                         std::abs(double(out[i * 4 + c]) -
                                                  double(in[i * 4 + c])));
                }
            }
        }

        // Chebyshev distance to the nearest pixel that could inform a
        // fit, by the two-pass 8-neighbour chamfer with unit weights.
        //
        //     ⚠ It carries the *identity* of that pixel as well, not
        //     only the distance. The distance says whether the window
        //     fit could see evidence; the identity says what the
        //     evidence would have told it, which is what decides
        //     whether the pixels it declines are visibly wrong or
        //     already nearly right. Rouf et al.'s §3.3 estimate is
        //     f*_k = (rho_k/rho_j)*f_j, so a nearest-evidence rho makes
        //     that arithmetic available without building any of it.
        constexpr std::uint16_t kFar = 4000;
        std::vector<std::uint16_t> dist(n, kFar);
        std::vector<std::uint32_t> feat(n, 0xFFFFFFFFu);
        for (std::size_t i = 0; i < n; ++i) {
            if (cls[i] == 0) { dist[i] = 0; feat[i] = std::uint32_t(i); }
        }
        const auto relax = [&](std::size_t here, std::int64_t x, std::int64_t y) {
            if (x < 0 || y < 0 || x >= std::int64_t(w) || y >= std::int64_t(h)) return;
            const std::size_t there = std::size_t(y) * w + std::size_t(x);
            const int d = std::min<int>(kFar, dist[there] + 1);
            if (d < dist[here]) {
                dist[here] = std::uint16_t(d);
                feat[here] = feat[there];
            }
        };
        for (std::int64_t y = 0; y < std::int64_t(h); ++y) {
            for (std::int64_t x = 0; x < std::int64_t(w); ++x) {
                const std::size_t i = std::size_t(y) * w + std::size_t(x);
                for (std::int64_t k = -1; k <= 1; ++k) relax(i, x + k, y - 1);
                relax(i, x - 1, y);
            }
        }
        for (std::int64_t y = std::int64_t(h) - 1; y >= 0; --y) {
            for (std::int64_t x = std::int64_t(w) - 1; x >= 0; --x) {
                const std::size_t i = std::size_t(y) * w + std::size_t(x);
                for (std::int64_t k = -1; k <= 1; ++k) relax(i, x + k, y + 1);
                relax(i, x + 1, y);
            }
        }

        std::size_t beyond = 0, beyondUntouched = 0, deepest = 0;
        std::size_t interBeyond = 0;
        // What §3.3 would move those pixels by, using the nearest
        // evidence pixel as rho: f*_k = (rho_k/rho_j)*f_j over the
        // clipped channels, with j the pixel's own valid channels.
        double sumMove = 0.0, worstProposed = 0.0;
        std::size_t moved = 0, capped = 0, floored = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (cls[i] == 2) {
                deepest = std::max<std::size_t>(deepest, dist[i]);
                if (dist[i] > 12) ++interBeyond;
                continue;
            }
            if (cls[i] != 1) continue;
            if (dist[i] <= 12) continue;
            ++beyond;
            const bool same = float(out[i * 4 + 0]) == float(in[i * 4 + 0]) &&
                              float(out[i * 4 + 1]) == float(in[i * 4 + 1]) &&
                              float(out[i * 4 + 2]) == float(in[i * 4 + 2]);
            if (same) ++beyondUntouched;

            const std::uint32_t f = feat[i];
            if (f == 0xFFFFFFFFu) continue;
            float c0[3], rho[3];
            for (int k = 0; k < 3; ++k) {
                c0[k]  = float(in[i * 4 + k]);
                rho[k] = float(in[std::size_t(f) * 4 + k]);
            }
            // The unclipped reference: the valid channels' own ratio to
            // rho, averaged, exactly as `highlights.slang` averages its
            // reference rather than picking one channel.
            double scale = 0.0;
            int refs = 0;
            for (int k = 0; k < 3; ++k) {
                if (c0[k] >= limit) continue;
                scale += double(c0[k]) / std::max(double(rho[k]), 1e-6);
                ++refs;
            }
            if (refs == 0) continue;
            scale /= refs;
            double worstHere = 0.0;
            for (int k = 0; k < 3; ++k) {
                if (c0[k] < limit) continue;
                const double proposed = scale * double(rho[k]);
                // Does `hl_apply.slang`'s kMaxGain ceiling ever bind?
                // An unreachable clamp reads as a guard somebody is
                // relying on — `hl_pull.slang` lost one for that.
                if (proposed > 2.0 * double(st.clip)) ++capped;
                // And the floor beneath it: a clipped channel read at
                // least its own level, so an estimate below that
                // contradicts the measurement.
                if (proposed < double(c0[k])) ++floored;
                worstHere = std::max(worstHere,
                                     std::abs(proposed - double(c0[k])));
            }
            sumMove += worstHere;
            worstProposed = std::max(worstProposed, worstHere);
            ++moved;
        }

        // ⚠ **The one assertion in this block, and it is exact.** Piece
        //   2's rule is that where Masood et al.'s measurement and this
        //   interpolant are both available, the measurement wins. On a
        //   photograph that means: every pixel with no channel still at
        //   the ceiling must leave `hl:fill` bit-identical to how it
        //   left `highlights` — the unclipped ones and, more to the
        //   point, the ones the window fit recovered.
        //
        //   ⚠ It exists because the fixture in `orion-tests` cannot see
        //   this. There the recovered ring is uniform, so rho equals the
        //   picture over it and §3.3's ratio is the identity whatever
        //   predicate the apply pass uses. A photograph has texture, rho
        //   is a quarter-resolution box average of it, and the identity
        //   stops holding — which is what makes the predicate load
        //   bearing rather than decorative.
        std::size_t trespass = 0;
        for (std::size_t i = 0; i < n; ++i) {
            bool stillClipped = false;
            for (int k = 0; k < 3; ++k) {
                if (float(in[i * 4 + k]) >= limit &&
                    !(float(out[i * 4 + k]) > float(in[i * 4 + k]))) {
                    stillClipped = true;
                }
            }
            if (stillClipped) continue;
            for (int k = 0; k < 3; ++k) {
                if (got[i * 4 + k] != out[i * 4 + k]) { ++trespass; break; }
            }
        }
        if (trespass != 0) invariantsPass = false;

        // ⚠ And the question that decides whether §3.3 is decoration or
        //   a correction to piece 3: the pixels that set rho for a
        //   blown core are the *innermost* ring of known ones, which is
        //   the annulus. If that ring is mostly beyond the window fit's
        //   reach then piece 3 is solving from a boundary condition
        //   that is itself un-recovered, and every core in the frame
        //   inherits the error measured above.
        std::size_t rimAll = 0, rimBeyond = 0;
        double rimSumMove = 0.0;
        for (std::int64_t y = 0; y < std::int64_t(h); ++y) {
            for (std::int64_t x = 0; x < std::int64_t(w); ++x) {
                const std::size_t i = std::size_t(y) * w + std::size_t(x);
                if (cls[i] != 1) continue;
                bool touchesCore = false;
                for (std::int64_t dy = -1; dy <= 1 && !touchesCore; ++dy) {
                    for (std::int64_t dx = -1; dx <= 1; ++dx) {
                        const std::int64_t qx = x + dx, qy = y + dy;
                        if (qx < 0 || qy < 0 || qx >= std::int64_t(w) ||
                            qy >= std::int64_t(h)) continue;
                        if (cls[std::size_t(qy) * w + std::size_t(qx)] == 2) {
                            touchesCore = true;
                            break;
                        }
                    }
                }
                if (!touchesCore) continue;
                ++rimAll;
                if (dist[i] > 12) ++rimBeyond;
                const bool same = float(out[i * 4 + 0]) == float(in[i * 4 + 0]) &&
                                  float(out[i * 4 + 1]) == float(in[i * 4 + 1]) &&
                                  float(out[i * 4 + 2]) == float(in[i * 4 + 2]);
                if (same) rimSumMove += 1.0;
            }
        }

        const double pct = 100.0 / double(n);
        std::printf("\nHighlight clip-set census (block 3e)  clip %.4f, "
                    "limit %.4f, %ux%u\n", st.clip, limit, w, h);
        std::printf("  %-34s %10zu  %7.4f%%\n",
                    "Omega^inter (every channel)", nInter, double(nInter) * pct);
        std::printf("  %-34s %10zu  %7.4f%%\n",
                    "Omega^union \\ Omega^inter", nPartial, double(nPartial) * pct);
        std::printf("  %-34s %10zu  %7.4f%% of the frame\n",
                    "  ...returned untouched", nUntouched, double(nUntouched) * pct);
        std::printf("  %-34s %10zu  %7.4f%% of the frame\n",
                    "  ...and beyond 12 px of evidence", beyond, double(beyond) * pct);
        std::printf("  %-34s %10zu\n",
                    "     of those, untouched", beyondUntouched);
        std::printf("  %-34s %10zu  (deepest blown core %zu px)\n",
                    "Omega^inter beyond 12 px", interBeyond, deepest);
        std::printf("  worst move the window fit made in the annulus: %.5f\n",
                    worstMove);
        std::printf("  what §3.3 would move the beyond-12px set by, "
                    "against clip %.4f: mean %.5f, worst %.5f (%zu px)\n",
                    st.clip, moved ? sumMove / double(moved) : 0.0,
                    worstProposed, moved);
        std::printf("  ...of which kMaxGain would cap: %zu channel(s), "
                    "and the 'may only raise it' floor: %zu\n", capped, floored);
        std::printf("  %-34s %10zu  %s\n",
                    "pixels the fill touched but must not", trespass,
                    trespass == 0 ? "ok" : "THE FILL OVERWROTE THE WINDOW FIT");
        std::printf("  the ring that sets rho for every core: %zu px, "
                    "%zu beyond 12 px (%.1f%%), %zu untouched (%.1f%%)\n",
                    rimAll, rimBeyond,
                    rimAll ? 100.0 * double(rimBeyond) / double(rimAll) : 0.0,
                    std::size_t(rimSumMove),
                    rimAll ? 100.0 * rimSumMove / double(rimAll) : 0.0);

        develop.apply(base);
        develop.render();
    }
}

}  // namespace bench

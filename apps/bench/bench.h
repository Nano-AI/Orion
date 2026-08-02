/*  orion-bench — the context every section is handed, and the instruments.
 *
 *  The bench was one 2,289-line `main`. It is now one file per section of the
 *  report it prints, and the rule for where a check goes is: **with the feature
 *  it pins, if that feature has a file** — the highlight fill and the brush do
 *  — and in `bench_invariants.cpp` otherwise. Adding a probe for a new control
 *  is one edit in `bench_controls.cpp`; adding an invalidation invariant for a
 *  new node is one edit in `bench_invariants.cpp`. Decision #118.
 *
 *  ⚠ The sections are not independent and must be called in the order
 *  `main.cpp` calls them. Each leaves the pipeline in the state the next
 *  expects, `controlProbes` loads the creative LUT that `composed` measures,
 *  and `exposureGate` measures the clean node count three later sections
 *  assert against. That is what `Bench::cleanNodes` carries.
 */
#pragma once

#include "gpu/MetalDevice.h"
#include "pipe/DevelopPipeline.h"
#include "raw/RawImage.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace bench {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t);

struct Stats { double min, median, p95, mean; };
Stats summarise(std::vector<double> v);

/// Frames in a drag sweep. The exposure gate and the curve drag both run one.
constexpr int kIterations = 60;

/// The developed output as 16-bit unsigned; see the definition for why it has
/// to be format-aware.
std::vector<std::uint16_t> output16(const orion::pipe::DevelopPipeline& d,
                                    std::uint32_t w, std::uint32_t h);

/// Mean absolute per-channel change between two renders, in 0..1 display units.
/// The one measure here that cannot cancel, which is why the control gate
/// reads it rather than the summary metric beside it.
double meanAbsDiff(const std::vector<std::uint16_t>& a,
                   const std::vector<std::uint16_t>& b);

void writeOut(const orion::pipe::DevelopPipeline& d, const std::string& path);

/// What a control is expected to move.
///
/// Saturation and vibrance preserve luminance *by construction* — the mean of
/// y + (c - y)*a weighted by the luminance weights is exactly y — so measuring
/// mean luma reported them as doing nothing, every run. A benchmark that cries
/// wolf trains you to ignore it, which is worse than not having one.
/// Detail is neighbor-to-neighbor luma difference. Sharpening adds as much
/// as it subtracts around an edge, so it barely moves a mean by construction —
/// on `_PIC8220.ARW` a full-strength unsharp mask moved mean luma by -0.0005,
/// under the noise floor of every other probe. Measuring an edge filter by the
/// image's average brightness is the same mistake as asserting that a slider
/// changed *something*: the instrument cannot see the thing it is pointed at.
/// Saturation is chroma *relative* to the pixel — `(max - min) / max`, the same
/// measure `--measure` reports. It is the right instrument for the grading
/// wheels, whose offsets scale with luminance on purpose: an absolute chroma
/// metric on a night frame reports a scale-invariant grade as almost nothing,
/// because near-black pixels have small absolute deviations however hard you
/// tint them. Measuring a relative control with an absolute ruler is the same
/// mistake as measuring an edge filter by mean brightness.
enum class Metric { Luma, Chroma, Detail, Saturation };

/// Mean luma, or mean distance from gray, for asserting that an adjustment
/// actually did something rather than silently no-op'ing.
double meanOf(const orion::pipe::DevelopPipeline& d, Metric metric);

/// What a section is handed, and what it writes back.
///
/// ⚠ `cleanNodes` is the only value that crosses a section boundary: how many
/// nodes an exposure tick recomputes with every other control at rest. The
/// exposure gate measures it and three invariants further down are stated
/// against it, so deriving it a second time — the obvious way to make the
/// sections independent — would turn those into checks comparing a number
/// against itself.
struct Bench {
    orion::gpu::Device& device;
    const orion::raw::BayerImage& img;
    orion::pipe::DevelopPipeline& develop;
    std::string path;      ///< the raw file; the export block reads its metadata
    std::string prefix;    ///< where the PNGs and the exported files go

    int cleanNodes = 0;

    // Every check below can fail the run. They could not before: the
    // thirteen control probes printed NO EFFECT and the exit code ignored
    // them, so a silently dead slider still exited 0.
    ///
    /// ⚠ Four flags rather than one, so a red run says which family went. The
    /// exit code is their conjunction and nothing else.
    bool m0Pass = true;
    bool curveWorks = true;
    bool controlsPass = true;
    bool invariantsPass = true;

    bool green() const {
        return m0Pass && curveWorks && controlsPass && invariantsPass;
    }
};

/// The sections, in the order `main` runs them. That order is the report's
/// printed order and is load-bearing; see the note at the top of this file.
void exposureGate(Bench& b);        ///< bench_gate.cpp — the M0 gate and the wide tail
void controlProbes(Bench& b);       ///< bench_controls.cpp — every control moves the picture
void invariants(Bench& b);          ///< bench_invariants.cpp — the guide chain, lens, dehaze, Balance
void highlightFill(Bench& b);       ///< bench_highlights.cpp — the fill switches off to nothing
void brushAccumulation(Bench& b);   ///< bench_brush.cpp — an append costs the appended dabs
void pathAgreement(Bench& b);       ///< bench_invariants.cpp — screen and export agree
void highlightCensus(Bench& b);     ///< bench_highlights.cpp — the clip-set census
void toneCurve(Bench& b);           ///< bench_gate.cpp — the curve drag, and it moved the image
void composed(Bench& b);            ///< bench_compose.cpp — the M3 features together
void autoEnhance(Bench& b);         ///< bench_compose.cpp — it converged
void nodeProfiles(Bench& b);        ///< bench_profile.cpp — where a clarity or dehaze drag goes
void brushProfiles(Bench& b);       ///< bench_brush.cpp — where a brush stroke goes
void exportTiming(Bench& b);        ///< bench_profile.cpp — what a write costs

}  // namespace bench

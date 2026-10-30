# Photo Stacking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stack N frames of one scene into one linear DNG — noise stack, star stack, landscape (sky on stars, ground unaligned) — with outlier rejection, optional calibration frames, a manual sheet and an automatic suggestion in the Library.

**Architecture:** A new engine folder `engine/src/stack/` holds pure maths (rejection, star registration, integration, calibration, sky split) and one orchestrator `Stacker` that sequences decode → calibrate → demosaic (`merge::MergeRender`) → register (`merge::align` or stars) → integrate (CPU, threaded) → `util::writeDngLinear`, exactly as `merge::HdrMerge` does. One facade call (`orion_engine_stack` + progress + cancel). The shell gets a pure grouping function, a sheet modelled on `HdrMergePanel`, a `--stack` command-line mode, and a suggestion bar in the Library.

**Tech Stack:** C++20, OpenCV (already linked: `connectedComponentsWithStats`, `GaussianBlur`, `estimateAffinePartial2D`), LibRaw, Avalonia 12 / .NET 10.

**Spec:** `/private/tmp/claude-501/-Users-grootbeat-Documents-Orion/4305e247-5c1f-404b-b41a-6abf23404531/scratchpad/stacking-brief.md` (read it before your task). Decision row: **#391**.

## Global Constraints

- Every non-trivial filter cites a published reference in `research/stacking.md` (Task 1). Constants that are ours go in `research/UNSOURCED.md`. **Never read or copy GPL code** (Siril, darktable, RawTherapee, HDRMerge, the `./hdrmerge/` folder if present). Astroalign (MIT) may be read; implement from the paper.
- Citation is not patent clearance: the sky/ground segmentation patent flag goes in `research/stacking.md` and row #391.
- No file over ~300 lines (C#) / ~400 lines (C++); the developer must be able to hand-edit any file. No new dependencies.
- No C++ exception may escape `engine/src/CApi.cpp` (use its `guard`).
- Heavy work (cmake builds, dotnet builds, `orion-tests`, `--selftest`, `--screenshot`, gates) **only** through `/private/tmp/claude-501/-Users-grootbeat-Documents-Orion/4305e247-5c1f-404b-b41a-6abf23404531/scratchpad/heavy.sh <command...>`, with the Bash sandbox off. Engine build: `heavy.sh /private/tmp/claude-501/-Users-grootbeat-Documents-Orion/4305e247-5c1f-404b-b41a-6abf23404531/scratchpad/stack-build.sh [targets]` (default target `orion-tests`; log in the scratchpad `stack-build.log`). Never open windows, apps or `open`.
- Run one test function: `./build/apps/tests/orion-tests` runs all; filter by reading its output for your `section(...)` names (there is no filter flag — check `apps/tests/main.cpp`).
- `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec`. Shell build: `shell/native/build.sh` then `dotnet build -c Release shell`. Samples: `ORION_SAMPLES=/Users/grootbeat/Documents/Orion/.claude/worktrees/agent-afc33b0f239ec8270/samples` (three Sony ARWs).
- Commit on `feat/stacking` only. End every commit message with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_018f6vEq4BJNKdyU4cfZxrYv
  ```
- Values pinned by this plan: ≥5 frames for a suggestion; cadence tolerance ±2 s on (gap − shutter); astro = shutter ≥ 5 s and ISO ≥ 800 (and a dark thumbnail when checked); auto rejection n<3 none, 3–6 percentile, 7–9 sigma, 10–20 winsorized, >20 linear fit; winsorization c = 1.5 with consistency factor 1.134 (Huber & Ronchetti); clip at 3σ low/high for sigma, winsorized and linear fit; percentile 0.2 low / 0.1 high of the median; output name `<reference stem>-stack.dng`, then `-stack-2`, …

## Review Focus

1. **A frame that will not register** (clouds, a star frame with < 6 matched stars, a blank frame): it must be left out of the stack with a log line, not warped by garbage; if fewer than 2 frames remain the run fails with a sentence. Test in Task 6.
2. **Frames of different sizes or cameras** in a manual selection: refused with a sentence before any decode cost. Test in Tasks 6 and 9.
3. **Memory at 24 MP × 20 frames**: frames are held as half floats (≈144 MB each), never as float. Test: Task 6's bench prints peak resident size.
4. **A portrait (flip 6) landscape stack**: the upright matte from the shell must land on the sensor frame correctly turned. Test in Task 5.
5. **An existing output path / cancel mid-run**: refusal, and no partial file. Test in Task 6.

---

### Task 1: Research file, ledger rows, declared gaps

**Files:**
- Create: `research/stacking.md`
- Modify: `research/UNSOURCED.md` (append rows), `research/README.md` (index line), `planning/DECISIONS.md` (gaps line: drop 386, add 389 and 390 "taken by branches open on 2026-10-30"; append row #391)

- [ ] **Step 1:** Write `research/stacking.md` in the house style of `research/hdr-merge.md`: a sources table with tags and a "which one supports what" table. Sources (from the brief): Siril stacking docs [SIRIL] (docs only, GPL code never read), Huber & Ronchetti 2009 [HR09], Rosner 1983 [R83] (cited as the alternative not built), PixInsight ImageIntegration docs [PI] (linear-fit clipping, winsorized defaults), Groth 1986 [G86], Valdes et al. 1995 [V95], Beroiz, Cabral & Sanchez 2020 [BCS20], Fischler & Bolles 1981 [FB81], Bertin & Arnouts 1996 "SExtractor", A&AS 117, 393 [BA96] (background + threshold + connected pixels + centroid), Stetson 1987 "DAOPHOT", PASP 99, 191 [S87] (smoothing before threshold), Grossberg & Nayar 2003 [GN03] (linear scale ratio), Lightroom Auto-Stack by Capture Time [LR], Shen & Wang 2013 via `research/sky-detection.md` for the split seed. Sections: pipeline; each rejection method with its formula; normalization (additive location = median per channel; scale = median of (v−L)/(v_ref−L_ref) over pixels ≥10σ above the reference background, the [GN03] ratio); noise weight 1/σ'² with σ from pixel differences; star detection; triangle invariants (L2/L1, L1/L0 over each star's 4 nearest neighbours, match radius 0.1, [BCS20]); similarity by RANSAC (`cv::estimateAffinePartial2D`, threshold 2 px, ≥6 inliers); landscape split; calibration on the CFA mosaic before demosaic (master dark = per-pixel median; master flat normalized per CFA channel); grouping rule with its tolerance. A "⚠ Patents" section: registration + averaging is decades-old prior art; automatic sky/ground segmentation for night photos is the kind of thing phone makers patent, nothing found either way, the developer said proceed; Orion reuses its existing classical sky detector as a seed and the user paints the split.
- [ ] **Step 2:** Append to `research/UNSOURCED.md`: sigma / linear-fit 3σ, percentile 0.2/0.1, ≥10σ scale-sample threshold, star threshold 5σ and blob area 2–400 px, 60 brightest stars, feather 1% of the long edge, cadence ±2 s, astro thresholds 5 s / ISO 800.
- [ ] **Step 3:** Row #391 in `planning/DECISIONS.md` (one row, newest last, same column shape as #388): title "Stacking: noise, star and landscape stacks to a linear DNG; CPU integration with automatic rejection; calibration on the mosaic; the Library suggests a stack" — reason column: what the brief asked, the rulings in this plan (CPU over GPU and why, half-float frames, flat facade function, upright matte un-turned by the engine, split seeded by `SkyDetector` and painted in the sheet, `--stack` mode), the patent flag. Task 12 completes it with measured numbers.
- [ ] **Step 4:** `./tools/check-decisions.py` → exit 0.
- [ ] **Step 5:** Commit `research: stacking sources, unsourced constants and decision #391`.

### Task 2: Rejection maths (TDD)

**Files:**
- Create: `engine/src/stack/Rejection.h`, `engine/src/stack/Rejection.cpp`, `apps/tests/tests_stack_rejection.cpp`
- Modify: `engine/CMakeLists.txt` (sources), `apps/tests/CMakeLists.txt`, `apps/tests/harness.h`, `apps/tests/main.cpp`

**Interfaces — Produces:**
```cpp
namespace orion::stack {
enum class Rejection { Auto = -1, None = 0, Percentile = 1, Sigma = 2, Winsorized = 3, LinearFit = 4 };
[[nodiscard]] Rejection autoRejection(int frames) noexcept;
/// Weighted mean of the values that survive `r`. Reorders v and w together in place.
/// n >= 1; w all > 0. Rejection stops before fewer than 3 values would survive.
[[nodiscard]] float combine(float* v, float* w, int n, Rejection r) noexcept;
}
```

- [ ] **Step 1: Failing tests** `testStackRejection()` in `tests_stack_rejection.cpp`:
  - `autoRejection`: 2→None, 3→Percentile, 6→Percentile, 7→Sigma, 9→Sigma, 10→Winsorized, 20→Winsorized, 21→LinearFit.
  - None: `{1,2,3,4}` weights `{1,1,1,1}` → 2.5; weights `{1,1,1,5}` → 3.25.
  - For each of Percentile (n=5), Sigma (n=8), Winsorized (n=16), LinearFit (n=24): values = 1.0 + seeded Gaussian noise σ=0.01, one value set to 10.0 (satellite) → result within 0.01 of 1.0; and one set to −5.0 (dead) → within 0.01 of 1.0.
  - No outliers (n=16, σ=0.01): each method's result within 0.005 of the plain mean.
- [ ] **Step 2:** Build and run; expect link/compile failure.
- [ ] **Step 3: Implement.** Percentile: m = median; reject v with (m−v) > 0.2·|m| or (v−m) > 0.1·|m|, one pass. Sigma: iterate {m = median, σ = std of survivors; reject outside [m−3σ, m+3σ]} until none rejected. Winsorized: outer loop as Sigma, but σ from Huber's winsorization: start σ = std; repeat {clamp survivors to m ± 1.5σ; σ' = 1.134·std(clamped)} until |σ'−σ| < 5e-4·σ; reject outside m ± 3σ'. LinearFit: sort survivors, least-squares line of value on rank, σ = mean |residual|; reject residual < −3σ or > 3σ; iterate. All stop when survivors would drop below 3. Every pass uses the original weights of the survivors.
- [ ] **Step 4:** Tests pass.
- [ ] **Step 5:** Commit `engine: stack rejection — percentile, sigma, winsorized, linear fit (#391)`.

### Task 3: Star detection and triangle registration (TDD)

**Files:**
- Create: `engine/src/stack/StarAlign.h`, `engine/src/stack/StarAlign.cpp`, `apps/tests/tests_stack_stars.cpp`
- Modify: `engine/CMakeLists.txt`, tests CMake/harness/main; the comment at the top of `engine/src/merge/Align.cpp` ("one of only two" TUs including OpenCV → three, naming StarAlign.cpp)

**Interfaces — Consumes:** `merge::AlignResult` (`engine/src/merge/Align.h`), `persp::Matrix3`/`persp::apply` (`pipe/Perspective.h`).
**Produces:**
```cpp
namespace orion::stack {
struct Star { float x = 0, y = 0, flux = 0; };
/// Packed float RGB (luma = mean of the three). mask: optional w*h, a star counts only where mask >= 0.5.
/// Brightest first, at most maxStars.
[[nodiscard]] std::vector<Star> detectStars(const float* rgb, std::uint32_t width, std::uint32_t height,
                                            const float* mask = nullptr, int maxStars = 60);
/// Similarity mapping reference star coordinates to frame coordinates (refToSource). ok=false, identity, below 6 inliers.
[[nodiscard]] merge::AlignResult alignStars(const std::vector<Star>& frame, const std::vector<Star>& reference);
}
```

- [ ] **Step 1: Failing tests** in `tests_stack_stars.cpp` (shared fixture helper `starField(seed, n, w, h, transform, noiseSigma, background)` rendering n Gaussian PSFs σ=1.2 px, flux 0.05–0.8, at positions mapped by a persp::Matrix3; put it in a small `apps/tests/stack_fixture.h` so Tasks 4–6 reuse it):
  - `testStarDetect`: 640×480, 80 stars, background 0.02, noise σ 0.003 → at least 55 of the 60 detected match a true star within 0.15 px; a pure-noise frame returns < 3 stars; with a mask covering the bottom half only stars in the top half are returned.
  - `testStarAlignRecovers`: reference vs frame related by rotation 1.7° about (900, −300), translation (12.5, −7.25) → `ok`, and the four image corners mapped by the result are within **0.3 px** of the truth. Print the measured max corner error.
  - `testStarAlignRefuses`: frame with 4 stars, or stars unrelated to the reference → `ok == false`, identity.
- [ ] **Step 2:** Run; fail.
- [ ] **Step 3: Implement.** Detection [BA96][S87]: luma → `cv::GaussianBlur` σ 1; background L = median, σ = 1.4826·MAD on a ~50k-sample grid; binary = smoothed > L + 5σ (and mask); `cv::connectedComponentsWithStats` (8-connected); keep area 2–400; centroid = flux-weighted mean of (luma−L) over the component's pixels on the unsmoothed luma; flux = that sum; sort by flux, cut at maxStars. Matching [BCS20]: for each star (≤ maxStars), its 4 nearest neighbours → C(5,3) triangles, dedupe; invariant (L2/L1, L1/L0) with sides sorted L2≥L1≥L0; vertices ordered by the side they are opposite (shortest first); match each reference triangle to its nearest frame invariant (brute force) when the Euclidean distance < 0.1; all vertex pairs of matched triangles → `cv::estimateAffinePartial2D(refPts, framePts, inliers, cv::RANSAC, 2.0)`; fewer than 6 inliers → refuse; convert the 2×3 to `persp::Matrix3` (row 3 = 0 0 1).
- [ ] **Step 4:** Pass.
- [ ] **Step 5:** Commit `engine: star detection and triangle-matched similarity registration (#391)`.

### Task 4: Integration with normalization, weights and the sky/ground blend (TDD)

**Files:**
- Create: `engine/src/stack/Integrate.h`, `engine/src/stack/Integrate.cpp`, `apps/tests/tests_stack_integrate.cpp`
- Modify: CMake/harness/main

**Interfaces — Consumes:** `combine`, `autoRejection` (Task 2); `util::halfToFloat`/`floatToHalf` (`util/Half.h`).
**Produces:**
```cpp
namespace orion::stack {
struct Frame {
    std::uint32_t width = 0, height = 0;
    const std::uint16_t* rgb = nullptr;   // half-float RGB, packed, own-normalized (1.0 = clip)
    persp::Matrix3 refToSource;            // the sky's (or the whole frame's) warp; identity for the reference
    persp::Matrix3 groundToSource;         // used only where a sky matte is given and < 1
};
struct Options { Rejection rejection = Rejection::Auto; bool weightByNoise = false; };
struct Result { std::vector<float> rgb; Rejection used = Rejection::None; };
/// skyMatte: nullptr, or reference width*height in 0..1 (1 = sky, frame coordinates).
/// Throws std::runtime_error on an empty stack, a bad reference, or a size mismatch.
[[nodiscard]] Result integrate(const std::vector<Frame>& frames, int reference,
                               const float* skyMatte, const Options& options);
}
```

- [ ] **Step 1: Failing tests** in `tests_stack_integrate.cpp`, using `stack_fixture.h`:
  - `testStackNoiseFallsSqrtN`: 16 frames 320×240, flat background 0.1 + noise σ 0.01, integer-pixel translations only (bilinear taps would smooth noise and flatter the ratio — say so in a comment), Auto rejection → measured σ(single)/σ(stack) over the overlap within **[0.85, 1.15]·√16**. Print the ratio.
  - `testStackRejectsStreak`: 10 star-field frames; frame 3 has a 2-px wide line of value 0.6 across the image → in the stack, every streak pixel is within 4σ_stack of the 9-frame clean mean.
  - `testStackRemovesHotPixel`: 12 frames with a hot pixel (value 1.0) at the same sensor (x,y) in every frame, frames translated by distinct integer shifts with matching `refToSource` → stack at each hot-pixel landing position within 4σ_stack of background.
  - `testStackNormalizesAndWeights`: frame B = 0.8·A + 0.05 (transparency + glow) → integrate {A, B} with None rejection equals A within 1% in the bright pixels; with weightByNoise, a frame with 3× the noise contributes less: stack σ lower than with weightByNoise=false.
  - `testStackSkyGroundBlend`: matte = top half 1, bottom 0; sky `refToSource` = translation (+3, 0), ground identity; frames carry a sharp vertical edge in the ground and one star in the sky that moves +3 px per frame → ground edge contrast ≥ 90% of a single frame's, star peak ≥ 70% of a single frame's.
- [ ] **Step 2:** Run; fail.
- [ ] **Step 3: Implement.** Per frame, before the pixel loop: location L_i,c = median per channel on a ~50k grid tapped through `refToSource`; scale k_i = median over grid pixels where the reference luma ≥ L_ref + 10σ_ref of (v_i−L_i)/(v_ref−L_ref), 1 when < 200 such pixels; normalized v' = (v−L_i)/k_i + L_ref. Noise σ_i = 1.4826·MAD of horizontal neighbour differences / √2; weight = (k_i/σ_i)² when `weightByNoise`, else 1. Pixel loop over rows split across `std::thread::hardware_concurrency()` threads: matte m (1 when null); if m > 0 combine the sky taps (bilinear through `refToSource`, a tap outside the frame contributes nothing), if m < 1 combine the ground taps (through `groundToSource`); out = m·sky + (1−m)·ground per channel; clamp ≥ 0. Rejection resolved once from `frames.size()` when Auto, reported in `Result::used`.
- [ ] **Step 4:** Pass.
- [ ] **Step 5:** Commit `engine: stack integration — normalization, noise weights, sky/ground blend (#391)`.

### Task 5: Shared DNG preview, and the sky split onto the sensor frame (TDD)

**Files:**
- Create: `engine/src/merge/Preview.h`, `engine/src/merge/Preview.cpp` (moved out of `HdrMerge.cpp`'s anonymous namespace, `invert3` with it), `engine/src/stack/SkySplit.h`, `engine/src/stack/SkySplit.cpp`, `apps/tests/tests_stack_split.cpp`
- Modify: `engine/src/merge/HdrMerge.cpp` (call the moved function; behaviour unchanged), CMake/harness/main

**Interfaces — Produces:**
```cpp
namespace orion::merge {
struct Preview { std::vector<std::uint8_t> jpeg; std::uint32_t width = 0, height = 0; };
/// rgb: packed linear camera RGB; gainEv: the BaselineExposure the reader applies.
[[nodiscard]] Preview buildPreview(const std::vector<float>& rgb, std::uint32_t width, std::uint32_t height,
                                   float gainEv, const std::array<float, 4>& camMul,
                                   const std::array<float, 9>& camToXyz, int flip);
}
namespace orion::stack {
/// An upright matte (as displayed: the EXIF turn applied) to the sensor frame: undo quarterTurnsFor(flip),
/// bilinear-resample to width x height, feather with a box blur of radius 1% of the long edge (two passes).
[[nodiscard]] std::vector<float> skyMatteToFrame(const float* upright, int mw, int mh,
                                                 std::uint32_t width, std::uint32_t height, int flip);
}
```

- [ ] **Step 1: Failing tests** `testSkySplitOrientation`: an upright 40×60 matte (portrait, top third = 1) with flip 6 onto a 600×400 sensor frame → the sky lands on the sensor side that `raw::quarterTurnsFor(6)` turns to the top (assert the sensor column/row means on both sides: >0.95 vs <0.05 away from the seam); flip 0 is a plain resample; the seam is feathered (a value strictly between 0.1 and 0.9 exists within 1% of the long edge of the seam, none farther than 3%). Existing `testHdrMergeFacade` still passes (preview unchanged).
- [ ] **Step 2:** Run; fail.
- [ ] **Step 3:** Implement; `HdrMerge.cpp` keeps only orchestration.
- [ ] **Step 4:** Pass, including all existing merge tests.
- [ ] **Step 5:** Commit `engine: the DNG preview shared, and the sky split onto the sensor frame (#391)`.

### Task 6: The Stacker orchestrator and the facade

**Files:**
- Create: `engine/src/stack/Stacker.h`, `engine/src/stack/Stacker.cpp`, `apps/tests/tests_stack_facade.cpp`
- Modify: `engine/src/Engine.h`/`.cpp` (an eager `std::unique_ptr<stack::Stacker> stacker_` beside `hdrMerge_`, and three forwarding methods), `engine/src/CApi.cpp`, `engine/include/orion/orion.h` (after the HDR merge block), CMake/harness/main

**Interfaces — Consumes:** Tasks 2–5; `merge::MergeRender`, `merge::align`, `raw::decode`, `raw::readInfo`, `util::writeDngLinear`.
**Produces (facade, C):**
```c
/* Stacks two or more frames of one scene into a linear DNG at output_path (must not exist).
 * mode: 0 noise (homography alignment, as HDR merge), 1 stars (sky-only, triangle matching),
 * 2 landscape (sky on stars, ground unaligned, blended over sky_matte).
 * rejection: -1 auto, 0 none, 1 percentile, 2 sigma, 3 winsorized, 4 linear fit.
 * darks/flats: optional calibration frames (NULL/0 for none).
 * sky_matte: landscape only, matte_width*matte_height floats 0..1, 1 = sky, UPRIGHT (as displayed);
 *            the engine undoes the reference's EXIF turn. Required for mode 2.
 * Blocking; progress and cancel from any thread, as orion_engine_hdr_merge. */
OrionStatus orion_engine_stack(OrionEngine* engine, const char* const* paths, int32_t count,
                               int32_t reference_index, int32_t mode, int32_t rejection,
                               int32_t weight_by_noise,
                               const char* const* darks, int32_t dark_count,
                               const char* const* flats, int32_t flat_count,
                               const float* sky_matte, int32_t matte_width, int32_t matte_height,
                               const char* output_path);
OrionStatus orion_engine_stack_progress(OrionEngine* engine, float* out);
OrionStatus orion_engine_stack_cancel(OrionEngine* engine);
```
**Produces (C++):**
```cpp
namespace orion::stack {
enum class Mode { Noise = 0, Stars = 1, Landscape = 2 };
struct Request {
    std::vector<std::string> paths; int reference = 0; Mode mode = Mode::Noise;
    Options options; std::vector<std::string> darks, flats;
    std::vector<float> skyMatte; int matteWidth = 0, matteHeight = 0;
    std::string output;
};
class Stacker {
public:
    Stacker(gpu::Device& device, std::string shaderDir);
    void run(const Request& request);          // throws std::runtime_error with a sentence
    [[nodiscard]] float progress() const noexcept;
    void cancel() noexcept;
    /// Frames left out of the last run because they did not register (for the log and the tests).
    [[nodiscard]] int skipped() const noexcept;
};
}
```

- [ ] **Step 1: Failing tests** in `tests_stack_facade.cpp` (frames written as linear DNGs with `util::writeDngLinear`, as `tests_hdr_merge.cpp` does; files under `/tmp/orion-stack-*`):
  - `testStackFacadeNoise`: 8 frames 480×360 of a textured scene (`tests_hdr_merge.cpp`'s tile radiance at 0.3 scale) + noise σ 0.01, small translations → `orion_engine_stack(..., mode 0, rejection -1, ...) == ORION_OK`; reopened with `raw::decode` → `LinearImage` of the same size; flat-tile σ ratio vs one frame ≥ 0.8·√8. Print it.
  - `testStackFacadeStars`: 8 star-field frames rotated 0.2° per frame about an off-frame point + translation → mode 1 → reopened stack: stars detected in it match the reference frame's within 0.5 px (median), ≥ 40 matches. Print the median registration error.
  - `testStackFacadeLandscape`: as Task 4's blend test but through files, with an upright matte passed via the facade (flip 0) → ground edge ≥ 90%, star peak ≥ 70%.
  - `testStackFacadeRefuses`: count 1 → `ORION_ERR_BAD_ARG`; existing output → error and the file untouched; mode 2 without a matte → error with "sky" in `orion_last_error`; one frame of a different size → error; a blank (no stars) frame among 6 star frames in mode 1 → OK, and `Stacker::skipped() == 1` (test through `stack::Stacker` directly, as `testHdrMergeCancelIsAtomic` does).
  - `testStackCancelIsAtomic`: as `testHdrMergeCancelIsAtomic`: complete valid file or no file.
  - `testStackBench` — runs only when `ORION_STACK_BENCH=1`: writes 20 synthetic 6000×4000 star frames (once, under `/tmp/orion-stack-bench/`), times mode 1 for the first 10 and for all 20, prints both times and the peak resident set (`getrusage` `ru_maxrss`). Not part of the normal run.
- [ ] **Step 2:** Run; fail.
- [ ] **Step 3: Implement** `Stacker::run`, modelled line for line on `HdrMerge::run` (validation first: ≥2 paths, reference in range, output absent, landscape needs a matte, darks/flats non-empty → throw "calibration frames are not built yet" — Task 11 removes this). Decode each frame (Bayer via `MergeRender::demosaic`, linear DNG via halves, as HdrMerge), check size against frame 0; while the float RGB is in hand: Noise → `merge::align(frame, reference)` (reference decoded first and its float kept until all are aligned; exposure ratio 1); Stars → `detectStars` + `alignStars` against the reference's stars; Landscape → stars inside the frame-space matte (from `skyMatteToFrame` with the reference's flip) for `refToSource`, identity `groundToSource`. A frame whose registration fails is skipped (stderr line, `skipped_++`), except the reference; < 2 frames left → throw. Convert to half and drop the float. `integrate`, then the DNG: the reference's camToXyz, AsShotNeutral from camMul (as HdrMerge), baselineExposure 0, camera, flip, `merge::buildPreview(..., 0.0f, ...)`. Progress: 0.05–0.75 across decode/registration, 0.8 integrate, 0.95 write; cancellation polled at each step. `CApi.cpp`: argument checks then `guard`, as `orion_engine_hdr_merge`.
- [ ] **Step 4:** Pass; all existing tests still pass. Then run the bench once (`ORION_STACK_BENCH=1`, through heavy.sh) and paste its output into the commit message body.
- [ ] **Step 5:** Commit `engine: Stacker and orion_engine_stack — noise, star and landscape stacks to a DNG (#391)`.

### Task 7: Shell — finding stacks in a folder (pure logic)

**Files:**
- Create: `shell/Slices/Library/StackGroups.cs`
- Modify: `shell/Shared/SelfTest.Library.cs` (checks)

**Interfaces — Produces:**
```csharp
namespace OrionShell.Library;
public sealed record Shot(string Path, string Camera, float Iso, float Shutter, float Aperture, float Focal, DateTime? Captured);
public sealed record StackGroup(IReadOnlyList<string> Paths, bool Astro);
public static class StackGroups {
    public const int MinFrames = 5;
    public const double CadenceTolerance = 2.0;   // seconds, on (gap − shutter); timestamps are whole seconds
    /// Contiguous runs (in capture order) of ≥ MinFrames with identical camera, ISO, shutter, aperture and focal
    /// length and a steady cadence. `dark` (optional) is asked once per candidate astro group, of its middle frame.
    public static List<StackGroup> Find(IReadOnlyList<Shot> shots, Func<string, bool>? dark = null);
    public static string Suggestion(StackGroup g);   // "14 frames look like a stack. Stack them?"
}
```

- [ ] **Step 1: Failing selftest checks** (`StackGroupChecks` in `SelfTest.Library.cs`, called where the other library checks are): 14 frames, 20 s, ISO 3200, 25 s apart → one astro group of 14 with the exact sentence `"14 frames look like a stack. Stack them?"`; the same with frame 8 at ISO 1600 → groups of 7 and 6, no frame 8; the same with one gap of 60 s after frame 5 → groups of 5 and 9; 4 frames → none; 1/200 s handheld burst, same second, ISO 100 → one group, not astro; `dark` returning false → not astro; frames without a capture time → no group; shuffled input order → the same groups (sorted by time).
- [ ] **Step 2:** Build the shell and run `--selftest` with a sample; the new checks fail.
- [ ] **Step 3:** Implement. Equality of the settings within 0.1% relative; cadence: c_i = gap_i − shutter, the run's first c sets c0, a frame joins while |c_i − c0| ≤ 2 s and c_i ≥ −1 s.
- [ ] **Step 4:** Selftest passes.
- [ ] **Step 5:** Commit `shell: find stacks in a folder by settings and cadence (#391)`.

### Task 8: Shell — the native call, `--stack`, and the real-file smoke

**Files:**
- Create: `shell/Slices/Sheets/Stack.cs`
- Modify: `shell/Shared/CommandLineModes.cs` (a `StackMode`), `shell/Program.cs` (dispatch and tool flag, `--mode` already in the values list), `tools/check-modes.py` (a third mode), `shell/README.md` (flag row)

**Interfaces — Consumes:** the facade (Task 6). **Produces:**
```csharp
namespace OrionShell.Sheets;
static partial class Stack {
    public enum Mode { Noise = 0, Stars = 1, Landscape = 2 }
    public static string OutputPath(string reference, Func<string, bool>? exists = null);   // <stem>-stack.dng, -stack-2…
    public static float? Progress { get; }
    public static void Cancel();
    /// Null when it wrote `output`, else the engine's reason. Its own engine, on a worker, as HdrMerge.Run.
    public static Task<string?> Run(IReadOnlyList<string> paths, int reference, Mode mode, int rejection, bool weight,
                                    IReadOnlyList<string> darks, IReadOnlyList<string> flats,
                                    float[]? skyMatte, int matteWidth, int matteHeight, string output);
}
```
`Orion --stack <output.dng> [--mode noise|stars] <frame> <frame> [frame...]`: runs, then opens and renders the output as `HdrMergeMode` does; exit 0/1/2.

- [ ] **Step 1:** Add the `check-modes.py` case first: copy one sample ARW three times into a temp folder, `--stack out.dng --mode noise a b c`, expect exit 0 and the file present; and a deleted dispatch must fail (by timeout, as the other two). Run it → fails (no mode).
- [ ] **Step 2:** Implement `Stack.cs` (mirror `HdrMerge.cs`), `StackMode`, the dispatch; selftest check for `OutputPath` naming (`a.ARW` → `a-stack.dng`, then `a-stack-2.dng`).
- [ ] **Step 3:** Build engine + shim + shell; `check-modes.py` passes; selftest passes. Record the `--stack` wall time on the three ARW copies.
- [ ] **Step 4:** Commit `shell: Orion --stack, and check-modes runs it on a real raw (#391)`.

### Task 9: Shell — the Stack sheet, its command, and the landscape split painter

**Files:**
- Create: `shell/Slices/Sheets/StackPanel.cs` (≤300 lines), `shell/Slices/Sheets/StackSplit.cs` (the painter)
- Modify: `shell/Shared/CommandTable.cs` (row `Built("stack", "Stack photos…", null, "StackPanel.cs", () => Loaded && StackPanel.HasCandidates, () => Sheet("sheet-stack", "Stack"))`), `shell/Shared/SelfTest.Contracts.cs` (add `"stack"` to the frozen command list), `shell/Slices/Masks/SkyDetector.cs` only if a public entry is missing

**Interfaces — Consumes:** `Stack` (Task 8), `StackGroups` (Task 7), `SkyDetector.Detect`, `SubjectMatte`-style thumbnail access (`LibraryModel.Photo.Thumbnail`), `SheetKit`, `HdrMergePanel`'s row pattern.
**Produces:** panel `[Panel("sheet-stack", Region.Sheet)]` with `public static bool HasCandidates`; `internal static string? Ineligibility(List<HdrMergePanel.Candidate>)` (fewer than 2 → "Select at least two frames to stack."; different cameras → HDR's sentence shape).

Behaviour: candidates = the explicit selection when it has ≥ 2 photos, else the `StackGroups` group containing the open photo (that is what the suggestion opens, and what a screenshot of a stack folder shows). Controls: mode (Segmented: Noise · Stars · Landscape; default Stars when the group is astro, else Noise), rejection (Auto — showing what auto picks for this count — None, Percentile, Sigma, Winsorized, Linear fit), "Weight by noise" toggle, reference rows (default: the middle frame), and in Landscape the split painter: the reference thumbnail with the sky tinted, seeded by `SkyDetector.Detect` on a 3×3 median of the thumbnail at ≤ 384 px long edge (stars removed so the sky reads as calm), Add sky / Remove sky brush (pointer drag paints a disc into the float matte, radius slider), "No horizon found — paint the sky" note when detection refuses. Stack button → `Stack.Run(...)` with progress and Stop, then open the output and `Say("Stacked N frames into NAME")`, as `HdrMergePanel.Merge`.

- [ ] **Step 1:** Selftest checks first: `StackPanel.Ineligibility` sentences; `"stack"` is a registered built command; the painter's `Paint(matte, w, h, x, y, radius, add)` sets inside the disc to 1 (add) / 0 (remove) and leaves the outside untouched. Run → fail.
- [ ] **Step 2:** Implement the sheet and the painter.
- [ ] **Step 3:** Build; selftest passes. Screenshot through heavy.sh: make a scratch folder with 6 copies of one sample ARW (same EXIF and second → one group), then `Orion <folder>/x1.ARW --panel sheet-stack --screenshot <scratchpad>/stack-sheet.png`, and Landscape selected via an env-free route only if the product has one (otherwise screenshot the default mode and say so). Look at the PNG.
- [ ] **Step 4:** Commit `shell: the Stack sheet — mode, rejection, reference, and the landscape split painter (#391)`.

### Task 10: Shell — the suggestion in the Library

**Files:**
- Create: `shell/Slices/Library/StackSuggestion.cs` (the bar + the background scan)
- Modify: `shell/Slices/Library/FilmstripPanel.cs` (host the bar), `shell/Slices/Library/LibraryModel.cs` only for a `Select(IEnumerable<string>)` if none exists in `PhotoSelection`

Behaviour: on `FolderLoaded`, off the UI thread, read `CaptureInfo.ReadFile` for every photo, `StackGroups.Find` (dark check: the embedded thumbnail's mean luma < 0.15, middle frame only), keep the groups. Show one line, unobtrusive (Faces.Label, Palette.Dim, one prominent "Stack…" text button and a dismiss ×): `StackGroups.Suggestion(group)` for the first group not dismissed in this session. "Stack…" selects the group's photos and runs command `stack`. A new folder clears dismissals.

- [ ] **Step 1:** Selftest check: on a list of `Shot`s through the same code path the bar uses (a static `StackSuggestion.Pick(groups, dismissed)`), dismissing the first group shows the second; dismissing all shows none. Run → fail.
- [ ] **Step 2:** Implement. `tools/check-wiring.py` must stay green (every new func has a product caller).
- [ ] **Step 3:** Build; selftest; screenshot `Orion <the 6-copy folder> --screenshot <scratchpad>/stack-suggestion.png`; look at it — the line must be visible and readable in the window.
- [ ] **Step 4:** Commit `shell: the Library suggests a stack (#391)`.

### Task 11: Calibration frames (TDD)

**Files:**
- Create: `engine/src/stack/Calibrate.h`, `engine/src/stack/Calibrate.cpp`, `apps/tests/tests_stack_calibrate.cpp`
- Modify: `engine/src/stack/Stacker.cpp` (remove the refusal; build masters once, calibrate each light before demosaic), `shell/Slices/Sheets/StackPanel.cs` (Darks… and Flats… buttons through the window's StorageProvider, a count line, Clear)

**Interfaces — Produces:**
```cpp
namespace orion::stack {
/// Per-sample median of equally sized frames (the master dark, and the flat before normalization).
[[nodiscard]] std::vector<float> medianOf(const std::vector<std::vector<float>>& frames);
/// (flat − dark) normalized so each channel's mean is 1; channel[i] names sample i's channel (0..3).
/// dark may be empty. Samples ≤ 0 get gain 1.
[[nodiscard]] std::vector<float> flatGain(const std::vector<float>& flat, const std::vector<float>& dark,
                                          const std::vector<std::uint8_t>& channel);
/// v = (v − dark) / gain, in place; either master may be empty.
void calibrate(float* samples, std::size_t n, const std::vector<float>& dark, const std::vector<float>& gain);
/// Bayer: samples minus per-channel black, as float; and back (round, clamp to [black, white]).
[[nodiscard]] std::vector<float> mosaicOf(const raw::BayerImage& image);
void storeMosaic(raw::BayerImage& image, const std::vector<float>& mosaic);
}
```
Bayer frames calibrate on the mosaic before demosaic (channel = `channelAt`); linear-DNG frames on the RGB samples (channel = i % 3). Darks/flats must match the lights' size and kind, else a sentence.

- [ ] **Step 1: Failing tests:** `testStackMasterDark`: 5 darks with a hot pixel and noise → median removes nothing but the hot pixel stays in the master; a light minus the master has no hot pixel (within 3σ). `testStackFlatGain`: a vignetting flat (1 − 0.4·r²) with a colour tint → gains have mean 1 per channel and dividing a vignetted uniform light yields uniform within 1%. `testStackBayerRoundTrip`: `storeMosaic(mosaicOf(img))` is the identity on an in-memory RGGB BayerImage. `testStackFacadeCalibrated`: through the facade, linear-DNG lights with a fixed hot pixel (tripod, mode 0, no motion) plus 3 darks → the hot pixel is gone in the output.
- [ ] **Step 2:** Run; fail.
- [ ] **Step 3:** Implement; Stacker decodes darks/flats first (progress 0–0.05), keeps only the masters.
- [ ] **Step 4:** Pass; all tests pass.
- [ ] **Step 5:** Commit `engine, shell: calibration frames — master dark, normalized flat, on the mosaic (#391)`.

### Task 12: Docs, gates

**Files:** `planning/STATUS.md` (one new session row at the top of *Recent sessions*, move the oldest rows to `planning/HISTORY.md` so six or so remain, update `Last updated`), `planning/DECISIONS.md` (#391 measured numbers: registration error, noise ratio vs √N, 10/20-frame 24 MP times), `planning/FEATURES.md` (stacking row), `planning/ARCHITECTURE.md` (one line for `engine/src/stack/`), `research/stacking.md` (measured numbers).

- [ ] **Step 1:** Write the docs.
- [ ] **Step 2:** Run all nine gates of CLAUDE.md's Tests section through heavy.sh; all green.
- [ ] **Step 3:** Commit `docs: stacking in STATUS, DECISIONS, FEATURES, ARCHITECTURE (#391)`.

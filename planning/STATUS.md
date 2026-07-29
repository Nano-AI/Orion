# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-07-29 (M3 — auto-enhance shipped; **M3 complete**)
**Phase:** M0 done. M1 ~98%. M2 complete. **M3 complete** — local Laplacian
clarity, dehaze, creative LUTs, single-image exposure fusion and auto-enhance
all built, measured and verified on real frames.
**Next story:** the developer's quality pass on M3, then M4 (local edits —
`research/masking.md` is the plan of record). Clarity is down to 58 ms. **Dehaze
is still 108 ms and has been profiled: there is no hotspot in it**, so the fix
is a different algorithm rather than a better arrangement of this one — written
up in `research/dehaze.md`, not attempted.

## Session 2026-07-29f — dehaze profiled, and deliberately not optimised

The bench's per-node profiler now points at any control, not just clarity. What
it says about dehaze is the opposite of what it said about clarity, and it
changes what the fix would have to be.

| Node | ms | share |
|---|---|---|
| `dehaze:min h` | 4.51 | 7.2% |
| `dehaze:dark h` | 4.50 | 7.2% |
| `dehaze:max h` | 4.48 | 7.1% |
| `dehaze:dark v` | 4.40 | 7.0% |
| `dehaze:max v` | 4.40 | 7.0% |
| `dehaze:min v` | 4.40 | 7.0% |

**Six rank passes, 26.7 ms between them, every one within 2% of the others.**
There is nothing to fix *one* of. And the trick that took clarity from 70 ms to
58 does not apply: these passes are already separable — that is how they were
built, and `testDehazeGpu` checks the claim against a 15 × 15 patch computed
directly.

At ~48 MB written per pass they run at roughly 22 GB/s on a 120 GB/s machine,
so they are **tap-count bound, not bandwidth bound**: fifteen comparisons per
pixel, six times over.

The published fix is a running min/max — van Herk (1992), Gil & Werman (1993) —
which is O(1) in the window size and would take fifteen comparisons to about
three. **Not attempted**, and the reason is recorded rather than the intention:
it is a sequential scan, which is what a GPU is worst at, and adapting it means
one thread per line segment with correctness at the joins being the whole
problem. That is a session's work with a real chance of ending slower — the
same shape as the change that already backfired once on clarity.

## Session 2026-07-29e — clarity, 70 ms to 58

The Burt kernel is separable and the fused 5×5 remap node was not using that.
Split into a horizontal pass that remaps and halves, and a vertical pass that
only halves, the remapping is evaluated at five taps per output instead of
twenty-five.

| | Clarity drag | The four remap nodes | Intermediates |
|---|---|---|---|
| Before | 70 ms | 12.07 / 8.47 / 7.47 / 7.46 ms | 5491 MiB |
| **After** | **58 ms** | **~2.8 ms each** | 5861 MiB |

**The filter is unchanged, and the bench proves it rather than asserting it:**
`clarity +1` measures 0.0163 moved and +0.0095 detail both before and after, to
four decimals. A change to how something is evaluated should be invisible in
what it produces, and that is exactly what the reference tests exist to check.

The trade is **370 MB for 12 ms**. Worth taking here; the first thing to look at
on a smaller GPU.

`clarity:collapse 0` is now the largest single node at 11.96 ms — 20% of the
drag, reading four packed stacks at nine taps each at full resolution. Recorded
in `research/local-laplacian.md` along with the warning that the obvious fix for
it was already tried and made things slower.

**Suites:** `orion-tests` **356 checks** · `orion-viewport-tests` **2088
checks** · both 0 failures. `orion-bench` exits 0 on all three sample frames.

## Session 2026-07-29d — auto-enhance wired, and M3 closes

`Engine::autoEnhance` runs the measure/correct loop; the facade writes back only
the five controls auto-enhance may move and leaves the rest of the caller's
block alone. The Auto button sets ordinary sliders, so what it decides is
visible, adjustable and undoable.

### The check that matters, on real photographs

Everything else about auto-enhance is tested against a stand-in for the
pipeline. The bench probe runs the real one, and asks the only question worth
asking — did the median land where it was aimed. Not a magnitude probe with a
floor, because "it moved" is not the claim.

| Frame | median | exposure | lift |
|---|---|---|---|
| daylight | 0.617 → **0.473** | −1.16 EV | 0.00 |
| forecourt | 0.148 → **0.461** | +0.03 EV | 1.00 |
| night | 0.129 → **0.461** | +0.26 EV | 1.00 |

The two dark frames barely move exposure, because the shadow lift is derived
from the photograph *before* the solver starts — by the time it runs there is
little left to correct. That division of labour was the intent, and it is
satisfying to watch it happen rather than have to argue for it.

**One constant changed because the measurement said so.** The endpoint gain
started at 2.0 and railed the whites slider at its maximum on two of three
frames — an automatic control handing the user a setting with nowhere left to
go. At 1.0 the median still lands on the anchor and the endpoints stay
somewhere a person can argue with.

### M3's features, composed — the check that was missing

Every M3 feature was verified alone; nothing verified them **together**, and
they are exactly the kind that interact. Dehaze divides by a transmission,
exposure fusion divides one proxy luminance by another, clarity raises a
normalised amplitude to a fractional power, and the creative LUT indexes a grid
with whatever comes out of all that. A NaN from any one of them is invisible on
screen — it renders as one black or white pixel — and propagates downstream.

The bench now renders all four at full strength over a tone move, and counts
pixels pinned hard at either end, against the same frame with the four off. A
photograph legitimately contains black and white pixels; the question is whether
these features *added* them.

| Frame | four on | four off | nodes / time |
|---|---|---|---|
| daylight | 1.01% | 0.00% | 83 / 141 ms |
| forecourt | 2.59% | 0.13% | 83 / 140 ms |
| night | **0.57%** | 0.63% | 83 / 142 ms |

All three compose cleanly. The night frame is the pleasing one: it ends with
*fewer* pinned pixels with the features on than off, which is exposure fusion
lifting shadows back out of pure black — the thing it exists to do, showing up
in a number rather than in an opinion.

140 ms is the worst case in the product and it is a single render, not a drag.

## M3 — what it cost, in one table

| Feature | Nodes | Drag | Resolution |
|---|---|---|---|
| Clarity (local Laplacian) | 32 | 70 ms | full |
| Dehaze (dark channel prior) | 16 | 108 ms | full |
| Exposure fusion | 32 | 37–48 ms | quarter |
| Creative LUT | — | 7 ms | fused into the display node |
| Auto-enhance | — | ~6 renders, one click | — |

**The M0 gate never moved**: 8.8–9.9 ms p95 throughout, exposure drag still
three nodes, because every one of these disables to nothing when it is off.
109 nodes, 5491 MiB of intermediates — the number to watch on a lesser GPU.

**The two slow ones are slow for the same reason and it is written down.**
Clarity and dehaze run at full resolution; fusion does not, and costs half as
much with the same node count. `Pipeline::setProfiling` prints a per-node
ranking on every bench run, and `research/local-laplacian.md` names the two
candidate fixes in order.

## Session 2026-07-29c — auto-enhance: researched, policy built, not yet wired

`research/auto-enhance.md`. **The research turned up two negative findings that
would otherwise have become confident wrong constants**, which is the whole
argument for chasing sources before writing numbers down.

### What has no source, and now says so

- **Simplest Color Balance recommends no clipping percentage.** Not in the text,
  and its reference implementation takes the levels as mandatory arguments with
  no fallback. The widely repeated "0.5% per side" is a reading of figure
  captions calling 1% total *"optimal"* and *"moderate"*. Orion uses it and
  records it as inference in `UNSOURCED.md` §15.
- **There is no published value for the mean or median luminance of a
  well-exposed photograph.** It was looked for. What exists is CIPA
  DC-004:2004's `MAX × 0.461`, which is a target for a uniform grey card under
  controlled lighting — and the standard itself calls the choice conventional:
  *"there is no single and absolute point of definition as long as the tone is
  in the middle range."* Aiming a photograph's median at it is a judgement.

### What is sourced

The quantile definition and — usefully — the reason to work on luminance rather
than per channel, which is the paper's own sentence: per-channel stretching
*"provides both a white balance and a contrast enhancement"*, and it is blunt
that this *"is not a real physical white balance"*. Orion already has one the
photographer set. Also sourced: a published ceiling on how hard an automatic
stretch may push (Lisani, Petro & Sbert, IPOL 2012, `smax = 2`).

**A trap avoided:** Mertens' well-exposedness Gaussian at 0.5 is a per-pixel
blending kernel for a bracketed stack, *not* evidence about the mean of a
photograph. It is used correctly inside exposure fusion; citing it here would
have been exactly the wrong-but-cited constant this repository exists to stop.

### The damping was backwards, and the comment says why

The solver's step is `log2(target / median)` — the correction that would be
right if the rendered median moved in proportion to exposure. The display
transform is compressive, so the true response is *smaller* than that estimate
and every step already undershoots. Damping below 1 only slows it: measured
0.064 from the anchor after five passes at 0.7, and inside 0.02 at 1.0.

### What is left

The policy is a pure function of a histogram and is fully tested. Not built:
`Engine::autoEnhance`, the C facade entry point, the Auto button, and the bench
probe that verifies the outcome on real frames. That last one matters most —
everything so far is checked against a stand-in for the pipeline, not the
pipeline.

## Session 2026-07-29b — exposure fusion, finished

The GPU chain, built to the plan the previous session recorded. Thirty-two
nodes, **all at quarter resolution**.

| | Nodes | Drag | Resolution |
|---|---|---|---|
| Clarity | 32 | 70 ms | full |
| Dehaze | 16 | 108 ms | full |
| **Fusion** | **32** | **37–48 ms** | **quarter** |

Fusion is the cheapest of the three despite having as many nodes as clarity,
because only a *gain* reaches the full-resolution picture — the pyramid never
does. That is worth remembering when the other two get optimised.

Measured lift at full strength: mean luma **+0.105 / +0.245 / +0.257** on the
three sample frames. **M0 gate unmoved at 8.84–9.93 ms p95.** 109 nodes,
5491 MiB.

### The test that stops two implementations drifting

`ops/fuse_ops.slang` and `pipe/ExposureFusion.h` are the same equations written
twice, and every other exposure-fusion test measures against the C++ side — so
if the two ever disagree, all of those tests are pinning something the product
does not run. The GPU test compares them per pixel on both the simulated
exposures and the weights, and separately checks that **the weights sum to one
at every pixel**, because if they do not the blend is quietly a gain as well.

**Strength zero is checked bit-identical** against a deliberately violent proxy
gain. That is load-bearing, not decoration: no published parameter of this
method degenerates to the identity — α → 1 collapses the exposure factors, but
`ρ(k)` contains no α, so the simulated images remain differently-clipped copies
and their blend is not the input. It is why the slider is a power on the gain.

### Where the whole-frame reductions now live

Two features need a statistic over the entire frame, which is the one thing a
per-pixel DAG cannot express: dehaze's atmospheric light, and fusion's median.
Both are handled the same way — `render()` renders once when the value is
stale, reads back a small texture, and renders again; the per-node cache means
the second pass only redoes what the new parameter touched. Stale means the
image or white balance changed. **Neither is ever recomputed on a slider tick**,
so neither is on the interaction path.

## Session 2026-07-29a — M3 story 4, exposure fusion (part one)

Simulated Exposure Fusion — Hessel & Morel (WACV 2020 / IPOL 9, 2019) on top of
Mertens et al. (2007). `research/exposure-fusion.md` has every constant with the
quotation it came from. **The CPU maths, its reference implementation and its
tests are in. The GPU chain is not built yet.**

### The placement decision, and why it went the way it did

The method needs a bounded, display-referred `t ∈ [0,1]`; this pipeline carries
unbounded scene-linear light. The faithful option is to split `develop:display`
so fusion sees the AgX-mapped image the user sees — and it was rejected for a
reason worth recording: **a faithful full-resolution RGB fusion is 30–60 ms at
any placement**, six simulated exposures each with two pyramids, on a
bandwidth-bound GPU. Once the method must be approximated regardless, paying a
permanent ~4 ms structural tax on every render — including when the feature is
off — buys an exactness that was never reachable.

So fusion gets its own chain emitting a scene-linear gain, the same shape as the
clarity node, disabled to zero cost when off.

**The proxy must be a sigmoid over log2, not raw normalised log**, and the
failure it avoids is specific: in raw log the shadow axis is stretched, so the
median falls, so `N* = ⌊(M−1)·median⌋` allocates nearly every simulated image to
the brightened side, and the weights then read the sensor's own noise floor as
underexposed content that needs lifting. AgX is itself a sigmoid in log2, so
matching one is a cheap faithful proxy rather than an invention.

**The paper's final 1% global stretch is dropped.** In an editor it fights the
user's exposure, whites and blacks; it makes a pixel's value depend on the
current crop; and it destroys identity-at-zero. The reference implementation
keeps it so comparisons against the paper stay possible.

**The slider raises the emitted gain to its own power.** No published parameter
degenerates to the identity — α → 1 collapses the exposure factors but `ρ(k)`
contains no α, so the simulated images stay differently-clipped copies. `gain^s`
is a lerp in log-gain, exact at `s = 0`.

### Three defects found by writing the tests

- **The simulated-image search started at M = 2**, where the median-derived split
  has a single image to allocate — so a bright frame could never be given a
  darkened one and the asymmetry the whole method rests on silently never
  appeared. It starts at 5 now, the count the paper reports for its own
  recommended α and β.
- **Robust normalisation divided by an epsilon** when its two clipped percentiles
  coincided, mapping a flat field plus one outlier to solid black.
- **A monotone ramp gains tonal reversals.** Measured 2.1e-4 / 2.3e-3 / 1.1e-2
  at α = 2 / 4 / 8. That it scales cleanly with amplification is what says it is
  the method — [M07] §4.1 names the artefact — and not a mistake in the blend.
  But 1% at the recommended α is enough to band a smooth gradient, so it is
  guarded as a regression and all three numbers print on every run.

### What the tests actually pin

The clip is continuous *and* has slope one on both sides of its join — a value
discontinuity is an edge in the simulated image, a slope discontinuity is an
edge in the *weight*, which is worse because it moves. The contrast weight is
checked against a finite difference, because Hessel & Morel replace Mertens'
Laplacian filter with that derivative, so if it is not the derivative there is no
contrast measure at all. And a flat field must fuse to the weighted average of
its own remaps — computable in closed form, and the one check that catches the
pyramid, the expand, the weighting or the collapse being wrong in a way that
does not cancel.

### Also this session

- `pipe/Pyramid.h` — the Burt & Adelson helpers lifted out of
  `LocalLaplacian.h`, since fusion analyses over the same construction. They
  exist to be the reference the GPU is measured against, so one copy matters.
- The `research/` index had gone **eight files stale**. Fixed.

## Session 2026-07-28f — M3 story 3, creative LUTs

`.cube` files, applied last in the display kernel with tetrahedral
interpolation. `research/luts.md`.

**Cheap, because it is fused.** The lookup lives inside
`develop_display.slang` rather than in a node of its own, following the rule
this pipeline already had about pointwise passes. Measured: changing the look
recomputes **2 nodes and 7 ms**.

### Tetrahedral, and why the test is built the way it is

Trilinear and tetrahedral **agree exactly on anything linear across a cell**, so
a gentle LUT cannot distinguish them and "it looks right" proves nothing. They
diverge only where a LUT has a hard boundary — a key, a hue restriction, most
film emulations — because trilinear reads four corners from the far side of it.

So the test builds a table that is zero at every corner except (1,1,1), where
the two must disagree: tetrahedral returns the smallest fractional coordinate,
trilinear their product. 0.4 against 0.12.

### Two bugs written and caught, both by tests that exist for the purpose

- **The grid's row stride used the texture's width where the shader used the
  LUT's own edge.** The texture is allocated at 65 to hold any grid; the packing
  must be `b·size + g` in both places. Getting it wrong puts every blue slice in
  the wrong row, which renders as a plausible colour cast rather than as
  anything obviously broken. The identity-LUT check is the only thing here that
  would have found it.
- **`lutStrength` was missing from the display node's change detection**, so the
  slider did nothing at all. Not visible by inspection; the bench reported the
  control as dead with 0 nodes recomputed.

### The reader reads the format, not the feature

Comments anywhere, CRLF, quoted titles, mixed-case keywords, `DOMAIN_MIN`/`MAX`,
and a 1D LUT **lifted onto the 3D grid** — a 1D LUT is a separable 3D one, so
the lift is exact at the grid nodes and downstream there is one code path
instead of a branch only some files exercise. Errors name the line: a LUT that
will not load is the user's file being wrong, and they need to know which line.

Sizes above 65 are refused by name rather than truncated.

### The references, chased down — and one of them found a bug

Both were unverified when the code landed; both are sourced now.

**Adobe, *Cube LUT Specification, Version 1.0*, September 2013.** The Adobe URL
is dead; the Internet Archive has it. It settles the byte ordering outright —
§7.2 states red changes most rapidly and then writes out the C index,
`r + N*g + N*N*b` — and, usefully, **§8 requires tetrahedral interpolation for
three-dimensional tables**. So the choice of tetrahedral is sourced by the file
format itself, which is a better reason than an accuracy argument.

**The six-tetrahedra construction is Sakamoto & Itooka, U.S. Patent 4,275,413
(1981)**, col. 10 and Table 2 — the origin of tetrahedral colour interpolation.
All six of Orion's cases were checked term by term against it. ⚠️ That table has
a **printing error** in rows 3–6 of its first half (two column headers
transposed, producing geometrically impossible non-adjacent vertex pairs); the
second half is correct and disambiguates it. Recorded in `research/luts.md` so
the next person to check the source does not conclude the code is wrong.

**Reading the specification found a real defect.** Comments in `.cube` are whole
*lines*, not trailing text (§5.8) — so the parser had been truncating a look
called `Look #3` to `Look`. Fixed, with a test. This is the argument for
chasing references down rather than implementing from recollection: the code
passed nineteen checks and was still wrong about the format.

**Still open, and no longer load-bearing:** whether tetrahedral is *more
accurate* than trilinear, prism or pyramid. Usually credited to Kasson et al.,
*J. Electronic Imaging* 4(3), 1995 — citation confirmed against DBLP and
Crossref, but the paper is paywalled and was not read, so `luts.md` does not
assert the ordering. `UNSOURCED.md` §12. Two dead ends recorded there too: the
ICC specifications contain zero occurrences of "tetrahedr", and neither does
*GPU Gems 2* ch. 24, which is the Cube spec's only bibliography entry.

## Session 2026-07-28e — M3 story 2, dehaze

He, Sun & Tang's dark channel prior (CVPR 2009 / TPAMI 33(12), 2011), refined
with their own guided filter rather than the matting Laplacian they published
it with. `research/dehaze.md` carries every constant with the quotation it came
out of — the patch is 15 × 15, ω is 0.95, t₀ is 0.1, and the atmospheric light
is the brightest *of the top 0.1% of the dark channel*, which is not the same
thing as the brightest pixel.

**Sixteen nodes, seven kernels.** The graph is now
`profile → dehaze → clarity → tone`, both restorations upstream of the tone
controls so an exposure drag recomputes neither. 77 nodes, 5238 MiB.

### What is pinned

- **The atmospheric light is the haze, not the brightest pixel.** A specular
  four times brighter than the sky is offered to `airlightFrom` and rejected,
  because the paper's first stage ranks by dark channel and only then by
  brightness. Getting that order wrong hands a wrong constant to the whole
  frame, and the paper says so explicitly.
- **A separable rank filter really is the square patch.** The 15-tap minimum
  along each axis is checked against the 15 × 15 minimum computed directly, on
  random data with hard zeros and ones. Same for the maximum, which TPAMI 2013
  §5 calls for to undo the min filter's morphological dilation. That claim is
  what buys 30 taps instead of 225, so it is worth a test rather than a comment.
- **Eq. (16) as arithmetic**, with the transmission pinned to a constant so the
  recovery is checked against the equation and not against another
  implementation of the guided filter. Also: `t = 1` is exactly the identity —
  which is *why* the slider is ω rather than a blend, so zero is exact by
  construction — and `t` is floored at t₀ rather than divided by.

### The night frame legitimately does nothing, and the bench says so

`_PIC8148` measures 0.0000 movement at full strength. That is the method
working: the dark channel is near zero across a night shot, the atmospheric
light lands on a light source, and Eq. (12) returns `t = 1` everywhere — no
veil to remove. The probe is **waived with that reason printed on the line**
rather than floored, because a floor that failed there would be a floor
demanding the filter invent haze. The other two frames move 0.123 and 0.057 of
the reference and are floored at half the smaller.

### Deliberate departures, both stated

- **Scene-linear, not display-encoded.** Eq. (1) is a physical mixture and only
  holds in linear light, so applying it here is a closer reading of the model
  than the paper's own gamma-encoded inputs. The prior survives the change; the
  *statistics* quoted in the paper were measured on encoded images and are not
  re-quoted as if they held here. Consequence handled: scene-linear is unbounded
  above, so `I_c/A_c` is clamped or a specular drives the transmission negative
  and Eq. (16) inverts the pixel.
- **The percentile is over pooled 4 × 4 block maxima, not over pixels.** Max
  pooling is right for a step hunting extremes, but it is not literally the
  paper's top 0.1% of pixels. `UNSOURCED.md`.

### ⚠️ Cost

A dehaze drag is **108 ms over 15 nodes** — six full-resolution 15-tap rank
passes are most of it. Same shape of problem as clarity's 70 ms, and the
per-node profiler added last session applies directly. The M0 gate is unmoved
because dehaze at zero disables the chain.

`A` is a reduction over the whole frame, so it is not a node: `render()` renders
once when it is stale, reads back a sixteenth-resolution candidate texture,
picks `A`, and renders again. Stale means the image or white balance changed —
never a slider, so it is off the interaction path.

## Session 2026-07-28d — M3 story 1, local Laplacian clarity

`research/local-laplacian.md` is the plan of record and carries the working;
this is what happened.

### The measurement that set the design

Paris, Hasinoff & Kautz (SIGGRAPH 2011) with Aubry et al.'s fast approximation
(ACM TOG 33(5), 2014). Aubry recommend sampling the intensity range **every
standard deviation σ** — eight γ levels here, which is what got built first.

Then it was measured against Paris et al.'s exact Algorithm 1, implemented
literally in `pipe/LocalLaplacian.h` — one full pyramid per output coefficient,
no approximation of any kind — at the strongest setting the slider reaches:

| samples per σ | γ levels | mean error | max error | PSNR |
|---|---|---|---|---|
| 1.0 | 8 | 0.354 EV | 1.359 EV | **28.0 dB** |
| 2.1 | **16** | 0.151 EV | 0.696 EV | **35.6 dB** |
| 4.4 | 32 | 0.159 EV | 0.408 EV | — |

The paper's own stated accuracy is "above 30 dB", and one sample per σ does not
reach it. Two do; four buy nothing, and **that plateau is the informative
part** — it says what is left is the linear interpolation standing in for a
sinc reconstruction, which no amount of extra γ levels can fix. Sixteen is a
measured knee. σr is now a constant in its own right instead of an alias for
the γ step.

Milder settings never needed it: α = 0.5 measures 42.0 dB, α = 4 measures 49.0.
It is the strongest boost that sets the requirement, which is what the Nyquist
argument in the paper predicts.

### Two references, because one number cannot diagnose

"The GPU disagrees with the paper" has two causes that want opposite fixes. So
there are two CPU references, and the checks are separate:

- **`referenceFast`** runs the *same* approximation on the CPU. A gap between it
  and the GPU is a bug in a kernel. Worst disagreement across all three slider
  settings is under 5e-3 — the shaders run Aubry's algorithm.
- **`reference`** runs Paris's exact algorithm. A gap between it and
  `referenceFast` is the approximation being an approximation, and it has to
  shrink as γ levels grow. It does, until it plateaus.

Also pinned: **α = 1 collapses back to the input** to 2e-3. With `fd(Δ) = Δ` the
remapping is exactly the identity, so the whole chain reduces to "analyse into a
Laplacian pyramid, collapse it again". Every other check here would still pass
with a subtly wrong expand operator, because both sides would share the mistake.
That one would not.

### What is in that the paper says must be

**The noise term.** §5.2, *Reducing Noise Amplification*: when α < 1,
`fd(Δ) = τΔ^α + (1−τ)Δ`, τ a smooth step over 1%…2% of the range. The paper
states every result in it was computed with that function. It matters because
the α < 1 branch has unbounded slope at the origin — without the term, the
lowest-amplitude signal in the frame receives the largest gain of anything in
the picture, and on a photograph that is the noise.

**Luminance only, ratios kept** (§5.3, Figure 9). Filtering the channels
separately also boosts *colour* contrast, which for a clarity slider means
fighting the grading wheels.

### Placement, and why the gate did not move

Before the tone controls, next to the guided filter, for the guided filter's own
reason: exposure is a multiply, so in log2 it is an additive constant, and the
Laplacian of a constant offset is zero. Clarity computed before exposure is
therefore *bit for bit* what computing it after would give, while all thirty-two
of its nodes stay cached for the slider people actually drag.

**M0 gate: 10.61 / 10.18 / 9.79 ms p95** on the three frames, exposure drag still
three nodes. Clarity at zero disables the whole chain and a disabled node
resolves to its first input, so it costs nothing when unused.

### ⚠️ A clarity drag is 70 ms, and the profile says where

Correct, not yet interactive. `Pipeline::setProfiling` now times each node in its
own command buffer and `orion-bench` prints the ranking every run:

| Node | ms | share |
|---|---|---|
| `clarity:remap 1.0` | 12.07 | 16% |
| `clarity:collapse 0` | 12.01 | 16% |
| `clarity:remap 1.1` | 8.47 | 11% |
| `clarity:remap 1.2/1.3` | 7.46 each | 20% |

The four remap nodes are 47% between them: each remaps a 5×5 footprint for four
γ at once, a hundred remappings per output pixel.

**That tool exists because a hunch was wrong first.** The collapse kernels read
all four packed stacks at all nine expansion taps while only two of sixteen γ
are ever used, so they were rewritten to fetch only what a pixel needs. Output
was bit-identical and it ran **slower — 78.9 ms against 71.6** — the branch
diverges more than the saved fetches were worth. Reverted, profiler written, and
it pointed at a different kernel.

Next, in order: separable halving in threadgroup memory (25 taps → 10, and the
remapping count falls with it), then measuring whether a full-resolution remap
into its own texture is a win or a wash. Neither changes the filter, so the
reference tests cover both.

### Also this session

- Intermediates **4027 → 4567 MiB**. The number to watch on a lesser GPU.
- `PixelFormat::R16Float` added — the pyramids are normalized into [0, 1], where
  a half-float quantum is 0.006 EV, an order of magnitude under the noise floor
  τ already declines to amplify.
- Bench probes `clarity +1` and `clarity -1` on the `Detail` metric, floors at
  half the smallest ratio over all three frames. Mean luma is the wrong
  instrument for a local-contrast filter, as it was for sharpening.
- **Texture is not built.** Paris §5.2 and Figure 7d/e specify it as the same
  filter restricted to fine pyramid levels, and §5.2 explicitly licenses
  interpolating between level subsets for a continuous control. It needs a
  second set of pyramids at its own α; the mechanism is written up.

## Session 2026-07-28c — closing M2

Everything M2 listed is now built, and the outside review's P2/P3 findings are
closed with it. In the order the work landed.

### The purple sky, closed — the camera profile grew its second stage

`research/camera-profiles.md` diagnosed it last session: Orion had one of the
five parts of a DNG profile, and a 3×3 matrix cannot be right for a saturated
narrow-band stimulus. The fix is the specification's own **HueSatMap** stage,
built as a real 90 × 25 table with `ValueDivisions = 1` in the spec's entry and
loop order — not a blue-only special case, so a `.dcp` reader later is a reader
and nothing else.

**The trap the plan nearly walked into:** `ProfileHueSatMapEncoding = 0`
requires the table to apply in **linear ProPhoto HSV**. Orion works in linear
Rec.2020. Indexing HSV built from Rec.2020 components would have looked right
against a hand-fitted table and been silently wrong the day a real profile
loaded — which is the entire reason for wearing the spec's shape. The node
converts in and out, from Lindbloom's three published matrices kept as separate
factors so each is checkable.

| | R/B | G/B |
|---|---|---|
| Orion, before | 0.622 | 0.678 |
| **Orion, after** | **0.451** | **0.689** |
| target (Sony/Apple mean) | 0.450 | 0.692 |

Fitted at −8°, saturation ×1.05, centred on 250° over a 60° half-width, swept
against two independent renderings of the same frame with a foliage patch and a
white sign watched for spread. The hazier sky near the horizon lands at 0.636
against 0.647 from the *same* numbers, because the correction is weighted by
saturation rather than applied flat across the hue.

Costs one full-resolution pass, upstream of exposure, so the gate did not move.

### What is pinned, and where

The process finding that built the `feedback/` folder was *the code was fine
wherever it was measured*. So:

- `orion-tests` holds everything checkable without the sample frame: the matrix
  round trip is the identity to 1e-5, every zero-saturation table entry is
  exactly (0, 1, 1), no entry scales value, an identity table leaves every pixel
  where it was, a grey ramp stays grey at every level, and blue moves while
  foliage and skin do not.
- `tools/huefit.py --check` holds the part that needs a photograph — it renders
  `_PIC8095.ARW` and fails if the sky drifts past 0.02 from the target. Outside
  the suite because `samples/` is local-only, and it measures the whole
  pipeline, so it also catches an upstream change that moves the sky without
  touching the node.

### Lens database — the data, not the library

The maths was never the missing part. lensfun's models were already implemented,
tested and running on the GPU; the measured coefficients were sitting in XML.
Linking the library would have added an LGPL-3 dependency, a build step and a
second implementation of the same polynomials to obtain a number that can be
read directly.

`data/lensfun/` is the database vendored unmodified with its CC BY-SA 3.0
licence — **1,558 lenses**. `pipe/LensDatabase.cpp` parses it once per process.
The shader now evaluates ptlens, of which poly3 is the a = c = 0 case, so both
of the database's distortion models land in one kernel; `autoScale` evaluates
the same polynomial, with a comment saying why it must.

**Matching is deliberately conservative, because a confident wrong profile is
worse than none** — it distorts the frame and reports that it measured it.
Names below eight characters never match; a differently-spelled match is
flagged and the panel says so. The developer's own Sigma 24mm F1.4 DG DN is not
in the database and correctly reports nothing rather than borrowing the DSLR
DG HSM entry, which is a different optical design. That case is asserted.

`a`, `c`, p_b and p_c would all have shipped untested — a manual slider can
only ever set `b` and p_a — so each has its own GPU assertion that it pins the
corner and moves the interior.

### Broader camera support

- **Unsupported sensors are refused by name.** X-Trans (6 × 6 mosaic), Foveon
  and linear DNG, and four-colour CFAs each produce their own message instead of
  a scrambled picture that reads as a bug in the pipeline.
- **The 2 × 2 black-level pattern is exact rather than averaged.** LibRaw's
  pattern lines up with the CFA cell for cell; averaging it left the spread in
  the shadows as a colour cast on every frame, with no control that could remove
  it. Asserted on RGGB and BGGR.
- One extension list. The Open panel took eight and the folder scan ten, so a
  folder could show a file the dialog refused.

### The review's P2 and P3 findings

| # | Was | Now |
|---|---|---|
| 6 | Sidecar escaping compounded one layer per save (`R&D` → `R&amp;D` → …) | unescape on read, asserted over three round trips |
| 7 | **Every export published the photographer's GPS**, silently | three-way control, default strips location, and the default itself is asserted |
| 8 | Generated design tokens existed and nothing imported them | the mirror is deleted; the generator emits sRGB (it emitted P3 while the app built sRGB) and a numeric `Components` enum for Metal |
| 9 | Serial folder load; a 30 ms busy-poll | bounded six-wide task group; `open` is async and the poll is gone |
| 10 | Five copy/behaviour mismatches | all five, including the export dialog now defaulting to the photo's own name |
| 11 | Dead state, per-call curve re-sorting | `minimumRating` deleted, curve sort and tangents hoisted per channel |
| 12 | Grading wheels, curve and filmstrip were mouse-only | all three have keyboard and VoiceOver paths; wheels speak hue and strength, the curve walks its points, filmstrip cells are buttons |
| 13 | `OrionApp.swift` 1,321 lines against a hard 1,000 | 1,120, with the three tool panels lifted into `DevelopPanels.swift`. **`apps/tests/main.cpp` is 2,828 and still over — stated, not softened** |
| 14 | Two copies of "where we are", four wrong entries | ROADMAP's status section is a pointer to this file; UNSOURCED rewritten |

**Not done, on purpose:** finding 9's suggestion to detach `Engine.exportedSize`
from the main actor. It renders a full-resolution frame through the same
pipeline the canvas is using, so detaching it races the render rather than
moving it — the fix is a serialized engine queue, which is a change to the
facade's threading contract and not a one-line detach. The hitch stays until
then.

### Adobe, and what Orion actually depends on

`/NOTICE` now carries the string the DNG patent grant requires — implementing
the specification triggered it, and the HueSatMap node is that implementation.
**No Adobe data is shipped.** Both profile values are fitted from the camera's
own JPEG and a second independent rendering, which is why they are also in
`research/UNSOURCED.md` §9: the *stages* are published, the *numbers* are
Orion's own measurement of one camera body.

## Session 2026-07-28b — answering the outside review

`feedback/2026-07-28-senior-review.md` is a senior review with 17 findings. This session took the three
P1s, one P2, and the process finding underneath them.

**Suites:** `orion-tests` **237 checks** (was 211) · `orion-viewport-tests`
**2081 checks** (was 2067) · both 0 failures. `orion-bench` now exits nonzero
when a control is dead or weak; verified by forcing one.

| # | Finding | What it was | Now |
|---|---|---|---|
| 1 | Edits lost on quit | `saveDevelop` ran only on a photo switch | `app/Autosave.swift`, coalesced writes + `willTerminate` |
| 2 | Disabled guide fed garbage to whites/blacks | `whites +1` moved mean luma **+0.1105**; correct is **+0.0064** | flag + pixel-EV fallback |
| 3 | Lens killed incremental invalidation | 7 nodes per exposure tick with a vignette on | 3, asserted by the bench |
| 5 | Newest node untested, bench could not fail | no grading GPU test, no probe, exit code ignored the probes | all three |
| — | **Lens distortion smeared the frame edges** | found by the developer mid-session | autoscale, `pipe/LensGeometry.h` |

### The correction the git history needs

The commit `02ad412` **"Edits persist per photo" claimed more than it built.**
It wired the sidecar and called it on a photo switch, and nothing else — so
editing one photo and quitting lost the work, which is the ordinary case. The
gap was noticed in that session, not built, and then shipped under a title that
reads as solved. This paragraph is the correction; the code landed today.

The same overstatement is in `feedback/2026-07-28-performance-and-quality.md` §2's "exposure drag,
3 nodes, 11.5 ms", which held only with every lens slider at zero — the one
state the bench measured. Both are fixed in the doc as well as in the code.

### What each fix cost, measured

- **Guide chain.** `develop_linear` sampled `guideAb`/`guideRaw` unconditionally.
  With highlights and shadows at zero the seven guide nodes are disabled, and
  `Pipeline::resolve` walks a disabled node back to the last live producer — the
  colour matrix. So linear RGB was read as log2 luminance and as filter
  coefficients. The *offsets* were zero, but the four band weights normalize to
  a partition of unity and two of them came from that garbage, so they sat in
  the denominator and diluted the other two per pixel.
  GPU-measured: blacks −1 at its strongest was worth **0.758 EV instead of
  1.948 EV**. On a real frame `whites +1` moved mean luma **+0.1105** where it
  should move **+0.0064** — an endpoint control acting as a second exposure
  slider on every photo. Fixed by telling the shader (`guideEnabled`) and
  falling back to the pixel's own EV, which is the correct semantics anyway.
- **Lens invalidation.** `correctingLens ||` tested nonzero, not changed. One
  clause deleted. The bench now drags exposure with a vignette and distortion
  applied and asserts the node count matches the clean drag: **3 of 28, 11.7 ms.**
- **Lens autoscale.** poly3's `(1 − k₁)` pins `r_d(1) = 1`, so the corners stay
  put — but `r = 1` is the corner and the frame is a rectangle. The edge
  midpoints sit at r ≈ 0.83, where a negative k₁ multiplies by 1 + 0.31·|k₁|.
  At the slider maximum that fetches **325 px past a 6024 px frame**, and
  `sampleClamped` returned the border pixel for all of it. Measured before the
  fix on `_PIC8148.ARW`: three columns 18 px apart returned identical means to
  four decimals. After: they differ, as real content does. Written up in
  `research/deep-research-2026-07-27.md` §4.
- **A half-texel shift in the same shader**, found while fixing the above. `d`
  is measured from pixel centers and `sampleClamped` indexes texels, so every
  fetch landed exactly between two texels — a half-pixel shift and a bilinear
  blur over the whole frame the moment any lens slider left zero. It survived
  the identity test because that test reads a linear ramp, where the average of
  two neighbours is the value between them, and the tolerance was 2e-3 — which
  is exactly one half of the ramp's texel step. The tolerance is 1e-4 now.

### The class of bug underneath findings 2, 3 and 5

All three lived in the gap between *something happened* and *the right thing
happened*. Three changes, in order of how much they are worth:

1. **Every bench probe is judged against its own baseline.** A probe that lifts
   exposure 5.5 EV was being compared against an unlifted frame, so the lift
   was counted as the control's own effect — it flattered the highlight grading
   wheel by more than tenfold. Fixed by giving each probe a `context` and
   measuring context-versus-context+control.
2. **Every probe asserts a magnitude**, as a fraction of what a reference
   control moves on the same frame, and **the exit code honours it.** Floors are
   printed on every line, passing or not. Verified on both sample frames.
3. **Invariant probes, not just magnitude probes.** Two exact questions that the
   loose version passed while the code was wrong: blacks and whites must land
   identically with the guide chain on and off, and an exposure drag with lens
   corrections applied must recompute the same node count as a clean one.

Also: `sharpen` was measured by mean luma, which an edge filter barely moves by
construction — it read −0.0005 on `_PIC8220`, under every other probe's noise.
There is a `Metric::Detail` now (neighbour-to-neighbour luma), and denoise has a
probe for the first time.

### Found while doing the above — not fixed, filed

**Feedback #4 is worse than it reads, and now has numbers.** The grading zones
partition on *linear* luma at 0.0/0.5/1.0, and separately the offset is an
additive constant in unbounded scene-linear — so what a wheel is worth relative
to the pixel falls as 1/level, while `wh` only switches on past linear 0.5. The
highlight wheel is therefore enabled exactly where its authority has gone. On
both sample frames lifted 5.5 EV it measures **−0.0000 and +0.0001** mean
chroma: inert. Midtones manage −0.0007. The shadow wheel works (+0.0396).

Third effect, same root: the shader clamps at zero and `kStrength = 0.03` at
full radius is ±0.038 — larger than a deep shadow — so the negative channels
stick at zero, the offsets stop cancelling, and the wheel *brightens* what it
should tint. A 0.0096-linear patch comes back at 0.0124, **+29%**.

Written up as `research/UNSOURCED.md` §8 with the fix (perceptual zone weights,
level-scaled offsets). The two dead probes are `WAIVED` in the bench with that
number, so they are stated on every run rather than quietly absent. **This is
the next story.**

### The M0 gate: 12.98 → 9.61 ms p95

Asked whether locality or caching had anything left to give. The answer is a
number: the pipeline runs at **96 GB/s against the M4's 120 GB/s peak** — 81%.
Spatial locality inside a kernel is already maxed, temporal locality across
frames *is* the per-node dirty cache, and the only lever left is moving fewer
bytes. Full working in `feedback/2026-07-28-performance-and-quality.md` §2.

So: the tail of the graph is eight bits for the screen now. The drawable is
`bgra8Unorm`, so `rgba16f` through `develop:display` and `geometry` was buying
precision nothing could show. Export widens the tail around its own read and
narrows it again, so 16-bit output is untouched.

| Tail | median | p95 | intermediates |
|---|---|---|---|
| RGBA8 (screen) | 9.09 ms | **9.61 ms** | 3828 MiB |
| RGBA16F (export) | 12.07 ms | 12.64 ms | 4211 MiB |

That is the 2.6 ms `feedback/2026-07-28-performance-and-quality.md` said 16-bit export had cost,
handed back, with the capability kept.

**Two process notes, because both nearly cost more than the change was worth:**

- **The first measurement was wrong and said "no gain at p95".** It compared a
  build from ten minutes earlier against one taken now, and this machine
  throttles hard across a long bench session — the same wide configuration read
  12.58 ms cool and 22.68 ms warm. The bench measures both tails **in one
  process, interleaved, and repeats the first configuration as a drift check.**
  If the two matching runs disagree, the comparison is noise and the numbers
  say so.
- **The bench's own readback was still asking for half float.** Downloading an
  `RGBA8Unorm` texture with a stride computed for `__fp16` does not fail, it
  returns nonsense — mean luma read 0.0023 instead of 0.0714 and every probe
  went with it. Four readers had the same assumption baked in (`Engine`'s
  histogram, `Engine::readOutput16`, the bench, the screenshot harness). All
  four ask the texture what it is now.

The display node dithers on the way down (ordered, Bayer 4×4). Not decoration:
geometry *resamples* those values and quantises a second time, and two roundings
of a smooth gradient is where contouring comes from — a night sky is the case.
The bench asserts the screen and export paths agree to better than one 8-bit
step; measured **0.00004 luma, 0.00005 chroma** with exposure, blacks and a 3°
straighten applied.

**Not done, and why.** Fusing `geometry` into `develop:display` would save
another ~2 ms, but `geometry.slang` resamples display-encoded pixels on purpose
— averaging unbounded scene-linear blooms a specular edge, which is why film
and VFX resample in log rather than linear. Fusion forces scene-linear
resampling and merges two small shaders into one large one. With 6.4 ms of
headroom that trade is not worth taking. Decision #40.

Also worth knowing: **the 4.2 GiB of intermediates is not waste, it is the
cache.** Resource aliasing would cut it to ~600 MB, but a cached node's output
has to stay resident, so aliasing and per-node caching are mutually exclusive.
Decision #39, written down because somebody will try.

### The flat, dark opening render — closed, and it took the shadow complaint with it

Two complaints from the developer, one root. *"Looks disgusting when loaded in"*
and *"shadows literally colours EVERYTHING"* were both the same defect: Orion
opened a daylight frame **1.3× darker** than the camera's own JPEG, which reads
as flat, **and** put the whole picture half a stop below middle gray — where the
grading shadow band legitimately catches it.

The mechanism has a name: the DNG specification's **`BaselineExposure`**
(tag 50730), *"by how much (in EV units) to move the zero point"*, which Adobe
applies silently on open. Orion had none.

LibRaw does not carry the tag for native ARW and no DNG Converter is installed,
so it was **fitted, not read**: mean absolute luma error over six patches per
frame, swept over a 2-D grid of exposure against base contrast, against two
independent references — the camera's JPEG and Apple's RAW rendering.

| Frame | best EV | best contrast | error |
|---|---|---|---|
| `_PIC8095` daylight | **+1.20** | **1.45** | 0.0171 |
| `_PIC8220` forecourt | **+1.20** | **1.45** | 0.0103 |
| `_PIC8148` night sky | +1.60 | 2.05 | 0.0068 |

Two of three agree exactly. The night frame's surface is nearly flat (0.0083 at
the old defaults against 0.0068 at its own minimum) because a near-black frame
barely moves a mean luma — its preference is noise, and at (+1.2, 1.45) its error
is still 0.0150.

Applied as `kBaselineExposureEv`, added inside `apply()` so **the Exposure slider
still reads 0.00** and Reset returns to the baseline rather than to darkness.
Base contrast 1.15 → 1.45. Daylight mean error **0.1543 → 0.0194**, and Orion now
lands *between* Sony and Apple on five of six patches — the right place to be
when two references disagree.

⚠️ It fits **one body**. A per-camera `BaselineExposure` and a property of
Orion's own AgX zero point cannot be told apart from one camera's data. The
caveat is written at the constant so whoever adds the second body re-measures.

**Bench floors recalibrated across three frames.** Adding the daylight frame
tripped six probes — a bright picture genuinely has no deep blacks, little noise
and few shadows, so those controls move less in it. Not regressions. One frame
had tripped four probes on the second; two frames tripped six on the third.
Floors are half the minimum ratio over all three now, and the reason is written
where the numbers are. All three frames exit 0; the gate passes on all three
(9.70 / 9.76 / 10.89 ms p95).

`apps/pixstat/` is in the repository rather than a scratchpad, with its
orientation handling rewritten as a pixel remap — the CGContext version was
vertically flipped, which is why the first "sky" measurement sampled foliage.

**Still open: the sky is still violet.** After the exposure fix its G/B is on
target (0.678 against Apple's 0.671) and the remaining error is almost purely
excess red (R/B 0.622 against a target of ≈0.45). One axis instead of two, which
is exactly why the exposure had to land first. `research/camera-profiles.md` has
the HueSatMap specification, the ProPhoto-HSV requirement, and the target.

### Grading regraded — feedback #4 closed

The developer reported it independently while this was being fixed: *"the color
grading for the shadows feels like it takes over the entire photo... it might
actually be pulling from the raw image instead of what's currently being
viewed."* Right in spirit. It reads the current scene-linear state, not the raw
— but it decided which zone a pixel was in using **linear** luminance, which
does not correspond to anything you can see on screen. Middle gray is Y = 0.18,
so it weighed 0.70 shadows.

Shadow-zone weight, before → after:

| Pixel | Linear Y | Old ws | New ws |
|---|---|---|---|
| Middle gray | 0.18 | **0.70** | 0.19 |
| A daylight sky | 0.30 | **0.35** | 0.08 |
| Two stops down | 0.045 | 0.87 | 0.77 |

Zones are Gaussian bands on `log2(Y/0.18)` at −2.5 / 0 / +2.5 EV, σ = 1.6 —
the same partition-of-unity construction the tone bands use, so a photograph has
one idea of where its shadows are. And the offset now scales with the pixel's
luminance, so a wheel is a constant chromaticity shift at every exposure instead
of an additive constant whose authority fell as 1/level. `k = 0.25` is derived
rather than tuned: `saturation = 1.5k/(1+k)`, so full travel is 30% from neutral.

`testColorGradeGpu` pins the property that matters: the same wheel measures
**0.1077 relative chroma at −3 EV and 0.1079 at +3 EV**, six stops apart. All
three bench probes pass on both frames; both waivers are gone.

### The instrument was wrong three times over

Worth recording, because it cost more than the fix did. A grading wheel rotates
hue at roughly constant saturation. **Mean luma, mean chroma and mean saturation
each reported a working wheel as doing nothing** — three different instruments,
same blind spot, because a frame mean cancels a rotation.

The bench gates on **mean absolute per-pixel movement** now. The summary metric
is still printed, for insight into *what* changed; movement decides *whether* it
did. It immediately paid for itself elsewhere: `tint +0.5` moves 0.0090 while
its mean-luma delta is −0.0014, so the old gate was reading a sixth of what that
control actually does.

Floors are half the *smaller* ratio measured across both sample frames.
Calibrating on one was not enough — four probes tuned on the night sky tripped
on the lit forecourt, because how far saturation, temperature, sharpening and
denoise move depends on how saturated, warm, detailed and noisy the picture
already is.

### Filmstrip: the frame line was 3 pt away from its own picture

Reported by eye, and a screenshot answered it. `.padding(3)` was applied
*before* the border overlay, so the line was drawn on the padded bounds and a
strip of film base sat between the frame line and the photo on every side — the
picture read as floating in a hole rather than as part of the film. The overlay
goes on the picture now, and the padding is horizontal only: on real stock the
rebate *is* the frame's top edge, while sideways the base is what separates one
negative from the next. New scenes `lens-barrel` and `lens-pincushion` in the
harness.

### Why the pipeline still runs at full resolution — asked, and worth recording

Not a stance, a deferral. The preview-ROI path in `ARCHITECTURE.md` is designed
and unbuilt because the budget passes without it: exposure drag is 11.9 ms p95
against 16 on this machine. Three separate things get conflated under "preview":

- **Tiling / chunk-by-chunk** does not reduce the work, it spreads it. It helps
  a first paint and does nothing for a slider drag, where a half-updated frame
  is worse than a whole one 12 ms later.
- **A downscaled proxy** is the real saving and the real risk. Every
  scale-dependent filter needs a scale-aware parameterization — the noise
  profile is per-pixel, the sharpen radius is in pixels, the guided filter's
  radius is `max(4, longest/200)` — and any mismatch means the preview lies
  about the export. That is the worst bug class in an editor: you find out after
  you have finished editing.
- **ROI — render only the visible region at the zoom you are at** — is the one
  that pays and the one that is designed. At fit the screen is ~2 MP against
  24 MP, roughly a tenfold saving, and it does not need a second parameterization
  because the pixels are the same pixels.

So the trade being taken is: one render path, no possible preview/export
disagreement, and 100% zoom shows real pixels with no re-render — against
carrying 4.2 GiB of intermediates and no headroom on a lesser GPU. The trigger
to build ROI is already named and already measured: temperature and tint at
43–53 ms, which no amount of caching fixes because white balance rewrites the
head of the graph.

## Overnight run — 2026-07-28

Working agreement for this run: commit and push per feature, screenshot every
major feature, measure the engine's output rather than eyeballing it, and only
reach for Gemini if `research/` genuinely does not cover something. It has not
been needed so far.

**Done**

| | Commit |
|---|---|
| Crop constrained to the turned frame; straighten opens to ±90; pivot is the frame center; corner marks in a fixed box; culling moved to a Photo menu | `28ca074` |
| Tone curve panel — the engine's spline had been unreachable through the facade since M2 | `829e565` |
| Profiled wavelet denoise, with a per-frame Poisson–Gaussian fit | `bb06700` |
| Highlight reconstruction; fast guided filter (90 ms → 19.6 ms); a bench that stops crying wolf | `a4ac2fa` |
| Lens corrections — distortion, TCA, vignetting | `bd8c23c` |
| Export panel: measured file size, typed dimensions | `06fff34` |
| 16-bit output end to end; red/blue swap in the screenshot harness | `a50908c` |
| Edits persist per photo; keys work; compare survives a rotation | `02ad412` |
| **A blown highlight came out magenta** — linearize never clipped | see below |
| Every adjustment resets from its own readout | see below |
| Compare came apart on zoom; the top/bottom split was upside down | see below |
| Analog track controls; American spelling; a sidecar that survives a rename | see below |
| Export color space, EXIF and rating; a resize that keeps its depth | see below |

### Export, finished

sRGB, Display P3 and Adobe RGB, converted by ColorSync rather than by a matrix
typed in here — CLAUDE.md's "prefer mature libraries", and a hand-rolled
chromatic adaptation is a cast waiting to happen. The pixels are tagged sRGB
where they are made, because that is what they are, and converted from there;
tagging them as the destination would relabel without moving them, which is how
a file comes to open oversaturated.

EXIF, lens, date and the star rating are carried onto the file, read with
ImageIO rather than exiv2 — DECISIONS #10, and it reads the RAW's own blocks
straight out of the container. Orientation and the RAW's pixel dimensions are
dropped deliberately: the geometry node has already applied the rotation, so
copying the tag would tell every viewer to turn the picture again.

Verified end to end on `_PIC8220.ARW`: Make SONY, Model ILCE-7M3, lens
"24mm F1.4 DG DN | Art 022", ISO 3200, 1/80 at f/1.4, the capture date, Software
Orion, rating 4, and the three profiles each landing on the right file.

**A resize was dropping to eight bits.** The 16-bit output path shipped the
night before survived exactly as far as the first resize, and only for exports
with a size limit — the ones nobody re-checks. The test now reads the written
file back, because a PNG of a smooth ramp compresses to almost nothing at either
depth and byte counts cannot tell them apart.

The bench was passing a bare options struct, so it measured a write the product
never performs. It builds the same options the app does now.

### Compare and zoom

The split happens across the drawn quad in the canvas shader; the panel was
drawing the divider, the labels and the grab band against the *fit* rectangle.
Same rectangle only at fit. `CanvasLayout.drawnRect` is where the picture
actually is. The top/bottom split was also upside down — the fragment shader
recovered its position by unpicking `uv`, which is flipped in y and scaled into
a sub-rectangle of the texture. The quad coordinate is a varying now.

### The sidecar could not survive a rename

Swift's synthesised decoder throws on a missing key rather than falling back to
a property's default, and `Engine.restore` swallows it with a `try?`. Renaming
`denoiseColour` would have silently discarded **every** adjustment in **every**
sidecar on disk, and the photo would have opened unedited with nothing said —
and that was already true of adding any field at all. Decoding is field-by-field
and forgiving now, and still reads the old spelling.

### The magenta highlights

The one that mattered. `linearize` scaled each channel by its white-balance gain
and clamped only at zero, so a blown pixel — which the sensor delivers as
(S, S, S) — left the node as the gains themselves, about (2.2, 1.0, 1.6) on a
warm frame. Everything downstream preserves ratios, so the tone curve, the
color matrix and AgX all carried it faithfully to the screen. Every clipped
light in a night shot rendered magenta.

Clipping all three to one ceiling is dcraw's default, and it belongs in the
mosaic for dcraw's reason: RCD interpolates across an unclipped neighbour, so
clipping afterwards leaves a fringe instead of a clean edge. Written up in
`research/color-pipeline.md`.

Measured over the blown sign in `_PIC8220.ARW`: mean saturation **0.242 → 0.015**,
R/G/B 0.878/0.677/0.896 → 0.800/0.809/0.811.

A side effect worth knowing: highlight recovery is now a measured no-op on that
frame (saturation 0.0146 → 0.0147 at full strength), because a fully blown pixel
is already white and there is nothing to correlate. It still earns its place
where one channel clipped alone. Left off by default, but the panel copy no
longer promises to fix a magenta the pipeline no longer produces.

⚠️ The clip moves with white balance, by design — the white point does. It also
spends the headroom a reconstruction could have used. Both are the right trade
against a cast that was on every frame.

**Still to do, in order**

0. **Masking** — the largest gap, and now the best-specified. `research/masking.md`
   is a full plan of record from a deep-research run: mask primitive maths,
   parametric-not-raster stroke storage, alpha applied to the *parameter* rather
   than blended, mask-group algebra, and Apple Vision for subject. The finding
   that matters most: **guided feathering is the guided filter's own named
   application** (He/Sun/Tang §"Matting/Guided Feathering", r = 60, ε = 1e-6), so
   auto-mask, feathering and AI-matte upsampling all come from the node already
   in the graph with one extra input binding. Steps 1–3 of that plan need no new
   dependency and no new licence position.

1. **A lens database.** The corrections are built and manual. lensfun would set
   the coefficients from what the EXIF names; the maths does not change. This is
   the largest remaining item — a dependency plus an XML database, not an
   afternoon.
2. **A real wide gamut.** The export picker offers sRGB, Display P3 and Adobe
   RGB, and converts correctly — but the display transform ends in Rec.709
   primaries and saturates there, so **nothing Orion renders yet falls outside
   sRGB**. Choosing P3 today buys correct tagging for a managed workflow, not
   more saturation. Widening it for real means giving `develop_display.slang`
   its output primaries as a parameter and moving the sRGB encode with them.
   The panel and the C header both say this plainly rather than implying
   otherwise.
3. **The EXIF read costs ~90 ms per export.** `writeImage` opens the RAW with
   ImageIO on every write to lift its metadata. Caching the property dictionary
   at open would give it straight back; export is off the interaction path, so
   it has not been worth doing yet.
4. **Temperature drag is 43 ms.** Structural: white balance rewrites the head of
   the graph, so the demosaic reruns. The fix is degrade-then-refine (a cheap
   demosaic mid-drag) or the preview-ROI path in `ARCHITECTURE.md`. Neither is
   built, and neither is small.

Verified 2026-07-28: a TIFF export reports `bitsPerSample: 16`, 6024×4024,
145 MB — which is exactly 6024·4024·3·2 bytes.

### Latency, re-measured 2026-07-28 (Sony ILCE-7M3, 6024×4024, M4)

**27 nodes, 4027 MiB of intermediates** — up from 16 nodes and 2.6 GiB. The M4
recommends a 17.8 GiB working set so this is comfortable, but it is the number
to watch on a lesser GPU.

| Drag | Nodes | Time |
|---|---|---|
| Exposure | 3 | 11.5 ms |
| Highlights / shadows | 10 | **23.6 ms** (was 90.1) |
| Color mixer | 5 | 19.2 ms |
| Temperature / tint / sharpen | 11 | ~50 ms |

**M0 gate passes at 11.67 ms p95**, against 16 ms. Re-measured 2026-07-28 after
the highlight clip: **12.70 ms p95** on `_PIC8220.ARW`. The clip is one
instruction in `linearize`; the difference is the frame, not the change.

⚠️ It was 9.04 ms before 16-bit output. Writing `RGBA16Float` from the display
and geometry nodes doubles the bytes those two move, and that is 2.6 ms of the
budget spent on a capability nobody sees on screen. It is a deliberate trade —
4.3 ms of headroom is still real headroom — but if the budget ever gets tight
this is the first place to look, and the fix is a second display path used only
for export rather than a wider one used always.

Temperature is over budget and always will be: it rewrites the head of the
graph, so the demosaic reruns. The fix is degrade-then-refine or the preview-ROI
path, neither of which is built.

### The screenshot harness

```
./build/Orion.app/Contents/MacOS/Orion --screenshot out.png --photo x.ARW \
    --scene crop-angle [--measure x,y,w,h] [--size 1680x1050]
```

Renders the real view hierarchy offscreen — no Screen Recording permission,
which a terminal does not have. Scenes live in `app/Screenshot.swift`; add one
there when you add a feature. `--measure` prints mean and standard deviation
for a region of the engine's output, which is the only way to tell whether a
filter did anything: noise that is obvious at 100% vanishes into a screenshot
scaled to fit a review pane. It is what caught the denoiser doing nothing, and the export panel's size
estimate reading twenty percent high.

`--measure` also prints mean saturation, which is what turned "there is purple
in my photo" into a number that could be watched going down.

A hover cannot be staged in an offscreen render, so `AdjustmentSlider.previewHover`
forces it for the `reset-hover` scene. Whether a control shifts sideways when its
hover state appears is exactly the kind of question a screenshot answers and
reading the code does not — this codebase has shipped that bug three times.

**What it does not prove:** the canvas is drawn as a still read off the GPU, not
through `MTKView` — AppKit cannot capture a Metal layer. Canvas geometry stays
the viewport suite's job.

**Second blind spot, found 2026-07-28:** a `rotation3DEffect` does not survive
`cacheDisplay` either. The mode dial renders every tab square-on in a
screenshot, and looks correct, while the live app shows the turn. Anything
relying on a 3D transform has to be checked by eye. The dial's horizontal scale
is deliberately redundant with its rotation so the part a screenshot *can* see
is still the right shape.

---

## Where we are

**M0 is done and the gate passed with room to spare.** A 24 MP Sony ARW goes
through a seven-node GPU pipeline and an exposure change re-renders in
**8.15 ms at p95 — at full resolution**, against a 16 ms budget. The preview-ROI
optimization the architecture assumes we would need is not needed yet.

```
Source          Sony ILCE-7M3, 6024 x 4024 (24.2 MP, RGGB)
  decode        48 ms   (504 MP/s, LibRaw)
Pipeline        7 nodes, 971 MiB of intermediates
  full render   45.8 ms  (every node)
  exposure drag  6.7 ms median, 7.9 p95  (2 of 7 nodes)
  curve drag     3.7 ms median, 4.6 p95  (1 of 7 nodes)
  WB drag       26.5 ms                  (7 of 7 nodes)  <- over budget
M0 gate         PASS
```

**The pipeline is bandwidth-bound, not compute-bound.** An `rgba16f` texture at
24 MP is 194 MB. Adding tone and color as separate pointwise nodes pushed
exposure drag to 19 ms and *failed the gate*; fusing all the scene-linear
pointwise work into one kernel, and AgX + curve into another, brought it back to
6.7 ms. Each operation still lives in its own function in
`shaders/ops/tone_ops.slang` — one file per adjustment, but one dispatch.

**Do not split pointwise operations back into separate nodes.** The
maintainability rule is about readable code, not one kernel per slider, and
every extra pointwise pass costs a 194 MB round trip for nothing.

⚠️ **White balance is over budget at 26.5 ms** and always will be: it rewrites
the linearize block at the head of the graph, so the demosaic has to rerun —
the demosaic interpolates white-balanced data. The fix is darktable's
degrade-then-refine (cheap demosaic mid-drag, full quality on release), or the
preview-ROI path. Neither is built yet.

Per-node caching works: moving exposure dirties only exposure + AgX, so
linearize, all three RCD passes and the color matrix are served from cache.

### Dev machine (measured, not assumed)
Apple **M4**, macOS 26.4.1, arm64 · Xcode 26.6 / clang 21 · 17.8 GiB recommended working set · 13.3 GiB max buffer · **unified memory** · Apple7 GPU family supported.

Unified memory is a real advantage: CPU↔GPU transfers are free, so LibRaw can decode straight into a shared buffer with no staging copy.

### Toolchain installed
`cmake` · `ninja` · `libraw 0.22.2` · `little-cms2` (was already present)
Still needed: **Slang** (S0.3, not in Homebrew — grab a GitHub release).

### Build
```
cmake -S . -B build -G Ninja
cmake --build build
./build/apps/probe/orion-probe
```

### What exists
```
engine/include/orion/orion.h        C facade — POD only, no exceptions cross it
engine/src/CApi.cpp                 exception firewall; guard() turns throws into status
engine/src/Engine.{h,cpp}           engine proper, RAII
engine/src/gpu/MetalDevice.{h,mm}   device + queue
engine/src/gpu/Resources.{h,mm}     Texture, Library, Kernel, CommandBuffer
engine/src/raw/RawImage.{h,cpp}     LibRaw decode -> untouched CFA mosaic
engine/src/pipe/Pipeline.{h,cpp}    the DAG: Kahn topo sort, per-node dirty caching
engine/src/pipe/DevelopPipeline.*   the standard 7-node graph + adjustments
engine/src/pipe/ShaderParams.h      host mirrors of shader structs, static_assert'd
engine/src/util/ImageWriter.mm      PNG out via ImageIO
engine/shaders/*.slang              7 kernels, one file each
app/*.swift                         SwiftUI shell, MTKView canvas, zero-copy
apps/probe, apps/bench              C-API smoke test, and the M0 gate
design/                             tokens.json -> CSS + Swift; darkroom mockup
```

### Bugs worth remembering
1. **Slang binding indices are cumulative across a module.** Compiling all
   kernels into one metallib gave kernel 2 textures at index 2/3 and kernel 3 at
   4/5, while the host binds from 0 every dispatch — so every kernel after the
   first read unbound slots and produced black. Fix: **one metallib per kernel**.
   Do not "optimize" that back into a single module.
2. **The camera matrix must be row-normalized.** Without it, white balance and
   the color matrix fight: the data is already neutral after WB, and an
   unnormalized matrix re-tints it (we had a magenta cast). dcraw normalizes
   rgb_cam for the same reason.
3. **One geometry, one function.** The renderer, the crop overlay and hit
   testing each computed the photo's on-screen rectangle for themselves, and
   drifted apart — handles landed on a rectangle the pixels were not drawn in.
   `app/CanvasLayout.swift` is now the only copy, and the *engine is given*
   the preview canvas rather than deriving a second one. Same class of bug in
   the shader: the straighten pivot was derived from cropOrigin/cropSize,
   which describe the canvas rather than the user's rectangle, so the preview
   turned about the frame center and the committed render about the crop
   center. Pass the pivot, do not derive it.
4. **The crop must stay inside the turned frame.** Nothing enforced it, so a
   straightened export had transparent wedges in its corners — the crop is
   what gets sampled, and it reached past the picture. `constrainedCrop`
   shrinks and recenters it, which is what Lightroom does. A fixed preview
   canvas could not hold a steep angle either: a 3:2 frame at 45 degrees
   reaches 1.77x its short side, so the old constant 1.42 clipped corners past
   about 17 degrees. The canvas is now computed per angle and aspect, and
   sampled into a frame-sized texture so its cost stays flat.
5. **A `Path` view takes the size it is offered.** An unsized one inside a
   `.position()` grows to the whole overlay, and `.position` then centers
   *that* — which threw the crop corner marks into the middle of the window.
   Give hand-drawn marks a fixed `.frame`.

## Settled

See `DECISIONS.md` for the full list with reasoning. Headlines:

- C++20 engine, Metal GPU, Slang shaders. **No Rust, no Vulkan.**
- Compute DAG, one shader per node, `rgba16f` linear Rec.2020, scene-referred, pixels stay on GPU.
- XMP sidecars = truth, SQLite = disposable rebuildable index. Folder-based, no catalog.
- macOS first. Sony ARW only for v1.
- RCD demosaic, AgX-family sigmoid tone mapper, profiled wavelet denoise.
- Maintainability is a hard constraint (solo dev): small shaders, 3-file feature changes.

## In flight

**Nothing in flight.** UI shell decision is closed — see `UI-DECISION.md`. Planning is complete enough to start coding.

⚠️ Session limit and the 200-call web-search budget were both exhausted on 2026-07-27. **Do research inline and sparingly** — the developer asked for fewer subagents, and they proved fragile at this scale.

## Blocked / needs a decision from the developer

1. ~~UI shell~~ ✅ **Resolved: SwiftUI/AppKit + C++ engine** (decision #25). Qt was picked then reversed — see `UI-DECISION.md` for why.
2. **License / business model** — undecided by choice. Building to keep both doors open: avoid GPL libraries, dynamically link LGPL ones. Revisit before v1 ships.

## Scope — locked 2026-07-27

Every feature now has a milestone. Notable calls:
- **Cut from v1:** card import (point at a folder instead), brush masking, keywords/search.
- **Local edits land in M4**, gradient + luminance/color-range masks + AI subject/sky. Spot removal kept.
- **Bilateral grid + BGU pulled forward to M1** — built before needed, as the escape hatch for the latency budget.
- No tethered shooting.

## M1 progress

Done: white balance (real Kelvin, as-shot on open), exposure, highlights,
shadows, whites, blacks, vibrance, saturation, contrast, tone curve, and
export (JPEG/PNG/TIFF with quality and resize). The app has all of them.

Remaining in M1: crop/rotate/straighten · XMP sidecars and the non-destructive
op stack · undo/redo and history · folder browse, filmstrip, ratings and
filtering · the SQLite index.

## M2 progress

1. ✅ **Tone curve** — `pipe/ToneCurve.{h,cpp}` evaluates the same monotone cubic
   Hermite spline as the mockup into a 256x4 LUT (master, R, G, B); the shader
   samples it. Runs after AgX, in display space.
2. ✅ **Color mixer** — eight hue bands with hue/saturation/luminance each,
   in `shaders/ops/hsl_ops.slang`. Weights overlap smoothly (60° falloff, squared)
   so a gradient crossing between bands does not band. Folded into the fused
   scene-linear kernel, so it costs no extra pass.
3. ✅ **Sharpening** — unsharp mask with detail masking, placed immediately after
   the demosaic. Upstream position is deliberate: dirt only flows downstream, so
   an exposure drag never recomputes it.
4. Profiled wavelet denoise + a per-camera noise profile
5. Lens corrections via lensfun
6. Before/after split — the mockup's Compare interaction

### Known gaps to close in M2
- Demosaic is **RCD-family, not a faithful RCD port** — directional +
  gradient-corrected + clamped, which is genuinely good but not the reference
  algorithm. Revisit against https://github.com/LuisSR/RCD-Demosaicing
- Highlight reconstruction is not implemented at all (clip only)
- The pipeline runs at full resolution; the preview-ROI path in ARCHITECTURE.md
  is designed but unbuilt. Not needed yet on an M4 — will be on lesser GPUs
- Black level ignores LibRaw's 2D cblack pattern (averaged instead)
- AgX output is sRGB-encoded; the EDR/P3 path is not wired up

## Culling — where the controls are

Rejection was reported broken three times and was never reproducible from the
code, because the failure was focus, not logic: the `x` handler was an
`onKeyPress` on the editor's root view, and the Metal canvas takes first
responder on any click. Culling now lives in a **Photo menu** (`PhotoCommands`
in `OrionApp.swift`), published through `@FocusedValue(\.cull)`. Menu shortcuts
route through the responder chain, so they work wherever focus sits — and the
shortcut is written next to its name instead of having to be known in advance.

R rejects · 1–5 rate · ⌘0 clears · ← → browse · 0 fits · 9 is actual size ·
⏎ applies a crop · ⎋ cancels one · ⌘R resets adjustments.

## Notes for whoever picks this up

- The developer wants **evidence, not agreement**. When they express skepticism about a technology, research it honestly — they explicitly asked to have their assumptions tested.
- Keep planning docs concise. Dense tables, not essays.
- The most important research finding is **Bilateral Guided Upsampling** (`RESEARCH.md` §4) — it is the general solution to "this algorithm is too slow to be interactive" and should be a DAG node type built in M1, before it's needed.

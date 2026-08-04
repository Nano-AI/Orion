# Highlight reconstruction — the segmentation question

Written 2026-08-01, for ROADMAP's last unbuilt M3 item, "segmentation-based
highlight reconstruction". Covers what is already built, what the literature
actually offers, and why the piece that landed is a solver rather than a
segmenter.

`tone-and-local-contrast.md` §"Highlight reconstruction" holds the entry for the
node that ships today; this file is about what sits beyond it.

---

## 0. A citation correction, made first because it is the point of the folder

Every mention of the cross-channel paper in this repository named its third
author **Tang**. The third author is **Marshall F. Tappen**.

> S. Z. Masood, J. Zhu and M. F. Tappen, "Automatic Correction of Saturated
> Regions in Photographs using Cross-Channel Correlation", *Computer Graphics
> Forum* **28**(7), 1861–1869, 2009.

Verified against the reference list of Rouf et al. 2012 (entry [16]) and against
the paper's own listing. Corrected in `README.md`,
`research/tone-and-local-contrast.md`, `research/deep-research-2026-07-27.md`,
`engine/shaders/highlights.slang` and `engine/src/pipe/DevelopPipeline.cpp`.

The likely origin is He, Sun & **Tang**, who are cited correctly in five other
places here for the guided filter and the dark channel prior. That is exactly
the failure this folder exists to catch: a plausible name, wrong, unchecked,
copied forward five times. A citation nobody can look up does not do the job the
rule was written for.

---

## 1. What is built, and precisely where it stops

`engine/shaders/highlights.slang` implements Masood et al.'s cross-channel
model: within a neighbourhood the channels are close to linearly related, so
fit `C_c = α·C_u + β` by ordinary least squares over the still-valid pixels and
use it to say what the clipped channel would have read.

Two limits are stated in its own entry as deliberate deviations, and both are
load-bearing here.

**Reach.** `kRadius = 12`, `kStride = 3`. A clipped pixel more than 12 pixels
from a valid one has no sample to fit against, `n < kMinSamples` fires, and the
shader returns its input. On a 6024×4024 frame a blown window, a lamp globe or a
blown sky is hundreds of pixels across, so the node is silent over almost all of
it. Measured in `testHighlightFillGpu`: on a 140-pixel blown disc the recovered
centre comes back at R/B = 1.000, which is the input, unchanged.

**All three channels clipped.** The `count == 3` branch pushes the pixel to
neutral. Under decision #29 that branch is a **literal identity**: `linearize`
clips every channel to one common ceiling before demosaic, so a fully blown
pixel already arrives as `(clip, clip, clip)` and `max` of the three is `clip`.
Nothing happens, and nothing can.

So the coverage is:

| Region | Notation (Rouf et al. §3.1) | Today |
|---|---|---|
| Some channels clipped, within 12 px of valid data | Ω<sup>∪</sup> \ Ω<sup>∩</sup> | **Recovered** by the window fit |
| Some channels clipped, further than 12 px | Ω<sup>∪</sup> \ Ω<sup>∩</sup> | Declined |
| Every channel clipped | Ω<sup>∩</sup> | **Untouched, at any distance** |

⚠ Under decision #29 these sets are not exotic. White balance scales red and
blue up relative to green and the common ceiling is the *lowest* per-channel
level, so with gains around (2.2, 1.0, 1.6) red meets the ceiling at 45% of the
sensor's own red saturation and blue at 62%, while green only meets it at 100%.
Every bright neutral highlight therefore has a genuine partial-clip annulus —
which is where the window fit earns its keep — around a fully clipped core that
nothing addresses.

---

## 2. The candidates

| Work | Venue, year | What it does | Verdict here |
|---|---|---|---|
| Masood, Zhu & Tappen | Computer Graphics Forum 28(7):1861–1869, **2009** | Segment the image into non-saturated / partially saturated / totally saturated; fit cross-channel linear models per region; edge-stopping interpolation for the rest | **Built, in its windowed form.** The un-built part is the region, not the model |
| Zhang & Brainard | JOSA A 21(12):2301–2310, **2004** | Bayesian: multivariate normal prior over channel correlation, estimated from the image's own unsaturated pixels, gives a MAP estimate of the clipped channel | Rejected — see §4 |
| Guo, Cheng, Zhuo & Sim | Proc. CVPR 2010, 515–521 | Correcting over-exposure by propagating colour and lightness into clipped regions | Rejected — the authors' method involves user intervention (Rouf et al. §2) |
| Elboher & Werman | CGVCVIP, **2010** | Recovering colour and details of clipped image regions | Not pursued; superseded in scope by Rouf et al. |
| **Rouf, Lau & Heidrich** | International Workshop on Projector-Camera Systems (**PROCAMS**) **2012** | Gradient-domain: Laplace-interpolate a smooth hue over the clipped region from its boundary, then transfer detail from unclipped channels by a Poisson solve | **Chosen.** See §3 |
| DNG Specification 1.7 | Adobe, dated | Defines `WhiteLevel`, `BaselineExposure`, `LinearizationTable` — the *inputs* to a reconstruction | Not a reconstruction algorithm. It says where clipping is, not what to do about it |
| dcraw / LibRaw `-H` modes | Coffin, ongoing | Mode 0 clip (built, #29); modes 1–2 unclip/blend; mode 3+ "rebuild" | Mode 0 is what Orion does. The rebuild modes are undocumented heuristics with no published derivation |

**Not consulted, and deliberately:** darktable's `inpaint opposed` and
`guided-laplacian`, and RawTherapee's equivalents. Both projects are GPL and
both have this feature. `deep-research-2026-07-27.md` §1 restates darktable's
approach from its *published description*, which is legitimate, but nothing in
this file or in the code that came out of it was taken from either source tree.

---

## 3. The method chosen — and why it deletes the segmentation step

> M. Rouf, C. Lau and W. Heidrich, "Gradient Domain Color Restoration of Clipped
> Highlights", *International Workshop on Projector-Camera Systems (PROCAMS)*,
> 2012.
> <https://www.cs.ubc.ca/labs/imager/tr/2012/GradientDomainColorRestoration/>

Implemented from the paper's description. No code was copied; the paper ships no
implementation and the authors' own was not consulted.

### The formulation

With `f` the captured image and `f*` the intrinsic one, `f = min(1, f* + n)`.
Define `Ω_k` as the pixels where channel `k` is clipped, `Ω^∪ = ∪_k Ω_k` and
`Ω^∩ = ∩_k Ω_k` (§3.1). Three steps:

**§3.2 — hue interpolation.** A smooth colour estimate `ρ` over the clipped
region, from its own boundary:

```
∇²ρ = 0    over Ω^∪,    ρ|∂Ω^∪ = f|∂Ω^∪
```

**§3.3 — cross-channel detail transfer.** With `ρ` known, a clipped channel is
tied to an unclipped one by the local hue: `f*_j = (ρ_j/ρ_k)·f_k`, whose
gradient form is `g*_j = (ρ_j/ρ_k)·g_k`, recovered by

```
∇²f*_k = ∇·g*_k   over Ω_k,   f*_k|∂Ω_k = f_k|∂Ω_k
```

With several unclipped references available, their gradients are combined by a
confidence weight (Eq. 7) using a piecewise cubic with an off-centre peak
`m = 0.65`, `ε = 10⁻³` (Eq. 8).

**§3.4 — gradient fill-in for Ω^∩.** Where every channel is clipped the captured
gradient is flat and the reconstruction would be a plateau with a visible Mach
band at its rim. Two further Poisson solves, **in log space**, extrapolate a
smooth gradient across it (Eqs. 9–10), which in linear space is a Gaussian
falloff — the shape a real blown highlight has.

### ⚠ The finding that decides the architecture

**A Dirichlet solve is intrinsically region-scoped, so the connected-component
pass is not needed.**

Nothing in `∇²ρ = 0 over Ω^∪ with ρ|∂Ω^∪ = f|∂Ω^∪` crosses a pixel outside
`Ω^∪`. Each connected blown region is therefore already solved on its own, from
its own boundary, with no information leaking between two lamps at opposite ends
of the frame — which is the entire behaviour "reconstruct the region as a
connected component" was asking for. Labelling the components would produce the
same answer more slowly.

This matters because a connected-component pass is the one shape that does not
fit this project. Union-find is a CPU algorithm over a 24 Mpx buffer behind a
readback stall; the GPU alternatives (Playne–Hawick, Komura) are iterative
label-propagation passes whose count depends on the *content*, and Orion's graph
is static. Rouf et al.'s formulation removes the requirement rather than
satisfying it.

**So the roadmap item is renamed rather than dropped.** What was wanted was
region-scoped reconstruction with a propagated gradient. That is what this is.
It is not segmentation, and calling it segmentation is what made it look
un-buildable.

---

## 4. Why not Zhang & Brainard

Their prior is a multivariate normal over channel correlation, **estimated from
the image's own unsaturated pixels** — a global statistic. Two objections, and
the first is fatal here.

⚠ **It is the exact shape of the bug that shipped.** A global channel-correlation
prior fitted on a night frame is fitted on the dark warm background, because that
is nearly all of the frame; extrapolating it to highlight brightness is what put
a purple halo around every light and is why `kShoulder` exists in
`highlights.slang`. Rouf et al. reach the same conclusion by measurement — their
Figures 4 and 7 show Zhang & Brainard reconstructing a curtain well and failing
on a neon sign "due to the absence of a localized model".

Second, it does not address Ω<sup>∩</sup> at all, which is the gap that actually
remains.

---

## 5. What was built — the solver, and nothing else

`hl_pull.slang`, `hl_push.slang`, `engine/src/pipe/HighlightFill.h`.

The Dirichlet fill of §3.2, as the pull-push interpolant of

> S. J. Gortler, R. Grzeszczuk, R. Szeliski and M. F. Cohen, "The Lumigraph",
> *SIGGRAPH '96*, 43–54, §3.5.1.

Pull the known values up a pyramid carrying their weight; push them back down
into the gaps. O(N) with two small kernels, and it reaches across a hole of any
width — which is precisely what the 12-pixel window cannot do.

**Storage is premultiplied**: `rgb = w·f`, `a = w`, with `w = 1` where the colour
is known and `0` in the hole. The push is then exactly premultiplied
source-over, `f + (1−f.a)·up`, and every value in the pyramid is a convex
combination of known pixels — the discrete maximum principle, which is why the
fill cannot invent a colour outside the range of the rim it read. The window fit
needs three explicit clamps (`kMaxGain`, `kMaxExtrapolation`, `maxRatio`) to get
the same guarantee.

### ⚠ Deliberate deviations, stated

- **Pull-push, not multigrid.** Rouf et al. solve with multigrid, which carries a
  residual correction this does not; pull-push is therefore an *approximation*
  to the harmonic solution rather than that solution. **Measured, not assumed**:
  `testHighlightFillGpu` runs a Gauss-Seidel reference to convergence over the
  same fixture and prints the deviation every run. It is **0.0368, 6.1% of the
  rim's own span**, on a 68-pixel round hole with a rim varying in all three
  channels. If a later piece needs better, the V-cycle is the same two kernels
  with a residual pass between them.
- **No `min(1, Σ)` weight cap**, which Gortler et al.'s pull has. Their weights
  are scattered-sample confidences that can sum past one. Here the taps are a
  partition of unity, so the pull is an *average* of weights already in [0,1] and
  cannot exceed one. ⚠ This was written first, and removed when deleting it
  changed no measured number to seven digits — an unreachable branch reads as a
  guard somebody is relying on. Rouf et al.'s Eq. 8 confidence is also on [0,1],
  so the later pieces do not make it reachable either.
- **Bilinear synthesis, hand-rolled, not a hardware sampler.** Same reason
  decision #81 gives for the grain plate: filtering precision is unspecified
  across GPU families, so preview and export could differ by device. Bilinear
  rather than Burt & Adelson because the tent is the filter Gortler et al.'s push
  actually uses, so this is the cited pairing and not a compromise.

### Measured

| Check | Result |
|---|---|
| Constant rim fills with that constant | worst 2.98 × 10⁻⁷ |
| Shader vs its host twin in `HighlightFill.h` | worst 1.79 × 10⁻⁷ |
| Maximum principle — excursion beyond the rim's range | 2.38 × 10⁻⁸ |
| Deviation from the Gauss-Seidel harmonic solution | 0.0368 = 6.1% of rim span |
| 140 px blown core: `highlightRecover` | R/B **1.000** — declines, unchanged |
| 140 px blown core: the fill | R/B **4.091** against a rim of **4.091** |

### The mutations

Recorded because two of the three are instructive about the tests rather than
about the code.

| Mutation | Effect |
|---|---|
| Drop half-texel centering in the push (`(x+0.5)·0.5−0.5` → `x·0.5`) | **2 failures** — twin disagreement 0.146, harmonic deviation to 29.9% |
| Source-over → plain add in the push | **2 failures** — twin disagreement 6.55, harmonic deviation to 17.6% |
| Truncate the pyramid to 4 levels | **Runs as a check on every build**, not as a one-off: the hole's centre must stay unresolved |
| Remove the `min(1, Σ)` cap | **No effect, to seven digits** — which is how the cap was found to be unreachable and removed |

⚠ The constant-rim check survives the first two mutations. A uniform scale and a
half-texel shift both leave a constant field constant, so it cannot see either.
That is why the twin and the harmonic reference are there: the cheap invariant
proves the normalization, and only a reference implementation proves the filter.

---

## 5b. What was wired — pieces 2 and 3

`hl_mask.slang`, `hl_apply.slang`, and 24 nodes between `highlights` and the
denoise chain. **149 → 173 nodes, 6971 → 7186 MiB.** Off at
`highlightRecovery = 0`, which is the default.

### ⚠ The pyramid runs at a quarter resolution, and that was measured first

ROADMAP costed piece 3 at **+25 nodes and ~516 MB**, at full resolution, and that
number was the reason this was a three-session item. `ρ` is harmonic — it has no
detail to lose, and the only place it moves quickly is the rim, which the apply
pass reads at full resolution anyway — so the factor was swept against the same
Gauss-Seidel reference the solver itself is judged by, before any node was
written. `testHighlightFillGpu` prints all five every run:

| Solved at | Hole, in coarse texels | Worst deviation | Of rim span |
|---|---|---|---|
| 1/1 | 68 | 0.0368 | 6.1% |
| 1/2 | 34 | 0.0376 | 6.3% |
| **1/4** | **17** | **0.0416** | **6.9%** |
| 1/8 | 8 | 0.0523 | 8.7% |
| 1/16 | 4 | 0.0758 | 12.6% |

A quarter costs 0.8 points on top of an approximation already worth 6.1 — less
than the pull-push approximation itself — for a sixteenth of the memory. Both
ends are pinned by a check, so the factor is a measured choice and not a free
one. Decision #102.

⚠ **What it does not buy is node count.** The level count is logarithmic in the
frame, so a quarter removes two levels and nothing else: 24 nodes where the
estimate said 25. The estimate's node number was right for the wrong reason, and
its memory number was wrong by 16×.

⚠ **And the real cost is not the pyramid.** Of the +215 MiB, the pyramid is
**30 MiB** and the apply node is **185 MiB** — one full-resolution `RGBA16Float`
pass, which no factor subsamples away. Decision #96 measured the same 194 MB for
the creative vignette and fused it into the grade rather than pay it; there is
nothing to fuse into here, because the fill must land after `highlights` and
before the denoise.

### The mask is `Ω^∩`, not the `Ω^∪` §3.2 nominally solves over

§3.2's `ρ` is defined over the union. Filling the union here would be a
regression, because the union's partial case is exactly what `highlights.slang`
already does — Masood et al.'s cross-channel fit, per pixel, from real evidence —
and replacing measurement with a smooth interpolant is strictly worse. §3.3 is
the step that wants the union, and it wants it *per channel*, which is a
different mask from a different kernel.

The mask also requires the **shoulder**: known means not blown *and* brighter
than `kShoulder · clip`, at `highlights.slang`'s own 0.35 and for its own reason.
It is what stops the night sky being read as evidence about the lamp, and it is
what makes the box restriction safe — a one-pixel annulus in a coarse texel would
otherwise be averaged with the sky. ⚠ It does not break decision #96's region
scoping: `Ω^∩` is enclosed by its own shoulder ring, which is known, so a core is
solved from its own rim regardless of what the background is marked as.

### What it leaves

A **plateau**. `ρ|∂Ω = f|∂Ω` makes the join continuous, so there is no Mach band
at the rim — but a harmonic function inside a roughly circular rim is nearly
flat, so a blown lamp comes back with its rim's color and none of its falloff.
That is §3.4's job and it is piece 5. Rescaling `ρ` here to imitate a falloff
would be inventing the shape their paper derives.

### Measured, in the graph

`testHighlightFillWiring` builds a real `DevelopPipeline` on a night frame in
miniature: a lamp 96 px across saturated in every channel, a warm annulus where
only red is clipped, a dark warm background. The existing recovery fails on it by
construction — `count == 3` is an identity, and the core is 48 px from the
nearest valid pixel against a 12 px reach.

| Check | Result |
|---|---|
| Core chromaticity, recovery off | (0.3333, 0.3333) — neutral |
| Core chromaticity, recovery on | (0.6525, −0.0452) |
| The annulus's own chromaticity | (0.6796, −0.0462) |
| Distance from the rim's color | **0.5137 → 0.0271** |
| An unclipped corner pixel | unchanged, bit for bit |

### The mutations

| Mutation | Effect |
|---|---|
| `filling = true` — the chain never switches off | ⚠ **Passed the bench as written.** The fill is upstream of exposure, so once run it stays cached and a drag never touches it. The check moved to the full render after the control goes to zero; it now reads 24 fill nodes and exits 1 |
| Drop the mask's shoulder rule | ⚠ **Passed every check as written**, at 0.0687 against a bound of 0.25. The wiring test gained the tighter check it fails: 0.0528 clean, bound 0.06 |
| Re-push the pyramid's params per tick | 23 fill nodes on an exposure tick — decision #92's shape exactly |
| Apply the fill outside `Ω^∩` | "an unclipped pixel is returned untouched" fails, delta 0.082 |
| Hand the apply the picture's size as the fill grid's | **No effect.** The clamp binds only at the frame's right and bottom edge, and premultiplication makes even the out-of-bounds read harmless: `rgb` and `a` are attenuated together, so `rgb/a` survives it. Recorded rather than patched, like `hl_pull`'s unreachable weight cap |

⚠ Two of five mutations found a check that could not fail. Both were checks
written against the state that was easy to measure — the drag, and the
feature-level distance — rather than against the thing the code actually
promises.

---

## 5c. What was built — piece 4, §3.3, at zero new nodes

`hl_mask.slang` and `hl_apply.slang`, edited. **173 nodes and 7186 MiB, both
unchanged.** Decision **#109**.

### ⚠ ROADMAP's cost was wrong, and it was wrong in the word "reuse"

The piece table read *"the pyramid can be reused: piece 3's is 30 MiB and its
shape is a function of the frame, so a second solve is +23 nodes and +30 MiB,
not another 215"*, and that is what made piece 4 look like the cheaper of the two
remaining pieces. It does not survive being checked against the shipped code.

**Piece 3's `ρ` carries no information anywhere §3.3 operates.** `hl_mask.slang`
writes `Ω^∩` as the hole; every pixel outside it and above the shoulder is
*known*, with `w = 1` and `rgb = f`. So over `Ω^∪ \ Ω^∩` — §3.3's entire domain —
`ρ ≡ f` exactly. `hl_apply.slang`'s own comment says so in as many words: at a
pixel the solver treated as known, "`rho == f` and the write is a no-op". §3.3's
estimate `f*_k = (ρ_k/ρ_j)·f_j` therefore evaluates to `(f_k/f_j)·f_j = f_k`, the
identity. There is nothing to reuse; §3.3 needs a `ρ` over a **different hole**,
which is a different mask.

**And pull-push cannot solve §3.3's equation at all.** §3.2 is Laplace with a
Dirichlet condition, which is what a pull-push interpolant approximates —
measured at 6.1% of rim span against Gauss-Seidel. §3.3 is **Poisson with a
source**, `∇²f*_k = ∇·g*_k`. Pull-push has no residual and no relaxation, so
there is nowhere to put `∇·g*_k`; a real V-cycle is a different chain with a
relaxation kernel per level. Substituting `f* = r·f_j + u` for `r = ρ_k/ρ_j`
gives

```
∇²u = f_j ∇²r + ∇r · ∇f_j
```

which vanishes only where `r` is constant — and the neglected `∇r·∇f_j` is of the
size of the detail being transferred. So "reuse the pyramid" was never §3.3; it
was §3.3's *model* with the integration dropped.

### The measurement that came before the design

Block **3e** of `apps/bench` counts the clip sets on the node's own two sides,
against the ceiling the node itself was given. Three real frames:

| | `_PIC8220` | `_PIC8095` | `_PIC8148` |
|---|---|---|---|
| `Ω^∩` | 112,618 (0.465%) | 54,704 (0.226%) | 17 |
| `Ω^∪ \ Ω^∩` | 86,894 (0.359%) | 89,415 (0.369%) | 77 |
| ...returned untouched by the window fit | 59,791 (69%) | 69,701 (78%) | 77 |
| ...and beyond its 12 px reach | 5,608 | 16,578 | 0 |
| what §3.3 would move those by, vs clip 1.0 | mean **0.221** | mean **0.141** | — |
| deepest blown core | 86 px | 61 px | 1 px |

⚠ **The set §3.3 newly serves is 0.023%–0.068% of the frame**, which on its own
does not buy +23 nodes. The number that changed the answer is the next row:

| | `_PIC8220` | `_PIC8095` |
|---|---|---|
| the ring that supplies `ρ` for **every** blown core | 11,901 px | 20,563 px |
| ...beyond the window fit's reach | 5.6% | 22.7% |
| ...returned untouched by it | **58%** | **69%** |

The pixels bounding a blown core are the *innermost* ring of the partial-clip
annulus, and piece 2 handed them to the solver as Dirichlet data while they were
still clipped. **Every core in the frame was being solved from a rim that was
itself wrong**, by a mean of 0.14–0.22 of the clip level. That is not a new
feature, it is a correction, and it does not need any of the +23 nodes.

### What was built

⚠ **Two sets that piece 2 conflated, separated.** Which pixels the fill may
**write** and which pixels are trustworthy **evidence** are different questions.
Piece 2's rule about the first is right and is untouched — Masood et al.'s
per-pixel fit is a measurement, this is an interpolant, and where both exist the
measurement wins. Nothing about that argument says a *still-clipped* pixel is
evidence.

- **`hl_mask.slang`** — the hole is now the part of `Ω^∪` the window fit did not
  recover. "Recovered" is read off the node rather than off a threshold: the
  kernel takes both sides of `highlights` and a channel is a hole where it
  arrived clipped and was not lifted, `raw_k ≥ limit ∧ ¬(rec_k > raw_k)`. A level
  test would need a constant to separate a lifted channel from a blown one, and
  demosaic ringing round a clipped edge puts pixels either side of any such
  constant.
- **`hl_apply.slang`** — where every channel is a hole, `ρ` is written, exactly as
  piece 3 did. Where some channel never clipped, §3.3's model runs instead:
  `f*_k = (ρ_k/ρ_j)·f_j`, averaged over every channel that never clipped, so the
  pixel keeps its own measured green and blue and `ρ` only supplies the hue.
  Clamped to `[f_k, kMaxGain·clip]`, both bounds `highlights.slang`'s and cited to
  the same place.

**Cost: no node, no texture, no pyramid level.** `nRgb_` already existed and was
already live at that point in the graph, so asking for it costs a binding.

### ⚠ It reopens decision #29 by a different route than #106, so it owes a
### different argument

#106 leans on the maximum principle: `ρ` cannot leave the range of the rim it
read, so a neutral rim gives a neutral core. **That does not cover this branch.**
`f*_k = (ρ_k/ρ_j)·f_j` multiplies a rim-derived ratio by the pixel's *own*
measured channel and can exceed the rim's range in level — which is the point,
since a recovered highlight should roll off through the display transform rather
than sit flat.

What is bounded is the **chromaticity**. `ρ` is a convex combination of known
colors, so `ρ_k/ρ_j` is a ratio the rim actually exhibited; the level comes from
evidence at that pixel and from nowhere else. A neutral rim gives `ρ_k = ρ_j` and
hence `f*_k = f_j`, which is neutral — #29's outcome, reached again by evidence
rather than by decree. And under #29's own clip the domain is *empty* at a
neutral white balance, because equal gains put `Ω_R = Ω_G = Ω_B`. The clip itself
is untouched: still pre-demosaic, still before RCD interpolates across it.

### Measured, in the graph

`testHighlightFillWiring`'s fixture gained a bright **wholly unclipped** ring, and
its partial ring went from 16 px wide to 32 so that it straddles the window fit's
reach. Both were necessary and neither was cosmetic:

- ⚠ **Without the clean ring the test's target was itself clipped.** Every ring in
  this fixture is neutral at the sensor, so the truth about the lamp is one
  number — the white balance gains, 2 : 1 : 1.5 — and the partial ring reads
  1.00 : 0.61 : 0.92 because its red hit the ceiling. The old test asked whether
  the core took *the partial ring's* color, which is exactly the error piece 4
  removes. The error was inside the target.
- ⚠ **Without the wider ring the window fit recovered nothing at all**, so two
  mutations that overwrite its answer were invisible.

| Check | Result |
|---|---|
| Core distance from the lamp's own color, off → on | 0.5756 → **0.0442** |
| Partial ring, off → on | 0.0836 → **0.0221** |
| Green and blue at a §3.3 pixel, across `hl:fill` | **bit-identical** |
| Red at the same pixel | 1.000 → 1.14, off the ceiling |
| A pixel the window fit recovered | **bit-identical**, all three channels |
| Radial red after the window fit | at the ceiling to 72 px, lifts at 76 |

⚠ The "green and blue untouched" check was written against `referenceImage()`
first and **failed on correct code**: that texture is downstream of the camera
matrix, which mixes the channels, so moving red moves Rec.2020's green and blue
with it. The claim is about `hl_apply`'s own space and had to be read there.

### The mutations — and three of the seven found something about the tests

| Mutation | Effect |
|---|---|
| `hl_mask`: hole reverts to `Ω^∩` | **3 failures** — §3.3 becomes the identity (0.0836 → 0.0836), and the core's own accuracy degrades because its rim is clipped again |
| `hl_apply`: write `ρ` into the partial ring instead of the ratio | **2 failures** — green 0.610 → 0.505 and blue 0.916 → 0.757, the measured detail discarded. This is the mutation the chromaticity checks alone cannot see |
| `hl_mask`: drop the shoulder rule | **1 failure** — the dark background is averaged into every coarse texel of the rim |
| `hl_apply`: predicate is a level test (`c ≥ limit`) rather than `rec > raw` | ⚠ **750 engine checks green, and the bench red at 13,135 pixels, exit 1.** The fixture's recovered ring is uniform, so `ρ` equals the picture over it and the ratio is the identity whatever predicate is used. A photograph has texture and `ρ` is a quarter-resolution box average of it, so the identity stops holding. The invariant moved to `apps/bench` block 3e for that reason |
| `hl_apply`: recovered channels count as reference (`!hole_k` for `raw_k < limit`) | ⚠ **Nothing red, and nothing can be.** The two spellings differ only at a pixel where one clipped channel was recovered and another was not, and `highlights.slang` declines *per pixel*: either every clipped channel is lifted, in which case `holes == 0` and §3.3 never runs, or none is. An equivalent mutation, recorded rather than papered over |
| `hl_apply`: drop the "may only raise it" floor | ⚠ **Nothing red.** The guard is reachable and the fixture does not reach it — the census says it binds on **150** and **645** channels on the two real frames |
| `hl_apply`: remove the `kMaxGain` ceiling | ⚠ **Nothing red**, and the census says it would cap **0** channels on either frame. Kept rather than deleted, and the difference from `hl_pull.slang`'s deleted weight cap is that that one was *provably* unreachable while this one is only unexercised: the ratio's denominator is `max(ρ_j, 1e-6)` and nothing bounds it below. `UNSOURCED.md` §28 |

### What piece 4 does **not** do

It is §3.3's model without §3.3's Poisson integration, so the recovered channel is
continuous with the picture only to the extent that `ρ` is. There is no seam by
construction the way `ρ|∂Ω = f|∂Ω` gives piece 3 one. Unmeasured, and named in
`UNSOURCED.md` §28 rather than implied away.

---

## 5d. Piece 5 — what must be settled before a line of it is written

⚠ **This section deliberately contains no discretisation.** §3.4 is summarised
above from the paper — two Poisson solves, in log space, extrapolating a gradient
across `Ω^∩` (Eqs. 9–10) — and that summary is as far as anyone should go from
memory. Writing a scheme here that *looks* like theirs would be the same error
§5b already names: *"rescaling `ρ` to imitate a falloff would be inventing the
shape their paper derives."* The purple cast this repository's sourcing rule
exists for came from exactly that kind of plausible construction.

What follows is the scaffolding: what the tree already provides, what it forbids,
and the questions the paper has to answer before code starts.

### What already exists and must be reused

- **A pull-push Dirichlet solver** — `hl_pull.slang` / `hl_push.slang`, after
  Gortler et al. (SIGGRAPH 1996 §3.5.1), with `pipe/HighlightFill.h` driving it.
  It solves `∇²ρ = 0` over `Ω^∪` with `ρ|∂Ω = f|∂Ω`. §3.4's solves are the same
  *class* of problem on a different field, so the pyramid is machinery to reuse
  rather than rebuild — which is what made piece 4 cost zero new nodes.
- **`Ω^∩` is already identified.** `hl_mask.slang` distinguishes the all-channels-
  clipped core from the shoulder where only some are, and §5b established the
  core is enclosed by its own shoulder ring, so it is solved from its own rim
  regardless of the background.
- **The node chain and its budget** — 11 pulls, 11 pushes, `hl_apply`. Piece 3
  was costed 16× wrong by assuming a new pyramid; the estimate to beat is
  whatever reuses this one.

### What the tree forbids

- ⚠ **The rim must stay continuous.** `ρ|∂Ω = f|∂Ω` is what makes the join
  seamless today — there is **no Mach band**, and that is not luck, it is the
  boundary condition. Anything §3.4 adds inside `Ω^∩` has to meet the existing
  solution at the boundary, and the regression to watch is a visible ring.
- ⚠ **No new control.** Piece 6 already argues one: the fill runs on
  `highlightRecovery`, which is plumbed end to end, and one control for both
  halves of one coverage is the honest default.
- **One node = one small shader**, 50–150 lines, and the citation lands with it.

### The target, as a number

`testHarmonicHighlightFillWiring` prints radial red after the window fit:
**1.00 at radii 40, 44, 48, 52, 56, 60, 64, 68 and 72**, then 1.18 at 76. Nine
samples flat at the clip value — that is the plateau, and **whatever §3.4 lands,
those nine must stop being equal.** The check already exists, so piece 5 does not
need a new oracle; it needs to move a number the suite already prints.

⚠ And the colour must not regress while the shape is fixed: distance from the
lamp's own colour is **0.5756 off, 0.0442 on**, against a truth of
(0.7631, −0.0496). Piece 5 changes the luminance profile; if that figure moves,
something has been broken rather than added.

### What the paper says — read 2026-08-04, and it settles three of the four

Rouf, Lau & Heidrich, *Gradient Domain Color Restoration of Clipped Highlights*,
PROCAMS 2012, **§3.4 "Gradient smoothing for fully clipped regions"** and **§3.5
"Discretization"**. Retrieved from the authors' copy at
`vccimaging.org/Publications/Rouf2012GDC/`.

**1. What is extrapolated, and with what boundary condition.** The **log-space
gradient field**, by a Laplace solve — their Eq. 9, hatted quantities being log
images:

    ∇²ĝ*_k = 0   over Ω_k,   with  ĝ*_k|∂Ω_k = ∇ log f_k|∂Ω_k

So the *gradient* is interpolated harmonically from the observed log-gradient on
the boundary. That is the step the current fill does not have — it interpolates
the **value**, which is why a roughly circular rim gives a flat interior.

**2. What the second solve integrates, and how the constant is fixed.** Eq. 10, a
Poisson solve whose Dirichlet condition *is* the constant of integration:

    ∇²f̂*_k = ∇ · ĝ*_k   over Ω_k,   with  f̂*_k|∂Ω_k = log f_k|∂Ω_k

then `f*_k = exp(f̂*_k)`. ⚠ **The rim therefore stays continuous by exactly the
mechanism it does today** — a Dirichlet condition matching the observed boundary,
in log space instead of linear. The invariant §5d flags as the regression to
watch is preserved *by the paper's own construction*, not by care in the port.

**3. Per-channel or joint — and this is the answer that changes the design.**
**One channel, not three, and only sometimes.** The paper: *"we generate
gradients for one of the channels over Ω^∩. We only apply this method if there is
one channel k whose clipping region is completely contained within the clipping
regions of the other channels, i.e. Ω^∩ = Ω_k."* It is also described as an
**optional pre-processing stage**. So piece 5 is a **conditional single-channel
pass** with a containment test to evaluate first — not three solves, and not
unconditional.

**4. Regions touching the frame edge — the paper does not say.** §3.5 defines the
boundary as *"unclipped pixels with at least one clipped pixel in their
neighborhood"*, which has no answer for a clipped region running off the frame:
there is no unclipped ring on that side to take a Dirichlet condition from. **The
implementer must decide and write down which**, and the existing pyramid's clamp
is the obvious candidate — but it is a choice this repository is making, not one
Rouf et al. hand over, and it must be labelled as such.

**Why log space, in their words.** It *"corresponds to a generalization of
fitting a Gaussian to the gradients"*: take the log of a clipped signal, solve
for a gradient that varies linearly across the region, integrate, and the log
image varies quadratically — which in linear space is a **Gaussian
extrapolation**. True Gaussians only for circular regions with rotationally
symmetric boundary gradients; other shapes come out asymmetric but still smooth.

**Also from §3.5, and easy to miss.** Gradients by divided differences over
4-connected neighbourhoods `N_p`. The Eq. 8 blending weights are computed
per-pixel but *applied* over a neighbourhood, so the paper **low-pass filters the
weights over that same neighbourhood with a minimum filter** — a detail that
belongs to piece 4's weighting and is worth checking against what shipped.

### ⚠ Measure the gate before building what sits behind it

§3.4 fires **only** when some channel `k` has `Ω^∩ = Ω_k` — its clipped region
wholly contained in every other channel's. Nothing in this repository knows how
often that is true of a real photograph, and **the answer decides whether piece 5
is worth its nodes**.

The measurement is small and belongs first. `hl_mask.slang` already computes the
per-channel `hole` at each pixel, so the statistic is: for each channel `k`,
count pixels where `hole[k]` is set and the other two are not — that is
`Ω_k \ Ω^∩`. **Containment holds for `k` exactly when that count is zero.**
Run it over the three bench frames and the `testHighlightFillWiring` fixture.

Three outcomes, and they lead different places:

- **Holds often** — build it as specified, single channel, conditional.
- **Holds rarely** — piece 5 fires on almost nothing, and the honest move is to
  say so in `ROADMAP.md` and spend the session elsewhere. A correct
  implementation of something that never runs is still a plateau on screen.
- **Holds only on the synthetic fixture** — the worst case, because the suite
  would go green on a feature no photograph exercises. That is the shape of
  every over-claimed check this project has already catalogued.

⚠ **The condition is a property of the *sensor and the scene*, not of the
algorithm**, so it cannot be reasoned about from the paper — Rouf et al. simply
state when their method applies. It has to be counted.

### ✅ Counted, 2026-08-04 — and it holds for nothing (#171)

`orion-rawstat` now reports it. Over 2×2 CFA blocks:

| frame | fully clipped | R only | G only | B only |
|---|---|---|---|---|
| `_PIC8220` | 27,530 of 6,060,144 (0.45%) | **18,364** | **7,190** | **3,143** |
| `_PIC8095` | 10,943 (0.18%) | **10,659** | **13,658** | **13,717** |
| `_PIC8148` | 5 (0.0001%) | 10 | 4 | 19 |

For `Ω^∩ = Ω_k` one of those columns must be **zero**. None is close. **Read
literally, §3.4 never fires on a photograph.**

⚠⚠ **The honest reading is that the test is meant per connected region, and this
count is global.** A frame can hold a blown lamp where red *is* contained inside
green and blue, and a red sign elsewhere where red clips alone — the global count
sees the sign and reports nothing, while the lamp qualifies perfectly. The paper
writes the test over the region sets without saying it is evaluated per
component, and nothing in it settles the question.

**So piece 5 cannot be gated globally. It must decide per region — and that is
this repository's choice, not Rouf et al.'s**, the second such alongside the
frame-edge case above. Both must be labelled as ours where they land.

⚠ The count is a **proxy**: raw CFA blocks, before demosaic and before the window
fit, so it over-counts clipping against `hl_mask.slang`, which calls a channel a
hole only where the fit failed to lift it. It answers *is this worth pursuing*.

⚠ And `_PIC8148` has **5 fully clipped blocks in six million** — it is not a
highlight frame, and any highlight number quoting it is quoting noise.

## 6. Honest limits

- **§3.4 is not built.** The fill puts a measured hue into a blown core and
  leaves it flat. Piece 5 in `ROADMAP.md`, and it is now the only visible gap.
- **§3.3 is built as its model, not as its solve.** The Poisson integration is
  dropped and replaced by a per-pixel clamp; see §5c and `UNSOURCED.md` §28.
- **Rouf et al.'s own stated failure case applies to Orion's sample frames.**
  Their assumption is that hue is independent of luminance over Ω<sup>∪</sup>, so
  the rim's hue describes the core. Their §4 shows it violated by sunsets, where
  hue and intensity are correlated and the correct core hue "is not observed
  anywhere in the image". Night frames with sodium lights are the good case; a
  sunset is the bad one, and the control must be able to be turned off.
- ⚠ **It reopened ground decision #29 settled, and the argument is
  decision #103**, written the day the node landed. #29's magenta was the white
  balance gains — the same magenta on every blown pixel of every frame, evidence
  of nothing — while `ρ` is the harmonic interpolant of the region's own rim and
  by the maximum principle cannot leave that rim's range. A neutral rim still
  gives a neutral core, so #29's *outcome* is reached by evidence rather than by
  decree, and #29 itself is untouched: the clip still happens, still pre-demosaic,
  still before RCD interpolates across it.
- **The headroom is gone before this node can see it.** #29 clips pre-demosaic,
  so the reconstruction works from clipped data and can only recover the shape
  and hue implied by the rim, never the sensor's original counts. Recovering
  above the ceiling is nonetheless meaningful in Orion specifically, because
  everything from the camera matrix to AgX is scene-linear and unbounded — a
  restored highlight rolls off through the display transform instead of sitting
  flat.

---

## History

- **2026-08-01** — Piece 4, §3.3, at **zero new nodes and zero new memory**.
  ⚠ ROADMAP's "+23 nodes and ~30 MiB, reusing the pyramid" was wrong in the word
  *reuse*: piece 3's `rho` equals `f` over §3.3's entire domain, and pull-push
  solves Laplace where §3.3 is Poisson. What the census found instead was that
  the ring supplying `rho` for every blown core was itself still clipped over
  58%/69% of its length, so this is a correction to piece 3 rather than a new
  feature. Decision #109. Three of seven mutations found something about the
  tests: one is red only on a photograph, one is provably equivalent, and two
  are guards the fixture cannot reach.
- **2026-08-01** — Pieces 2 and 3. `hl_mask.slang` and `hl_apply.slang`, 24 nodes
  wired between `highlights` and the denoise chain, off by default. Measured that
  the pyramid runs at a quarter resolution before spending ROADMAP's ~516 MB on
  it (decision #102), and argued the reopening of #29 (decision #103). Two of
  five mutations found a check that could not fail; both are recorded above.
- **2026-08-01** — Written. Corrected the Masood et al. author error across six
  files. Chose Rouf, Lau & Heidrich (PROCAMS 2012) over Zhang & Brainard and
  Guo et al. Recorded the finding that a Dirichlet solve makes the
  connected-component pass unnecessary, and built §3.2's fill as pull-push
  (Gortler et al. 1996) with its deviation from the harmonic solution measured.

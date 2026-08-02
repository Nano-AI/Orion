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

## 6. Honest limits

- **§3.3 and §3.4 are not built.** The fill puts a measured hue into a blown
  core and leaves it flat. Pieces 4 and 5 in `ROADMAP.md`.
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

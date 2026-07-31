# Local Adjustments and Masking

**Status: research only. Nothing here is built yet.** This is the plan of record
for M3's largest feature, written down so the next session starts from a
decision rather than a blank page.

**Provenance:** a deep-research run commissioned 2026-07-28 and reviewed here.
The primary papers it cites are ones I can vouch for independently (He/Sun/Tang,
Levin et al., Lischinski et al., Fattal, Gastal & Oliveira, Chen et al.). The
XMP `crs:` field-level details and the Apple Vision behavioural limits come from
that run and are **not independently verified** — they are marked ⚠ below and
should be checked against ExifTool output and a real device before being relied
on.

---

## The decision, in one paragraph

Build parametric mask primitives (linear gradient, radial gradient, brush dabs)
as ordinary per-node shaders; store strokes as normalized coordinates rather than
raster; combine masks into **one alpha per adjustment group** and apply the
adjustment **once**; refine edges with the guided filter Orion already has; and
apply everything in **scene-linear light before AgX**. Apple Vision supplies
subject and person selection. Sky does not exist as an API and needs its own
model later.

---

## 1. Mask primitives

### Linear gradient

Two points in normalized image coordinates: `(ZeroX, ZeroY)` where the effect is
zero, `(FullX, FullY)` where it is full. Direction **d** = Full − Zero. Angle and
position are both encoded by the two points, so there is no separate angle field.

```
t     = clamp( dot(p − Zero, d) / dot(d, d), 0, 1 )
alpha = smoothstep(0, 1, t)
```

### Radial gradient

Centre **c**, semi-axes (a, b), rotation θ, feather f ∈ [0,1]:

```
u =  cosθ·(p.x−c.x) + sinθ·(p.y−c.y)
v = −sinθ·(p.x−c.x) + cosθ·(p.y−c.y)
r = sqrt( (u/a)² + (v/b)² )          // r = 1 is the boundary
alpha = 1 − smoothstep(1 − f, 1, r)
```

Roundness raises the exponent from 2 toward a superellipse:
`r = (|u/a|ⁿ + |v/b|ⁿ)^(1/n)`.

### Falloff function

`smoothstep` (3t² − 2t³) is the right default — C¹, cheap, and what users expect
from Lightroom. **Perlin's smootherstep** (6t⁵ − 15t⁴ + 10t³) is C² and removes
the faint Mach band that smoothstep leaves at the feather boundary on clear
skies. Worth using internally even if the UI exposes one Feather slider.

Reference: Perlin, *"Improving Noise"*, ACM TOG 21(3), 2002 (where smootherstep
is introduced as the improved interpolant).

### Brush dabs

⚠ Per the research: strokes are a list of dab centres in normalized coordinates,
with **one radius per mask** rather than per dab, plus mask-level Flow and
CenterWeight. Each dab is a radial falloff stamp accumulated into the mask.

**Accumulate in R16F, not R8.** Many low-flow dabs stacking at 8 bits bands
visibly; this is the same class of error as the 8-bit resize that cost the export
its depth.

---

## 2. Where the alpha is applied — the part most likely to be got wrong

Three candidates, and the choice differs by adjustment type.

**Apply alpha to the *parameter* (Orion's default).** For a parametric
adjustment `f(x; θ)`, interpolate the parameter and evaluate once:

```
θ_eff = lerp(θ_identity, θ_full, alpha)
out   = f(x; θ_eff)
```

Artifact-free by construction: there is no blend between two rendered states, so
no gamma-incorrect mixing, no hue shift, no double application. For exposure it
is literally `x · 2^(alpha · stops)` — a smooth multiplicative ramp in linear
light. This suits a node graph better than blending, and it is what Orion should
do everywhere it can.

**Blend two rendered results in scene-linear.** `lerp(x, f(x), alpha)`, in
linear Rec.2020. Correct when `f` is not smoothly parameterizable — a LUT, or a
subject replacement.

**Never blend after AgX.** Mixing radiances in a display-encoded space darkens
the transition band, shifts hue as channels cross the nonlinearity at different
rates, and exaggerates edge contrast. Reference: NVIDIA, *GPU Gems 3*, ch. 24,
"The Importance of Being Linear".

The alpha itself stays a **linear coverage value** — that is the industry
convention even when RGB is encoded. Any perceptual shaping of the feather ramp
is an explicit curve on alpha, not a change of compositing space.

---

## 2b. What a mask is allowed to change, and what it is not

§2 settled *how* a local adjustment is applied: the alpha scales the
**parameter**, not the result, so half coverage at one stop is `2^0.5` — a
smooth multiplicative ramp in linear light rather than a blend of two rendered
frames. That rule decides which adjustments can be local at all, and the answer
is narrower than it looks.

### The test: is it pointwise?

An adjustment can be local exactly when it is a **function of the pixel alone**.
Then "scale the parameter by α" is well defined per pixel and costs one more
term in a node that is already running.

| Local | Why it works |
|---|---|
| Exposure | a multiply; `α·EV` in log is one add |
| Contrast | a gain on the pixel's distance from the pivot, in log |
| Saturation | a lerp toward the pixel's own luminance |
| Colour cast | a per-channel multiply |

### ⚠ White balance cannot be local, and the reason is structural

Temperature and tint are applied in `linearize`, at the **head of the graph,
before the demosaic** — because the demosaic interpolates white-balanced data
and the clipping level a channel saturates at moves with its multiplier. A
local white balance would mean demosaicing the frame twice and choosing between
the results per pixel, which is not an adjustment, it is a second pipeline.

So the local panel offers **Warmth** and **Tint** that are a *pointwise colour
cast* — a per-channel multiply applied where the mask covers — and they are
named differently from the global Temperature and Tint on purpose. They are not
the same operation, they do not have the same units, and calling them the same
thing would be the kind of lie that is only discovered when someone tries to
neutralise a colour cast with one and finds it cannot.

### ⚠ And the pyramid operators cannot be local either

Clarity, dehaze and exposure fusion are 16–32 node pyramids each. A per-layer
copy multiplies both the node count and the ~6.4 GiB of cached intermediates
that make an exposure drag 10 ms rather than 67. You also cannot "half-apply" a
Laplacian decomposition by scaling a parameter — the operator is not pointwise,
so §2's rule does not even define what it would mean.

The honest version, if it is ever wanted: render the pyramid **once** globally
and let the mask blend its output toward its input by `α × amount`. One pyramid,
one mask-weighted lerp. That is a different thing from a local clarity and
should be described as what it is.

### The contrast pivot

`−2.5` in log2, which is the number `develop_display` already pivots its base
contrast about, and middle grey (0.18) is `−2.47`. Sharing it means a local
contrast and a global one bend the picture about the same point; two pivots in
one program would make a local contrast of zero visibly different from no local
contrast at all near the crossover.

---

## 3. Brush masks under crop, straighten and rotate

**Store strokes parametrically, in the uncropped image's normalized space, and
re-rasterize.** Geometry edits are affine transforms of the sampling
coordinates, so each dab centre goes through the same matrix the image does. A
few kilobytes per mask, resolution-independent, and exact under any geometric
edit.

The alternative — a raster mask in image space — costs 24–120 MB per mask at
24–60 MP, must be resampled on every geometry change, and compounds blur because
an already-feathered mask is being re-interpolated.

**Hybrid for AI and refined masks:** keep the authored input parametric, cache
the *rendered* result (a Vision matte, a guided-filter output) as tiled sparse
R16F, and invalidate tiles on geometry change.

darktable does the parametric thing — shapes with control points in normalized
coordinates, rasterized on demand — which is useful confirmation. **It is GPLv3,
so that is an observation about its design, not a licence to read its source.**
`libmypaint` (ISC) is the copy-safe dab engine if one is ever wanted, though a
masking brush needs far less than its dynamics system.

---

## 4. Edge-aware refinement — reuse what is already here

**Guided feathering is the guided filter's own named application.** He, Sun &
Tang, *"Guided Image Filtering"*, ECCV 2010 (LNCS 6311, 1–14) and IEEE TPAMI
35(6):1397–1409, 2013, DOI 10.1109/TPAMI.2012.213, devote a section to refining
a binary mask into an alpha matte near object boundaries, and name Photoshop's
"Refine Edge" as the comparable feature. Their figure uses **r = 60, ε = 10⁻⁶**.

The mechanism is the coefficients Orion already computes:

```
aₖ = ( mean(I·p)ₖ − μₖ·p̄ₖ ) / ( σₖ² + ε )
bₖ = p̄ₖ − aₖ·μₖ
q  = ā·I + b̄
```

**The same aₖ, bₖ used for edge-preserving smoothing are the auto-mask
coefficients.** Only the *input* changes — from the image to the rough mask —
while the guide stays the image. `guide_ab.slang` and the box passes around it
need a second input binding and nothing else.

Uses: brush auto-mask edge snapping; feathering a hard AI selection; and joint
upsampling of a low-resolution Vision matte.

**Do not build a matting solver.** Levin, Lischinski & Weiss's closed-form
matting (CVPR 2006; TPAMI 30(2):228–242, 2008) solves a global sparse system;
Lischinski et al.'s scribble propagation (SIGGRAPH 2006, TOG 25(3):646–653) is a
global least-squares minimisation. Both need a linear solver, neither fits the
one-node-one-shader rule, and the guided filter is their local approximation —
visually comparable near boundaries at O(N) with no solve. Lightroom's own "Auto
Mask" is understood to be a colour-similarity and spatial-distance heuristic
rather than a matting solve, so the guided filter is faithful to the reference
behaviour rather than a compromise.

Alternatives considered and rejected for fit: edge-avoiding wavelets (Fattal,
TOG 28(3), 2009) needs a pyramid; the domain transform (Gastal & Oliveira,
SIGGRAPH 2011, TOG 30(4):69) is recursive and order-dependent, awkward in a
compute kernel; the bilateral grid (Chen, Paris & Durand, SIGGRAPH 2007,
TOG 26(3)) needs a 3D grid texture — heavier than what is already here.

⚠ **Patent:** the research found no patent covering the guided filter, and it
ships in OpenCV and MATLAB, which is strong evidence. That is a "none found",
not a clearance. PatchMatch is Adobe-patented (already known). Local Laplacian
filters (Paris/Hasinoff/Kautz 2011; Aubry et al. 2014, Adobe-affiliated) may
carry Adobe patents — check before implementing, though masking does not need
them.

---

## 4b. Range masks — a band on the photograph, not on position

A range mask selects by what a pixel *is* rather than where it is: "the bright
part of this", "the sky's blue". Lightroom's own range masks *refine* an
existing selection, and in Orion's model that falls out for free — a range
component composed with **intersect** (§6) is exactly that, and composed with
add it is a selection in its own right.

There is no algorithm here to cite and this section says so rather than dressing
one up. What there are, are three decisions that are easy to get wrong.

### Which image it reads

**The reference image — after white balance and the camera matrix, before any
user adjustment.** `DevelopPipeline::referenceImage`, the same texture the
colour picker samples, and for the same published-in-this-repo reason: read the
*edited* result and adjusting a masked band changes which pixels the band
selects, which is a feedback loop rather than a tool. Raise exposure through a
highlight mask and the mask would grow, so the exposure would rise further.

### Which luminance, and in what units

Rec.2020 luminance, `Y = 0.2627 R + 0.6780 G + 0.0593 B` — ITU-R BT.2020-2
(10/2015), Table 3, the coefficients already used by `guide_prep.slang` and
`develop_display.slang`. Using a different luma here than the rest of the
pipeline would mean two definitions of brightness in one program.

⚠ **The band is in log2 luminance — stops — not in linear light.** Scene-linear
luminance is unbounded and its interesting range is logarithmic: between 0.01
and 0.02 lies a stop, and so does everything between 4.0 and 8.0. A band of
fixed linear width is therefore an enormous selection in the shadows and a
sliver in the highlights, and a slider drawn over it would be unusable across
nine tenths of its travel. `guide_prep.slang` already works in log2 for the same
reason and says so.

This is the Weber–Fechner argument and it is not novel; it is written down here
because the *linear* version looks correct in code and fails only when a
photographer tries to drag it.

### The falloff

Perlin's smootherstep, shared with every other mask through
`ops/mask_ops.slang` — Perlin, *"Improving Noise"*, ACM TOG 21(3), 2002. One
edge ramps up, the other ramps down, and the two are independent so a band can
be a high-pass or a low-pass by pushing one edge past the frame's range.

C² rather than C¹ matters more here than for a gradient: a luminance band's
boundary follows the *texture* of the photograph rather than a smooth line
across it, so a discontinuity in the second derivative shows up as a mottled
edge through cloud or skin rather than as one clean Mach band.

### What is deliberately not built

- **A colour range mask.** ⚠ Built now — §4c, and the answer was neither of the
  two candidates this section named.
- **A bilateral grid.** `FEATURES.md` claimed range masks were cheap *because*
  M1 had built one. ⚠ M1 did not build one — there is no bilateral grid in the
  tree — and it would not be the right tool anyway: a range mask is pointwise,
  and the edge-aware part is already covered by §4's guided refinement, which a
  range component composes with like any other.

---

## 4c. Colour range masks — a band on chromaticity

§4b's band is on brightness. This one is on colour: pick a shade off the
photograph, select what is near it. Composed with **intersect** it refines
another component the way Lightroom's range masks do; composed with add it
stands alone.

Everything §4b decided applies here unchanged — it reads the **reference**
image, and the falloff is the shared smootherstep. What is new is the *distance*,
and that is the whole of this section.

### ⚠ Neither CIE76 nor CIEDE2000. Oklab.

§4b left this as a choice between CIE76 ΔE*ab (CIE 15:2004) and CIEDE2000
(CIE 142-2001; ISO/CIE 11664-6:2014). Both were rejected, for reasons that only
appear once you ask what the number is *for*.

**CIEDE2000 is scoped to small differences.** It was fitted against datasets of
near-threshold pairs — RIT-DuPont and its relatives, ΔE ≲ 5 — and the CIE scopes
it to small colour differences. A photographer dragging a selection tolerance is
working at ΔE 10–40, outside its validated range. Worse, its accuracy is spent
on a property this control does not need: **any monotone miscalibration of the
distance is absorbed by the slider**, because the person is closing the loop with
their eyes on the overlay. What a slider cannot fix is the *shape* of the
iso-distance surface — whether "equally far from this blue" follows perceived
hue — and on that, CIEDE2000 buys little over CIE76.

**CIELAB's shape is wrong exactly where it matters.** Its blue-to-purple hue
bend is long documented, and sky is the first thing anyone reaches for a colour
range mask to select. At a generous tolerance a CIELAB selection on sky bleeds
into purples. So CIE76's flaw is the one that bites and CIEDE2000's virtue is the
one that does not. CIE94 inherits the same bend; CIELUV's hue uniformity is
worse still.

**And CIEDE2000 is discontinuous.** Its mean-hue handling has genuine
discontinuities — Sharma, Wu & Dalal, *Color Research & Application* 30(1), 2005,
documented them while producing the reference implementation. A discontinuous
distance field feeds a smootherstep and prints a **seam** across a smooth sky
gradient. §4b already argued that a range mask's boundary follows the
photograph's own texture, so C² continuity matters more here than for a gradient;
a metric with a jump in it is disqualified for a mask on that ground alone,
before any argument about line count.

**Oklab** — Björn Ottosson, *"A perceptual color space for image processing"*,
20 December 2020 — fixes precisely the failure that matters, at plain Euclidean
cost. It is a blog post with its full derivation and fitting data, which on its
own would be thin for this repository's sourcing rule; it is also **normatively
specified by the W3C in CSS Color Module Level 4** as `oklab()` / `oklch()`,
which is a standards-body document. Cited together, it clears the bar.

### ⚠ Chromaticity only, and the reason is an exact identity

Oklab's nonlinearity is a **pure cube root** — no linear toe, no division by a
reference white. So scaling the input by k scales L, a and b uniformly by k^⅓,
and therefore

>  **a/L and b/L are exactly invariant under exposure.**

Verified numerically rather than asserted: a sky patch at ×0.25, ×1, ×4 and ×64
gives a/L = −0.081461 and b/L = −0.202005 at every one of them.

That identity is what makes this control well-defined on a scene-linear,
**unbounded** input. There is no honest `Yn` for a raw whose speculars run well
above 1.0, and any grey anchor — 0.18 → L\* 50? — is a convention that would have
to be defended. CIELAB cannot avoid the question: its `f(t)` has a linear toe and
a division by the white point. Oklab makes it vanish.

So the metric is Euclidean distance in **(a/L, b/L)**, and lightness is
deliberately **not** in it. Two consequences, both wanted:

- A shade in shadow and the same shade in sun are the same colour. Selecting the
  blue of a sky should not stop at the point where a cloud shades it.
- All neutrals collapse to the origin, so a colour range mask on grey selects
  grey at every brightness — which is what "this colour" means.

**The lightness axis already exists.** §4b's luminance band is exactly that, and
§6's fold composes the two with intersect. Building lightness into this metric
would be a second, worse copy of a control that is already there.

### The transform, composed once

Linear Rec.2020 → XYZ (D65) → LMS is two matrices and the shader carries their
product, so the kernel does one 3×3, three cube roots and one more 3×3.

    +0.6166884417  +0.3601590705  +0.0230433072
    +0.2651401962  +0.6358564847  +0.0990302685
    +0.1001506451  +0.2040043234  +0.6963246874

⚠ **Derived from the engine's own matrix, not from a table.** `Rec.2020 → XYZ` is
the inverse of `kXyzToRec2020` in `DevelopPipeline.cpp`; that inverse agrees with
the published BT.2020 RGB→XYZ matrix to 3 × 10⁻⁸, which is the check that says
the two halves of this program share one idea of the working space. Ottosson's
M₁ and M₂ are used unmodified.

⚠ **Residual, stated rather than tuned away.** The composed rows sum to 0.99989,
1.00003 and 1.00048 rather than exactly 1, because Ottosson's published M₁ is
fitted and rounded. A neutral therefore lands at |(a/L, b/L)| ≈ 1.2 × 10⁻⁴
instead of exactly zero. That is three orders of magnitude below the smallest
usable tolerance and it is asserted as a bound in the suite. Row-normalising the
matrix would make the invariant exact and would also mean shipping numbers that
are nobody's published ones.

### Scale, so the slider has a range

Distances between ordinary photographic colours, measured in (a/L, b/L):

| | blue sky | yellow car | tarmac | red | foliage |
|---|---|---|---|---|---|
| **yellow car** | 0.425 | | | | |
| **tarmac** | 0.203 | 0.235 | | | |
| **red** | 0.604 | 0.405 | 0.433 | | |
| **foliage** | 0.369 | 0.193 | 0.257 | 0.584 | |
| **skin** | 0.323 | 0.184 | 0.126 | 0.314 | 0.299 |

So a tolerance running to about 0.6 spans "this shade only" to "most of the
picture", and the interesting travel is the first third of it. The nearest pair
here — tarmac and skin at 0.126 — is the resolution the control has to be able
to separate.

### The target is stored as RGB, not as a converted pair

The parameter block carries the picked colour as scene-linear Rec.2020 **RGB**
and the shader converts it, per pixel, with the same function it uses on the
pixel.

⚠ Converting once on the host would be a second implementation of the transform,
and this repository has been bitten by a duplicated transform twice — the display
transform in the histogram, and the tone map for the segmentation input. The
target conversion is about twenty flops against a twenty-four-megapixel pass, and
buying that back would cost the one guarantee that matters: the target and the
pixel cannot disagree about what Oklab is.

The picker itself is the colour picker that already exists —
`Engine::sampleAt`'s scene value. ⚠ That value is normalised by its own peak, and
it does not matter here for exactly the reason above: the metric is scale
invariant, so a normalised target and the unnormalised pixel land in the same
place.

### What is deliberately not built

- **A hue-only or chroma-only variant.** Both fall out of composing this with the
  luminance band; a third control would be a third thing to keep in step.
- **Multiple target colours per component.** A group already holds four
  components and `add` is `max`, so two colours is two rows.

---

## 5. Subject and sky

**Vision handles subject and person.** `VNGenerateForegroundInstanceMaskRequest`
(macOS 14+, which is Orion's floor) is class-agnostic subject lifting;
`VNGeneratePersonSegmentationRequest` gives a person matte at three quality
levels. On-device, free, no dependency, no licence question.

**Sky has no API.** Apple's panoptic model does predict sky, and
`CIRAWFilter.semanticSegmentationSkyMatte` exists — but those mattes are produced
**at capture time and embedded in the file**. They cannot be generated by
analysing an imported RAW. ⚠ Worth re-checking each macOS release.

**Feed Vision a tone-mapped preview, not the working buffer.** These models were
trained on ordinary display-referred 8-bit photos. Passing scene-linear
Rec.2020 HDR gives them crushed or clipped tones and degrades accuracy. The
pipeline is: AgX → sRGB or P3 8-bit `CGImage` → Vision → take the matte back as
a coverage signal.

**The returned matte is low resolution** — Vision runs at a fixed internal
network resolution and upsamples. Recover the edge with the guided filter: guide
= full-resolution tone-mapped luminance, input = the low-res matte. That is
exactly §4's joint-upsampling case, and it is why §4 comes before §5.

⚠ Latency at 24 MP is not published by Apple. Run async, cache as a mask node,
measure on target hardware.

### ⚠ Settled: there is no Apple route to a sky matte, and no clean model to bundle

This section long said sky "is not available this way". Checked properly, and
the answer is firmer than that.

**No first-party API can produce one from an imported RAW.** Vision offers
people, class-agnostic subjects and saliency. Saliency is actively the wrong
tool — sky is the *least* salient region in almost every frame, and the
attention request returns a coarse heatmap rather than a matte.
`AVSemanticSegmentationMatte`, and the matching ImageIO auxiliary-data
constants, *do* include a sky matte — but they are **read accessors for a matte
the capturing iPhone embedded at shutter time**. `AVCapturePhotoOutput` is the
only producer and it needs a live capture session. A Sony ARW will never carry
one. Core Image's segmentation filter wraps Vision's person request; the Photos
framework hands back embedded auxiliary images with the same limitation.

**And bundling a segmentation network is refused, on the grounds already written
down for camera profiles.** Sky appears as a class in ADE20K, COCO-Stuff and
Cityscapes; Cityscapes is research-only, ADE20K's terms are unclear, and weights
inherit their training data's ambiguity even when the architecture's code is
permissive. No sky-capable model is known here whose architecture, weights *and*
training data all carry a clean redistribution grant — and saying that is better
than naming one and hoping. It would also add a Python and coremltools
conversion step for a solo maintainer of a C++ and Swift program to keep alive.
Same reasoning as `DECISIONS.md` #44.

### What to build instead

⚠ **A classical detector, and it is honest about being an estimate.** Sky is
smooth, bright, connected, touches the top edge, and is separated from the
ground by a strong gradient — the gradient-energy border search of Shen & Wang
(2013) and Hoiem's geometric-context work before it. Roughly 80–90% right on
daytime landscapes.

It fails on sunsets, on water and glass reflecting the sky, on white overcast
against white buildings, on night skies, and on sky seen through foliage. That
failure list is not a reason to refuse it, and the distinction matters: the
purple cast was dangerous because it was **invisible** wrong output. A mask is a
proposal the photographer inspects as an overlay and corrects with the brush.
Visibly wrong and correctable is a different category from silently wrong.

The pipeline is already built for exactly this: a coarse matte at 1024 px on the
long edge, §4's guided filter recovering the boundary, and add/subtract brushing
on top.

**And an interim that costs nothing:** a composed preset — a linear gradient from
the top, intersected with a luminance range and a colour range. ⚠ Note that an
*inverted subject matte* is **not** sky: trees and buildings are also
not-the-subject.

**What it must not claim.** Night skies, reflections, sky-through-foliage
transparency, or any semantic notion beyond "smooth bright region connected to
the top edge". The control should say estimated, and the panel should not
pretend otherwise.

### Model licences, if a sky model is ever added

| Model | Licence | Usable |
|---|---|---|
| **BiRefNet** (arXiv:2401.03407, CAAI AIR 2024) | MIT | **Yes** — best edge/hair detail |
| **U²-Net** | Apache-2.0 | **Yes** — small and fast |
| **IS-Net / DIS** | Apache-2.0 | Yes |
| **SAM / SAM 2, MobileSAM, EdgeTAM** | Apache-2.0 (with patent grant) | Yes — prompt-driven, poor unprompted |
| **RMBG-1.4 / 2.0** | CC BY-NC 4.0 | **No — non-commercial** |
| **SegFormer** | code Apache-2.0, NVIDIA weights non-commercial | Weights need checking |

RMBG is the trap: it is the model most tutorials reach for, it is built on
IS-Net and BiRefNet respectively, and its licence forbids the commercial use
this project is heading toward. Use the upstream models instead.

---

## 6. Combining masks

**One combined alpha per adjustment group; apply the adjustment once.** Never
apply the same +1 stop twice because two masks overlap.

```
add        α = max(α₁, α₂)        // no buildup in the overlap
           α = α₁ + α₂ − α₁α₂     // screen; smoother accumulation, stronger seam
subtract   α = α₁ · (1 − α₂)
intersect  α = α₁ · α₂
invert     α = 1 − α
```

Components fold left in listed order, so add and subtract are order-sensitive as
a sequence — deterministic and cheap.

Different adjustment groups need no normalisation against each other: they are
separate nodes in the DAG, applied in order, each gated by its own alpha. That is
Lightroom's stack, and it falls out of Orion's graph for free.

⚠ Reported: Lightroom implements "Intersect" internally as subtract-then-invert.
Interesting if XMP round-trip fidelity ever matters; irrelevant otherwise, since
`α₁·α₂` is the same thing and clearer.

---

## 6b. What is actually built, as of 2026-07-29

The plan above is the plan of record; this is the state of it.

| Step | Status |
|---|---|
| 1. Primitives — linear, radial | ✅ draggable on the canvas |
| 1. Primitives — brush dabs | ✅ paintable on the canvas |
| 2. Groups and compositing (§6) | ✅ **done** — `mask_component.slang` folds per §6 (add = max, subtract, intersect), one node per component, `kMaxMaskComponents = 4`. Panel rows with a per-row op, sidecar and undo all carry the list; pre-group sidecars migrate to one component |
| 3. Guided refinement (§4) | ✅ **done** — `mask_guide_prep` / `mask_guide_ab` / `mask_guide_apply`, seven nodes on the *folded group*, disabled at strength 0. The second input binding §4 asked for, and nothing else changed in the filter. Radius and epsilon are Orion's own: `UNSOURCED.md` §20 |
| 4. Vision subject and person (§5) | ✅ **done** — `SubjectMatte.swift`. Mask kind 4 is the raster component; the matte is read back from the *existing* render with the geometry neutralised, so no second copy of the display transform, and only the EXIF turn is undone. Sky is still unavailable and §5 says why |
| 5. Sky | ❌ not started |

The two primitive kernels are gone as separate files: one `mask_component.slang`
serves all three primitives plus the fold, and the shared falloff moved to
`ops/mask_ops.slang` with the compose ops (decision #62). The merge fixed a
fourth dead control of session 2026-07-29n's class — invert never reached a
brush, because the gradient node held it and the brush node discarded that
node's output. Pinned by a GPU test now.

**Departures from §1 and §3, both deliberate:**

- §3 says store strokes in the *uncropped* image's normalized space. Orion
  stores them in the **displayed** picture's space instead, and `MaskGeometry.h`
  transforms them at render. Same result — a stroke stays on its subject through
  crop, straighten and quarter turns — but it keeps one copy of that transform,
  shared with the gradients, rather than two conventions to reconcile.
- §1's "one radius per mask, plus mask-level Flow and CenterWeight" is followed.
  **CenterWeight is called Hardness** in the interface, because that is the word
  photographers use.

✅ **A long stroke is no longer truncated** (2026-07-30). ⚠ The note that stood
here said the fix was more nodes chained through `accumulate`, and that it was
"more nodes, not a bigger buffer" — reasoning from the wrong constraint. The cap
was Metal'+chr(39)+'s four-kilobyte limit on `setBytes`; the stroke now lives in an
auxiliary texture, 16,384 dabs, no chain and no spare component.

⚠ **The nib's constants are not sourced.** Dab spacing, the hardness clamp and
the overlay tint are all Orion's own, recorded in `UNSOURCED.md` §17 and §18.
The falloff is not among them — that is Perlin's smootherstep, shared with the
gradient masks.

---

## 7. Build order

1. **Primitives.** Linear and radial gradient shaders, brush-dab rasteriser.
   Normalized coordinates in, R16F alpha out. One shader each, no dependencies.
2. **Groups and compositing.** A mask-group node folding component alphas;
   adjustment nodes take (image, alpha) and apply once, pre-AgX.
3. **Guided refinement.** Second input binding on the existing guided filter.
   Auto-mask, feather, and matte upsampling all fall out of it.
4. **Vision subject and person.** Async, on a tone-mapped preview, refined by
   step 3.
5. **Sky.** Core ML, BiRefNet or U²-Net, or a colour/position heuristic plus the
   guided filter. Lowest priority and the only step needing a new dependency.

Steps 1–3 need no new dependency and no new licence position. That is the whole
argument for this order.

---

## 8. What would change the plan

- Guided-filter auto-mask failing on hair or foliage → escalate those cases to a
  BiRefNet matte, **not** to a matting solver.
- Vision latency over a second or two at 24 MP hurting the interaction → run it
  on a downscaled preview and lean harder on guided upsampling.
- XMP round-trip fidelity with Lightroom becoming a product requirement →
  match Adobe's falloff and feather mapping exactly, and drop smootherstep.

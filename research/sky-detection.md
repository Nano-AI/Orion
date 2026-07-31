# Sky detection, without a model

> ## ⚠ STATUS: WRITTEN, TESTED, AND NOT SHIPPED
>
> The algorithm below is implemented in `app/SkyDetector.swift` and pinned by
> `orion-viewport-tests`. **It is not in the interface**, because on a
> photograph it does not produce a sky. Measured on `_PIC8095`, the one daylight
> frame in the sample set: 18.2% coverage that read as a plausible *number* and
> was **vertical stripes** when drawn, and after median-smoothing the border,
> a few wide vertical bands running from the top edge down through the trees.
>
> The number is the lesson. Coverage looked reasonable at every stage; only the
> overlay showed what it was. A control that produced this would be worse than
> no control, so it is not offered — `DECISIONS.md` #78 argued that a *visibly*
> wrong mask is acceptable because it can be corrected, and that argument covers
> a mask that is roughly right, not one that is stripes.
>
> **What is wrong, and what would fix it.** A per-column first-exceedance border
> cannot express a region: it assumes the sky is a function of x, one row per
> column, and on a frame with a tower's lattice or an irregular treeline the
> column-wise answers are unrelated to each other. Median smoothing reduces the
> comb and does not address the cause. The two candidates worth trying next are
> a **flood fill from the top edge** over a smoothness predicate, which produces
> a region rather than a function, and the paper's own §3.4 K-means refinement,
> which this deliberately truncated.


`research/masking.md` §5 and `planning/DECISIONS.md` #78 settled why this is not
a segmentation network: **no Apple API produces a sky matte from an imported
RAW**, and no model is known whose architecture, weights *and* training data all
carry a clean redistribution grant. This file is what gets built instead.

## The published method

**Y. Shen and Q. Wang, "Sky Region Detection in a Single Image for Autonomous
Ground Robot Navigation", *International Journal of Advanced Robotic Systems*
10(10):362, 2013.**

Prior art it builds on: D. Hoiem, A. A. Efros and M. Hebert, "Geometric Context
from a Single Image", *ICCV 2005* — the earlier statement that a photograph's
coarse layout can be recovered from colour, texture and position priors alone.

### What the paper does

Sky is smooth, and the ground is not. So the boundary between them is where the
image gradient first becomes large, scanning down each column — and the whole
problem becomes choosing *how large*.

1. **Gradient.** Sobel magnitude over the greyscale image.
2. **A border for a given threshold.** For threshold `t`, the sky border in
   column `x` is the first row whose gradient exceeds `t`; if none does, the
   column is sky all the way down.
3. **Score that partition.** The paper's energy is

   > `J = 1 / ( γ·det(Σs) + det(Σg) + γ·λs₁² + λg₁² )`

   where `Σs`, `Σg` are the 3×3 RGB covariance matrices of the sky and ground
   pixels, `λ₁` their largest eigenvalues, and `γ = 2`. Maximising `J` means
   minimising the spread *within* each region — a good border makes both sides
   internally uniform.
4. **Search.** Evaluate `J` over a set of thresholds spanning the gradient's
   range and keep the best.

The insight worth restating: it never asks what sky *looks like*. It asks which
horizontal cut makes the two halves each most self-consistent. That is why it
works on a blue sky and an overcast one without a hue prior.

## ⚠ What Orion implements, and what it truncates

Implemented: the gradient, the per-column border, the energy, and the search.

**Not implemented: the paper's §3.4 refinement**, which re-segments partial-sky
images with K-means on the sky pixels and splits a bimodal region. `UNSOURCED.md`
records it as a truncation rather than a method. The bounded cost is that a frame
where the sky is genuinely two populations — a sunset gradient, or sky seen
through a gap — gets one border rather than a refined one.

That truncation is affordable **only because of what the matte is for**: a coarse
result at 1024 px, refined onto the photograph's own edges by §4's guided filter,
and correctable with the brush's add and subtract. The same argument does not
license truncating anything whose output nobody sees.

## ⚠ Where it fails, stated rather than discovered

| Fails on | Why |
|---|---|
| Sunsets | the sky is not internally uniform; the energy prefers a cut *inside* it |
| Water, glass, car paint reflecting sky | the reflection has the sky's statistics and is smooth |
| White overcast against a white building | no gradient at the boundary to find |
| Night skies | the ground is often darker and smoother than the sky |
| Sky through foliage | a per-column border cannot express holes |

**This list is why the control says *estimated*.** And why the failure is
acceptable at all: the purple cast was dangerous because it was **invisible**
wrong output, whereas a mask is a proposal the photographer sees as an overlay
and fixes with the brush. Visibly wrong and correctable is a different category
from silently wrong. `DECISIONS.md` #78.

## Orion's own numbers

Recorded in `UNSOURCED.md` because the paper fixes none of them:

- **The threshold search runs 24 steps** between the gradient's 5th and 95th
  percentiles. The paper searches a fixed absolute range, which does not
  transfer: it assumes 8-bit camera JPEGs, and this runs on an AgX-mapped
  render whose gradient scale is different. Percentiles make the search
  frame-relative, which is what actually transfers.
- **A column with no gradient above the threshold is sky to the bottom**, which
  is the paper's rule, and the reason an all-sky frame comes out fully covered
  rather than empty.
- **A result covering more than 90% or less than 2% of the frame is rejected**
  as no-sky. Without it, a frame with no sky at all returns "everything", which
  is indistinguishable from the feature being broken — the same failure the
  person matte had before it learned to say "nothing found".

## What it must not claim

Not a semantic classifier. It finds a smooth bright region connected to the top
edge, and the interface must not suggest otherwise — no "sky" confidence, no
sub-classes, no attempt at reflections or foliage transparency.

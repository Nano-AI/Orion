# Sky detection, without a model

> ## ⚠ Attempt one drew stripes. This is attempt two.
>
> The first version followed the paper literally: a **per-column border**, the
> first row in each column whose gradient exceeds a threshold. On the daylight
> frame it reported **18.2% coverage** — an entirely reasonable amount of sky —
> and drew as **vertical stripes**. Median-smoothing across columns reduced the
> comb into a few wide vertical bands and left the coverage figure just as
> reasonable.
>
> **The number never showed it. Only the overlay did.** Every synthetic test
> passed throughout, and they were not weak tests — a comb has the right
> coverage and the right above/below answer wherever you sample it.
>
> The cause was structural. A per-column border assumes the sky is a **function
> of x**, one row per column, and on a frame with a tower's lattice or an
> irregular treeline the column answers are unrelated to each other.
>
> What ships is a **flood fill from the top edge**: 2D and connected, so it goes
> around the tower, stops at the treeline, and cannot produce a stripe, because
> every pixel it takes is joined to the top by a path of calm ones.

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

Sky is smooth, and the ground is not. So the region is the calm one, and the
whole problem becomes choosing *how calm*.

1. **Gradient.** Sobel magnitude over the greyscale image.
2. **A region for a given threshold.** ⚠ The paper takes the first row per
   column whose gradient exceeds `t`. Orion **floods from the top edge**
   instead, four-connected, over pixels at or below `t` — see the note at the
   top for why. Four-connected rather than eight: a diagonal step squeezes
   through a one-pixel gap in a branch, which is how a fill escapes into the
   ground and takes the frame.
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
| Night skies | the sky is grainy with high-ISO noise while silhouettes are flat black, so the calmest region joined to the top edge is the **foliage** — see below, it inverts rather than refusing |
| Sky through foliage | the gaps are smaller than the fill's connectivity |
| Smooth ground touching the sky | the fill can leak through a calm wall or road |
| **Interiors** | the search floods from the **top edge**, and indoors the top edge is a ceiling. Added 2026-08-02 after the `--scene sky` still on an atrium frame came back with a wooden ceiling and a structural column selected — and **accepted** rather than refused, the same inversion the night-frame row describes, so it is not a night-exposure problem but a top-edge assumption. ⚠ Not pinned by a check: it was found on a photograph outside `samples/`, and a test that depends on a file which is not in the repository is a test that fails for everyone else |

**This list is why the control says *estimated*.** And why the failure is
acceptable at all: the purple cast was dangerous because it was **invisible**
wrong output, whereas a mask is a proposal the photographer sees as an overlay
and fixes with the brush. Visibly wrong and correctable is a different category
from silently wrong. `DECISIONS.md` #78.

### ⚠ The answer depends on the edit, not only on the photograph

Measured 2026-07-31 on `_PIC8148`, a starry sky over a treeline:

| Exposure | Answer |
|---|---|
| 0 EV | **accepts**, 4.6% covered — and what it covers is the *treetops along the top edge*, with the sky itself unselected |
| +1 EV | accepts, same inversion |
| +2.6 EV | **refuses** — "no calmer than the picture as a whole" |

Nothing here is a bug in the sense of a wrong line. The search is frame-relative
by construction — the thresholds are percentiles of *this render's* gradient and
the guard compares against *this render's* mean — and the render is the graded
one, because the detector reads the picture after the display transform on
purpose, since that is the picture the method's assumptions are about. Lifting
the shadows lifts the sky's high-ISO grain with them, and past some lift the
region stops looking calm.

⚠ **Two consequences worth saying out loud.** The night-frame entry above is not
"it refuses", it is "it inverts": on a grainy sky over flat black silhouettes,
the calmest region joined to the top edge is the *foliage*. And because a matte
is never regenerated when the edit changes (`STATUS.md`'s gap table), whichever
answer the photographer was looking at when they pressed the button is the one
frozen into the edit.

⚠ **This went unrecorded because the check that was supposed to catch it could
not fail.** `repro/sky-mask.txt` asserted the night frame was refused by opening
it, setting a local exposure and measuring that the picture had not moved — with
no mask row on the photograph, a local exposure does nothing, so it passed
whether the detector refused, accepted, or did not exist. The scenario now calls
`refuses`, and pins the inversion at 0 EV as the behaviour that ships.

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
- **Coverage is bounded to 2%-90%, and the bound is applied *during* the
  search.** The paper's energy assumes a uniform sky and a real one is not — it
  runs light at the horizon and deep at the zenith — so a sliver, being
  perfectly uniform, always scores best. Measured before this guard: every
  photograph reported no sky.
- ⚠ **The smoothness check is against the whole frame, not against the ground.**
  Comparing the two regions is circular: the fill *defines* them by gradient, so
  the unfilled part is rougher by construction and the check can never fail. On
  a frame of pure texture the region grew to 81% and the comparison passed it.

## What it must not claim

Not a semantic classifier. It finds a smooth bright region connected to the top
edge, and the interface must not suggest otherwise — no "sky" confidence, no
sub-classes, no attempt at reflections or foliage transparency.

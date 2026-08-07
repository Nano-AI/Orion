# The brush nib — why the dabs sit a quarter of a radius apart

**Status:** derived and measured, 2026-08-07. Closes the spacing half of
`UNSOURCED.md` §17, which said the constant was "the usual figure in paint
engines" — recollection, not a citation.

**Where:** `app/CanvasLayout.swift` (`brushSpacing`),
`engine/shaders/mask_component.slang` (`dabCoverage`).

**Pinned by:** `testBrushSpacingRipple` (`orion-tests`, renders real strokes and
measures their edges) and `testBrushSpacingIsUnderItsBound`
(`orion-viewport-tests`, owns the shipped constant).

---

## The model, and its source

A stroke is not a swept shape. It is a row of stamps: the nib is rasterised
once and composited repeatedly along the path, which is why a stroke costs the
same per unit length whatever its curvature.

> Turner Whitted, **"Anti-Aliased Line Drawing Using Brush Extrusion"**,
> *Proceedings of SIGGRAPH '83*, pp. 151–156. ACM.
> <https://dl.acm.org/doi/10.1145/800059.801144>

Whitted's contribution is exactly the property Orion relies on — the
anti-aliasing calculation is performed once, for the brush, and each position
along the path costs only a composite. That is the model. **It does not give a
spacing constant, and no published paper does**, because the spacing is not a
matter of taste: it follows from the nib's own falloff.

⚠ Searching for a citation for the *constant* mostly turns up **US patents**
(stamp-texture and stroke-tapestry filings). Per decision #174 those are not a
source to implement from, and they are not cited here.

## The geometry

Two dabs of radius `r` with centres `d` apart. Put them at `±d/2`. On the
perpendicular bisector the union of the two discs reaches only

    sqrt(r² − d²/4)

where a continuous sweep would have reached `r`. So the edge of the stroke dips
inward between every pair of dabs by the **sagitta**

    s(k) = r · (1 − sqrt(1 − k²/4)),   for spacing d = k·r

That dip is what the eye reads as beading. Nothing here is an approximation and
nothing needs a source: it is the chord of a circle.

| k (spacing, in radii) | dip, as a fraction of r |
|---|---|
| 0.125 | 0.00196 |
| **0.25 (shipped)** | **0.00784** |
| 0.30 | 0.01131 |
| 0.398 | 0.02000 |
| 0.50 | 0.03175 |
| 1.00 | 0.13397 |

## What hides it

`dabCoverage` clamps hardness to **0.98** and ramps `maskFalloff`
(Perlin's smootherstep) from `d = h` to the rim at `d = 1`. So the falloff
occupies a band `(1 − h)` wide in units of the radius, and at the clamp — the
hardest edge the nib can draw — that band is **0.02 r**.

A dip smaller than the band it sits in is swallowed by the ramp. Setting
`s(k) = 0.02 r` and solving:

    k_max = 2·sqrt(1 − 0.98²) = 0.398

**So the spacing is bounded by the hardness clamp, and the two constants that
`UNSOURCED.md` §17 listed separately are one decision.** 0.25 sits under that
bound; 0.5 does not, and would bead at maximum hardness.

## What was measured, and where the algebra flatters itself

⚠ **The band above is the whole falloff, and no edge is visible over its whole
falloff.** The part of a smootherstep that reads as an edge is its steep middle:
the 10%-to-90% span is 0.47 of the full band, so the honest comparison is
against **≈0.0094 r**, not 0.02 r — which puts the bound nearer **0.274** and
leaves the shipped 0.25 with about **9% of margin, not 37%**.

That is why this was measured rather than left as algebra.
`testBrushSpacingRipple` renders a straight stroke at `r = 200 px`, hardness
asked for at **1.0** so the shader's own clamp answers, and reports the ripple
of the half-coverage contour together with the 10–90 feather **taken off the
same frame**:

| spacing | dabs | measured ripple | sagitta | measured feather |
|---|---|---|---|---|
| **0.25 r** | 17 | **1.12 px** | 1.57 px | 2.26 px |
| 0.50 r | 9 | 5.95 px | 6.35 px | 2.28 px |
| 1.00 r | 5 | 26.55 px | 26.79 px | 2.35 px |

So at the shipped spacing the dip is **about half the width of the visible part
of the edge** — inside it, and by a factor of two rather than by a comfortable
margin. At 0.5 r it is **2.6× outside** it. The shipped value is the right side
of a line that is closer than the phrase "comfortably inside" in the old comment
implied.

⚠ **The sagitta is a good predictor and, at the shipped clamp, an upper
bound** — every measured ripple came in under it, because compositing fills the
dip in. That is not guaranteed in general: with the clamp moved to 0.999 the
0.25 r ripple measured 2.00 px against a sagitta of 1.57. Treat it as the
scale of the thing, not as a ceiling.

## What is still not sourced

- **The 0.98 clamp itself.** Its *reason* is sound and written down — a truly
  hard circle aliases, and the mask multiplies a parameter, so the staircase
  becomes a banded edit rather than a jagged edge — but the value is chosen.
  What this file adds is that it is no longer free: it sets the spacing bound,
  so the two must move together.
- **Flow, and the buildup series.** Untouched here.
- `kMaskDabsPerPass = 256` is a buffer size, not a claim.

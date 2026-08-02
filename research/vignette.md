# The creative vignette — a deliberate falloff, post-crop

**Where:** `ops/vignette_ops.slang`, fused into `color_grade.slang`;
`DevelopPipeline::compositionCircle`.

⚠ **This is not `lens-corrections.md`'s vignetting.** That one *removes* a
falloff a lens measured, from lensfun's polynomial, before the demosaic. This one
puts a falloff in on purpose, after the grade, centred on the crop. Two controls,
two nodes, opposite signs, and a photograph can carry both — the panel text and
`testCreativeVignetteGpu` both say so, because confusing them is the obvious
mistake and it looks roughly right on screen.

## Source

### The falloff curve

**The cos⁴ law of illumination.** Off-axis illuminance in an aberration-free
system falls as the fourth power of the cosine of the field angle:

> M. Reiss, **"The cos⁴ Law of Illumination"**, *Journal of the Optical Society
> of America* **35**(4), 283–288 (1945).
> [doi:10.1364/JOSA.35.000283](https://doi.org/10.1364/JOSA.35.000283)
>
> R. Kingslake, *Optics in Photography*, SPIE Press (1992), p. 121 — the
> standard textbook statement of the same result, in photographic terms.

Published, dated, and about as established as an optical result gets: it is the
first thing every lens-design text says about natural vignetting.

⚠ **And it is a first-order model, which Reiss's own paper says.** Real lenses
depart from cos⁴ through pupil aberration and through mechanical vignetting, and
the departure is why the *correction* side of Orion reads lensfun's measured
polynomial rather than evaluating a law. That is the right call for a
correction and irrelevant here: this control is imitating the look of natural
falloff, and cos⁴ is the shape of the thing being imitated.

### The control surface — and why post-crop

Adobe's Post-Crop Vignetting is the reference implementation of this control in
a photo editor, and the distinction it draws is the one that matters:

> Julieanne Kost (Adobe), *Adding Vignette and Grain Effects in Lightroom
> Classic*, August 2024 —
> <https://jkost.com/blog/2024/08/adding-vignette-and-grain-effects-in-lightroom-classic.html>
>
> The post-crop vignette is "applied to the image after cropping and is
> readjusted and updated as the crop is altered", where Lens Correction
> vignetting "affects the entire image regardless of crop adjustments,
> potentially appearing uneven on cropped images".

Documentation of a *control*, not of an algorithm — Adobe publishes no falloff
curve, no midpoint mapping and no blend for any of its three styles, so nothing
numeric is taken from it. What is taken is the design: the circle belongs to the
composition, and the correction is a different control that must not be
confused with it.

## What we implement

With `r` the distance from the composition's centre in units of its own
half-diagonal — so `r = 0` on axis and `r = 1` at the corner — and `T = tan θ`
for a half-diagonal field angle θ:

```
V(r) = cos⁴ = 1 / (1 + (r·T)²)²           the law, with no trigonometry:
                                          cos² = 1/(1 + tan²)

s(r) = (1 − V(r)) / (1 − V(1))            normalized: 0 on axis, 1 at the corner

out  = in · 2^( amount · s(r) )           an exposure change, in scene-linear
```

Two controls and both are physical quantities:

| Control | Is | Range |
|---|---|---|
| **Amount** | the exposure change **at the corner of the composition**, in stops | −3…+3 EV |
| **Field angle** | the half-diagonal angle of view of the lens being imitated | 10…70° |

45° is roughly a 21 mm lens on full frame and 23° roughly a 50 mm, so "wide
angle of view, falloff reaching well into the frame" is not an analogy — it is
what the number means.

**Why the normalization matters.** Without `1 − V(1)`, moving the field angle
would move what the corner is worth: at 20° a raw cos⁴ reaches 0.22 of the way
and at 65° it reaches 0.97, so the same Amount would be four times the darkening
at one setting as at the other and the slider would mean nothing on its own.
Normalized, Amount is the corner and Field angle is only the shape. That is the
one property `testCreativeVignetteGpu` asserts directly, and the mutation that
drops the division fails it.

**Not clamped past `r = 1`.** Pixels outside the crop exist while the crop tool
is open, and cos⁴ saturates on its own — `s` approaches `1/(1 − V(1))`, a finite
ceiling — so the field stays smooth rather than gaining a visible ring at the
edge of the rectangle.

### Why a circle rather than an ellipse

A lens's iso-illuminance contours are **circles about the optical axis**; the
frame is a rectangle cut out of one. So the falloff is a circle in pixels, which
is why the kernel scales x by the frame's aspect, and why on a 4:3 frame the long
edge's midpoint (r = 0.8) falls further than the short edge's (r = 0.6).

This is also why there is **no Roundness control**. Adobe has one, from
rectangular to elliptical; it is a departure from what optics does, and adding it
would mean the shape no longer has a source. If a photographer wants a
non-circular darkening, that is a radial mask carrying a local exposure, which
Orion already has and which can be placed anywhere.

**No Feather either**, for a smaller reason: the cos⁴ curve *is* the feather, and
the field angle moves it. Two controls for one axis is what `color-grading.md`
refuses a per-zone gamma for.

**And no styles.** Adobe's three exist because its vignette is a
display-referred blend; a Highlight Priority mode is what you need when the
default crushes highlights. Orion's is an exposure change in scene-linear light
*before* the display transform, so a bright corner rolls off through AgX exactly
as a darker exposure would — the picture keeps its shape and there is nothing for
a second mode to rescue.

## Where it runs, and why there

Fused into `color_grade.slang`, the last thing in scene-linear light before the
display transform.

**Scene-linear, not after AgX.** A vignette is light: a lens loses it in the
optics, and multiplying scene-linear values is what that is. Two consequences
that a display-referred blend does not get — a bright corner rolls off through
the sigmoid instead of clipping, and the amplitude is in stops, which means the
same thing on every photograph. There is also a mechanical reason: the graph's
tail is eight bits for the screen (#38), the dither that hides the quantisation
is applied there, and multiplying an already-quantised, already-dithered gradient
by 0.25 in the corner scales the dither down with it and bands a smooth sky.

**After the grade, not before it.** Physically the lens comes first, but the
grading zones are Gaussians on luminance, so a vignette applied first would slide
every corner pixel into the shadow band and silently change what all three wheels
do. Decision #41 took that interaction out of the wheels; this does not put it
back.

**In that kernel rather than in its own node.** It is pointwise, and every extra
pointwise pass is a ~194 MB round trip at 24 MP. Same trade the creative LUT lost
inside `develop_display.slang` (`luts.md`), and the same one #82 made grain pay
only while its slider is up: at Amount 0 the node disables to nothing and an
exposure tick costs exactly what it did before this existed.

## Post-crop, and how a kernel that never sees the crop manages it

Everything upstream of `geometry` renders the whole frame; `geometry` is the node
that crops, straightens and turns, and it runs last. So the vignette kernel is
looking at the uncropped frame and has to be *told* where the composition is.

`DevelopPipeline::compositionCircle` computes that on the host, as three numbers:
the crop rectangle's centre normalized in the unrotated frame, and its
half-diagonal in units of the frame's height.

⚠ **Only a circle, and that is what keeps it honest.** A rotation cannot change
a length, so the straighten and the quarter turns move the centre and leave the
radius alone; there is no second copy of `geometry.slang`'s inverse map, which is
the mistake decision #70 exists about. What the host does re-state is the
rectangle's clamp and the quarter-turn `switch`, and both are asserted against
hand arithmetic in `testCompositionCircle`.

It also comes out resolution-independent for free: the centre is a fraction and
the radius is in units of the frame's height, so the preview graph at 1/16 puts
the vignette in the same place as the full render without knowing it is smaller.

## Confidence

High for the curve: cos⁴ is a published law and the implementation is its closed
form.

High for the placement: scene-linear before the display transform, after the
grade, keyed to the crop — each of those has a reason above and a test.

**What is chosen rather than derived** — and is in
[`UNSOURCED.md §24`](UNSOURCED.md):

- the slider ranges (±3 EV, 10–70°) and the 45° default;
- the decision to normalize the curve so Amount is the corner value, rather than
  some other point on it;
- reading the second control as a field angle at all. cos⁴ is a law about a
  *lens*, and this vignette is not attached to one — the frame's real half-
  diagonal field angle is knowable from EXIF, and this control deliberately
  ignores it, because a creative vignette that could only be as wide as the lens
  that took the picture would be a correction.

## Tests

`orion-tests`:

- **`testCompositionCircle`** — no GPU. An uncropped frame is centred and its
  radius is half the diagonal; a crop moves the centre and shrinks the radius; a
  quarter turn and a straighten leave the radius alone; one turn puts a top-left
  crop at the bottom left of the sensor; a nonsense rectangle still lands inside
  the frame.
- **`testCreativeVignetteGpu`** — the real graph on a flat 4:3 frame. Off is off
  and the node does not run; −2 EV drops every corner and leaves the middle where
  it was; all four corners drop equally; the long edge's midpoint drops further
  than the short edge's; a positive Amount lifts instead; the corner is worth the
  same at 20° and at 65° while the mid-radius is not; the lens correction is
  neither switched on by this control nor switching this one on; Amount back at 0
  is bit-identical; and an exposure tick costs one node more with it on and
  exactly what it used to with it off.
- **`testVignetteFollowsTheCrop`** — a quadrant crop, and the *cropped* picture's
  four corners drop by the same amount with its own middle the brightest part of
  it, which a frame-centred vignette cannot produce.

⚠ The corner checks are **differential** — the drop from an unvignetted capture —
because RCD has no neighbours to interpolate from at the frame's border and an
untouched corner already reads a couple of 8-bit steps off the middle. An
absolute comparison there would be measuring the demosaic and calling it a
vignette.

`repro/vignette-follows-the-crop.txt` covers the two things no engine test can
reach: the round trip through `Engine.state` and the sidecar (the memberwise
initializer that dropped film grain on the floor with every suite green), and the
post-crop claim stated about **one patch of one photograph** — the frame's own
centre, untouched while uncropped and dark once a crop makes it a corner.

`orion-bench` — `vignette -2 EV`, on mean luma, floored at 0.35 of the exposure
reference. ⚠ Measured 0.79, 1.05 and 0.70 across the three sample frames, which
is a wider spread than grain's and for a reason worth keeping: grain's amplitude
is defined in display units, so it is the same wherever the scene sits, while
this one is in stops of scene-linear light and what two stops down is worth on
screen depends on where the corner started on AgX's curve.

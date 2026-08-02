# Color Grading — three-way, shadows / midtones / highlights

**Where:** `color_grade.slang`, `DevelopPipeline::gradeOffsets`.

## Source

**ASC CDL** — *"ASC Color Decision List (ASC CDL) Transfer Functions and
Interchange Syntax"*, American Society of Cinematographers Technology Committee,
version 1.2 (2009). The slope / offset / power form:

```
out = ( in × slope + offset ) ^ power        applied per channel, clamped at 0
```

- [ASC CDL overview and the SOP definition](https://en.wikipedia.org/wiki/ASC_CDL)
- Restated in Van Hurkman, *Color Correction Handbook*, 2nd ed. (Peachpit,
  2014), ch. 4, which is also the standard description of the three-way
  lift/gamma/gain control surface this panel presents.

Published, dated, and the interchange format every grading application on the
market reads and writes. Nothing here is invented.

## What we implement

Three ASC CDL corrections — one per tonal zone — blended by a smooth partition
of luminance.

### The tonal partition — rewritten 2026-07-28

```
Y   = 0.2627·R + 0.6780·G + 0.0593·B        Rec.2020 luma, the working space
EV  = log2(Y / 0.18)                        stops relative to middle gray

w_z = exp( −½ · ((EV − c_z) / σ)² )         c = −2.5 / 0 / +2.5,  σ = 1.6
      normalized so the three sum to 1
```

Gaussian bands on a **log** axis, normalized to a partition of unity. This is
the same construction the tone controls use, for the same reason and from the
same source — `ops/tone_ops.slang` and `deep-research-2026-07-27.md` §3. Summed
weights of one everywhere are what make an untouched image come back unchanged;
a Gaussian has no corner where two zones meet. The centres sit one stop inside
the tone controls' own inner pair, and share their σ, so the two sets of bands
describe the same picture.

**What this replaced, and why.** The first version partitioned on *linear*
luminance with `smoothstep` at 0.0/0.5 and 0.5/1.0. That sounds like "shadows,
midtones, highlights" and is not: middle gray is Y = 0.18, so it weighed
**0.70 shadows / 0.30 midtones / 0.00 highlights**. The shadow wheel graded most
of a normal photograph, the midtone weight peaked a stop and a half above middle
gray, and the highlight wheel only switched on past Y = 0.5 linear. Measured on
`_PIC8148.ARW` and `_PIC8220.ARW` lifted 5.5 EV, the highlight wheel moved mean
chroma by **−0.0000 and +0.0001**. Inert. The midtone wheel managed −0.0007.

Middle gray now sits at EV = 0, which is the midtone band's centre — where the
word says it should be.

### The correction

Each zone contributes an **offset** (the color) and a **slope** (the
brightness), weighted by that zone's share of the pixel:

```
offset = Σ_z  w_z · O_z
slope  = 1 + Σ_z  w_z · s_z
out    = max( in × slope + offset · Y , 0 )
```

### Why the offset is multiplied by Y

ASC CDL defines its offset on **code values** — a bounded, display-referred
signal. Orion applies the correction in unbounded scene-linear light, which is
the right place for the *zones* to mean what they say, but it means a constant
offset is not scale-invariant: what it is worth relative to the pixel — and
relative is what survives a log-shaped display transform — falls as 1/Y.

So a wheel had all its authority in the deep shadows and none in the highlights,
which is precisely where the highlight wheel lives. Multiplying by the pixel's
own luminance makes it a constant **chromaticity** shift at every exposure, and
`testColorGradeGpu` pins it: the same wheel measures **0.1077** relative chroma
at −3 EV and **0.1079** at +3 EV, six stops apart.

It also removes the clamp failure. A zero-sum offset always has a negative
component, and as a constant it was larger than a deep shadow — so two channels
stuck at the shader's zero clamp, the sum stopped cancelling, and the wheel
*brightened* what it was meant to tint. Measured: a 0.0096-linear patch came
back at 0.0124, **+29%**. Scaled by Y the offset cannot exceed the pixel, and
the test now checks luminance drift across the whole wedge rather than at one
convenient level.

Power is left at 1. A per-zone gamma on top of a per-zone slope is two controls
for one perceptual axis, and the tone curve upstream already owns contrast.

### The wheels are zero-sum

A puck at (x, y) on a wheel gives a hue angle and a radius. That becomes an RGB
offset through the three primaries at 120° apart, and then **the mean is
subtracted**:

```
O = k · r · ( cos(θ − θ_R), cos(θ − θ_G), cos(θ − θ_B) )
O = O − mean(O)
```

Subtracting the mean is what makes the wheel a *color* control rather than a
brightness one. Without it, pushing toward yellow also lifts the zone, and every
adjustment fights the exposure slider. With it, a neutral gray keeps its
luminance and only changes hue — so the luminance slider beside each wheel is
the only thing that moves brightness, which is what the control surface promises.

**`k = 0.25`** — and the constant changed meaning when the offset started
scaling with Y, so the old value and its story are both obsolete.

It used to be an absolute quantity in scene-linear light, where a dark patch
sits around 0.005; `k = 0.15` drove two channels of every shadow through zero,
`k = 0.03` did it less often, and neither was really a strength — both were
"how far can this go before it clips". Now `k` is a fraction of the pixel, and
it can be derived instead of tuned. A full-radius push toward a primary gives
the zero-sum offset `k·(1, −½, −½)`, so a neutral pixel comes out as

```
Y · (1 + k, 1 − k/2, 1 − k/2)      saturation = (max − min) / max = 1.5k / (1 + k)
```

`k = 0.25` is therefore **30% saturation from neutral at full travel** — a
strong grade, and the number is checkable rather than felt. Measured on both
sample frames, a full push moves the picture by 17–23% of what a one-stop
exposure change moves it: more than vibrance at full travel, less than
saturation. Predicted relative chroma for the test's wheel is 0.100 against
0.1077 measured, the difference being the mean shifting slightly off zero.

## The control surface — why a wheel, and which way it winds

**Where:** `app/ColorWheel.swift`.

**Source:** Van Hurkman, *Color Correction Handbook*, 2nd ed. (Peachpit, 2014),
ch. 4 — the three-way color balance control, its history in telecine, and why
the hue/strength disc rather than three sliders. The hue-angle convention is
HSV's: Alvy Ray Smith, *"Color Gamut Transform Pairs"*, SIGGRAPH '78,
Computer Graphics 12(3), 12–19 — red at 0°, increasing counter-clockwise.

**Why a disc.** Hue and strength are one gesture rather than two controls, and
the direction you push is the direction the color goes. That second property is
the whole value, and it is also the thing that is easy to get wrong.

**Which way it winds — and the bug that came of it.** The engine's
decomposition, `cos(θ − c·120°)`, is the mathematical convention: red at 0°,
green at 120°, blue at 240°, measured **counter-clockwise with y upward**.
SwiftUI's `AngularGradient` sweeps **clockwise from three o'clock in screen
coordinates, where y runs downward**. Painting the familiar R-Y-G-C-B-M ring
into it therefore put green exactly where the engine puts blue: dragging toward
what looked like green made the picture blue, and the wheel was lying about what
it did.

The ring is wound in reverse to match. `testGradePrimaryAngles` pins the engine
half — that each primary direction raises its own channel above the other two,
and that 60° is yellow — which is what makes the panel's ordering checkable
against something rather than against a reading of the code.

**Deliberately not on the disc: brightness.** Because the offsets are zero-sum,
a wheel changes hue and nothing else; the track beneath each one is the only
thing that moves that zone's luminance. Two controls, two axes, no interaction —
which is what makes a grade recoverable when you overshoot. Resolve's wheels put
luminance on a separate slider for the same reason.

**Gap:** the ring is a plain HSV hue sweep, not a vectorscope-aligned one. A
colorist reading a vectorscope alongside these wheels would expect the targets
at the SMPTE angles rather than at even 120° spacing. That is a real difference
and it is not implemented; there is no vectorscope in Orion yet to disagree with.

## Split toning — refused, and the argument for refusing it

`ROADMAP.md` listed split toning beside these wheels as an M3 item. It is not
built and it is not going to be. Decision #97.

**Split toning is the wheels, with fewer controls.** It is a hue and a saturation
for the shadows, a hue and a saturation for the highlights, and a Balance that
moves the crossover between them. Every one of those except Balance is already
here: two of the three wheels *are* the two tints, each already carries a
luminance track that split toning never had, and the third grades the midtones,
which split toning cannot reach at all.

**Adobe reached the same conclusion and acted on it.** Camera Raw 13.0 and
Lightroom Classic 10.0, October 2020, replaced the Split Toning panel with Color
Grading — three wheels, shadows/midtones/highlights, hue and saturation and
luminance each — and kept a Blending slider whose documented behaviour at 100 is
"the same effect as the pre-existing Split Toning feature".

- [Adobe: new and enhanced features, October 2020 release of Lightroom
  Classic](https://helpx.adobe.com/lightroom-classic/help/whats-new/2021.html)
- [Lightroom Classic 10.0 released, includes Color Grading and
  more](https://www.dpreview.com/news/8793158963/adobe-lightroom-classic-10-0-released-includes-color-grading-and-more)

So the request is not "build a feature Orion lacks". It is "build the control the
vendor of the reference implementation retired six years ago, beside the control
they retired it in favour of, which Orion already ships." A second panel would be
two ways to do one thing, disagreeing at the edges, and the maintenance is
permanent.

### ⚠ What split toning has that these wheels do not, said plainly

**Balance.** Orion's zones are Gaussians fixed at −2.5 / 0 / +2.5 EV with
σ = 1.6; nothing moves them. Split toning's Balance, and Color Grading's, slides
the crossover so a photographer can decide how much of the picture counts as
shadow. That is a real gap and it is the only one.

It is also **five lines in `color_grade.slang`** — the three centres shifted by a
signed offset in EV — and one float through the twenty files any adjustment
crosses. It belongs *on the grading panel*, beside the wheels whose bands it
moves, and not in a second panel wearing an older name. Costed in `ROADMAP.md`
rather than built in the same session as the vignette: one story per session, and
a half-threaded adjustment is the failure mode this repository has already paid
for twice.

## Position in the graph

After the color matrix and the tone controls, before the guided filter and the
display transform — **in scene-linear light**.

This matters and is the part most likely to be got wrong. Grading after the
display transform, which is where a "color balance" control usually sits in a
display-referred editor, applies the correction to values that have already been
through a filmic shoulder; the same offset then does something different to a
highlight than to a midtone for reasons that have nothing to do with the tonal
zones the user selected. In linear light the weights mean what they say.

## Confidence

High for the SOP arithmetic, which is a published transfer function. High for
the partition now: the previous version of this section said the linear
thresholds were "a reasonable reading ... and a reviewer could reasonably argue
for placing them in a log or perceptual space instead." A reviewer did, the
measurement agreed with them, and the log axis is what shipped.

What remains chosen rather than derived: the band centres at ±2.5 EV and
σ = 1.6. They inherit the tone controls' geometry, which is sourced, but
"shadows are two and a half stops down" is a convention rather than a
psychophysical boundary. Consistency with the tone bands is the argument for
them, and it is a real one — a photograph should not have two different ideas of
where its shadows are.

## Tests

`orion-tests`:

- `testGradeOffsets` — a wheel at any angle produces a zero-sum RGB offset, so a
  neutral keeps its luminance; radius scales it linearly; the center is exactly
  zero.
- `testGradePrimaryAngles` — each primary direction on the wheel raises its own
  channel above the other two, and 60° is yellow. This is what the panel's
  gradient ordering is checked against.

`testColorGradeGpu` — **added 2026-07-28**, over a neutral wedge spanning
−7…+4.5 EV:

- every wheel centred is the identity
- the shadow wheel tints its own zone and leaves the highlight zone alone
- **a wheel is worth the same at every level** — 0.1077 relative chroma at
  −3 EV against 0.1079 at +3 EV. This is the scale-invariance the Y-scaled
  offset exists for, and the one assertion that would have caught the original
  bug on its own
- zero-sum holds across the *whole* wedge, not at one level — the deep-shadow
  clamp is what broke it before
- the luminance slider lifts its own zone and leaves the far one alone

`orion-bench` — one probe per wheel, on a frame lifted 3 EV so all three zones
are populated, gated on how far the pixels actually moved.

**The instrument mattered more than the code here.** Three separate summary
metrics — mean luma, mean chroma, mean saturation — each reported a working
grading wheel as doing nothing, because a wheel rotates hue at roughly constant
saturation and a frame mean cancels it. The bench gates on mean absolute
per-pixel difference now. Measuring a hue rotation with a saturation average is
the same mistake as measuring an edge filter by mean brightness.

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

### The tonal partition

```
Y   = 0.2627·R + 0.6780·G + 0.0593·B        Rec.2020 luma, the working space
w_s = 1 − smoothstep(0.0, 0.5, Y)
w_h =     smoothstep(0.5, 1.0, Y)
w_m = 1 − w_s − w_h
```

`smoothstep` rather than a linear ramp because its derivative is zero at both
ends, so a correction fades in without a visible edge where the zones meet. The
three weights sum to 1 everywhere by construction, which is the property that
makes an untouched image come back bit-identical.

### The correction

Each zone contributes an **offset** (the color) and a **slope** (the
brightness), weighted by that zone's share of the pixel:

```
offset = Σ_z  w_z · O_z
slope  = 1 + Σ_z  w_z · s_z
out    = max(in × slope + offset, 0)
```

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

`k = 0.03`, and the number was measured rather than guessed. It started at 0.15,
which is a reasonable figure in a display-referred space and badly wrong here:
this is scene-linear light, where a dark patch sits around 0.005. A zero-sum
offset always has a negative component, so 0.15 drove two channels of every
shadow straight through zero and the shader's clamp held them there. On a night
frame the shadow patch measured luma 0.12 at k = 0.15 and 0.22 at k = 0.03 —
the *larger* setting was darker, because it was crushing channels to black
instead of tinting them. At 0.03 the same push moves mean saturation from 0.46
to 0.51 and leaves the tonality intact.

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

High for the SOP arithmetic, which is a published transfer function. Medium for
the partition thresholds (0.0/0.5 and 0.5/1.0): they are a reasonable reading of
"shadows, midtones, highlights" in linear light rather than a measured
psychophysical boundary, and a reviewer could reasonably argue for placing them
in a log or perceptual space instead.

## Tests

`orion-tests`:

- `testGradeOffsets` — a wheel at any angle produces a zero-sum RGB offset, so a
  neutral keeps its luminance; radius scales it linearly; the center is exactly
  zero.
- `testColorGradeGpu` — runs the real kernel. Asserts the identity (all controls
  at zero returns the input unchanged), that a shadow-only push moves a dark
  patch and leaves a bright one alone, that a highlight-only push does the
  reverse, and that the three weights sum to one by checking a mid-gray ramp is
  not dimmed anywhere.

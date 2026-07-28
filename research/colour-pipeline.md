# Colour Pipeline

Covers everything from sensor values to display pixels: linearisation, white
balance, the camera matrix, working space, and the display transform.

---

## Scene-referred pipeline design

**Where:** the whole engine. `develop_linear.slang` operates on unbounded linear
Rec.2020; there is exactly one display transform, in `develop_display.slang`.

**Source:** darktable's scene-referred workflow documentation, and vkdt's
pipeline design.
- [darktable — scene-referred workflow](https://docs.darktable.org/usermanual/3.6/en/overview/workflow/edit-scene-referred/) (2021)
- [vkdt — pipeline design notes](https://github.com/hanatos/vkdt/blob/master/src/pipe/readme.md), Johannes Hanika (darktable's original author)

**What it gives us:** editing operations stay physically meaningful. Exposure is
a multiply rather than a curve, values above 1.0 survive until the display
transform, and filtering happens on linear data so it does not shift hue.
darktable moved to this default because the display-referred alternative bakes
in a display assumption early and produces artefacts when filtering nonlinear
pixels.

**Confidence:** high. This is the direction the whole field has moved.

---

## Camera colour matrix

**Where:** `DevelopPipeline.cpp`, `camToWorking`.

**Source:** dcraw's `adobe_coeff` lineage, as carried by LibRaw and documented by
darktable and RawTherapee.
- [darktable — input colour profile](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/input-color-profile/)
- [RawPedia — Colour Management](https://rawpedia.rawtherapee.com/Color_Management)

**What we implement:** LibRaw supplies `cam_xyz` (XYZ → camera). We invert it to
get camera → XYZ, compose with XYZ → linear Rec.2020, then **normalise each row
to sum to 1**.

**Why the normalisation matters:** after white balance the data is already
neutral, so a matrix whose rows do not sum to 1 re-tints it. dcraw normalises
`rgb_cam` for exactly this reason. Omitting it produced a visible magenta cast
during development.

**Gap:** we use only the 3×3 matrix. DCP profiles additionally carry dual
illuminants, a ForwardMatrix, and HueSatMap LUTs, which is why Adobe's rendering
of the same file differs. Loading DCPs is planned for M5.
- [RawPedia — creating DCP profiles](https://rawpedia.rawtherapee.com/How_to_create_DCP_color_profiles)
- [dcamprof](https://torger.se/anders/dcamprof.html), Anders Torger — builds DCP/ICC from a ColorChecker or spectral data

**Confidence:** high for the baseline matrix; the DCP gap is a known accuracy
limit, not an error.

---

## White balance from colour temperature

**Where:** `WhiteBalance.cpp`.

**Source:** Kim, Jo, Kweon, Lee — *"Design of Advanced Color Temperature Control
System for HDTV Applications"* (2002); the cubic approximation of the Planckian
locus in CIE 1931 xy, valid 1667 K–25000 K.
- [Planckian locus — approximation](https://en.wikipedia.org/wiki/Planckian_locus#Approximation) (carries the coefficients and the citation)

**What we implement:** temperature → xy on the locus → XYZ at unit luminance →
camera RGB via `xyzToCam` → reciprocal, normalised to green.

**Important design choice:** the temperature is a *handle*, not the source of
truth. "As shot" uses the camera's own multipliers directly, and moving the
slider applies a **ratio** against the estimated as-shot temperature. Routing
as-shot through the estimate would bake every estimation error into the image as
a cast.

**Gap:** tint is applied as a straight offset in y. Strictly it should move
perpendicular to the locus in a uniform chromaticity space (CIE 1960 uv).
Adequate over photographic range; worth revisiting.

**Confidence:** high for temperature, medium for tint.

**Tests:** `orion-tests` checks direction (warmer needs more blue gain), green
normalisation, and round-trip accuracy within 60 K.

---

## Highlight clipping — the white level after white balance

**Where:** `linearize.slang`, `DevelopPipeline::whiteClipFor`.

**Source:** Dave Coffin, dcraw `scale_colors()`, highlight mode 0 — the default,
and the convention LibRaw inherits along with the rest of dcraw's front end.
- [dcraw source](https://www.dechifro.org/dcraw/dcraw.c) — `scale_colors()`,
  the `pre_mul` / `scale_mul` block ending in `CLIP(val)`
- [LibRaw docs: `imgdata.params.highlight`](https://www.libraw.org/docs/API-datastruct.html) — 0 clip, 1 unclip, 2 blend, 3+ rebuild

**The problem.** A sensor saturates at one count for every channel, so a blown
highlight arrives as (S, S, S). White balance then multiplies each channel by
its own gain — for a warm scene something like (2.2, 1.0, 1.6) — and what was a
white light now carries the gains themselves as a colour. Nothing downstream can
undo it: the tone curve, the colour matrix and AgX all preserve ratios, so they
preserve the cast, and it lands on every clipped light in the frame.

**What we implement.** Clip all three channels to one ceiling, in the mosaic,
inside `linearize`:

```
T_k  = (W − B_k) / (W − B_ref) · m_k        per-channel saturation level
clip = min_k T_k
```

`min` and not `max`: the lowest of the three is the brightest neutral the frame
can still describe. A channel above it is claiming more of one primary than a
white at full brightness, which the white point does not admit — and that claim
is precisely what an unclipped blown pixel makes.

**Why before demosaic.** dcraw clips here, and for a reason that shows up in the
output: RCD interpolates the mosaic, so an unclipped neighbour sitting at 2.2
drags the estimate at every pixel around it. Clip afterwards and the cast does
not stop at the highlight's edge, it spreads past it as a fringe.

**The cost.** The clip moves with white balance, so dragging temperature moves
the white point — correctly, but it does mean highlights shift under the slider.
It also throws away the headroom that a reconstruction could have used; the
highlight node now works from clipped data, predicting a clipped channel from
whichever channels are still reading, which is the part of Masood et al. that
does not need the headroom.

**Confidence:** high. This is what every raw converter does by default.

**Tests:** `orion-tests` → `testLinearizeClipsToWhite` renders a blown mosaic
and a midtone through the real kernel, and reads the result *per CFA channel* —
the cast is a difference between channels, and an average over all of them hides
it entirely. It asserts the blown half is neutral and lands on the clip, and
that the midtone still carries its gains, so a fix that desaturated everything
would fail rather than pass.

---

## AgX display transform

**Where:** `develop_display.slang`.

**Source:** Troy Sobotka's AgX, adopted as Blender 4.0's default view transform.
- [sobotka/AgX](https://github.com/sobotka/AgX) (2022–)
- [darktable — AgX module](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/agx/) (darktable 5.4, 2025)
- [Explainer: AgX and the "notorious six"](https://avidandrew.com/agx-color.html)

**What we implement:** convert Rec.2020 → Rec.709, apply the inset matrix, map to
normalised log2 exposure over [−12.47, +4.03] EV, apply contrast about the middle
grey pivot, run the six-term sigmoid fit per channel, then the outset matrix.

**Why the inset matters:** applying a sigmoid per channel in the working
primaries skews hue as channels clip at different points — the "notorious six"
failure, where bright saturated colours rotate toward the nearest primary. AgX
compresses the gamut inward first, so saturated highlights desaturate toward
white instead.

**⚠️ The bug this documentation exists to prevent.** An earlier revision used
inset/outset matrices whose rows did **not** sum to 1. The outset mapped neutral
grey (1,1,1) to roughly (0.84, 0.94, 1.22), lifting blue above green, and cast
**every image purple**. A gamut matrix in a tone mapper must preserve the
achromatic axis; row-sum-to-1 is that property, and it is checkable in one line.

`orion-tests` now runs a 12-stop neutral ramp through the real kernel and
asserts the output stays neutral within 8-bit rounding.

**Second bug worth recording:** the AgX polynomial is a fit of the *display*
transform, so its output is already display-referred. Applying an sRGB transfer
function on top encoded the signal twice — middle grey landed at 189/255 instead
of ~128, washing out tone and colour together. The neutrality test also checks
middle grey lands mid-range.

**Gap:** the sigmoid is a polynomial approximation, not the full AgX with its
per-channel look transforms. Output is sRGB-encoded; the EDR/P3 path the native
shell was chosen for is not wired up.

**Confidence:** high, now that neutrality is asserted.

---

## Rec.2020 ↔ Rec.709

**Where:** `develop_display.slang`, `kRec2020ToRec709`.

**Source:** ITU-R BT.2020 and BT.709 primaries; the derived matrix is standard.
- [ITU-R BT.2020](https://www.itu.int/rec/R-REC-BT.2020/en)

**Note:** AgX is defined against Rec.709 primaries. Applying it directly to
Rec.2020 data silently rotates every hue — not a subtle error, and one that is
easy to miss because the result still looks like a photograph.

---

## History

- **2026-07-27** — AgX inset/outset replaced after the purple-cast bug; Rec.2020 →
  Rec.709 conversion added; sRGB double-encode removed. Neutrality test added.
- **2026-07-27** — White balance re-anchored on the camera's own multipliers
  rather than an inferred temperature.

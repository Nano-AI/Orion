# Color Pipeline

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

## Camera color matrix

**Where:** `DevelopPipeline.cpp`, `camToWorking`.

**Source:** dcraw's `adobe_coeff` lineage, as carried by LibRaw and documented by
darktable and RawTherapee.
- [darktable — input color profile](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/input-color-profile/)
- [RawPedia — Color Management](https://rawpedia.rawtherapee.com/Color_Management)

**What we implement:** LibRaw supplies `cam_xyz` (XYZ → camera). We invert it to
get camera → XYZ, compose with XYZ → linear Rec.2020, then **normalize each row
to sum to 1**.

**Why the normalization matters:** after white balance the data is already
neutral, so a matrix whose rows do not sum to 1 re-tints it. dcraw normalizes
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

## White balance from color temperature

**Where:** `WhiteBalance.cpp`.

**Source:** Kim, Jo, Kweon, Lee — *"Design of Advanced Color Temperature Control
System for HDTV Applications"* (2002); the cubic approximation of the Planckian
locus in CIE 1931 xy, valid 1667 K–25000 K.
- [Planckian locus — approximation](https://en.wikipedia.org/wiki/Planckian_locus#Approximation) (carries the coefficients and the citation)

**What we implement:** temperature → xy on the locus → XYZ at unit luminance →
camera RGB via `xyzToCam` → reciprocal, normalized to green.

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
normalization, and round-trip accuracy within 60 K.

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
white light now carries the gains themselves as a color. Nothing downstream can
undo it: the tone curve, the color matrix and AgX all preserve ratios, so they
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

**What we implement:** convert Rec.2020 → Rec.709, apply the inset matrix, map
log2 exposure onto the sigmoid's normalized axis, apply contrast about middle
gray, run the six-term sigmoid fit per channel, then the outset matrix.

### ⚠️ The latitude is a look, and Orion's is 8 stops under gray, not 10

**Source for the number:** darktable's *filmic rgb*, whose **black relative
exposure defaults to −8.00 EV** against a +4.00 EV white — the same "how far
under middle gray does the toe run out" parameter, in the same units.
- [darktable — filmic rgb](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/filmic-rgb/) (scene tab, *black relative exposure*)
- [Aurélien Pierre, *filmic FAQ / darktable 3.0*](https://eng.aurelienpierre.com/2020/01/filmic-faq/) — why the black end is set from the scene's dynamic range rather than left at the transform's maximum

AgX's reference range is **10 stops** under middle gray and 6.5 above. That is
right for rendering a scene with ten usable stops beneath the key; a photograph
out of a camera does not have them, and carrying the reference value means the
darkest tones never reach black. Measured on an indoor frame from an ILCE-7RM3
(a lamp, a ball, shorts, grass, a shirt, a face) against macOS ImageIO's decode
of the same raw, patch means in display encoding:

| latitude under gray | darkest patch, Orion / macOS |
|---|---|
| 10 stops (AgX reference) | **1.63×** |
| 9 | 1.29× |
| 8.5 | 1.13× |
| **8 (shipped)** | **0.98×** |

At 8 stops every patch measured lands within 10% of the reference — lamp 0.95×,
ball 0.99×, shorts 0.95×, grass 1.07×, shirt 1.10×, face 1.01×, background
0.98× — where at 10 the highlights already agreed (0.97×) and only the shadows
did not. **The signature of a missing toe is exactly that**: agreement at the
top, monotonically worse the darker the patch. It is why the fix is the latitude
and not the contrast slope; a slope steep enough to buy the same toe takes the
low midtones with it, and at slope 1.8 the face lands at 0.058 against 0.085.

**⚠️ Third bug worth recording, and it is a trap for anyone changing this
number.** `agxCurve` is a fit over a *normalized* axis on which middle gray sits
at 10/16.5 = 0.606061 — not at the middle. Narrowing the range while leaving
`(ev − min) / (max − min)` in place slides gray down to 0.498, where the
polynomial returns 0.285 instead of 0.497, and the whole picture drops 1.7
stops. The axis is therefore normalized in **two pieces about gray**, pinning it
to the polynomial's anchor. `testAgxLatitudeIsAnAnchoredRescale` asserts middle
gray in gives middle gray out whatever the latitude is; the mutation that
restores the single expression fails it at 0.387.

### ⚠️ Re-measured 2026-09-05 against night frames: 8 stops stays, and the number that decides the look is the contrast

The frames #214 was fitted on are gone (`samples/*.ARW` were relinked to moon
photographs on 2026-09-03), so this is a **fresh fit on different frames**, not a
reproduction of the one above. Reference is macOS ImageIO (`sips -s format jpeg
-Z 900 <raw>`) and, where the camera wrote one, the in-camera JPEG. Every Orion
figure is the **product's own defaults** — exposure 0.00 EV, `Engine.contrast`
1.45 — read off the output texture with `--measure`.

**The night sky is not lifted.** Three night frames, mean display code value:

| frame | patch | macOS | Orion | Orion / macOS |
|---|---|---|---|---|
| DSC09506 · moon, 1/60 ISO 800 | sky | 0.0001 | 0.0000 | — |
| | moon disc | 0.9684 | 0.9340 | 0.96× |
| | whole frame | 0.0243 | 0.0220 | 0.91× |
| DSC09665 · moon in cloud, 1/40 ISO 3200 | sky, top-left | 0.0033 | 0.0038 | 1.15× |
| | sky, top-right | 0.0014 | 0.0021 | 1.50× |
| | moon glow | 0.2159 | 0.1996 | 0.92× |
| | lit cloud | 0.3329 | 0.3482 | 1.05× |
| | moon disc | 1.0000 | 0.9662 | 0.97× |
| DSC09640 · 30 s ISO 1250 star field | sky, top-left | 0.0593 | 0.0316 | 0.53× |
| | whole frame | 0.1019 | 0.0638 | 0.63× |

**Where it *is* lifted is at `contrast = 1.0`** — the `Adjustments{}` default
`orion-bench` renders with, and the value any headless measurement that does not
pass `--contrast 1.45` uses. Same frame, same regions:

| patch | macOS | Orion @ 1.0 | Orion @ 1.45 (ships) |
|---|---|---|---|
| sky, top-left | 0.0033 | 0.0199 (6.0×) | 0.0038 (1.15×) |
| sky, top-right | 0.0014 | 0.0143 (10.2×) | 0.0021 (1.50×) |
| sky, upper middle | 0.0055 | 0.0292 (5.3×) | 0.0064 (1.16×) |
| lit cloud | 0.3329 | 0.3915 (1.18×) | 0.3482 (1.05×) |
| whole frame | 0.1220 | 0.1505 (1.23×) | 0.1123 (0.92×) |

⚠️ **A milky night sky measured off the bench is a milky night sky nobody is
shown.** The slope pivots about gray on the normalized axis, so 1.45 multiplies
the distance to the black end: the render reaches zero at about **5.5** stops
under gray, not at `kBlackStops`. The name of the constant describes the axis,
not the shipped toe.

**The latitude sweep, at 1.45.** `sips`/camera-JPEG references in the header row:

| `kBlackStops` | night sky (0.0033) | night cloud (0.3329) | night whole (0.1220) | day hollow (0.0690 / 0.0877) | day log (0.7172 / 0.7132) | day whole (0.3340 / 0.3605) |
|---|---|---|---|---|---|---|
| 6 | 0.0004 | 0.3055 | 0.0898 | 0.0143 | 0.9045 | 0.4250 |
| **8 (ships)** | **0.0038** | **0.3482** | **0.1123** | **0.0401** | **0.9045** | **0.4511** |
| 10 (AgX reference) | 0.0115 | 0.3758 | 0.1355 | 0.0758 | 0.9045 | 0.4728 |
| 12 | 0.0223 | 0.3949 | 0.1584 | 0.1139 | 0.9045 | 0.4905 |

Two things fall out. **#214's central claim survives on the GPU**: the bright
driftwood log reads 0.9045 at every latitude, so this really is a shape control
and the highlights genuinely do not move. And **narrowing makes both frames
worse** — at 6 the night sky is 0.0004 against 0.0033 and the daylight shadow is
0.0143 against 0.069–0.088. The two frames disagree about the *right* value
(the night sky prefers 8, the daylight shadow prefers 10), which is a look
question and not one these measurements settle; 8 is kept because it is the
incumbent and the only setting where neither frame is badly wrong.

**Baseline exposure cannot fix a contrast error.** `kBaselineExposureEv` swept on
the one daylight frame that *is* off (DSC09760, deep forest shade):

| `kBaselineExposureEv` | whole frame | bright log | deep hollow |
|---|---|---|---|
| **1.2 (ships)** | 0.4511 | 0.9045 | 0.0401 |
| 0.9 | 0.4103 | 0.8768 | 0.0295 |
| 0.6 | 0.3708 | 0.8442 | 0.0218 |
| macOS / camera JPEG | 0.3340 / 0.3605 | 0.7172 / 0.7132 | 0.0690 / 0.0877 |

Lowering it lands the *mean* and leaves the shape wrong: the log is still blown
and the hollow gets darker still. On the other three daylight frames Orion
already agrees with both references — whole-frame mean 0.5318 vs 0.5331/0.5460,
0.4878 vs 0.5083/0.5439, 0.5251 vs 0.5042/0.5090 — so #46's +1.2 EV is not the
defect either. **What DSC09760 shows is too much contrast, and the contrast is
`Engine.contrast = 1.45`, which #46 co-fitted with the baseline and which this
work did not touch.**

**Confidence:** the night-frame conclusion is high (three frames, two of them
without sidecars, both instruments agreeing). The daylight side rests on four
frames from one camera and one afternoon.

**Why the inset matters:** applying a sigmoid per channel in the working
primaries skews hue as channels clip at different points — the "notorious six"
failure, where bright saturated colors rotate toward the nearest primary. AgX
compresses the gamut inward first, so saturated highlights desaturate toward
white instead.

**⚠️ The bug this documentation exists to prevent.** An earlier revision used
inset/outset matrices whose rows did **not** sum to 1. The outset mapped neutral
gray (1,1,1) to roughly (0.84, 0.94, 1.22), lifting blue above green, and cast
**every image purple**. A gamut matrix in a tone mapper must preserve the
achromatic axis; row-sum-to-1 is that property, and it is checkable in one line.

`orion-tests` now runs a 12-stop neutral ramp through the real kernel and
asserts the output stays neutral within 8-bit rounding.

**Second bug worth recording:** the AgX polynomial is a fit of the *display*
transform, so its output is already display-referred. Applying an sRGB transfer
function on top encoded the signal twice — middle gray landed at 189/255 instead
of ~128, washing out tone and color together. The neutrality test also checks
middle gray lands mid-range.

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

- **2026-08-14** — AgX's black latitude cut from the reference 10 stops under
  middle gray to darktable filmic's 8, after a photograph opened with its
  darkest patch 1.63× brighter than macOS ImageIO's decode. The log axis is now
  anchored in two pieces so middle gray cannot move with the range.
- **2026-09-05** — a washed-out night sky was re-measured on three night frames
  and did not reproduce at the product's defaults; it reproduces at
  `contrast = 1.0`, which only the bench renders with. 8 stops kept, nothing
  changed, and the shipping contrast is now asserted on the GPU (#220).
- **2026-07-27** — AgX inset/outset replaced after the purple-cast bug; Rec.2020 →
  Rec.709 conversion added; sRGB double-encode removed. Neutrality test added.
- **2026-07-27** — White balance re-anchored on the camera's own multipliers
  rather than an inferred temperature.


## White balance: temperature and tint, done Adobe's way — 2026-07-29

**This replaces a real defect.** Tint used to shift CIE 1931 *y* by
`tint × 0.05`. That is wrong three ways at once: wrong **space** (the offset
belongs in CIE 1960 UCS, not in the non-uniform 1931 xy plane), wrong
**direction** (it runs along the isotemperature line, whose slope changes with
temperature, not along a fixed axis), and wrong **scale**.

**Method:** Robertson, A. R., *Computation of Correlated Color Temperature and
Distribution Temperature*, JOSA **58**(11), 1968, 1528–1535
([DOI](https://doi.org/10.1364/JOSA.58.001528)).

**The numbers are not Robertson's.** Adobe's own comment in
`dng_sdk/source/dng_temperature.cpp` attributes the 31-row isotemperature table
to Wyszecki & Stiles, *Color Science: Concepts and Methods, Quantitative Data
and Formulae*, 2nd ed., Table 1(3.11), p. 228. Cite both: Robertson for the
method, W&S for the table.

Orion implements `dng_temperature::Get_xy_coord`: bracket on reciprocal
temperature, interpolate the locus point, blend and **renormalise** the two
bracketing isotherm unit vectors — not the slopes, which is a different thing
once they steepen past −100 — and displace along the result by
`tint / −3000`, Adobe's `kTintScale`. Orion's −1…1 tint maps onto Adobe's
±150, which is also Lightroom's range.

### ⚠️ A typo is kept on purpose

Row `r = 325` carries `u = 0.24702` in the DNG SDK, versions 1.1 through 1.7.1.
Bruce Lindbloom's transcription — and every implementation descended from it,
including RawTherapee and colour-science — uses **0.24792**, with an explicit
correction note. Recomputing the locus from Planck's law against the CIE 1931 2°
observer gives **0.247924**: the error at that row is roughly *two hundred times*
any other row's, so 0.24702 is a genuine mistake in the source book, copied
verbatim by Adobe and still shipping.

**Orion keeps 0.24702**, because the goal is to agree with Adobe rather than
with physics. A photographer cross-checking a tungsten frame against Lightroom
should see the same numbers. Correcting it would move the white point by up to
**0.0011 in xy around 3080 K** — about 23 K and 1.1 tint units, squarely in
tungsten territory. Bug-compatibility is the deliberate choice, recorded here so
it is not "fixed" by someone who spots it.

### What the tests pin

Thirteen `(temperature, tint) → xy` vectors from a line-by-line port of Adobe's
routine, matched to better than 2 × 10⁻⁵. And separately, that the tint
displacement's **direction turns with temperature** — which is exactly what the
old fixed-axis version could not do.

⚠️ **D65 does not sit at tint 0.** It lands near **+9.77**, because D65 is on
the *daylight* locus, which is above the Planckian locus in uv; every D-series
illuminant reads about +9.5 to +10. Illuminant A, which *is* defined as a
Planckian radiator at 2856 K, reads tint 0.008 — that is the sanity check that
the locus is right. Anyone writing a test that asserts `tint(D65) ≈ 0` will find
it fails against real Adobe behaviour, and the test would be wrong, not the code.

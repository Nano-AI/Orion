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

### ⚠️ 2026-09-06: the contrast that gives punch is the contrast that hard-clips shadows, and a mean-luma fit cannot see it (decision #222)

The developer, repeatedly, on a daylight frame: *"the RAW doesn't render
anything close to the JPEG. it comes out blasted, over contrasted, and looking
awful."* Confirmed on `DSC09734.ARW`: a person in the bottom-left corner
disappears into flat black that the camera's own JPEG still shows with
texture. The mechanism was already known — `develop_display.slang:238`,
`c = saturate((c - kPivotNorm) * p.contrast + kPivotNorm)`, is a **hard clamp
in log space, applied before the AgX sigmoid**. Solving it, the clamp lands at
`-kBlackStops / contrast` stops under gray: `-8.0` at `contrast = 1.0`,
**`-5.517`** at the shipping `1.45` — 2.5 stops shallower than the declared
8-stop latitude.

**7 frames** (`~/Pictures/sept 5th forks/`, none with `.xmp` sidecars except
the reported frame, whose sidecar was overridden for the test), covering deep
shade, open daylight and backlit, a shadow/midtone/highlight patch each,
against the camera's own JPEG and `sips -s format jpeg` (macOS ImageIO):

| frame | patch | cam JPEG | macOS | Orion @1.00 | Orion @1.45 (ships) |
|---|---|---|---|---|---|
| DSC09734 (reported) | shadow | 0.0264 | 0.0240 | 0.0734 | 0.0269 |
| | midtone | 0.4371 | 0.4218 | 0.5148 | 0.5189 |
| | highlight | 0.9673 | 0.9615 | 0.8268 | 0.9104 |
| DSC09736 (sidewalk, sun) | shadow | 0.0723 | 0.1184 | 0.2513 | 0.1693 |
| | midtone | 0.4449 | 0.4728 | 0.5044 | 0.5080 |
| | highlight | 0.9694 | 0.9673 | 0.8097 | 0.8986 |
| DSC09760 (forest hollow) | shadow | 0.0355 | 0.0233 | 0.1217 | 0.0679 |
| | midtone | 0.4490 | 0.4304 | 0.5913 | 0.6321 |
| | highlight | 0.8031 | 0.8041 | 0.8102 | 0.9039 |
| DSC09765 (mossy cave) | shadow | 0.0170 | 0.0076 | 0.0447 | 0.0122 |
| | midtone | 0.4687 | 0.4336 | 0.4827 | 0.4802 |
| | highlight | 0.6589 | 0.6397 | 0.7131 | 0.7660 |
| DSC09783 (backlit arch) | shadow | 0.0202 | 0.0065 | 0.0266 | 0.0041 |
| | midtone | 0.4354 | 0.3609 | 0.3666 | 0.3463 |
| | highlight | 0.9654 | 0.9810 | 0.8193 | 0.9112 |
| DSC09759 (overcast portrait) | shadow | 0.0203 | 0.0133 | 0.0837 | 0.0289 |
| | midtone | 0.4515 | 0.4079 | 0.4819 | 0.4781 |
| | highlight | 0.7371 | 0.7195 | 0.6251 | 0.6787 |
| DSC09747 (overcast beach) | shadow | 0.1051 | 0.1384 | 0.3341 | 0.2706 |
| | midtone | 0.4518 | 0.4736 | 0.5226 | 0.5344 |
| | highlight | 0.9691 | 0.9768 | 0.8275 | 0.9199 |

**⚠️ The trap: a mean-luma fit, swept from 0.8 to 2.0 contrast, does not
favor lowering it — it favors raising it further.** Mean absolute error
against macOS, aggregated over all 21 patches:

| contrast | all | shadow | midtone | highlight |
|---|---|---|---|---|
| 0.80 | 0.1122 | 0.1249 | 0.0651 | 0.1467 |
| 1.00 | 0.0878 | 0.0863 | 0.0662 | 0.1110 |
| 1.15 | 0.0767 | 0.0652 | 0.0685 | 0.0965 |
| **1.45 (ships)** | **0.0615** | **0.0359** | **0.0752** | **0.0733** |
| 1.60 | 0.0565 | 0.0271 | 0.0780 | 0.0643 |
| 2.00 | 0.0516 | 0.0186 | 0.0844 | 0.0518 |

Every column keeps improving past 1.45. This is not a contradiction of the
crush — it is the shadow-patch *mean* being pulled up by whichever pixels in
the patch survive the clip, while highlight error falls because the same slope
that clips shadows is the only thing giving highlights punch (they sit below
the +3.674 EV data ceiling, so it is the slope and not a clamp doing that
work — decision #221). **A single coarse mean cannot see a hard clip**: it
only sees that the average got darker, which happens to move toward the
reference for an unrelated reason.

**What does see it: the fraction of a real patch flattened to one value.**
Orion's own GPU output (`--scenario`'s `shot`, not the bench), same three
deep-shadow patches, `near-black` = luma < 2/255, `exact-(0,0,0)` = the literal
hard-clip signature a sensor's noise floor does not produce on its own:

| contrast | DSC09734 near-black / exact-0 | DSC09765 | DSC09783 |
|---|---|---|---|
| 1.00 | 0.01% / 0.00% | 1.94% / 0.00% | 0.71% / 0.04% |
| 1.15 | 0.15% / 0.00% | 12.76% / 0.45% | 7.67% / 0.32% |
| 1.30 | 2.77% / 0.00% | 38.39% / 6.32% | 48.84% / 4.15% |
| **1.45 (ships)** | **20.73% / 0.07%** | **62.62% / 26.14%** | **91.82% / 36.01%** |
| 1.60 | 52.30% / 1.10% | 78.26% / 49.01% | 99.73% / 78.33% |

Same patches, the two references: cam JPEG 0% exact-black on all three;
macOS ImageIO 0% / 1.30% / 2.47% — macOS's own default rendering is not gentle
(it is *darker* than Orion's on these patches, per the table above), but it
gets there with a soft toe, not a flat plateau: at the shipping contrast Orion
produces 15-25x more literal `(0,0,0)` pixels than macOS on the same patch.
That is the signature of a hard clamp specifically, not of "shadows going
dark," which both references also do.

**Conclusion: contrast alone does not fix this, and no compromise value is
defensible.** The crush grows continuously and steeply from 1.0 — there is no
plateau, no knee that separates "safe" from "not." A contrast low enough to
keep exact-black near zero on the deepest patch measured (≈1.0-1.05) gives up
essentially all the midtone/highlight punch decision #46 fitted 1.45 *for*,
returning the "flat and washed" complaint #46 was answering in the first
place. `Engine.contrast` **stays at 1.45**, undefended as anything but the
least-bad single number, and the doc comment now says so.

**`kBaselineExposureEv` was not touched, and could not have helped** — #220
already established this ("baseline exposure cannot fix a contrast error"):
shifting the baseline moves *which* scene EVs land in the clipped zone, not
whether the clamp is hard. Raising it to lift shadows clear of the clip pushes
more of the frame past the highlight ceiling and re-breaks the midtone fit
#46 measured; lowering it makes the clip strictly worse.

**Built, decision #223 — a switch, not a single answer.** The developer asked
for a blind A/B across published operators rather than one this session
picked, so `develop_display.slang:238`'s hard `saturate` is now
`DisplayParams::rollOff`-gated: mode 0 is that exact `saturate` and stays the
default everywhere (verified byte-for-byte in the paragraph below), modes 1-4
are four published soft-clips. `ORION_ROLLOFF=0..4` (an env var, mirroring
`ORION_DEBUG_NOISE`) or `--batch-export ... --rolloff N` selects one; nothing
in a filename, log line or EXIF field says which.

**Mode 1 — ACES Reference Gamut Compression.** Academy of Motion Picture Arts
and Sciences, *"Reference Gamut Compression Specification"*, ACES 1.3,
https://docs.acescentral.com/rgc/specification/. The published parametric
family is `f(x) = t + (x-t) / (1 + ((x-t)/s)^p)^(1/p)` for `x >= t`.
**`s = 1 - t` here, not the spec's `l`-derived `s`**: the spec solves for `s`
so the curve reaches exactly 1 at a chosen limit `l`, which is correct for a
gamut *distance* and puts the asymptote above 1 — wrong for a value that must
stay under 1. `s = 1 - t` asymptotes to exactly 1. `p = 2`, so the root is a
`sqrt` and `f'(u) = (1 + (u/s)^2)^(-3/2)`, positive for every real `u` —
monotonic by construction, not by clamping. Mirrored for the low side by the
identity `g(y) = 1 - f(1-y, 1-t)` rather than a second derivation.
`engine/shaders/ops/rolloff_ops.slang`, `acesRgc`/`acesRgcHi`.

**Mode 2 — ITU-R BT.2390 EETF.** Report ITU-R BT.2390-11 (07/2023), *"High
dynamic range television for production and international programme
exchange"*, Annex 5. The knee-to-white shoulder is a cubic Hermite spline —
value `KS` and tangent 1 at the knee (matching identity), value `MAXLUM` and
tangent 0 at the top — `T = (E-KS)/(1-KS)`,
`P(T) = (2T³-3T²+1)·KS + (T³-2T²+T)·(1-KS) + (-2T³+3T²)·MAXLUM`. The PDF text
itself would not extract cleanly through automated fetching (an image-scanned
page); the formula above was cross-checked against two independent secondary
technical sources describing the same standard (a GitHub issue quoting it and
a third-party HDR tone-mapping reference) rather than taken from memory alone.
⚠ **The standard's own domain does not fit here as published.** BT.2390
assumes its input already lives in `[KS, 1]`, true for a PQ signal and false
once `contrast > 1` pushes this axis's `y` past 1 — using the formula as
written would re-hard-clip anything past `y = 1`, the defect this exists to
remove. So the far anchor is this pixel's actual reachable extreme (`yMin`/
`yMax`, computed from `x ∈ [0,1]` through the live contrast) rather than a
literal 0/1: an adaptation of the published formula's domain, not the formula
itself. `bt2390`/`hermite01` in the same file.

**Mode 3 — Reinhard.** Reinhard, Stark, Shirley & Ferwerda, *"Photographic
Tone Reproduction for Digital Images"*, ACM TOG 21(3), 2002, the global
operator `L/(1+L)` (their eq. 3), applied about the pivot instead of 0 so gray
is its fixed point. **Deliberately has no identity zone** — it is the same
mirrored-rational family as mode 1 with the identity-zone half-width set to
zero, so compression starts the instant a value leaves gray. Included as the
comparison's "no latitude" arm, not fixed to match the others: the measured
table below shows it perturbing midtones and highlights on every frame, which
is expected and is why it is there.

**Mode 4 — darktable filmic rgb's construction, from its published
description, not its (GPL) source.** Worked from Aurélien Pierre's
*"Filmic, darktable and the quest of the HDR tone mapping"*
(eng.aurelienpierre.com, Nov. 2018) and independently re-fetched to verify
before implementing — `src/iop/filmicrgb.c` was not opened, read, or
consulted, per `CLAUDE.md`'s rule that a GPL module's source is not a
published algorithm even as a check on a constant. The article describes a
middle segment linear in the log-encoded axis with slope = contrast, joined to
toe/shoulder quartics by value + first + second derivative continuity at the
latitude bounds and zero tangent at the black/white extremes — five conditions
on a quartic's five coefficients, an exactly determined linear system. This
session solved that system itself (`research/UNSOURCED.md §31` has the
worked boundary-value derivation); the coefficients in `filmicToe`/
`filmicShoulder` are this session's solution, not a transcription. The
article's display-space grey anchor (`G_d = 0.18^(1/gamma)`) is replaced with
`kPivotNorm` — an adaptation, registered in `research/UNSOURCED.md §31`, since
Orion's roll-off sits earlier in the pipe than Pierre's gamma encode.

**Where the identity zone ends, for modes 1/2/4.** Symmetric about
`kPivotNorm` on the contrast-scaled axis, sized from decision #221's measured
real-photograph ceiling: real frames' brightest content never exceeds
**+3.674 EV** over gray, so setting the high threshold there means every
measured highlight sits at or under it — untouched, not merely close. Solving
for the half-width that puts the threshold at that ceiling at the shipping
contrast (1.45) gives **`kRollOffHalf` = 0.3229** on the normalized axis,
`kRollOffLo = 0.2832`, `kRollOffHi = 0.9289` — preserving **~2.94 EV** below
gray and **3.674 EV** above it untouched. That preserved range is this
session's own rule (not read off any of the four papers) for turning one
empirical number into two thresholds; `research/UNSOURCED.md §31` says so.
Mode 4 uses the pre-contrast equivalent of the same boundary (`kRollOffXLo` =
0.3834, `kRollOffXHi` = 0.8287) so all three threshold-based modes share
exactly the same identity zone and the comparison isolates *shape*, not zone
width.

**Injectivity, on the GPU (`testDisplayRollOffIsInjective`,
`apps/tests/tests_display.cpp`).** Two scene EVs 1.5 EV apart (-6 and -7.5
under gray), both inside the declared 8-stop latitude and both past mode 0's
contrast-scaled clip boundary (`kBlackStops/contrast` = -5.517 EV, decision
#222) — under mode 0 both render `0.000000`; the test asserts they must. Every
other mode renders them distinctly, asserted the same way: mode 1
`0.006065`/`0.002340`, mode 2 `0.003437`/`0.000000`, mode 3
`0.053955`/`0.038910`, mode 4 `0.001512`/`0.000000` (measured `fp16` values,
green channel). One check per direction, not a description plus a manual
restore-the-clamp step.

**Mode 0 is not quite bit-identical to the build before `rollOff` existed —
measured, not assumed.** `-ffast-math` (`engine/shaders/CMakeLists.txt`, a
project-wide flag, unchanged by this session) lets the shader compiler
contract and reassociate float arithmetic, and its choices for mode 0's
expression shifted when the other four branches were added to the same
kernel, even though that expression is textually identical to what shipped
before. Measured directly (two metallibs, one built from each source, same
dispatch): a 4096-point gray-ramp sweep at the shipping contrast differs at 2
of 4096 samples, by exactly one `fp16` ULP (`0.000488`), both within half a
stop of the old clip boundary where `agxCurve`'s slope is steepest. On
`samples/_PIC8095.ARW`, `--batch-export`'s default output differs from a
pre-`rollOff` build's at 3,667 of 127,169,280 decoded pixel bytes (0.0029%),
max per-channel delta 4/255 — a handful of pixels crossing an 8-bit rounding
edge in deep shadow, which then cascades through the rest of a JPEG's
entropy-coded stream the way any single-pixel change does. This is a
compiler-codegen effect of the branches existing at all, not a logic
difference in mode 0's own arithmetic, and fixing it would mean touching
`-ffast-math` for every shader in the engine — outside this session's scope
and this file's ownership.

**Measured: near-black / exact-0% of a real dark patch, five modes, seven
frames** (`~/Pictures/sept 5th forks/`, `--scenario`'s `open`+`shot` so no
saved `.xmp` state intervenes; patch picked per frame from the darkest cell of
a 12×12 grid over the camera's own JPEG, applied as the same fractional box to
every render):

| frame | camera JPEG | macOS ImageIO | mode 0 (hard) | mode 1 (ACES) | mode 2 (BT.2390) | mode 3 (Reinhard) | mode 4 (filmic) |
|---|---|---|---|---|---|---|---|
| DSC09734 | 0.02% / 0.000% | 0.00% / 0.000% | 3.06% / 0.008% | 0.26% / 0.000% | 0.62% / 0.000% | 0.00% / 0.000% | 1.31% / 0.000% |
| DSC09765 | 6.53% / 0.543% | 26.15% / 0.470% | 29.05% / 9.616% | 10.76% / 0.013% | 15.66% / 0.585% | 0.00% / 0.000% | 21.38% / 1.745% |
| DSC09783 | 1.31% / 0.024% | 60.50% / 0.242% | 70.14% / 19.757% | 21.77% / 0.004% | 33.04% / 0.300% | 0.00% / 0.000% | 51.06% / 1.275% |
| DSC09745 | 0.05% / 0.000% | 4.52% / 0.000% | 2.51% / 0.024% | 0.12% / 0.000% | 0.29% / 0.000% | 0.00% / 0.000% | 0.81% / 0.000% |
| DSC09755 | 0.13% / 0.008% | 5.84% / 0.194% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% |
| DSC09775 | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% |
| DSC09800 | 0.01% / 0.002% | 0.00% / 0.000% | 0.08% / 0.001% | 0.00% / 0.000% | 0.00% / 0.000% | 0.00% / 0.000% | 0.01% / 0.000% |

⚠ This is a *different* patch on each frame than decision #222's own table
(pixel coordinates were never recorded there), so absolute magnitudes are not
directly comparable frame-for-frame — the shape is: on every frame with a real
crush (DSC09765, DSC09783), modes 1/2/4 all cut it substantially, mode 1 by
the most and mode 4 by the least, and mode 3 eliminates it by moving the whole
patch instead (next paragraph). Frames with little or no crush at mode 0
(DSC09755, DSC09775, DSC09800) show all modes near zero, as they should.

**Midtones and highlights, same seven frames, mean luma of a midtone and a
bright patch.** Modes 0/1/2/4 agree to 4-6 significant figures on every
frame's bright patch and all but two frames' midtone patch (where a 12×12
grid cell straddles the roll-off boundary by a few percent of its area,
moving the cell mean by ~1e-4 — not the midtones themselves moving, the patch
partly overlapping the tail). Mode 3 moves both on every frame: bright patch
falls 8-24% (e.g. DSC09783: 0.9469 -> 0.8133; DSC09745: 0.9600 -> 0.8239),
confirming its no-identity-zone design perturbs exactly what it is documented
to perturb. Full per-frame table in the decision-223 validation run;
representative rows:

| frame | patch | mode 0 | mode 1 | mode 2 | mode 4 | mode 3 |
|---|---|---|---|---|---|---|
| DSC09734 | bright | 0.942777 | 0.942777 | 0.942777 | 0.942777 | 0.811030 |
| DSC09783 | bright | 0.946906 | 0.946906 | 0.946906 | 0.946906 | 0.813308 |
| DSC09775 | bright | 0.892269 | 0.892269 | 0.892269 | 0.892269 | 0.776622 |
| DSC09765 | mid | 0.500655 | 0.500655 | 0.500655 | 0.500655 | 0.498709 |
| DSC09783 | mid | 0.431064 | 0.431181 | 0.431234 | 0.431123 | 0.438931 |

**No winner chosen.** Mode 0 stays the default everywhere; the developer
selects blind from rendered images.

**Confidence:** the crush and its fix are both measured on the GPU, not
inferred from a mean (decision #222's mutation test; this session's
injectivity test). Which shape is *preferred* is a look question this
session's numbers do not settle — that is the point of shipping four rather
than one.

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

- **2026-09-06** — a hue-only ablation (pre- vs post-AgX linear buffer, two
  frames, 11 patches) placed the developer's "over saturated greenish image" /
  "color is gone" complaint upstream of the display transform: the pre-AgX
  buffer and the full post-AgX render disagree with the camera JPEG by 8.4°
  and 9.3° respectively, and hand-running AgX's own inset/curve/outset on the
  pre-AgX values moves hue by under 1° on every patch. `develop_display.slang`
  is close to hue-preserving away from its roll-off boundary; the rotation is
  in white balance / the camera matrix / the profile stage. Fixed by fitting a
  second `HueSatMap` region (warm/tan, center 38°, −10°) alongside the
  existing blue-sky one — mean error against the camera JPEG **9.1° → 2.2°**
  across 14 patches, 5 frames, landing next to the macOS−camera baseline
  (+2.09°) rather than at zero. Full method, fit sweep and the verification
  table: `research/camera-profiles.md`. Chroma re-measured after (not
  assumed fixed): saturation ratio to the camera JPEG is **0.83**, still low,
  left open (decision #225).
- **2026-09-06** — ACES RGC (mode 1) became the default roll-off after a blind
  A/B across #223's five operators came back unable to tell four of them
  apart — the developer identified Reinhard's missing identity zone every
  time and never the other four from each other — so the choice moved to
  injectivity (#224), which is measurable rather than a look. Did not touch
  hue; the complaint that motivated it was chroma and white balance, which
  #225 above addresses.
- **2026-09-06** — the hard clamp #222 measured is now switchable
  (`DisplayParams::rollOff`, `ORION_ROLLOFF`/`--rolloff`) rather than replaced
  with one chosen fix: mode 0 is the same clamp and stays the default, modes
  1-4 are ACES RGC, ITU-R BT.2390's EETF, Reinhard, and darktable filmic rgb's
  construction from its published description. All four cut the measured
  crush; mode 3 does it by perturbing midtones and highlights too, which is
  documented as the point of including it. No winner chosen (#223).
- **2026-09-06** — the shipping contrast's hard clamp measured on real
  photographs (a person disappearing in shadow): 21-92% of a genuinely dark
  patch flattened to one value at 1.45, against 0-2.5% for the camera JPEG and
  macOS. No contrast value fixes it without giving up the midtone punch #46
  fitted for; `Engine.contrast` unchanged, the defect documented, a soft
  roll-off specified but not built (#222).
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

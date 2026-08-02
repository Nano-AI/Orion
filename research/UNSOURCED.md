# Unsourced — the honest register

Everything here is **my formulation, not a published algorithm**. It is listed
so nobody mistakes plausible code for grounded code, and so the list can shrink.

Ordered by how much the gap actually costs.

**Resolved 2026-07-27:** the demosaic. RCD is now ported from the reference
implementation — see [demosaic.md](demosaic.md).

---

## 1. Highlight / shadow mask shapes

**Where:** `ops/tone_ops.slang`, `applyTone`.

**Sourced:** the local, guided-filter-driven *structure*
([tone-and-local-contrast.md](tone-and-local-contrast.md)).

**Rewritten 2026-07-28** — this entry described smoothstep knees at −4…+1 EV
that the code has not used since the partition-of-unity change. A register that
describes code which no longer exists is worse than no register.

**Not sourced:** the band centers (−5.5 / −2.5 / +2.5 / +5.5 EV relative to
middle gray), `kBandSigma = 1.6`, and `kEvPerUnit = 2.0`. The *shape* — Gaussian
bands normalized to a partition of unity, summing exponents rather than
multiplying gains — is sourced (deep-research §3); where the centers sit is not.

**Cost:** the controls will not agree numerically with Lightroom or darktable.
A tuning difference rather than a behavioral one, but "+50 shadows" still will
not mean the same thing.

**To fix:** derive centers from a published tone-mapping operator, or calibrate
against reference renders of the same file.

---

## 2. Output sharpening amounts

**Where:** `util/ImageWriter.mm`, `unsharpFor`.

**Sourced:** the *pass* and its placement — an unsharp mask applied after the
resize, at final output size, stronger for print than for screen. Fraser and
Schewe (2009); see [detail.md](detail.md#output-sharpening).

**Not sourced:** the two numbers each preset carries.

| Preset | Gaussian sigma | Amount |
|---|---|---|
| Screen | 0.6 px | 0.40 |
| Print  | 1.0 px | 0.80 |

**Said plainly: I chose these.** Fraser's book gives the workflow and the
reasoning — ink spreads on paper, so a print takes more than a screen — but the
figures a specific implementation should use are not in it, and PhotoKit
Sharpener's are proprietary. Nothing was measured against a print.

**What is held instead.** The invariant a photographer would actually notice is
tested, even though the constants are not derived: None does nothing at all,
Screen overshoots a step edge, Print overshoots more than Screen, none of them
moves the frame's overall brightness, and all three channels move together so a
neutral edge cannot pick up a color fringe. If someone replaces these numbers
with measured ones, those assertions still hold and the tests do not need
rewriting.

**Cost:** low but real. "Print" is a plausible amount rather than a calibrated
one, and a print at 240 ppi and one at 360 ppi are given the same treatment when
Fraser's model says they should not be.

**To fix:** derive the radius from the output resolution — the model is a
function of the output process, and two presets are a two-point sample of it.
That needs the panel to know the intended print size, which it does not today.

---

## 3. Vibrance weighting

**Where:** `ops/tone_ops.slang`, `applyColor`.

**Not sourced:** vibrance backs off in proportion to `1 − chroma`, where chroma
is `(max − min) / max`. The *idea* — weight the boost by how unsaturated a
color already is — is universal and correct. The specific weighting is mine.

**Cost:** low. It behaves sensibly; it just is not anyone's published curve.

---

## 4. Color mixer band weighting

**Where:** `ops/hsl_ops.slang`.

**Not sourced:** eight bands at 0/30/60/120/180/240/285/320°, weighted by
`(1 − distance/60°)²` and normalized.

**Reasoned, not arbitrary:** the 60° falloff makes neighbouring bands overlap so
a gradient crossing between them does not band, and squaring sharpens the
attribution. Band names and centers follow Lightroom's Color Mixer so a user's
existing instincts transfer.

**Cost:** low.

**One real subtlety, handled:** HSL is bounded to 0–1 but the working data is
not. Clamping into range would crush every highlight above 1.0 to white and
strip its color — an actual bug that shipped. It now normalizes by the peak
channel and restores magnitude afterwards, which is safe because hue and
saturation are scale-invariant.

---

## 5. Sharpening

**Where:** `sharpen.slang`.

**Not sourced:** unsharp mask with a binomial kernel and a contrast-based mask.
The technique is universal; this parameterisation is not.

**Better:** Richardson–Lucy deconvolution, as RawTherapee's capture sharpening
does. See [detail.md](detail.md).

**Cost:** medium.

---

## 6. ~~Tint as a y-offset~~ — RESOLVED 2026-07-29

Replaced with Robertson's isotemperature lines as Adobe's DNG SDK implements
them: the offset now runs along the interpolated isotherm in CIE 1960 UCS, with
Adobe's `kTintScale = −3000`. Thirteen of Adobe's own `(temperature, tint) → xy`
vectors are asserted to 2 × 10⁻⁵. See `research/color-pipeline.md`.

### The original entry



**Where:** `WhiteBalance.cpp`, `multipliersFor`.

**Not sourced:** tint shifts CIE 1931 y by `tint × 0.05`.

**Correct approach:** move perpendicular to the Planckian locus in CIE 1960 uv,
which is the uniform space the offset is properly defined in.

**Cost:** low over photographic range; the error grows at extreme temperatures.

---

## 7. Guided filter parameters

**Where:** `DevelopPipeline.cpp`.

**Sourced:** the filter itself (He, Sun & Tang).

**Not sourced:** radius `max(4, longest/200)` and `eps = 0.04` in squared
log2-exposure units.

**Reasoned:** the radius scales with the frame so the effect covers a constant
fraction of the picture regardless of megapixels; eps ≈ a fifth of a stop, below
which is texture and noise.

**Cost:** low, but untested against a reference.

---

## 8. ~~The grading zone partition~~ — RESOLVED 2026-07-28

**Was:** zones split by `smoothstep` over **linear** luminance at 0.0/0.5/1.0,
Orion's own formulation, in no CDL specification. Middle gray weighed 0.70
shadows, so the shadow wheel graded most of a normal photograph and the
highlight wheel was inert (−0.0000 and +0.0001 mean chroma on the two samples).
The offset was an additive constant in unbounded scene-linear, so a wheel's
authority fell as 1/level; and the zero clamp broke the zero-sum property in
deep shadow, brightening a 0.0096-linear patch by +29%.

**Now:** Gaussian bands on `log2(Y/0.18)` centred at −2.5 / 0 / +2.5 EV with
σ = 1.6, normalized to a partition of unity — the same construction the tone
controls use, from the same source (`deep-research-2026-07-27.md` §3). The
offset is multiplied by the pixel's luminance, making a wheel a constant
chromaticity shift at every exposure. `k = 0.25` is now derived rather than
tuned: `saturation = 1.5k/(1+k)`, so a full-radius push is 30% saturation from
neutral.

Pinned by `testColorGradeGpu` — the same wheel measures 0.1077 relative chroma
at −3 EV and 0.1079 at +3 EV — and by one bench probe per wheel, all three
passing on both sample frames with no waiver. See
[color-grading.md](color-grading.md).

What is still chosen rather than derived: the centres at ±2.5 EV and σ = 1.6.
They inherit the tone bands' geometry, which is sourced; "shadows are two and a
half stops down" remains a convention. Consistency with the tone controls is the
argument, and a photograph should not have two different ideas of where its
shadows are.

---

## 9. The camera profile's numbers are fitted, not read

⚠️ **Worse than this entry originally said — updated 2026-07-29.** Outside
research puts measured `BaselineExposure` for real bodies in roughly
**−1.0 … +0.4 EV** (Sony A7R V: −0.65 at ISO 50–80, +0.35 at ISO 100+; Nikon Z8:
+0.2; Fujifilm GFX100S: ≈0.0 at ISO 100). **Orion's fitted +1.20 EV sits well
outside that range**, which says the fit absorbed something that is not
BaselineExposure — almost certainly Apple's rendering intent, since the fit was
made against Apple's output and the camera JPEG, and both carry their makers'
looks on top of any baseline.

The same defect applies to the HueSatMap twist in the same file: fitted against
two rendered images, so it reproduces a *look*, and presenting it as a camera
correction claims more than the method supports. `research/camera-profiles.md`
now says so.

**To close:** read the real value from a DCP (via the DNG SDK or RawTherapee's
`rtengine/dcp.cc`), or measure per model and per ISO band. Do not ship Adobe's
DCPs — they are licensed for use with Adobe products; dcamprof-generated
profiles are redistributable.



**Where:** `pipe/DevelopPipeline.cpp` (`kBaselineExposureEv`),
`pipe/HueSatMap.h` (`blueSky`).

**Sourced:** both *stages* are the DNG specification's, in the spec's own
structures — BaselineExposure (tag 50730) and HueSatMap (50938), the latter
applied in linear ProPhoto HSV as `ProfileHueSatMapEncoding = 0` requires.
[camera-profiles.md](camera-profiles.md).

**Not sourced:** the values. No profile for this body is redistributable
(DECISIONS #44) and LibRaw does not carry BaselineExposure for native ARW, so
+1.20 EV and the blue correction (−8°, ×1.05, centred 250° over a 60° half
width) were **measured against two independent renderings of the same frame** —
the camera's JPEG and Apple's RAW pipeline. That is evidence, and it is written
down with the sweep tables, but it is not a published constant.

**Cost:** they fit one camera body. A per-camera value and a property of
Orion's own display transform are indistinguishable from one body's data.

**To fix:** measure a second body. If the numbers move they are per-camera and
belong in a table; if they do not, they belong in the display transform. Or read
a real profile — the tables are already the shape a `.dcp` loads into.

---

## 10. Lens profile interpolation is linear, lensfun's is not

**Where:** `pipe/LensDatabase.cpp`, `interpolate`.

**Sourced:** the models and the data — ptlens, poly3, `pa` vignetting, and
lensfun's own database. [lens-corrections.md](lens-corrections.md).

**Not sourced:** lensfun interpolates cubically in a transformed variable;
Orion interpolates linearly between the bracketing calibrations, takes the
nearest calibrated aperture rather than interpolating, and fixes focus distance
at infinity.

**Cost:** small — calibrations are dense in focal length and the coefficients
vary smoothly — but a close-up gets a slightly weak vignetting correction, and
the numbers will not match lensfun's to the last digit.

**To fix:** port lensfun's interpolation, which is a documented function of a
few lines, and interpolate the distance axis.

---

## Not unsourced, just absent

Listed so the two categories do not get confused. These are **missing features**,
not questionable implementations:

- DCP profile *files* — Orion builds the spec's tables but reads no `.dcp`, and
  has no ForwardMatrix or LookTable
- Dual-illuminant interpolation — specified, blocked on a second matrix source
- EDR / P3 display output
- X-Trans demosaic (Markesteijn) — the files are recognised and refused by name

**Struck 2026-07-28, all four now built:** highlight reconstruction, noise
reduction, lens corrections, 16-bit export. **Struck 2026-07-28b:** the lens
database, and the camera profile beyond the 3×3.

## 11. Clarity's slider-to-alpha mapping

**Where:** `pipe/LocalLaplacian.h`, `alphaForClarity`.

`alpha = 2^(-2 * clarity)`, so the slider's endpoints land on 0.25 and 4.

**Published:** the exponent itself, and both endpoint values. Paris, Hasinoff &
Kautz (SIGGRAPH 2011) section 5.2 defines `fd(D) = D^alpha` with alpha < 1
increasing detail contrast and alpha > 1 smoothing it; Figure 7b illustrates
alpha = 4 and Figure 7c alpha = 0.25. See `research/local-laplacian.md`.

**Ours:** that the curve *between* the endpoints is exponential in the slider
rather than some other monotone interpolation. Exponential means equal slider
travel is equal ratio in alpha, which is the same reasoning every other control
in the pipeline uses for a multiplicative parameter — but it is a choice, not a
finding, and nothing has been measured against a reference for the midpoints.

**Also ours:** the twelve-stop window (-10 to +2 EV) the luminance is normalized
over, and `kMaxCorrectionEv`. The window sets what `sigmaR` and the noise
thresholds mean in stops, so it is not cosmetic — a different window is a
different filter.

## 12. Dehaze — the atmospheric light's percentile is over blocks, not pixels

**Where:** `pipe/Dehaze.h`, `airlightFrom`, with `dehaze_peak.slang`.

**Published:** the whole method, and this step's intent — "We first pick the top
0.1 percent brightest pixels in the dark channel... Among these pixels, the
pixels with highest intensity in the input image I are selected as the
atmospheric light" (He, Sun & Tang, CVPR 2009 §4.4). See `research/dehaze.md`.

**Ours:** the pooling. A percentile over the whole frame is a reduction the DAG
has no node type for, so `dehaze_peak.slang` max-pools 4 × 4 blocks first and
the percentile is taken over block maxima. Max pooling is the right choice —
the step is looking for the most haze-opaque extremes, and an average of a block
is not in the block — but the selected set is not literally the paper's top 0.1%
of pixels, and nothing has measured how far apart the two answers are on a real
frame.

**Also ours:** applying the method in scene-linear rather than on display-encoded
pixels. That one is argued in `research/dehaze.md` as a closer reading of Eq. (1)
than the paper's own inputs, but it is still a departure and the clamp it makes
necessary is a piece of behaviour the paper does not specify.

## 13. Creative LUTs — one claim left open

**Where:** `pipe/CubeLut.cpp`, and `sampleCube` in `shaders/develop_display.slang`.
Full write-up in `research/luts.md`.

**Closed since this entry was first written.** The format and the interpolation
are both sourced now: Adobe, *Cube LUT Specification, Version 1.0* (September
2013) gives the grammar, the defaults, the size ranges, the byte ordering — with
the C index expression written out — and §8 *requires* tetrahedral interpolation
for three-dimensional tables. The six-tetrahedra construction is Sakamoto &
Itooka, U.S. Patent 4,275,413 (1981), col. 10 and Table 2, checked term by term
against what Orion implements.

Reading the specification also corrected a real defect: comments in `.cube` are
whole *lines*, not trailing text, so the parser had been truncating a title like
`Look #3` to `Look`. Fixed, with a test.

**What remains open** is one claim, and Orion no longer depends on it: that
tetrahedral is *more accurate* than trilinear, prism or pyramid subdivision.
That is usually credited to Kasson, Nin, Plouffe & Hafner, *Journal of Electronic
Imaging* 4(3), 226–250, 1995 (DOI 10.1117/12.208656). The citation is confirmed
against DBLP, Crossref and Semantic Scholar; **the paper has not been read** —
it is not open access and no copy could be retrieved. A secondary source
confirms only that it analyses trilinear, prism and tetrahedral, not pyramid,
and not the conclusion.

So `research/luts.md` does not assert the accuracy ordering. The reason Orion
interpolates tetrahedrally is that the file format's own specification says to,
which is a better reason for this decision in any case. To close: read the paper,
or drop the reference entirely.

## 14. Exposure fusion — four departures from the published method

**Where:** `pipe/ExposureFusion.h`. Argued in full in `research/exposure-fusion.md`.

The algorithm itself is sourced — Mertens et al. (2007) and Hessel & Morel
(WACV 2020 / IPOL 9, 2019), with every constant quoted. These four are Orion's.

~~**The number of simulated images.**~~ **CLOSED 2026-07-29.** [H279] Eq. (7) and
Algorithm 1 are now implemented, replacing a hardcoded floor of five. The edges
are in the *input* domain, so the exposure factor is inverted rather than
applied — inverting Eq. (3) gives `t = v/λ^k` for `k ≥ 0` and
`t = (v−1)/λ^|k| + 1` for `k < 0`. Verified against the paper's own table: at
α = 8 it reports N = 6, 4 and 3 for β = 0.4, 0.5 and 0.6, and the implementation
reproduces all three. `testExposureFusionMath` prints the counts every run.

**The proxy transfer function.** The method requires `t ∈ [0,1]`,
display-referred. Orion is scene-linear, so the chain maps luminance through a
sigmoid over log2 before running SEF. The reasoning — that a raw log domain
depresses the median, over-allocates brightened images and amplifies the sensor
noise floor — is argued but not published, and the specific sigmoid is a choice.

**The robust normalisation is dropped.** [HM20] §4 stretches the result to [0,1]
allowing 1% clipping. In an editor that fights the user's own exposure controls,
makes a pixel depend on the current crop, and destroys identity-at-zero. Replaced
by a fixed clamp on the emitted gain. The reference implementation keeps the
paper's version so comparisons remain possible.

**The strength control.** No published parameter degenerates to the identity, so
the slider raises the emitted gain to its own power — a lerp in log-gain, exact
at zero. Reasoned, not sourced.

**Also unspecified by any paper:** the epsilon guarding the weight
normalisation, whose denominator can reach zero.

## 15. Auto-enhance — the percentage, the target, and the taste

**Where:** `pipe/AutoEnhance.h`. Argued in `research/auto-enhance.md`.

The statistic is sourced (Simplest Color Balance, IPOL 2011, §3.1), as is
working on luminance rather than per channel (§4.1), the cap on how hard to
push (Lisani, Petro & Sbert, IPOL 2012, `smax = 2`), and the mid-grey anchor
(CIPA DC-004:2004 §2.3(3), `MAX × 0.461`). These four are not.

**The clipped percentage — 0.5% per side — is an inference.** Simplest Color
Balance recommends no value: §7 says only that "some saturation is almost always
necessary, but that the needed percentage is variable", and its reference
implementation takes the levels as mandatory arguments with no default. What
supports 0.5% is that its figure captions call s = 1% total "optimal" and
"moderate", and §7 fixes the split half and half. That is a reading of examples,
not a recommendation, and it is recorded as one.

**That a photograph's median should sit at mid grey is an extrapolation.**
CIPA DC-004 defines `MAX × 0.461` for a uniform 18% reflectance card under
controlled lighting. Applying it to the median of a pictorial image is a step
the standard does not take — and the standard explicitly calls its own value
conventional: "there is no single and absolute point of definition as long as
the tone is in the middle range; therefore, it becomes necessary to select some
value." **No published value for the mean or median luminance of a well-exposed
photograph was found**, and one was looked for.

**How much clarity to apply is taste.** There is no measurable target for it. It
is a fixed modest default, visible as a slider the moment auto-enhance runs, so
disagreeing with it costs one drag.

**Dehaze is deliberately not set.** Deciding whether a frame is hazy needs the
dark-channel statistic, which the auto path does not compute, and applying
dehaze to a haze-free frame is precisely the case He, Sun & Tang document the
prior getting wrong.


## 16. ~~As-shot white balance does not round-trip~~ — RESOLVED 2026-07-29

**Where:** `pipe/WhiteBalance.cpp`, `estimateFrom`. Found by a test written
while replacing the tint axis, and **fixed the same day**. The round trip is now
exact — 0 K, 0.000 tint, 0.000 in the multipliers across fifteen pairs. What
follows is the record of how it was wrong, because two of the three wrong
answers were plausible.

Feed `multipliersFor` a temperature and tint, hand the resulting multipliers
back to `estimateFrom`, and it does not recover them. Measured against a
hypothetical camera whose responses are XYZ, over 2800–9000 K and tint −0.4…0.3:

| | error |
|---|---|
| multipliers | **0.026**, about 2.6% |
| temperature | 120 K |
| tint | 0.050 |

**Improved, not solved.** The original searched temperature with tint pinned at
zero and *then* searched tint, which cannot work — tint displaces the white
point along the isotemperature line, moving the very red/blue ratio the
temperature stage matches on. That version was **845 K** out. Alternating the
two axes does not fix it either, and that is worth recording: the error surface
is a curved valley and coordinate descent zigzags along it. A joint
two-dimensional search brought it to 120 K, which is where it stands.

**Why it is not float noise and does matter.** 2.6% on a multiplier is a visible
colour shift. The consequence is that a file's white balance is not perfectly
idempotent: what the camera reported and what Orion re-derives from it differ
slightly. In practice the as-shot estimate runs once at open and the user's own
temperature and tint are stored in the sidecar, so nothing drifts on repeated
saves — but the number Orion shows for "as shot" is not exactly the camera's.

**It was none of the things it looked like.** Not the camera matrix — an
XYZ-to-sRGB matrix drives a "camera" response negative for a tinted white point,
where the log-error clamps and the surface flattens, but switching to the
identity kept the defect. Not multimodality either: the error at the true point
is exactly zero, so nothing can beat it, and only **one pair in fifteen** ever
failed.

**The cause was the refinement window.** It searched one coarse cell either way
around the coarse winner, which assumes the coarse stage lands in the cell
containing the minimum. Where the valley runs obliquely across the grid it does
not — at 2800 K with tint +0.30 the true minimum sat just outside, and the
search returned 2920 K and +0.350 having never evaluated it. Two cells either
way fixes it, for a few thousand more evaluations of a table walk, once per file.

**The lesson worth keeping:** the diagnostic that settled it was printing *which*
pair failed rather than the worst error. "0.026 worst" reads like a systematic
accuracy limit and invites loosening a threshold; "fourteen exact, one 120 K
out" reads like a bug and points straight at the search.

---

## 17. Brush masks — the nib is entirely Orion's own

**Where:** `shaders/mask_brush.slang`, `app/CanvasLayout.swift`.

`research/masking.md` §1 sources the *shape* of a brush mask — a stroke is a
list of dab centres with one radius for the whole mask, accumulated in R16F —
and that part is followed. **What the research does not give is any of the
numbers below, and none of them are cited.**

### Dab spacing: a quarter of the radius

`CanvasLayout.brushSpacing = 0.25`. Justified in the code as "the usual figure
in paint engines", which is **not a citation** — it is recollection. It is the
right order of magnitude (scalloping on a stroke's edge becomes visible
somewhere above roughly half a radius, and below about an eighth the extra dabs
are wasted work), but the specific value is a judgement.

What *is* pinned is the property that matters more than the constant: spacing is
continuous across pointer events, so a fast stroke and a slow one over the same
path lay identical paint. Tested against a 3-event and a 60-event stream.

⚠ **A published source for dab spacing was not looked for.** The obvious places
are libmypaint's dynamics (ISC, so readable) and the GIMP/Krita brush engines
(GPL — readable as *description* only, never as code). Worth an hour before
anyone tunes this by feel.

### Hardness

`dabCoverage` clamps hardness to 0.98 and ramps smootherstep from `h` to the
rim. The clamp exists because a truly hard circle aliases into a visible
staircase, and the mask is multiplied into a *parameter*, so that staircase
becomes a banded edit rather than a jagged edge. The reasoning is sound; the
0.98 is chosen, not derived.

The falloff itself *is* sourced — Perlin's smootherstep, shared with the
gradient masks rather than reimplemented, so a feather behaves the same under a
brush as under a gradient.

### The 256-dab pass, and the truncation

`kMaskDabsPerPass = 256` is a buffer size, not a claim about anything. The
kernel accumulates into the alpha it is handed and is built to chain, so this is
not a cap in principle — but **the graph holds one brush node, so a longer
stroke is truncated** and says so on stderr. Fixing it is more nodes, not a
bigger buffer.

---

## 18. The mask overlay's tint is invented, and does not need not to be

**Where:** `shaders/develop_linear.slang`, the `maskOverlay` branch.

Scene-linear red `(0.35, 0.012, 0.02)`, mixed at `alpha * 0.6`, keeping
`c * 0.25` of the image underneath. Every one of those four numbers was picked
by looking at the result.

Recorded here for completeness rather than as a debt. **This is a viewing aid,
not a filter** — it never reaches an export, never enters the sidecar, and
changes no stored value, so there is no photograph whose rendering depends on
getting it "right". The bar it has to clear is legibility, and the two
properties that matter are asserted by measurement rather than by taste:

- zero coverage is **bit-identical** to the mask being off, so the overlay
  cannot imply coverage that is not there;
- the tint keeps a constant floor, so it stays visible in deep shadow — where a
  purely proportional tint vanishes exactly where coverage most needs checking.

The convention it follows — red for a coverage overlay — is Photoshop's quick
mask and Lightroom's mask overlay, i.e. what photographers already expect. That
is a UI convention, not a published result, and is not claimed as one.

## §19 — The histogram's clipping-flag threshold

`Histogram.clipThreshold = 0.001`. Orion's own number, no source.

A clipping flag has to distinguish "you have crushed the blacks" from "one pixel
of sensor noise landed on zero". At 24 MP a single pixel is 0.000004% of the
frame, so a flag lit by any nonzero count is lit on every photograph and
therefore tells the photographer nothing. 0.1% of the frame is roughly 24,000
pixels — a region, not a speck.

Lightroom and Capture One both show clipping indicators without publishing a
threshold, so there is nothing to cite and no way to match them. This is a
display heuristic rather than a filter constant: it changes when a flag lights,
never what a pixel renders as, and the exact percentage is printed beside the
flag so the reader is never relying on the threshold alone.

## §20 — Guided feathering of the mask group: the radius and the epsilon

`research/masking.md` §4 is fully sourced for the *method* — He, Sun & Tang name
mask refinement as an application of the guided filter and give the formula. Two
constants in `DevelopPipeline::applyImageParams` are not, and copying the
paper's would have been worse than admitting it.

### The radius: `max(width, height) / 100`

The paper's figure uses **r = 60**. That number does not transfer, and the way
it fails is instructive: the paper's figures are sub-megapixel, roughly 600–1000
pixels on the long side, so its r = 60 is 6–10% of the frame. Carried across as
a fraction, that is a radius of about 500 pixels on a 6024-wide frame — a window
covering a tenth of the picture, which is not a feather.

What does transfer is the mechanism, and it is in the paper's own local linear
model: the filter can only pull a boundary onto an edge that lies **inside the
window**. So r is a *search radius*, and it should be a small multiple of how
far the placed mask misses the real edge by.

That miss is a property of the mask's **source**, not of the photograph. A brush
stroke laid at fit zoom, or a segmentation run at a fixed internal resolution,
produces a boundary error that scales linearly with the output resolution when
lifted to full size. That is the argument for a frame fraction rather than a
constant, and it is Orion's, not the paper's. `maxdim / 100` is 60 pixels at
6024 — 15 on the subsampled grid — which matches the paper's absolute number by
coincidence rather than by derivation, and the coincidence is not the reason.

### The epsilon: 0.01 squared log2-exposure units

The paper's **ε = 10⁻⁶** assumes the guide is display-encoded intensity in
[0, 1]. Orion's guide is log2 luminance, shared with the highlight and shadow
recovery chain, so ε carries different units and the number cannot be copied.

The faithful conversion, for the record: near midtones d(encoded)/d(stop) ≈
0.15, so their σ = 10⁻³ encoded units is about 0.0065 of a stop, i.e. **ε ≈
4 × 10⁻⁵ stops²**.

⚠ **That value is unusable here, for a reason specific to this codebase.**
`mask_guide_prep.slang` area-averages both moments over the s × s block, exactly
as `guide_down.slang` does, so `var` is the true *full-resolution* window
variance and carries the photograph's noise at full strength. He & Sun's own
arrangement subsamples the signal first, which divides the noise variance by
roughly s² — but it also aliases the variance term, and this codebase chose
against it deliberately and says so in both shaders. Deep shadows on a 14-stop
raw run to a window variance around 0.02 stops², so any ε below that snaps the
matte to shadow noise instead of to edges.

**0.01 is the compromise**: a tenth of a stop of spread. A step of height h
across half a window has variance h²/4, so the filter follows a half-stop edge
at a = 0.86 and ignores a tenth-stop one at a = 0.2. That is the behaviour
wanted — a mask boundary is placed against a subject, not against texture. It is
a quarter of the recovery chain's 0.04, because feathering should follow weaker
edges than tone recovery should.

### What would settle either of them

A measurement on photographs rather than an argument: place a mask deliberately
off a hard subject edge, refine, and measure how much of the boundary error is
recovered as r and ε vary. Neither constant is load-bearing for correctness —
the GPU test pins the *algebra* (a constant mask is unchanged, the complement is
symmetric, the boundary moves toward the guide's edge), and these two only
decide how far and how eagerly.

### One limit worth naming, not a constant

The guide is a single luminance channel, so an **iso-luminant** boundary — a red
subject against a tonally matched green background — has var(I) ≈ 0 and the
matte feathers straight across as though the edge were not there. The paper's
colour variant (a 3 × 3 covariance, `a` becoming a 3-vector) fixes it and is
citable, at three times the moment channels plus a small solve. Not built:
grey-guide first, and upgrade only if real photographs show the failure.

## §21 — Spot removal evaluates only the constant term of a published interpolant

`research/spot-removal.md` §3. The framework is cited — Pérez, Gangnet & Blake
(SIGGRAPH 2003) for what healing *is*, Farbman et al. (SIGGRAPH 2009) for the
solver-free closed form. What Orion evaluates is the **zeroth-order term** of
Farbman's mean-value interpolant: the mean of the boundary difference, applied
uniformly inside the disc.

That is a truncation, and truncations are the kind of thing that get written up
as though they were the method. They are not the same:

| | Correction inside the disc |
|---|---|
| Poisson (2003) | the harmonic function matching the boundary difference |
| Mean-value coordinates (2009) | a geometry-weighted average of the boundary difference, per pixel |
| **Orion** | **the boundary difference's mean, one number per spot** |

**The three agree exactly when the boundary difference is constant**, and on a
smooth background — sky, a wall, defocused foliage, which is where sensor dust
is visible in the first place — it very nearly is. The higher-order terms buy
the case where it varies, which `ROADMAP.md` explicitly puts out of scope:
"sensor dust and blemishes, not Photoshop-grade healing."

**The failure is bounded and known**, which is the argument for truncating on
purpose. Place a spot straddling a hard edge and the correction is wrong on both
sides by roughly half the edge's contrast, which reads as a disc that is too
dark on one side and too light on the other. It does not diverge, smear or
introduce colour; it is simply the wrong constant.

### What would settle it

Farbman's full interpolant is the same node with a loop over the boundary
instead of a mean of it — a bounded, known change, not a rewrite. The number to
watch before spending it is how often a spot is actually placed across an edge,
which is a usage question rather than an algorithmic one.

### And the boundary sample count is Orion's own too

The mean is taken over **32 points** on the disc's rim. Nothing published fixes
that number; it is chosen so that a spot at the smallest usable radius still
averages several distinct pixels, while a spot at the largest does not spend
more time on its boundary than on its interior. Too few and the correction
picks up whatever noise happens to sit on the sampled points; the cost is one
tiny dispatch per spot, so there is little reason to go lower.

---

## §22 — The colour range mask's shadow floor, and its slider ranges

`research/masking.md` §4c ships a colour range mask whose *metric* is fully
cited — Oklab is Björn Ottosson's (2020) with its derivation published, and it
is normatively specified by the W3C in CSS Color Module Level 4 as `oklab()`.
The distance is plain Euclidean in that space's chromaticity. None of that is
Orion's own.

Two numbers around it are.

### The floor on L, at 0.1

The metric is `(a, b) / L`, and it is that ratio — rather than `a` and `b`
themselves — precisely because the ratio is exactly invariant under exposure.
The cost of the division is that as a pixel goes to black the ratio goes to
infinity: two nearly-black pixels a code apart in one channel land arbitrarily
far apart, so a colour mask would speckle through every shadow in the frame.

`max(L, 0.1)` is the guard. **Derived rather than picked**, as far as anything
here can be: for a neutral, Oklab's `L` is exactly `Y^(1/3)`, so `L = 0.1` is a
linear luminance of `1e-3` — about **7.5 stops below middle grey**. On a
fourteen-stop raw that is inside the noise floor, which is the level at which a
pixel stops having a colour worth selecting on.

What it does *not* do is a step change. Below the floor the chromaticity is
scaled by `L/0.1`, so the suppression is gradual and predictable — the GPU test
asserts the factor rather than merely a direction, and separately asserts that
an ordinary shadow at `L = 0.31`, three times the floor, is untouched.

⚠ **What would settle it.** A measurement of where a real sensor's chroma
signal-to-noise crosses one, per ISO, rather than an argument from the raw's
stated dynamic range. That is a per-body number and Orion supports one body.

### The tolerance and softness ranges

The tolerance slider runs 0.01 to 0.8 and softness 0.002 to 0.4; the defaults
are 0.10 and 0.05. Nothing published fixes these — they come from measuring the
metric on real colours, which §4c tabulates: ordinary photographic colours sit
0.12 to 0.60 apart, and the closest pair measured (tarmac against skin, 0.126)
is the resolution the control has to be able to separate. So the useful travel
is the first third of the slider, and the top of the range exists to let a
tolerance take most of a picture deliberately rather than by running out.

⚠ These are *ranges*, not constants in a formula: a wrong one makes a control
awkward, not a photograph wrong. Recorded because a slider range is exactly the
kind of number that looks derived when it is not.

---

## §23 — The sky detector's own numbers

`research/sky-detection.md` implements Shen & Wang (2013) with one substitution
— a flood fill from the top edge in place of the paper's per-column border, for
the reason its header gives. The published method fixes none of the following.

- **24 threshold steps between the gradient's 5th and 95th percentiles.** The
  paper searches a fixed absolute range, which assumes 8-bit camera JPEGs; this
  runs on an AgX-mapped render whose gradient scale is different, so the search
  is made frame-relative.
- **Coverage bounded to 2%–90%, applied during the search rather than after.**
  The paper's energy assumes a uniform sky, and a real one is not — it runs
  light at the horizon and deep at the zenith. A one-row sky is perfectly
  uniform and therefore always scored best: measured before this guard, 673 of
  684 columns cut inside the top eighth and every photograph reported no sky.
- **A smoothness ratio of 0.5 on mean gradient magnitude**, sky against ground.
  The method's premise is that sky is calmer; nothing else checked it. ⚠ The
  first version compared colour *covariance* and rejected genuine skies — a sky
  with a gentle gradient has wide colour spread and no edges in it, which is
  precisely the thing being looked for.
- **Four-connected rather than eight.** A diagonal step lets the region squeeze
  through a one-pixel gap in a branch, which is how a fill escapes into the
  ground and takes the whole frame. ✅ **Covered 2026-07-31.**
  `testSkyFillCannotSqueezeThroughADiagonal` builds a wall breached by two calm
  pixels touching only at their corner; the eight-connected mutation now fills
  **384 of 384** ground pixels and fails two checks. The gap stood from the day
  the detector shipped.

### ~~An approximation inside the energy~~ — REMOVED 2026-07-31

The entry read:

> ⚠ **And an approximation inside the energy:** the largest eigenvalue of each
> covariance is taken as its largest *diagonal* entry. A 3×3 symmetric solve per
> threshold per frame is real work for a term that only breaks ties, and the
> diagonal is a lower bound. Untested against the exact form.

⚠ **Both halves of that were wrong, and writing the test is what showed it.**

*"Orders candidates the same way in every case measured"* — the claim the code's
own comment made — held only because every case measured had the same covariance
**shape**. Given populations wide in different channels, the diagonal entry
reorders a pair in 21 against the true eigenvalue.

*"Real work"* — it is not. The solve runs once per candidate threshold per
region: **48 times in a whole detection**, against a Sobel over every pixel. The
cost it was avoiding was never on the table, and nobody had checked.

It is now Smith's closed form, which is published, exact and non-iterative:

> Oliver K. Smith, **"Eigenvalues of a symmetric 3 × 3 matrix"**,
> *Communications of the ACM* 4(4), p. 168, April 1961.
> [doi:10.1145/355578.366316](https://doi.org/10.1145/355578.366316)

So this stops being a departure from the published method at all. Pinned against
a Jacobi rotation — a different algorithm, iterative where this one is closed
form — agreeing to under 1e-9 on seven covariance shapes.

⚠ **And it changed no output.** Coverage on all three sample frames is identical
to the digit: 67.5%, 4.6%, 14.6%. The term really does only break ties, and on
this corpus the ties do not arise. Said plainly rather than dressed up — the
value here is that an unsourced approximation and a false justification are gone,
not that any photograph looks different.

---

## §24 — Perspective correction: the slider travel (the extent of a mask is sourced as of 2026-08-01)

**Where:** `pipe/Perspective.h` (`kCornerTravel`, the aspect factor),
`pipe/MaskGeometry.h` (`unperspective`). Sourced companion:
[`perspective.md`](perspective.md).

**Sourced:** everything that decides *where a pixel comes from*. The homography
is Hartley & Zisserman §2.3, the four-point solve is their DLT (§4.1) in its
inhomogeneous form (§4.1.2), the coordinates it is posed in are their
normalization (§4.4.4), and the inverse-mapped single-resample warp is Heckbert
§3. None of that is invention.

**Not sourced — one thing now.** This section named two; the second was closed
on 2026-08-01 and is kept below struck through, because what it got wrong is
worth as much as what it got right.

### 1. The travel, which is a range and not a constant

`kCornerTravel = 0.35`: at full slider a frame corner is asked to come from a
point 35% of the half-width in or out. The aspect factor spans 2^±½.

Neither is measured against anything. They are the same kind of decision as the
lens sliders' |k₁| ≤ 0.35 — a ceiling on how far a control may go, not a claim
about optics. What sets it is auto-scale: more correction is more zoom is more
magnification of the source, so the ceiling is a picture-quality judgement about
how much softening a photographer should be able to ask for by accident.

**Cost:** "Vertical +50" will not mean numerically what it means in Lightroom.
A tuning difference, and one a photographer sets by eye against the picture.

**To fix:** nothing needs fixing. If it should be recalibrated, do it against
the zoom it costs at full travel, and no test needs rewriting — nothing asserts
a number derived from it.

### ~~2. A mask's *extent* under the correction is first order~~ — SOURCED 2026-08-01

The entry read: a homography carries a point and a line's direction exactly, so
a mask's **centre**, every **brush dab**, every **spot** and a linear gradient's
**direction** go through `mask::toFrame` with no approximation at all — while
its *size* is carried by **√|det J| at the mask's own centre**, one isotropic
number, exact only where the map happens to be conformal. The fix it named for
itself was the eigen-decomposition of a symmetric 2×2.

That fix is built (decision #102), and the departure it recorded is closed:

> **Golub, G. H. and Van Loan, C. F., *Matrix Computations*, 4th edition,
> Johns Hopkins University Press, 2013.** ISBN 978-1-4214-0794-4.
> §2.4.1 — the image of the unit sphere under a matrix is a hyperellipse whose
> semiaxes are σᵢuᵢ. §8.5.2 — the 2×2 symmetric Schur decomposition in closed
> form, which Jacobi's method is built on.

An ellipse with semi-axes (aₓ, a_y) at φ is the image of the unit disc under
A = R(φ)·diag(aₓ, a_y); its image under J is the unit disc under B = J·A; the
semi-axes and axis of that are read off the symmetric S = B·Bᵀ. `radiusToFrame`
is that, in thirty lines, with the quarter turns still outside it (#83) and a
short-circuited neutral case so an uncorrected photograph is bit-identical.

⚠ **Smith's closed form (CACM 1961) is in this repository and was not reused.**
`SkyDetector.Stats.largestVariance()` is Swift, is the 3×3 case, and returns the
largest eigenvalue and no eigenvector. This needs both roots and an axis, in
C++, on the other side of the facade. Smith's construction reduces to the
quadratic formula at n = 2, so what is written is the reduction, not a rival
derivation. Said here because "reuse what is already there" was the instruction
and this is why it was not followed.

**What is left: nothing.** ✅ **Both remaining terms are gone as of 2026-08-02,
decisions #137 and #138** — and both went the same way, which is the part worth
carrying: **the mask stopped being transported and the pixel started.** A mask is
a formula on the displayed picture, the kernel runs on the frame, and the map
between them is one invertible 3 × 3 (`mask::displayMatrix`). Carrying the mask
forward needs a derivative and is therefore first order forever; carrying the
pixel back is a matrix multiply and is exact. `radiusToFrame`, `lengthAlong` and
`Placement::scale` are off the render path. The two entries below are kept as
the record of what they were:

1. ~~**A gradient's ramp length** is still √|det J|~~ — **the anisotropy is
   removed as of 2026-08-02, decision #134.** `mask::lengthAlong` returns |J·u|
   for the ramp's own pre-image direction, which is the scale a length with one
   direction actually has; √|det J| is the geometric mean of two and was right
   only for a length whose direction is not tracked. Exactly 1 at the identity,
   so nothing moves without a correction. The **non-uniformity** stays and
   cannot go the same way: a projective map preserves cross-ratios along a line
   and not ratios, so the ramp is unevenly spaced however it is scaled.

   ⚠ **CORRECTED 2026-08-02.** What stood here said `perspectiveAspect` on its
   own leaves `Placement::jac` the identity, so the squeeze changes nothing at
   all for the ramp and the frames come out byte-identical. Printed directly,
   that Jacobian is **diag(0.500050, 1.000100)**, and the frames differ: a 6 × 6
   patch grid moves in **7 of 36** places under the squeeze (worst 0.0003 luma)
   and **10 of 36** under a keystone (worst 0.0018, corroborating the 0.0019
   below). The observation it was invented to explain is real — the first
   fixture was built around the squeeze and passed with the fix reverted — but
   the reason is duller: the term is small *everywhere*, not absent there.

   Under a keystone the fix is worth **0.6753 against 0.6734 luma** on a patch
   straddling the ramp's edge, against the **0.1461** the radial mask's
   first-order error was worth. So the call site is **not pinned**: no cell
   classification can see 0.002 luma, and a golden-value check with that margin
   would fail for reasons other than the defect. `lengthAlong` itself has four
   checks in `tests_mask_geom.cpp`; the line that calls it has none.

   ⚠ **And measuring that paragraph turned up a first-order defect in the same
   mask kind, in the shipping build.** A gradient's *level sets* go through J⁻ᵀ,
   because t is a covector, while the kernel's two endpoints go through J — so
   under any non-conformal correction the ramp is drawn across the wrong lines.
   `maskcheck 20 -2.0` under `perspectiveAspect 1.0`: **3 of 27 clear cells
   leaked, worst 0.1300 luma**. It is not in §24's list of leftovers because it
   is not an unsourced constant; it is a wrong derivation, and
   `research/perspective.md` now carries the exact closed form.
2. ~~**The map's curvature**, which no derivative at a point can see~~ — ✅
   **gone 2026-08-02, decision #138.** It was what leaked at the rim of a mask
   larger than about a third of the frame under a strong keystone, and after the
   ellipse it was the *whole* remaining error there.

   ⚠ **"About a fifth of a keystone's rim error" undersold it badly, and the
   correction is the useful part.** That figure came from counting *cells*, and a
   cell is 1% of the frame at 10 × 10, so most of the disagreement sat inside
   cells the overlay already classified as covered. Measured directly against the
   exact answer over a 600 × 600 grid: a 0.34 mask under vertical 1.00 differed
   by **1.0000 of coverage at worst** — pixels the render covers completely and
   the interface draws clear — mean 0.039, over 5.8% of the frame. Quadratic in
   mask size, so 0.25% of the frame at 0.10. An aspect squeeze measured
   **exactly 0.0000**, which is the harness checking itself against a map that
   has no curvature to find.

   The kernel now carries each pixel back through the matrix and evaluates the
   ellipse as drawn, so there is no derivative left to be first order about, at
   any mask size. It cost nothing measurable: **1.02 ms against 1.07 ms** on
   24 MP, the pass being bound by writing R16Float over the frame.

⚠ **And the table this entry used to print was not reproducible.** It recorded
`0.34 × 0.22` leaking 2 of 60 cells at 0.0105 luma under vertical 0.45. Re-run
through the scenario file itself, on the build before the fix and the build
after, that configuration gives **64 clear cells and no leak**. The keystone's
error grows with the mask's extent along the axis it *stretches*; the old sweep
varied the other one and read as exact where it should have read as clean.
`repro/perspective-carries-the-mask.txt` sits at 0.34 now. Until #138 the only
section that failed when the ellipse was reverted was the **aspect** one, not the
keystone one — because a squeeze is exactly linear, so it is where the
*first-order* error lives and the whole of it shows. §4d is the keystone's own
section, and it fails in **four places at once** on a revert of #138, worst
0.1219 luma.
## §25 — The creative vignette's controls, though not its curve

**Where:** `ops/vignette_ops.slang`, `DevelopPipeline::compositionCircle`,
`AdjustmentCatalogue`. Full entry in [`vignette.md`](vignette.md).

**Sourced:** the falloff itself. cos⁴ is Reiss, *JOSA* 35(4), 283–288 (1945),
restated in Kingslake, *Optics in Photography* (SPIE 1992) p. 121, and the
kernel evaluates its closed form. The post-crop *placement* is documented in
Adobe's own description of the control it copies.

**Not sourced**, four things, in order of how much they cost:

1. **Normalizing the curve so that Amount is the value at the corner.** The
   published law says how illuminance falls with field angle; it says nothing
   about which point on the curve a photographer's slider should name. Dividing
   by `1 − cos⁴(θmax)` is my choice, made so that Amount and Field angle are
   independent — without it, changing the shape changes the strength and neither
   control means anything alone. **Tested as an invariant rather than as a
   constant**: `testCreativeVignetteGpu` asserts the corner is worth the same at
   20° and at 65°, so a better parametrization can replace this without a test
   being rewritten.

2. **Reading the second control as a field angle at all.** cos⁴ is a law about a
   lens, and this vignette is not attached to one. The frame's real half-diagonal
   field angle is knowable from EXIF and is deliberately ignored — a creative
   vignette that could only ever be as wide as the taking lens would be a
   correction, not a look. So "field angle" here is a shape parameter wearing an
   optical name, which is honest about the curve and slightly dishonest about the
   lens. Said here rather than only in the panel.

3. **The ranges and the default**: ±3 EV, 10–70°, 45° at rest. Nothing published
   says a creative vignette should stop at three stops. ±3 is where a corner on
   the sample frames stops carrying any texture; 45° is roughly a 21 mm lens,
   chosen because it is the shape most photographs are given. All three are
   taste and none of them changes what a control *means*.

4. **The block sizes and tolerances in the tests** — 8-pixel corner blocks, a
   2-step spread allowance, a 5-step separation between the two edge midpoints.
   Calibrated on the fixture, not derived. The mutations they catch are listed
   beside them, which is the argument that they are the right order of magnitude.

**Cost:** low, and bounded. Amount is in stops of scene-linear light, which is a
physical quantity a photographer can predict, and the *shape* between axis and
corner is the published law. What is invented is which slider position maps to
which point on a curve that is itself sourced.

**To fix:** nothing here needs fixing so much as it needs someone to disagree
with it. If a published creative-vignette parametrization turns up — Adobe
publishes none, and neither does any of darktable's or RawTherapee's
documentation — items 1 and 2 should be replaced by it and this entry struck
through.

---

## §26 — Balance's slider-to-EV mapping, though not the control itself

**Where:** `color_grade.slang`, `kBalanceEv`. Threaded through
`ShaderParams::Grade::balance` and `Adjustments::gradeBalance`. Full entry in
[`color-grading.md`](color-grading.md#balance).

**Sourced:** the control's existence and its behaviour. Balance is Adobe's, on
the Split Toning panel from Camera Raw 4 (2007) and carried into the Color
Grading panel that replaced it in Camera Raw 13.0 / Lightroom Classic 10.0
(October 2020); Adobe documents it as balancing the effect between the highlight
and shadow ranges, positive favouring the highlights. Orion's does the same
thing in the same direction, and decision #101 named it as the one thing split
toning had that three grading wheels did not.

Sourced also, and separately, is the *form*: the zones are a partition of unity
of Gaussians on log2(Y/0.18), and translating a partition of unity along its own
axis leaves it a partition of unity. That is why Balance is a rigid shift of all
three centres rather than a re-spacing — a re-spacing can bring two centres
together, and two Gaussians on one centre is one zone with two wheels fighting
over it.

**Not sourced:** `kBalanceEv = 1.25`, the EV the slider's full deflection is
worth. Adobe's Balance runs −100..+100 against a partition they have never
published, so there is no number to copy and no way to match their feel.

**Said plainly: I chose it.** The argument for 1.25 is that it is *half the zone
spacing* — the largest travel where every crossover stays strictly between the
two centres it separates. At full deflection the shadow/midtone crossover lands
exactly on a neighbouring zone centre and never past it. That is derived from a
constant already in the file rather than picked, but the decision to spend
exactly one half-spacing of travel on the slider is still taste.

**What is held instead.** The invariants, in `testColorGradeGpu`:

- Balance centred is the fixed −2.5 / 0 / +2.5 EV partition the shader has
  always had, checked against the analytic weights to 3e-3. **This is what stops
  a new control silently rebasing every baseline in the repository** — the
  arithmetic makes it exact (`x + (-0.0f) == x`), and this makes it checkable.
  Moving `kEvShadow` by 0.05 EV fails here and nowhere else.
- Off-centre is that same partition, translated — checked against the analytic
  form at five settings, not against "something moved".
- The split point slides monotonically and travels 2.50 EV end to end.
- The two crossovers stay ordered and stay one zone spacing apart.
- With the wheels centred, any Balance is the **exact** identity, bit for bit.

**Cost:** low. "Balance +0.5" will not mean Adobe's "+50", the same way none of
Orion's tone controls numerically match theirs. The direction, the units of the
axis and the neutral all match, and the neutral is the part that matters.

**To fix:** calibrate the travel against reference renders of the same file at
matched Balance settings, and replace the number with a measured one. The tests
above are invariants and would survive it.

---

## §27 — The learned denoiser's proposals, none of which is in the codebase

**Where:** nowhere. ⚠ **This section is unusual and the difference matters:**
every other entry in this file is about a constant that is *shipping*. These are
proposals in [`denoise-learned.md`](denoise-learned.md) and
`planning/ROADMAP.md`'s piece table, and **not one line of it has been built.**

It is written down anyway, because the working agreement's rule is that an
uncited proposal is registered *before* someone builds it on the strength of it
having appeared in a planning document. That is how a plausible number gets into
a shader.

### 1. "Unprocess to linear camera RGB, not to raw" — option C

**Sourced:** the idea. Brooks, Mildenhall, Xue, Chen, Sharlet & Barron,
*Unprocessing Images for Learned Raw Denoising*
([arXiv:1811.11127](https://arxiv.org/abs/1811.11127), CVPR 2019) invert a
camera pipeline to synthesise realistic raw from ordinary photographs, and
evaluate the loss through a forward model of the same pipeline. Published,
dated, and heavily built on.

**Not sourced:** stopping the inversion **one stage earlier**, at
post-demosaic linear camera RGB, because that is where Orion's denoise node
actually sits (`denoise:blur 0..3`, between `hl:fill` and `lens`).

**Said plainly: nobody published that, and I am proposing it.** The argument is
that Brooks et al.'s reason for going all the way to raw is that a raw denoiser
runs on raw; Orion's runs on demosaiced linear camera RGB, so the matching
target is the intermediate. The *technique* transfers unchanged — it is the same
inversion with two fewer steps undone.

⚠ **The argument against it, which is real.** A network trained on demosaiced
data has to learn the correlations RCD introduced between neighbouring pixels,
and those correlations are the demosaic's, not the sensor's. That is why the
published order is denoise-then-demosaic, and it is a genuine reason to prefer
option A — move the model upstream — over this. **It is not settled**, and piece
3 of the decomposition is the session where it gets settled.

### 2. The tile size and the halo

**Not sourced:** 512×512, 32 px of halo, 126 tiles.

Tiling a convolutional network over a large image with an overlap is ordinary
practice and needs no citation. **The numbers do.** The correct halo is the
network's receptive field, which for a four-level U-net with 3×3 convolutions is
substantially more than 32 px — so **the 32 is written down as wrong-on-purpose,
a placeholder that makes the arithmetic concrete**, and piece 4 of the
decomposition measures the real one before any pixels depend on it.

Getting it wrong produces **visible tile seams**. That is the classic failure of
this approach, it is invisible to code inspection, and it is exactly the shape of
the two bugs `CLAUDE.md` names — a torn frame and a purple cast — that a
five-line assertion caught and reading did not. Piece 4's bit-identical
reassembly check exists for it.

### 3. The memory estimate past the arithmetic

**Sourced by arithmetic:** 185 MiB a full-resolution `RGBA16Float` node, 1,480
MiB for one fp16 32-channel activation at 24 Mpx, 2,868 MiB for one activation
per level of a four-level width-32 U-net. These follow from the frame size and
the tensor shapes and are as solid as the shapes are.

**Not sourced: the 4–8 GiB "in practice" and the ~100 MiB tile working set.**
Both are multipliers on the above for the tensors a block keeps live and the
skips the U keeps alive across it, and both are **guesses by a factor of two or
three either way**. They are load-bearing only for the conclusion "it does not
fit at full resolution", which survives any multiplier ≥ 1.

**Cost:** low, while nothing is built. It becomes real the moment a session
plans an allocation against them.

**To fix:** all three by measurement — pieces 1, 2 and 4 exist to do exactly
that, and the piece table says so.

---

## §28 — Rouf et al. §3.3 as its model without its solve, and two clamps on it

**What is not sourced.** Not a constant this time — a **deviation from a
published method**, which the folder's rule covers for the same reason.

Rouf, Lau & Heidrich (PROCAMS 2012) §3.3 transfers detail into a clipped channel
by a Poisson solve,

```
∇²f*_k = ∇·g*_k   over Ω_k,   f*_k|∂Ω_k = f_k|∂Ω_k,   g*_k = (ρ_k/ρ_j)·g_j
```

Orion ships the **pointwise model** `f*_k = (ρ_k/ρ_j)·f_j` and drops the
integration. Decision #109, `research/highlight-reconstruction.md` §5c.

**Why.** The solver in the graph is a pull-push interpolant (Gortler et al. 1996
§3.5.1), which approximates **Laplace** with a Dirichlet condition and has no
residual and no relaxation — there is nowhere in it to put a source term. Writing
`f* = r·f_j + u` for `r = ρ_k/ρ_j` gives `∇²u = f_j ∇²r + ∇r·∇f_j`, which is zero
only where the hue ratio is constant, and the neglected `∇r·∇f_j` is of the size
of the detail being transferred. Building the paper's solve means a multigrid
V-cycle: a relaxation kernel per level and roughly twice the chain, against a
domain measured at 0.023%–0.068% of a frame.

**What that costs, and it is not measured.** The integration is what makes `f*_k`
continuous with the picture across `∂Ω_k`. Piece 3 gets its continuity for free
from `ρ|∂Ω = f|∂Ω`; this branch does not, and how visible that is has **not been
measured**. Nothing in the suite would see a seam a pixel wide.

**The two clamps, and what is known about them.** `hl_apply.slang` bounds the
estimate to `[f_k, kMaxGain·clip]`. Both bounds are `highlights.slang`'s, at its
values and cited to Masood et al. through it. Measured in `apps/bench` block 3e:

| Clamp | `_PIC8220` | `_PIC8095` |
|---|---|---|
| "may only raise it" floor | binds on **150** channels | **645** |
| `kMaxGain` ceiling | **0** | **0** |

⚠ The ceiling is therefore **unexercised on every sample frame in the
repository**, and the repository's own habit is to delete a guard nothing
reaches — `hl_pull.slang` lost its `min(1, Σ)` weight cap for exactly that. It
was kept, and the difference is that the weight cap was *provably* unreachable
(the taps are a partition of unity) while this one is only unexercised: the
ratio's denominator is `max(ρ_j, 1e-6)` and nothing bounds it below.

**To fix.** Either measure the seam and, if it is visible, build the V-cycle —
the same two kernels with a residual pass and a relaxation between them, which
piece 1's entry already names as the upgrade path — or find a frame that reaches
`kMaxGain` and pin it, or delete the ceiling and say so.

---

## §29 — X-Trans: nothing is in the codebase, and three of the proposals would need this page

Nothing here is implemented. `research/demosaic-xtrans.md` and `ROADMAP.md`'s
piece table are research only; `engine/src/raw/RawImage.cpp:165` still refuses
X-Trans and no line of the graph knows the pattern exists. This entry is here so
that if any of it is ever built, the parts that would *not* be sourced are
already named — rather than discovered afterwards, which is how §17 and §24
happened.

**What is sourced, and comfortably.** Markesteijn's algorithm as LibRaw ships it
is not an Orion formulation at all — it is a dependency call, and calling a
library is not the same as inventing a coefficient. Rafinazari & Dubois's
frequency-domain method (ICIP 2014; Rafi Nazari, Ph.D. thesis, University of
Ottawa, 2017, ch. 3) is published, dated and open access, with its filter
constants stated: `σ = 2.32`, three modulated Gaussians, a 5×5 moving average on
the energy index, luma `(2R + 5G + 2B)/9`.

**What would not be sourced, if it is ever built:**

**1. ⚠ Re-applying white balance as a ratio after a CPU demosaic.** This is
option (d) of `demosaic-xtrans.md` §4.2 and it is the one somebody will reach
for, because it makes the temperature slider live again for the price of a
multiply. It is not sound. Markesteijn's *interpolation* is linear in the
samples, but its direction and homogeneity decisions are not, so demosaicing at
as-shot gains and rescaling to the slider's gains is an approximation whose error
is a function of the picture's content. **Nobody has published a bound on it, and
nothing in either suite would see it** — the result is a perfectly plausible
photograph. Same shape as the purple cast. If it is built, it is invention and it
belongs in this file with a measured error, not a plausibility argument.

**2. The synthetic-mosaic test's own fidelity.** `demosaic-xtrans.md` §5
proposes sampling a known RGB image through the `xtrans[6][6]` table and scoring
the reconstruction against it. That is exactly Rafi Nazari's Kodak experiment and
it is the only quantitative check available without a second implementation to
compare against — but ⚠ **a synthetic mosaic from an sRGB source is not a
sensor.** It carries no black level, no compression, no read noise and no
Poisson component, and its source is gamma-encoded where Orion's domain is
linear. Any dB figure it produces is about the interpolator and about nothing
else, and quoting it as "X-Trans quality" would be the domain error §27 found in
the learned denoiser, one milestone later.

**3. Any node count or memory figure for a GPU LSLCD path.** The table in
`demosaic-xtrans.md` §4.3 option B — ~13 nodes, ~2,376 MiB — is a **guess**,
built from a reading of the paper rather than from a design. It is labelled as
one in both files. It is here as well because a guess that survives two document
revisions starts being quoted as a number.

**To fix.** Nothing, until a piece is actually built. If piece 3 chooses option
(d), measure its error against a full re-decode on a real frame before shipping
it, and replace item 1 with that measurement or with the decision not to.

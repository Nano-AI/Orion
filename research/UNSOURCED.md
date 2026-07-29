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

## 6. Tint as a y-offset

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

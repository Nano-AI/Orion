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

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

**Not sourced:** the smoothstep knee positions (−4…+1 EV for highlights,
−7…−1.5 EV for shadows, −2…+2.5 for whites, −9…−3.5 for blacks) and the 1.5×
gain scaling. I chose these by reasoning about where middle grey sits and what
felt proportionate.

**Cost:** the controls will not agree numerically with Lightroom or darktable.
Now that the structure is right this is a tuning difference rather than a
behavioural one, but "+50 shadows" still will not mean the same thing.

**To fix:** derive knees from a published tone-mapping operator, or calibrate
against reference renders of the same file.

---

## 3. Vibrance weighting

**Where:** `ops/tone_ops.slang`, `applyColor`.

**Not sourced:** vibrance backs off in proportion to `1 − chroma`, where chroma
is `(max − min) / max`. The *idea* — weight the boost by how unsaturated a
colour already is — is universal and correct. The specific weighting is mine.

**Cost:** low. It behaves sensibly; it just is not anyone's published curve.

---

## 4. Colour mixer band weighting

**Where:** `ops/hsl_ops.slang`.

**Not sourced:** eight bands at 0/30/60/120/180/240/285/320°, weighted by
`(1 − distance/60°)²` and normalised.

**Reasoned, not arbitrary:** the 60° falloff makes neighbouring bands overlap so
a gradient crossing between them does not band, and squaring sharpens the
attribution. Band names and centres follow Lightroom's Color Mixer so a user's
existing instincts transfer.

**Cost:** low.

**One real subtlety, handled:** HSL is bounded to 0–1 but the working data is
not. Clamping into range would crush every highlight above 1.0 to white and
strip its colour — an actual bug that shipped. It now normalises by the peak
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

## Not unsourced, just absent

Listed so the two categories do not get confused. These are **missing features**,
not questionable implementations:

- Highlight reconstruction — clipped stays clipped
- Noise reduction
- Lens corrections
- 16-bit export (the pipeline ends in `RGBA8Unorm`)
- DCP camera profiles — we use only the 3×3 matrix, no ForwardMatrix or HueSatMap
- EDR / P3 display output

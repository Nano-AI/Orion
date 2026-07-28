# Detail — Sharpening and Noise

---

## Capture sharpening

**Where:** `sharpen.slang`.

**Status:** ⚠️ **Standard technique, no specific citation.** Listed in
[`UNSOURCED.md`](UNSOURCED.md).

**What it does:** unsharp mask — subtract a binomial-weighted blur from the
centre pixel and add back a scaled difference. Radius scales the tap offsets
rather than the tap count, so cost stays constant.

**The masking control** suppresses sharpening where local contrast is low. That
is where noise lives and detail does not, so it is what stops the control being
a noise amplifier. This mirrors Lightroom's Masking slider, though the
implementation is ours.

**Placement:** immediately after the demosaic. Capture sharpening undoes the
anti-alias filter and the interpolation's own softening; output sharpening for a
specific print or screen size is a separate, later step. It also sits upstream
of the tone controls, so — since dirt only propagates downstream — an exposure
drag never recomputes it.

**Better approaches to consider:**
- **Deconvolution** rather than unsharp mask. RawTherapee's capture sharpening
  uses Richardson–Lucy against an estimated PSF, which is closer to physically
  undoing the blur than heuristically boosting edges.
  [RawPedia — Capture Sharpening](https://rawpedia.rawtherapee.com/Capture_Sharpening)
- **Contrast-limited unsharp** to bound halos explicitly.

**Priority:** medium. It works; it is not principled.

---

## Noise reduction

**Status:** ❌ **Not implemented.**

Planned for M2. Research completed, decision made, no code yet.

**Chosen approach:** profiled wavelet shrinkage (à-trous) with a per-camera
Poisson–Gaussian noise model. Cheap, GPU-friendly, and the model is what lets
the strength adapt correctly across the tonal range.
- [darktable — denoise (profiled)](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/denoise-profiled/)

**Explicitly rejected — BM3D.** Dabov, Foi, Katkovnik & Egiazarian, *Image
denoising by sparse 3-D transform-domain collaborative filtering*,
[IEEE TIP 16(8), 2007](https://doi.org/10.1109/TIP.2007.901238). Excellent
quality, but block matching is GPU-hostile: published GPU ports achieve only
4–7× over CPU.
[Real-time study](https://link.springer.com/article/10.1007/s11554-020-00945-4)

**Quality ceiling for later — learned denoising.** NAFNet reaches 40.30 dB on
SIDD at 65 GMACs, the best quality-per-compute in the literature.
[Chen, Chu, Zhang & Sun, ECCV 2022](https://arxiv.org/abs/2204.04676) ·
[repo](https://github.com/megvii-research/NAFNet)

**Hard constraint on any learned approach:** DxO's DeepPRIME runs at roughly
2 MP/s on GPU — about 12 seconds for a 24 MP frame.
[DxO's own figures](https://support.dxo.com/hc/en-us/articles/7077934620701-DeepPRIME-and-DeepPRIME-XD-hardware-acceleration-further-information).
ML denoising can therefore only ever be a background pass, never a live slider.

---

## Lens corrections

**Status:** ❌ **Not implemented.** M2.

**Planned source:** [lensfun](https://lensfun.github.io/) — the established
open database of distortion, vignetting and TCA models, keyed by lens and
focal length. LGPL-3; the database itself is CC-BY-SA.

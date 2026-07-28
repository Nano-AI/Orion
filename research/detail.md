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

### Implemented — profiled wavelet denoise (M2, 2026-07-28)

| Piece | Source | Where |
|---|---|---|
| Starlet / à-trous transform, `h = [1/16, 1/4, 3/8, 1/4, 1/16]`, taps spaced 2^j | Starck, Fadili & Murtagh (2007), *The Undecimated Wavelet Decomposition and its Reconstruction*, IEEE TIP 16(2). Restated in `deep-research-2026-07-27.md` §2 | `shaders/denoise_blur.slang` |
| Non-negative garrote shrinkage, `w − τ²/w` | Gao (1998), *Wavelet Shrinkage Denoising Using the Non-Negative Garrote*, J. Comput. Graph. Stat. 7(4) | `shaders/denoise_accum.slang` |
| Per-scale norms ‖W_j‖₂ = 0.8907, 0.2007, 0.0855, 0.0412 | `deep-research-2026-07-27.md` §2 | `DevelopPipeline.cpp` |
| MAD → σ, constant 1.4826 | Donoho & Johnstone (1994), *Ideal Spatial Adaptation by Wavelet Shrinkage*, Biometrika 81(3) | `raw/NoiseProfile.cpp` |
| Poisson–Gaussian model `var = a·x + b`, fitted per frame | Foi, Trimeche, Katkovnik & Egiazarian (2008), *Practical Poissonian-Gaussian Noise Modeling and Fitting for Single-Image Raw-Data*, IEEE TIP 17(10), 1737–1754 | `raw/NoiseProfile.cpp` |

**Deliberate deviations, and why.**

- **Fitted per frame, not per camera and ISO.** The research points at
  darktable's `noiseprofiles.json`, which would be the obvious source. It is
  part of a GPL project, so it is out of bounds here (see `CLAUDE.md`). Fitting
  Foi et al.'s model to the frame in front of us needs no database, and adapts
  to the actual exposure rather than to a laboratory measurement of the sensor.
- **Second differences, not first.** A first difference measures the local
  gradient as though it were noise; on a frame with a sky in it, that is most
  of what it measures. `p − 2q + r` annihilates a linear ramp exactly, and
  `var(p − 2q + r) = 6·var(p)`.
- **Quantile bins, not equal-width.** A night frame puts every pixel in the
  bottom twelfth of the range. With equal-width bins eleven came back empty,
  the fit was abandoned, and the denoiser silently did nothing on exactly the
  frames that needed it most.
- **Four scales, not five.** Each scale is two full-resolution `rgba16f`
  textures — 388 MB a scale at 24 MP. The fifth scale's norm is 0.0202, a
  fifth of the fourth's, so it removes very little for another 388 MB.
- **Luminance/chroma split with Rec.2020 weights, applied to camera RGB.** The
  denoise runs before the colour matrix, because the noise model only holds in
  linear camera RGB. The weights are therefore approximate. Camera primaries
  are close enough to the working space's for the split to separate colour
  blotches from luminance grain, which is all the split is for.

**Measured**, Sony ILCE-7M3 night frame at +2.6 EV, flat sky region 903×603:

| Setting | R sd | G sd | B sd |
|---|---|---|---|
| Off | 0.1409 | 0.1276 | 0.1521 |
| Luminance 2.0 | 0.1213 | 0.1000 | 0.1309 |
| Luminance 2.0, Colour 3.0 | 0.0558 | 0.0532 | 0.0516 |

The three channels converging is the chroma scatter going, which is what the
colour control is for.

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

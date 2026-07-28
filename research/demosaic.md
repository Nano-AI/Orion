# Demosaicing

---

## What Orion ships today

**Where:** `rcd_dirs.slang`, `rcd_green.slang`, `rcd_rb.slang`.

**Status:** ⚠️ **RCD-family, not RCD.** Described honestly below, and listed in
[`UNSOURCED.md`](UNSOURCED.md).

**What it does:**
1. **Directional discrimination** — gradient energy along each axis, squared to
   sharpen the decision on edges while leaving flat areas near 0.5 where the two
   estimates agree anyway.
2. **Green** — Hamilton–Adams gradient-corrected estimates, clamped to the
   bracketing green samples. Green is interpolated first because it carries most
   of the luminance detail and sets the quality ceiling for everything after.
3. **Red and blue** — interpolated in the colour-difference domain (C − G) over a
   3×3 neighbourhood, then green added back. Chroma varies slowly compared with
   luminance, so this preserves the detail green already resolved.

**Partial source — the green step:** Hamilton & Adams, *Adaptive color plane
interpolation in single sensor color electronic camera*, US Patent 5,629,734
(1997), Eastman Kodak. Widely reimplemented and described in the demosaicing
literature.

**What is missing versus real RCD.** The "RC" is **ratio correction**: at a red
or blue site, RCD interpolates the *ratio* G/C rather than the *difference*
G − C. Ratios hold up better across strong luminance steps, where the additive
form overshoots. Our clamp suppresses the resulting artefacts rather than
avoiding them.

**The reference to port:**
- [LuisSR/RCD-Demosaicing](https://github.com/LuisSR/RCD-Demosaicing) — Luis Sanz
  Rodríguez, **MIT licensed**, so it can be ported directly rather than
  reimplemented from a description.
- Adopted as darktable's default Bayer demosaic
  ([documentation](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/demosaic/)),
  which is a strong signal it is the right default.

**Expected difference:** most visible on high-frequency texture — foliage, brick,
fabric weave, fine branches — and in colour fringing on hard edges, where our
3×3 chroma average is blurrier than RCD's directional handling. On smooth
gradients and silhouettes the difference is negligible, which is why a night-sky
test frame will not reveal it.

**Priority:** high. This is foundational and everything downstream inherits it.

---

## Alternatives considered

| Algorithm | Verdict | Source |
|---|---|---|
| **AMaZE** | Best fine detail, but GPU-hostile (branchy, serial) and GPL-3 from RawTherapee. darktable still runs it CPU-only. | [RawPedia](https://rawpedia.rawtherapee.com/Demosaicing) |
| **LMMSE** | Better on high-ISO frames; worth adding as a toggle. No OpenCL path in darktable. | [darktable](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/demosaic/) |
| **VNG4 / PPG** | Faster, visibly worse. VNG4 "no longer recommended" by darktable. | as above |
| **Dual demosaic** | Detail algorithm on texture, smooth algorithm on flat areas, blended by a mask. Roughly 2× the cost. | as above |
| **Learned (CNN)** | Quality ceiling. Gharbi, Chaurasia, Paris & Durand, *Deep Joint Demosaicking and Denoising*, [SIGGRAPH Asia 2016](https://groups.csail.mit.edu/graphics/demosaicnet/) (MIT CSAIL). vkdt compiles ONNX to SPIR-V and runs one in-pipeline. | [vkdt-nn](https://codeberg.org/hanatos/vkdt-nn) |

---

## Highlight reconstruction

**Status:** ❌ **Not implemented.** Clipped channels stay clipped.

This is why blown skies do not recover the way Lightroom's do — it is a missing
feature, not a tuning difference.

**To implement, in order of cost:**
1. **Inpaint opposed** — estimate clipped channels from adjacent unclipped ones.
   Cheap, stable, and darktable's current default.
2. **Segmentation based** — reconstruct whole clipped regions from surrounding
   colour ratios. Best on large blown areas.
3. **Guided laplacians** — multi-scale, Bayer-only, expensive.
   [Maths writeup](https://ansel.photos/en/resources/guided-laplacian-highlights/)

All documented in [darktable's highlight reconstruction module](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/highlight-reconstruction/).

---

## CFA indexing

**Where:** `cfa.slang`, `RawImage.h`.

**Source:** LibRaw's `FC` macro and `filters` bitmask, inherited from dcraw.

The shader and the C++ side must agree exactly; if they drift, the demosaic
reads the wrong colour everywhere. `orion-tests` checks the pattern for RGGB
including the 2×2 repeat.

Note LibRaw's `0x94949494` encodes both greens as index 1; the distinct G2 index
only appears in patterns that separate them. `cfaChannelRGB` folds 3 onto 1 so
the shader never has to care.

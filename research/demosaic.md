# Demosaicing

---

## RCD — Ratio Corrected Demosaicing

**Where:** `rcd_dirs.slang`, `rcd_lpf.slang`, `rcd_green.slang`, `rcd_rb.slang`,
`ops/rcd_stat.slang`.

**Status:** ✅ **Ported from the reference implementation.**

**Source:** Luis Sanz Rodríguez,
[LuisSR/RCD-Demosaicing](https://github.com/LuisSR/RCD-Demosaicing), MIT
licensed. Adopted as darktable's default Bayer demosaic
([documentation](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/demosaic/)),
which is the strongest available signal that it is the right default: darktable
ships every serious alternative and chose this one.

MIT licensing matters here. A direct port is permitted, so the coefficients
below are the reference's own rather than a reimplementation from a description.

### The four steps

**1 — Direction discrimination.** A quadratic form over nine samples along each
axis, with the reference's exact 44 coefficients (`ops/rcd_stat.slang`). It is
the energy of a high-pass filter along that direction, so a large value means
the signal changes fast there and interpolation should run the other way.

```
VH_Dir = V_Stat / (V_Stat + H_Stat)
```

**2 — Low-pass filter.** A 3×3 binomial over the mosaic:

```
lpf = 0.25·c + 0.125·(4-neighbours) + 0.0625·(diagonals)
```

It deliberately mixes all three colors, because it is estimating local
*luminance* regardless of which channel each sample carries. Step 3 needs it.

**3 — Green, with ratio correction.** This is the step the algorithm is named
for, and the one Orion was missing:

```
N_Est = cfa[N] · (1 + (lpf[c] − lpf[N2]) / (eps + lpf[c] + lpf[N2]))
V_Est = (S_Grad·N_Est + N_Grad·S_Est) / (N_Grad + S_Grad)
green = VH_Dir·H_Est + (1 − VH_Dir)·V_Est
```

Two things to notice. The estimate is **scaled** by a ratio of local low-pass
values rather than corrected additively — a ratio tracks a luminance step
correctly where an additive term overshoots it. And the directional estimates
are combined with **opposing** weights: `N_Est` is weighted by `S_Grad`, so an
estimate coming from the calmer direction is trusted more.

**4 — Red and blue.** Both interpolate the color difference C − G. At a red or
blue site the missing channel sits on the diagonals, and a P/Q discrimination —
the same statistic, sampled diagonally — chooses which diagonal to favor. At a
green site the cardinal neighbours carry them and step 1's discrimination is
reused.

### What this replaced

The previous implementation was RCD-*family*: directional and
gradient-corrected, but using **Hamilton–Adams additive** correction with a
clamp to suppress the overshoot, and a plain 3×3 average for red and blue with
no directionality at all. The visible differences were softer fine texture and
color fringing on hard edges.

**Confidence:** high. Ported from the reference with its own coefficients.

**Remaining gap:** X-Trans sensors need a 6×6 path; Bayer only for now.
**Researched and costed 2026-08-02 — see [`demosaic-xtrans.md`](demosaic-xtrans.md),
decision #114.** ⚠ Two corrections to the sentence this line used to carry. It is
not "port Markesteijn": **no published description of that algorithm exists**, so
a Slang port could only come from darktable's or RawTherapee's GPL-3 source, and
that is closed. And it does not need to be ported — **LibRaw already ships it as
`xtrans_interpolate` under LGPL-2.1 / CDDL-1.0** (`libraw.h:451`), which Orion
already links. The price is decision #29's ordering, not the licence.

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
   color ratios. Best on large blown areas.
3. **Guided laplacians** — multi-scale, Bayer-only, expensive.
   [Maths writeup](https://ansel.photos/en/resources/guided-laplacian-highlights/)

All documented in [darktable's highlight reconstruction module](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/highlight-reconstruction/).

---

## CFA indexing

**Where:** `cfa.slang`, `RawImage.h`.

**Source:** LibRaw's `FC` macro and `filters` bitmask, inherited from dcraw.

The shader and the C++ side must agree exactly; if they drift, the demosaic
reads the wrong color everywhere. `orion-tests` checks the pattern for RGGB
including the 2×2 repeat.

Note LibRaw's `0x94949494` encodes both greens as index 1; the distinct G2 index
only appears in patterns that separate them. `cfaChannelRGB` folds 3 onto 1 so
the shader never has to care.

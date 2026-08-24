# HDR merge — bracketed exposures to one linear DNG

Merge 2+ handheld bracketed frames into a floating-point linear-RGB DNG written next to the sources.
The epic's stories, order and costs live in `planning/ROADMAP.md`; this file is the algorithm sourcing.

⚠ `./hdrmerge/` (jcelaya/HDRMerge, GPLv3) exists in the working tree as a *reference for what to cite*, never for what to copy.
Its DNG writer is itself derived from GPL `dngconvert`, its mask dilation from The GIMP, its `pseudoinverse` from dcraw.
Nothing in Orion may descend from any of it.
The papers and specifications below are the implementation sources.

---

## The sources, and which one to cite for what

| Tag | Source | Where |
|---|---|---|
| **[TIFF6]** | Adobe — *TIFF Revision 6.0*, June 1992 | [PDF](https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf) |
| **[DNG14]** | Adobe — *Digital Negative (DNG) Specification*, Version 1.4.0.0, June 2012 | [PDF](https://web.archive.org/web/2021/https://www.adobe.com/content/dam/acom/en/products/photoshop/pdfs/dng_spec_1_4_0_0.pdf) |
| **[DM97]** | Debevec & Malik — *Recovering High Dynamic Range Radiance Maps from Photographs*, SIGGRAPH 1997 | [PDF](https://www.pauldebevec.com/Research/HDR/debevec-siggraph97.pdf) |
| **[HDF10]** | Hasinoff, Durand & Freeman — *Noise-Optimal Capture for High Dynamic Range Photography*, CVPR 2010 | [PDF](https://people.csail.mit.edu/hasinoff/pubs/hasinoff-hdrnoise-2010.pdf) |
| **[GL]** | Luijk — *Zero Noise*, guillermoluijk.com tutorial (published web write-up, ~2007) | [link](http://www.guillermoluijk.com/tutorial/zeronoise/index.html) |
| **[GN03]** | Grossberg & Nayar — *Determining the Camera Response from Images: What Is Knowable?*, IEEE TPAMI 25(11), 2003 | [PDF](https://www1.cs.columbia.edu/CAVE/publications/pdfs/Grossberg_PAMI03.pdf) |
| **[RRKB11]** | Rublee, Rabaud, Konolige & Bradski — *ORB: an efficient alternative to SIFT or SURF*, ICCV 2011 | [PDF](http://www.gwylab.com/download/ORB_2012.pdf) |
| **[FB81]** | Fischler & Bolles — *Random Sample Consensus*, CACM 24(6), 1981 | [PDF](https://www.cs.columbia.edu/~belhumeur/courses/compPhoto/ransac.pdf) |
| **[EP08]** | Evangelidis & Psarakis — *Parametric Image Alignment Using Enhanced Correlation Coefficient Maximization*, IEEE TPAMI 30(10), 2008 | [PDF](https://xanthippi.ceid.upatras.gr/people/evangelidis/george_files/PAMI_2008.pdf) |

**Which one supports what:**

| Claim | Actually from |
|---|---|
| File structure: header, IFD layout, field types, ascending tag order, values wider than 4 bytes at even external offsets | [TIFF6] §2 "TIFF Structure" |
| `PhotometricInterpretation = 34892` (LinearRaw) for demosaiced camera-native data | [DNG14] chapter 4 |
| 16-bit IEEE floating-point samples (`SampleFormat = 3`) are legal DNG, and require both version tags at 1.4.0.0 | [DNG14] chapter 3, "Floating Point Data" |
| For floating-point data the default `WhiteLevel` is 1.0 and `BlackLevel` 0 — relied on, not written | [DNG14] chapter 4 |
| `ColorMatrix1` maps XYZ→camera under `CalibrationIlluminant1`; `AsShotNeutral` is the neutral's camera-space color | [DNG14] chapter 6, "Mapping Camera Color Space to CIE XYZ Space" |
| `BaselineExposure` shifts the renderer's exposure in EV | [DNG14] chapter 5 |
| Merging differently-exposed frames into one radiance map, discarding clipped samples | [DM97] §2.2 (the framing; their response-curve recovery is *not* needed — raw data is linear) |
| Inverse-variance (SNR-optimal) weighting of exposure-normalized samples | [HDF10] §3 |
| Raw-domain "take the most-exposed unclipped data" idea and shortest-exposure fallback for pixels clipped everywhere | [GL] (implemented from the write-up's description) |
| For a linear response the inter-frame brightness transfer function degenerates to one scalar ratio, estimable from pixel-value ratios | [GN03] (their general BTF theory, specialized to linear) |
| Feature detection/description for alignment, patent-free by design | [RRKB11] |
| Robust homography estimation from noisy correspondences | [FB81] |
| Optional dense refinement of a coarse alignment | [EP08] |

⚠ **Patent note (decisions #173/#174/#176):** a citation is not clearance.
Everything above is either an open specification, a pre-1996 publication, or an academic method published without known live patent claims ([RRKB11] states patent-freedom as a design goal).
The deghosting weight is deliberately the simplest reference-consistency test; **HDR+ style tile merging (Hasinoff et al. 2016) is off the table — Google holds patents there.**
The deghost citation question is story D's decision point.

---

## The DNG writer (story A — built)

`engine/src/util/DngWriter.{h,cpp}` writes LinearRaw, three fp16 samples per pixel, uncompressed, one IFD, little-endian.
Tags written: NewSubfileType, ImageWidth/Length, BitsPerSample 16×3, Compression 1, PhotometricInterpretation 34892, Make/Model, StripOffsets/RowsPerStrip/StripByteCounts (one strip), Orientation 1, SamplesPerPixel 3, PlanarConfiguration 1, SampleFormat 3×3, DNGVersion + DNGBackwardVersion 1.4.0.0, UniqueCameraModel, ColorMatrix1 (SRATIONAL den 10⁴), AsShotNeutral (RATIONAL den 10⁶), BaselineExposure (SRATIONAL den 10²), CalibrationIlluminant1 = 21 (D65).

**Why fp16:** [DNG14] allows 16/24/32-bit floats.
Half precision carries an 11-bit significand — relative quantization 2⁻¹¹ ≈ 0.05%, below the sensor noise floor at every level — and halves the file (42 MP × 3 × 2 B ≈ 253 MiB) against fp32.
Unlike uint16, precision is *relative*, so the deep shadows the merge exists to improve keep more steps than a linear integer encoding would give them.

**Why samples are clamped to 1.0:** the ceiling contract.
1.0 = the merged stack's true ceiling (reference saturation × the headroom the shortest exposure buys), and the rendering gain rides in `BaselineExposure` instead of in samples above the white level.
This keeps [DNG14]'s default WhiteLevel semantics and Orion's clip-at-white linearize (decision #29) both correct with no reader changes.
⚠ Story D re-examines this before it hardens.

### What the LibRaw read spike established (measured 2026-08-24, LibRaw 0.22.2, `apps/tests/tests_dng.cpp`)

- LibRaw opens and unpacks the uncompressed fp16 LinearRaw layout: `filters == 0`, `colors == 3`, `is_floating_point() != 0`, and pixel values return bit-exact against an independent half-quantization model.
  **The fp16 read strategy stands; the uint16 fallback is not needed.**
- ⚠ `rawparams.options` ships with `LIBRAW_RAWOPTIONS_CONVERTFLOAT_TO_INT` set: by default unpack flattens floats to 16-bit integers and `float3_image` stays null.
  Story B's `decodeLinear` must clear that flag before `unpack()`.
- ⚠ `imgdata.color.cam_xyz` is dcraw-cooked: for a camera LibRaw recognizes (`SONY ILCE-7RM3` is in its table) it prefers its own Adobe matrix over the file's `ColorMatrix1`.
  The verbatim tag is in `imgdata.color.dng_color[0].colormatrix`.
  For the product this preference is acceptable — the merged file names the real camera, and both matrices describe it — but tests must assert the verbatim field.
- `AsShotNeutral` returns as `cam_mul` gains (LibRaw's scale, ratios preserved); `BaselineExposure` lands in `dng_levels.baseline_exposure`.
- A file truncated to a third is refused at open, not misread.

---

## The merge (story C — built, `engine/src/merge/Merge.{h,cpp}`)

Per frame *i*, per pixel, with yᵢ the frame's own normalized linear value (1.0 = that frame's clip) and x̂ᵢ = yᵢ/Eᵢ its radiance estimate at reference scale:

```
wᵢ = w_sat(yᵢ) · w_snr(yᵢ) · w_ghost(x̂ᵢ)

w_sat   = 1 for max(y_rgb) ≤ 0.85; smooth ramp to 0 on 0.85..0.98; 0 above
w_snr   = Eᵢ² / (aᵢ·yᵢ + bᵢ)      inverse variance [HDF10]; (a, b) from raw::NoiseProfile
w_ghost = 0 or 1                   reference-consistency, k·σ_ref; always 1 for the reference

X = Σ wᵢx̂ᵢ / Σ wᵢ;  Σwᵢ = 0 → X = x̂ of the shortest exposure [GL]
```

⚠ The w_sat ramp bounds (0.85, 0.98) and the deghost k are **ours, not published** — they go to `research/UNSOURCED.md` when story C lands, with the measurement that justifies them.
Exposure ratios come from EXIF (tᵢ·ISOᵢ/Nᵢ²), checked against the median of yᵢ/y_ref over aligned pixels where both sit in [0.1, 0.8]; past ⅙ stop disagreement the image-derived ratio wins and the discrepancy is logged ([GN03] specialization).

# Stacking — noise, star and landscape stacks to one linear DNG

Stack N frames of one scene into a floating-point linear DNG written next to the sources, reusing `engine/src/merge/Align.*` (noise stacks), `engine/src/util/DngWriter.*` and the HDR merge's patterns.
Plan: `planning/plans/stacking.md`; ledger row: #391. This file is the algorithm sourcing.

⚠ **Siril is GPL-3.** Its *documentation* is cited; its source was never opened, and neither was darktable's, RawTherapee's or HDRMerge's.
Astroalign is MIT; it is cited through its paper and Orion implements from the paper's description.
Every constant that is ours is in [`UNSOURCED.md`](UNSOURCED.md) §32, not here.

---

## The sources, and which one to cite for what

| Tag | Source | Where |
|---|---|---|
| **[SIRIL]** | Siril documentation — *Stacking* (rejection algorithms, normalization, weighting) | [docs](https://siril.readthedocs.io/en/stable/preprocessing/stacking.html) |
| **[HR09]** | Huber & Ronchetti — *Robust Statistics*, 2nd ed., Wiley, 2009 (winsorizing, robust scale) | book |
| **[R83]** | Rosner — *Percentage Points for a Generalized ESD Many-Outlier Procedure*, Technometrics 25(2), 165–172, 1983 | journal |
| **[PI]** | PixInsight — *ImageIntegration* reference documentation (linear-fit clipping by Juan Conejero; winsorized sigma clipping) | PixInsight docs |
| **[G86]** | Groth — *A pattern-matching algorithm for two-dimensional coordinate lists*, Astronomical Journal 91, 1986 | ADS |
| **[V95]** | Valdes, Campusano, Velasquez & Stetson — *FOCAS Automatic Catalog Matching Algorithms*, PASP 107, 1995 | ADS |
| **[BCS20]** | Beroiz, Cabral & Sanchez — *Astroalign: A Python module for astronomical image registration*, Astronomy & Computing 32, 2020 | [arXiv 1909.02946](https://arxiv.org/abs/1909.02946) |
| **[FB81]** | Fischler & Bolles — *Random Sample Consensus*, CACM 24(6), 1981 | [PDF](https://www.cs.columbia.edu/~belhumeur/courses/compPhoto/ransac.pdf) |
| **[BA96]** | Bertin & Arnouts — *SExtractor: Software for source extraction*, A&AS 117, 393, 1996 | ADS |
| **[S87]** | Stetson — *DAOPHOT: A computer program for crowded-field stellar photometry*, PASP 99, 191, 1987 | ADS |
| **[GN03]** | Grossberg & Nayar — *Determining the Camera Response from Images: What Is Knowable?*, IEEE TPAMI 25(11), 2003 | [PDF](https://www1.cs.columbia.edu/CAVE/publications/pdfs/Grossberg_PAMI03.pdf) |
| **[SW13]** | Shen & Wang — *Sky Region Detection in a Single Image for Autonomous Ground Robot Navigation*, IJARS 10(10):362, 2013 | via [`sky-detection.md`](sky-detection.md) |
| **[LR]** | Adobe — *Group photos into stacks* (Auto-Stack by Capture Time) | [Adobe Help](https://helpx.adobe.com/lightroom-classic/desktop/organize-photos-in-lightroom-classic/grouping-photos-stacks.html) |

**Which one supports what:**

| Claim | Actually from |
|---|---|
| The pipeline order: calibrate → register → normalize → combine with rejection, in linear data | [SIRIL] (and every comparable tool's documentation) |
| Percentile clipping is a one-step method for small sets, "up to 6 images" | [SIRIL] |
| Sigma clipping iterates: reject beyond σ_low / σ_high of the median, repeat | [SIRIL] |
| Winsorizing: clamp values beyond cσ to the boundary before estimating σ; consistency factor 1/√E[min(\|z\|,c)²] = 1.134 at c = 1.5 | [HR09]; the factor is recomputed here (§2), not read off a page |
| Linear-fit clipping: fit a straight line through each pixel's sorted stack and reject by residual; "performs very well with large stacks and sky gradients" | [PI], [SIRIL] for the idea; Orion's fit is a plain least-squares line on rank, the plan's choice |
| Generalized ESD as the alternative for large stacks ([SIRIL]: "more 50 images") — **cited, not built** | [R83] |
| Rule of thumb by frame count (< 10 sigma/percentile, 10–20 winsorized, > 20 linear fit) | astro-imaging forum practice (BAA); not a publication. Orion's own table is in UNSOURCED §32 |
| Additive + scaling normalization for light frames; median and MAD as the fast estimators | [SIRIL] (it defaults to IKSS estimators; Orion takes the median/MAD option) |
| Weight by background noise | [SIRIL] ("Noise" weighting); the form 1/σ'² is inverse variance, as hdr-merge.md's [HDF10] |
| The scale between two linear frames is one scalar ratio, estimable from pixel pairs | [GN03], specialized to a linear response as in hdr-merge.md |
| Stars have no texture, so ORB/SIFT-style matching fails; match triangles of star positions instead | [G86], [V95], [BCS20] |
| Scale-, rotation- and translation-free triangle invariants: side ratios (L2/L1, L1/L0) | [G86], [V95], [BCS20] |
| Fit a similarity from the matched stars, robust to bad matches | [FB81] |
| Star detection: background estimate, threshold above it, connected pixels, centroid | [BA96] |
| Smooth before thresholding so single noisy pixels do not become stars | [S87] |
| Group contiguous frames whose capture-time gaps are under a threshold | [LR] |
| Landscape split: a classical sky detector as the seed, the user paints the rest | [SW13] via `sky-detection.md` |

---

## 1. The pipeline

```
decode ─ (calibrate on the mosaic) ─ demosaic ─ register ─ normalize ─ combine with rejection ─ DNG
```

Averaging N frames cuts noise by ≈ √N; a median by ≈ 0.8·√N (the median is for dark and flat masters, not the stack).
Returns diminish past 15–25 frames.
CPU, not GPU: per-pixel rejection sorts N values per channel, a divergent loop whose work depends on N; measured under "Measured" below and in #391: 24 MP, 10 frames 15.6 s.

## 2. Rejection (per pixel, per channel, over the N normalized values)

| N | Method | Rule |
|---|---|---|
| < 3 | none | weighted mean |
| 3–6 | percentile [SIRIL] | m = median; reject v with m − v > 0.2·\|m\| or v − m > 0.1·\|m\|, one pass |
| manual only | sigma [SIRIL] | iterate: m = median, σ = std of survivors; reject beyond m ± 3σ |
| 7–20 | winsorized [HR09][PI] | σ from clamped values: clamp to m ± 1.5σ, σ' = 1.134·std(clamped), repeat to convergence, once, on the full sample; then iterate: re-centre m on the survivors, reject beyond m ± 3σ'. σ' stays fixed: re-estimated from survivors already cut at 3σ' it shrinks each round, since 1.134 assumes an uncut Gaussian |
| > 20 | linear fit [PI] | sort survivors; least-squares line of value on rank; σ = mean \|residual\|; reject \|residual\| > 3σ; iterate |

Rejection never leaves fewer than 3 values. A manual override picks any row.
Auto departs from the forum rule of thumb (sigma below 10 frames): 3σ clipping of n values cannot reject a lone outlier of any size when n ≤ 7 (σ = d·√(n−1)/n, so 3σ ≥ d), and only just at n = 8; winsorizing shrinks σ to the noise floor first, so 7–20 use it.
The **1.134** is `1 / sqrt(E[min(|z|, 1.5)²])` for z ~ N(0,1) = 1/0.8823 = 1.1334, recomputed for this file (2026-10-30) so the number is checkable and not a remembered constant.
⚠ PixInsight's winsorization cutoff was quoted to us as 5σ; that is a different parameter from Huber's c, could not be checked against the documentation text, and is **not** what Orion uses.

## 3. Normalization

Per frame i and channel c, against the reference frame, on the registered data:

```
L_i,c = median of the frame                       (additive location)
k_i   = median over pixels where v_ref ≥ L_ref + 10σ_ref of (v_i − L_i)/(v_ref − L_ref)    (1 below 200 samples)
v'    = (v − L_i)/k_i + L_ref
```

The scale is one scalar per frame, [GN03] specialized to a linear response; sampling only bright pixels keeps the ratio away from the noise floor, where it would be a ratio of noises.
Orion takes v as a 7×7 luma box sum about each bright local maximum (a star's core) rather than the pixel: registration taps at sub-pixel positions, and a bilinear tap lowers a star's peak while nearly conserving its flux. The box size is ours (UNSOURCED §32). The bright pixels are found at full resolution (stars cover ~0.1% of a sky, too few for a coarse grid).
σ_i for the weight comes from pixel differences, 1.4826·MAD of horizontal neighbour differences / √2, which ignores slowly varying signal.
Weight w_i = (k_i/σ_i)² when noise weighting is on (inverse variance after scaling), else 1.

## 4. Star detection [BA96][S87]

Luma → Gaussian blur σ 1 [S87] → background L = median and σ = 1.4826·MAD on a sparse grid → pixels > L + 5σ → 8-connected components [BA96] → keep area 2–400 px → centroid and flux from the unsmoothed luma above L → the 60 brightest.
A sky mask, where given, restricts stars to the sky.
A component whose brightest unsmoothed pixel carries more than half its flux is rejected: DAOPHOT's sharpness criterion [S87], which tells hot pixels and cosmic rays (all their flux in one pixel) from stars (a σ 1.2 PSF's peak carries ≈ 0.11, σ 0.7 ≈ 0.32). Without it, hot pixels pass the area gate, sit at the same sensor position in every frame, and their identical triangles lock registration at identity. Calibration darks (applied on the mosaic before detection) are the full remedy for hot pixels; this test is the guard for uncalibrated frames.

## 5. Registration

**Noise stacks** (tripod or small handheld shifts): the HDR merge's ORB + RANSAC alignment in `engine/src/merge/Align.*` ([RRKB11][FB81], see hdr-merge.md), unchanged.

**Star stacks, and the sky of a landscape:**

| Step | Rule | Source |
|---|---|---|
| Triangles | for each of the 60 stars, its 4 nearest neighbours → all C(5,3) = 10 triangles of the star plus its 4 neighbours, deduplicated | [BCS20] |
| Invariants | sides sorted L2 ≥ L1 ≥ L0; (L2/L1, L1/L0) | [G86][V95][BCS20] |
| Match | nearest invariant pair in a k-d tree within radius 0.1; vertices ordered by the side they are opposite, giving star-to-star votes | [BCS20] |
| Fit | similarity by RANSAC: `cv::estimateAffinePartial2D`, reprojection threshold 2 px, ≥ 6 inliers or the frame is refused (identity, `ok = false`) | [FB81] |

OpenCV (Apache-2.0) supplies the estimator; Orion's code is the detector and the triangle voting.
The Earth turns 15°/hour: 20 frames × 20 s turns the sky ≈ 1.7°, so a rotation-capable similarity is needed, not translation alone.

## 6. Landscape split

Sky region: registered on the stars. Ground: stacked unaligned (tripod).
They blend across a seam feathered over 1% of the image's long edge.
The split is a matte the user can paint; its **seed** is Orion's existing classical sky detector (`shell/Slices/Masks/SkyDetector.cs`, [SW13], [`sky-detection.md`](sky-detection.md)), not a new segmenter.
The hard cases users of the established tools report are the horizon seam and trees against sky, which is why the matte is editable.

## 7. Calibration frames

| Master | Rule |
|---|---|
| Dark | per-sample median of the darks, subtracted from each light; on the CFA mosaic before demosaic, so pattern noise is removed where it was made |
| Flat | per-sample median, minus the dark when given, normalized so **each CFA channel's mean is 1**; lights are divided by it |

Both are the standard calibration steps of the pipeline in [SIRIL]; doing them on the mosaic rather than after demosaic is Orion's choice, and the reason is that demosaic mixes neighbouring samples and a per-pixel defect would be smeared before it could be subtracted.

## 8. Finding stacks in a folder

[LR] groups contiguous photos whose capture-time gaps are under a user threshold.
Orion's rule is stricter, because a stack needs identical settings: ≥ 5 contiguous frames from the same camera with the same ISO, shutter, aperture and focal length, whose (gap − shutter) agrees within ±2 s.
Astro tag: shutter ≥ 5 s and ISO ≥ 800 (and, when checked, a dark thumbnail). All the thresholds are ours: UNSOURCED §32.

---

## Measured (2026-10-30)

Synthetic data unless said; `orion-tests` 1537 checks, 0 failures. These are checks of the implementation against the published methods above, not a claim about real frames: the thresholds in UNSOURCED §32 are untuned on real star, flat and dark frames.

| What | Result |
|---|---|
| Star registration, max corner error | 0.0124 px (59 inliers); 0.0136 px with 40 fixed hot pixels |
| Through the facade: stacked DNG's median star error | 0.031 px, 59 of 60 stars matched |
| Noise vs √N, integrate, 16 frames, integer shifts | σ ratio 3.860 vs 4 (0.965·√N) |
| Noise vs √N, facade noise mode, 8 frames | 2.930 vs √8 = 2.828; RMS against the true scene 0.00351 (identity registration 0.01206, fails) |
| Landscape | ground edge 1.000, star peak 0.995 of a single frame |
| Satellite streak (rejection) | 0 of 480 pixels outside 4σ |
| Fixed hot pixel (registration) | 0 of 72 landings outside |
| Calibration | tripod hot pixel 0.6807 → 0.0696 with darks; vignette corner/centre 0.844 → 0.997 with flats |
| 24 MP (6000x4000), star mode, CPU on 10 cores, low priority | 10 frames 15.6 s; 20 frames 29.8 s; peak RSS 3.9 GB (half-float frames). No GPU node: the CPU is fast enough |
| Real raw (not synthetic) | `Orion --stack --mode noise` on three copies of a 42 MP Sony ARW: 2.44 s, 145.7 MB DNG; `check-modes` runs it |

Open: tune the sharpness 0.5, flat 0.1 and dark 0.15 thresholds on real frames; frames are held in memory, so streaming is the next step past ~50 frames at 24 MP.

## ⚠ Patents

A citation is not clearance, and freedom to operate has not been assessed (CLAUDE.md).

| Part | Position |
|---|---|
| Registration + averaging, sigma-type rejection, triangle matching | decades-old prior art (1983–1996 papers above); none assessed; prior art by age only |
| Automatic sky/ground segmentation for night photos | the kind of feature phone makers patent; **nothing found either way**, and no patent database was searched. The developer said proceed. Orion takes the least inventive route: its *existing* classical sky detector is only a seed, and the user paints the split |
| HDR+ style tile merging (Hasinoff et al. 2016) | off the table, as in hdr-merge.md: Google holds patents there |

## What was and was not verified (2026-10-30)

Checked against the published page: [SIRIL]'s rejection list, normalization options, weighting options and the "up to 6 images" percentile note; [BCS20]'s title, authors, arXiv id and that it matches 3-point asterisms (triangles).
Recomputed: the 1.134 factor.
Taken from the epic brief, not re-checked against the primary: the journal volume and page numbers of [G86], [V95], [BCS20], [R83], [BA96], [S87]; Astroalign's 4-neighbour and 0.1-radius defaults; PixInsight's parameters.

# Tone and Local Contrast

Covers highlights, shadows, whites, blacks, the tone curve, and the edge-aware
guide they depend on.

---

## Guided filter

**Where:** `guide_prep.slang`, `box_blur.slang`, `guide_ab.slang`.

**Source:** He, Sun & Tang — *Guided Image Filtering*.
- [ECCV 2010](http://kaiminghe.com/publications/eccv10guidedfilter.pdf) · [IEEE TPAMI 35(6), 2013](https://doi.org/10.1109/TPAMI.2012.213)
- Kaiming He was at Microsoft Research Asia; the paper has tens of thousands of
  citations and the filter is standard in image editing and computational
  photography.

**What we implement:** the self-guided form on log2 luminance. Within a window,
output is a linear function of the guide, `q = a·I + b`, with

```
a = var(I) / (var(I) + eps)
b = mean(I) − a·mean(I)
```

Both moments are packed into one two-channel target so the separable box blurs
filter them together — four dispatches instead of eight.

**Why this filter specifically:**
- **O(1) per pixel** regardless of radius, unlike a bilateral filter. That is
  what makes a large-radius edge-aware guide affordable at 24 MP.
- **No gradient reversal.** The bilateral filter produces them; the guided
  filter's local linear model does not, which is exactly the artefact that shows
  up as halos around a skyline during highlight recovery.
- `eps` sets the edge threshold directly. Above it, `a → 1` and signal passes
  through; below it, `a → 0` and the output is the local mean.

**Our parameters:** radius scales with the frame (`max(4, longest/200)`) so the
effect covers a constant fraction of the picture. `eps = 0.04` in squared
log2-exposure units — roughly a fifth of a stop, below which is texture and
noise, above which is structure.

**Placement:** deliberately *before* exposure. Exposure is a multiply, so in log2
it is an addition the tone node applies for free — which keeps this entire
six-pass chain cached while the exposure slider moves.

**Confidence:** high on the algorithm. The parameter values are ours and
untuned against a reference.

---

## Local highlights and shadows

**Where:** `ops/tone_ops.slang`, `applyTone`.

**Source (approach):** local tone adjustment driven by an edge-aware
neighbourhood estimate is the standard, established by:
- Lischinski, Farbman, Uyttendaele & Szeliski — *Interactive Local Adjustment of
  Tonal Values*, [SIGGRAPH 2006](https://www.cs.huji.ac.il/~danix/itm/) (Hebrew
  University / Microsoft Research)
- Farbman, Fattal, Lischinski & Szeliski — *Edge-Preserving Decompositions for
  Multi-Scale Tone and Detail Manipulation*, [SIGGRAPH 2008](https://www.cs.huji.ac.il/w~danix/epd/)

**What changed and why it matters.** Highlights and shadows were previously
**pointwise** — the gain a pixel received depended only on its own luminance.
That is why they never matched Lightroom or darktable: theirs are *local*. A
bright window against a dark wall should be pulled down as a region; a pointwise
curve pulls down every bright pixel in the frame identically, including
highlights you wanted to keep.

Masks are now built from the guided-filter estimate of the neighbourhood rather
than the pixel's own value.

**Deliberately still pointwise:** whites and blacks. They set the endpoints of
the range, and an endpoint that shifted with local content would be
unpredictable to use. This matches how Lightroom separates the two pairs.

**⚠️ Honest gap:** the mask *shapes* — smoothstep knees at −4…+1 EV for
highlights, −7…−1.5 EV for shadows, and the 1.5× gain scaling — are **mine, not
published**. The local approach is now correct and cited; the tuning is not.
Recorded in [`UNSOURCED.md`](UNSOURCED.md).

**Confidence:** high on structure, low on the specific constants.

---

## Tone curve interpolation

**Where:** `pipe/ToneCurve.cpp`, `evaluateCurve`.

**Source:** Fritsch & Carlson — *Monotone Piecewise Cubic Interpolation*,
[SIAM Journal on Numerical Analysis 17(2), 1980](https://doi.org/10.1137/0717021).

**What we implement:** monotone cubic Hermite. Tangents are zeroed wherever the
data changes direction, which is the condition that guarantees monotonicity.

**Why not Catmull-Rom:** it passes through every control point but **overshoots
between them**. In a tone curve that appears as banding or an outright tonal
reversal — the image gets darker where you asked for brighter. Monotone cubic
cannot do that by construction.

**Confidence:** high. This is the standard choice; Lightroom and darktable
behave the same way.

**Tests:** `orion-tests` verifies monotonicity across 512 samples, range
containment, exact control-point interpolation, and that unsorted input gives
the same curve.

---

## Planned, not yet implemented

- ✅ **Local Laplacian filters** — built for clarity; see
  [`local-laplacian.md`](local-laplacian.md). Texture, the fine-level variant,
  is not built. Paris, Hasinoff & Kautz,
  [SIGGRAPH 2011](https://people.csail.mit.edu/sparis/publi/2011/siggraph/Paris_11_Local_Laplacian_Filters.pdf) (MIT CSAIL / Adobe / NVIDIA), with the
  [fast approximation](https://imagine.enpc.fr/~aubrym/projects/llf/texts/2014-fast-laplacian-filter.pdf) by Aubry, Paris, Hasinoff, Kautz & Durand. ~50× faster and halo-free.
- **Single-image exposure fusion** for shadow lift with preserved local contrast
  — Hasinoff et al., HDR+, [SIGGRAPH Asia 2016](https://www.ipol.im/pub/art/2021/336/article_lr.pdf), extended to a single image by Hessel & Morel, [WACV 2020](https://openaccess.thecvf.com/content_WACV_2020/papers/Hessel_An_Extended_Exposure_Fusion_and_its_Application_to_Single_Image_WACV_2020_paper.pdf).
- **Dehaze** — He, Sun & Tang, *Single Image Haze Removal Using Dark Channel
  Prior*, [CVPR 2009](https://doi.org/10.1109/CVPR.2009.5206515). Same authors as
  the guided filter, which was in fact developed to refine its transmission map.
  Written up in full, with every constant read out of the papers, in
  [`dehaze.md`](dehaze.md).

All three are M3.


## Fast guided filter — 2026-07-28

He & Sun (2015), *Fast Guided Filter*, arXiv:1505.00996. Same authors as the
original; their own answer to its cost.

`a` and `b` are averages over a window of radius r, so they vary slowly by
construction and there is no point solving them at full resolution. Subsample
the guide by s, divide the radius by s, solve there, and lift the coefficients
back bilinearly. Cost falls by roughly s². They report s = 4 as visually
indistinguishable.

**Why it was needed.** The radius scales with the frame — 30 px at 24 MP — and
`box_blur.slang` looped over every tap. Four passes of 61 taps over 24 million
pixels took **90 ms**, five times the budget for a slider you drag. The comment
in that file claimed the filter was O(1) in the radius; the loop in it was not.

**Measured**, Sony ILCE-7M3 at 6024×4024, `highlights −1` against a +5.5 EV
baseline:

| | Time | Mean luma |
|---|---|---|
| Full resolution | 90.09 ms | 0.3917 |
| Subsampled, s = 4 | 19.60 ms | 0.3917 |

Identical to four decimal places, which is what He & Sun predict. Bilinear on
the lift, not nearest — a nearest lift prints the subsampling grid into every
gradient.

## Highlight reconstruction — 2026-07-28

Masood, Zhu & Tang (2009), *Automatic Correction of Saturated Regions in
Photographs using Cross-Channel Correlation*, Computer Graphics Forum 28(7),
1861–1869. Restated in `deep-research-2026-07-27.md` §1.

A sensor clips per channel, not per pixel. For a neutral subject every channel
stops at the same scene brightness and only brightness is lost; for a colored
one the strongest channel stops first, the ratio between channels changes, and a
warm cloud comes out cyan. Within a small neighbourhood the channels are close
to linearly related, so fitting `C_c = α·C_u + β` by ordinary least squares over
the still-valid pixels says what the clipped channel would have read.

**The per-channel threshold** — `deep-research-2026-07-27.md` §1 ★, marked there
as the part Orion lacked entirely. White balance scales red and blue *upward*
relative to green, so their clipping levels routinely exceed 1.0. Testing
against a single level after white balance is itself a source of false color.
`T_k = (W − B_k)·invRange·m_k`, and γ = 0.97, mid-range of the 0.95–0.99 given.

**Deliberate deviations.**

- **A fixed window, not a traced boundary region.** Masood et al. fit over Ω, the
  valid boundary of each saturated region, which needs segmentation and a
  reduction per region. A fixed window round each clipped pixel is the same
  model with a cruder Ω, and it is one shader of 150 lines rather than a
  multi-pass region solver. The cost is reach: a highlight wider than the window
  has no valid neighbours to fit against, and the shader declines rather than
  inventing color.
- **All three channels clipped goes neutral.** There is nothing left to
  correlate against. A blown sun is white; inventing a hue for it is worse than
  declining to.
- **The estimate can only raise a channel.** A clipped channel read *at least*
  its clipping level, so `max(clipped, estimate)` is sound — and it makes a poor
  fit harmless rather than actively wrong.

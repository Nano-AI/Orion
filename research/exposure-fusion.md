# Single-image exposure fusion

Shadow lift that keeps local contrast. M3's fourth story.

---

## The papers, and which one to cite for what

| Tag | Paper | Where |
|---|---|---|
| **[M07]** | Mertens, Kautz & Van Reeth — *Exposure Fusion*, Pacific Graphics 2007 | [PDF](https://web.archive.org/web/20211201063858id_/https://mericam.github.io/papers/exposure_fusion_reduced.pdf) |
| **[H230]** | Hessel — *An Implementation of the Exposure Fusion Algorithm*, IPOL 8 (2018) 369–387 | [PDF](https://www.ipol.im/pub/art/2018/230/article_lr.pdf) |
| **[H278]** | Hessel — *Extended Exposure Fusion*, IPOL 9 (2019) 453–468 | [PDF](https://www.ipol.im/pub/art/2019/278/article.pdf) |
| **[H279]** | Hessel — *Simulated Exposure Fusion*, IPOL 9 (2019) 469–484 | [PDF](https://www.ipol.im/pub/art/2019/279/article_lr.pdf) |
| **[HM20]** | Hessel & Morel — *An Extended Exposure Fusion and its Application to Single Image Contrast Enhancement*, WACV 2020 | [PDF](https://openaccess.thecvf.com/content_WACV_2020/papers/Hessel_An_Extended_Exposure_Fusion_and_its_Application_to_Single_Image_WACV_2020_paper.pdf) |
| **[HDR+]** | Hasinoff et al. — *Burst photography for HDR and low-light imaging on mobile cameras*, SIGGRAPH Asia 2016 | [PDF](https://static.googleusercontent.com/media/hdrplusdata.org/en//hdrplus.pdf) |

**⚠️ Several numbers everyone attributes to [M07] are not in it.** They come from
the IPOL articles reporting Mertens' own reference implementation. Attribute
them correctly or the citation does not support the claim:

| Number | Actually from |
|---|---|
| The Laplacian kernel `[[0,1,0],[1,−4,1],[0,1,0]]` | [H230] Eq. (1). [M07] says only "a Laplacian filter", citing Malik & Perona 1990 |
| The weight exponents defaulting to 1 | [H230] Eq. (4). [M07] defines the exponents but states no default |
| Pyramid depth `⌊log₂ min(H,W)⌋` | [H230] Eq. (10) and [HM20] §4. [M07] gives no number |

**⚠️ The CGF 28(1), 2009 journal version was not read** — paywalled, and no
archived copy. Everything here is [M07] or IPOL. Nothing below may be cited to
the 2009 version.

---

## Mertens' method, as published

Three per-pixel quality measures, combined into a weight, used to blend
Laplacian pyramids.

**Contrast `C`** — [M07] §3.1: *"we apply a Laplacian filter to the grayscale
version of each image, and take the absolute value of the filter response"*.
The grayscale is a plain mean of R, G, B ([H230] Eq. 1) — not a luma-weighted
one.

**Saturation `S`** — [M07] §3.1: *"the standard deviation within the R, G and B
channel, at each pixel"*. [H230] Eq. (2) makes it the population standard
deviation, dividing by 3.

**Well-exposedness `E`** — [M07] §3.1, verbatim:

> *"We weight each intensity i based on how close it is to 0.5 using a Gauss
> curve: exp(−(i−0.5)²/2σ²), where **σ equals 0.2 in our implementation**. To
> account for multiple color channels, we apply the Gauss curve to each channel
> separately, and multiply the results."*

**The weight** is a product, `W = C^ωC · S^ωS · E^ωE`, and [M07] §3.1 says why
that rather than a sum:

> *"we want to enforce all qualities defined by the measures at once (i.e. like
> an 'AND' selection, as opposed to an 'OR' selection)."*

**Normalised per pixel across the N images** so the weights sum to one
([M07] §3.2). No paper specifies an epsilon to guard that division.

**The blend** — [M07] §3.2:

```
L{R}ˡ = Σₖ G{Ŵ}ˡₖ · L{I}ˡₖ
```

each level of the output Laplacian pyramid is a weighted average of the inputs'
Laplacian coefficients, weighted by the *Gaussian* pyramid of the weight map at
the same level. Collapse to finish.

### Why a pyramid at all — the quote that justifies the whole cost

[M07] §3.2:

> *"Wherever weights vary quickly, disturbing seams will appear. This happens
> because the images we are combining contain different absolute intensities…
> We could avoid sharp weight map transitions by smoothing the weight map with a
> Gaussian filter, but this results in undesirable halos around edges, and spills
> information across object boundaries."*

> *"Multiresolution blending is quite effective at avoiding seams, because it
> **blends image features instead of intensities**."*

That sentence is the reason this is not simply a weighted average, and it is
worth keeping in mind when someone proposes to make it cheaper by dropping the
pyramid.

---

## Simulated Exposure Fusion — one image, [H279] / [HM20]

Three changes from Mertens, all stated:

**1 — Luminance only, and the saturation measure is dropped.** [HM20] §5,
verbatim:

> *"we convert the input to the HSV color space and enhance the luminance only,
> i.e. V (value), while preserving the H (hue) and S (saturation) channels. This
> color space has the advantage of preserving the dynamic range when restoring
> the color, whereas other color spaces tend to generate colors outside the RGB
> color cube. Besides, this accelerates the algorithm. **We thus do not use the
> saturation measure proposed by Mertens et al.**"*

This is not a shortcut taken here — it is the published single-image method.
[HDR+] §6 arrives at the same place independently: *"We perform these
extractions **in grayscale**… we **simplify the per-pixel blend-weights**
compared to those in the work by Mertens et al."*, re-colourising afterwards by
*"copying per-pixel chroma ratios from the original linear RGB image"*.

**2 — Contrast is the analytic derivative of the remapping, not a Laplacian
filter.** [HM20] §5: *"It is directly given by the derivative of the remapping
function."* Which removes a full filtering pass per simulated exposure.

**3 — Well-exposedness is unchanged**, σ = 0.2, but now on one channel, so it is
a single Gaussian rather than a product of three.

Final weight, [H279] Eq. (11) — a plain product, with the exponents gone
entirely (they were fixed at 1, *"which is equivalent to removing them"*,
[H278] §2.2):

```
ŵₖ = (w_e,ₖ · w_c,ₖ) / Σₖ (w_e,ₖ · w_c,ₖ)
```

### Simulating the exposures

**Exposure factors**, [H279] Eq. (3) — note both branches are ≥ 1, so every
simulated image *gains* contrast somewhere:

```
k < 0 :  f*(t,k) = α^(|k|/Nmax)·(t − 1) + 1
k ≥ 0 :  f (t,k) = α^( k /Nmax)· t
```

[H279] §2.1.1 explains why there is no short-exposure branch: simulating a
shorter exposure *"is not desirable, because [it] would have lower contrast and
still no information in the clipped pixels"* — so the dark end is handled by
**shifting intensities toward the bottom** instead.

**The restrained range.** Each simulated image is smoothly clipped around a
centre that walks across the range, [H279] Eq. (4) and (5):

```
ρ(k) = 1 − β/2 − (k + N*)(1 − β)/(N + N*)

g(t,k) = t                                              if |t − ρ(k)| ≤ β/2
       = sign(t−ρ)·( a − λ²/(|t−ρ| − b) ) + ρ(k)        otherwise
   with a = β/2 + λ,  b = β/2 − λ,  λ = 0.125 fixed
```

The join is C¹: at `|t−ρ| = β/2` the second branch gives `λ²/λ = λ`, so
`a − λ = β/2`, and both branches have slope 1 there. [H279] §2.1.2 is explicit
that *"the particular shape of g is not important; any function with a
sufficiently fast decay and a smooth transition… can be used"* — so the shape is
sourced but not sacred.

**How many, and the asymmetry.** [H279] Eq. (6):

```
N* = ⌊(M − 1) · median{u}⌋      N = M − N* − 1
```

> *"contrast enhancement is generally needed in the dark parts only… The number
> of darkened or brightened images N and N* is thus determined from the input
> image's histogram, using the median."*

`M` itself is not a parameter: it is the smallest `M > 1` whose simulated images'
valid ranges overlap ([H279] Eq. 7, Alg. 1), and *"in practice this constraint
can be computed for the images with index k = −1, 0 and +1 only, because they
are the ones with the smallest overlap."* In their worked example α = 8, β = 0.5
gives N* = 0, N = 4 — **five images fused** ([HM20] Fig. 10).

**Contrast weight**, [H279] Eq. (9) and (10):

```
k < 0 :  w_c,ₖ = α^(|k|/Nmax) · (g′∘f*)(uₖ, k)
k ≥ 0 :  w_c,ₖ = α^( k /Nmax) · (g′∘f )(uₖ, k)

g′(t,k) = 1                            if |t − ρ(k)| ≤ β/2
        = λ² / (|t − ρ(k)| − b)²       otherwise
```

⚠️ **[HM20] Eq. (11) prints these two branch conditions swapped**, contradicting
its own Eq. (9). [H279] Eq. (9) and its Algorithm 2 have it right. Implement
from [H279]; the WACV camera-ready has a typo, and anyone checking the source
will otherwise think this code is wrong.

**Robust normalisation, and it is not optional.** SEF stretches rather than
compresses, so the fused result runs out of range. [HM20] §4: the final step
*"over-stretches the colors to [0,1] by allowing **1% of clipping** in both
sides of the histogram"*, and [H279] §2.2.5 adds that the channels are *"handled
together to preserve colors"* rather than stretched independently.

### Published defaults

| Parameter | Value | Source |
|---|---|---|
| α — maximum contrast amplification | **8**; usable 2…16 | [H279] §3.1, [HM20] Fig. 10 |
| β — restrained dynamic range | **0.5**; usable 0.2…0.8; β = 1 degenerates to plain exposure fusion | [H279] §3.1, [H278] Fig. 5 |
| λ — decay speed in `g` | **0.125**, fixed, not exposed | [HM20] §4, [H279] §2.1.2 |
| σ — well-exposedness | **0.2** | [H279] Eq. (8) |
| ω_c, ω_s, ω_e | removed (were fixed at 1) | [H278] §2.2 |
| Robust-normalisation clip | **1%** each side | [HM20] §4 |
| M, N, N* | derived from α, β and the median — not set | [H279] Alg. 1 |
| Pyramid depth | free; the β fix decouples depth from artefacts | [HM20] §4 |

> *"The values **α = 8 and β = 0.5** are recommended by the authors. The fused
> images are good and artifact-free for a wide range of values."* — [H279] Fig. 5

The depth claim is worth noting because it is the improvement over plain
exposure fusion: [HM20] §4, *"Our result displays no halo nor out-of-range
artifacts, **independently of the depth**"*, where under plain EF *"these halos
can be removed by using deeper pyramids. But the out-of-range artifact is
amplified by this modification."*

And on cost, [H279] §3: SEF *"only required the fusion of **three** Laplacian
pyramids, while LLF used ten"* — cheaper than the clarity filter already in this
pipeline.

---

## Open, and to be recorded in UNSOURCED

- **The epsilon guarding the weight normalisation.** No paper specifies one, and
  the denominator can reach zero.
- **CGF 2009** unread; nothing may be attributed to it.

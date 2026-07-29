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

## How this lands in Orion

**The method needs a bounded, display-referred `t ∈ [0,1]`.** Orion's pipeline
carries unbounded scene-linear light, so the placement question is real. Three
options were weighed:

**A — split `develop:display`** so fusion sees exactly the AgX-mapped image the
user sees. Faithful, and rejected. The split is structural, so it costs a
full-resolution round trip (~4 ms) on *every* render including when fusion is
off, against an architecture whose rule is that a disabled feature costs
nothing. And the faithfulness it buys is illusory: a full-resolution RGB fusion
of six simulated exposures, each with its own Laplacian pyramid and Gaussian
weight pyramid, is multiple gigabytes of traffic — 30–60 ms on its own, at *any*
placement. Once the method has to be approximated regardless, paying a permanent
tax for exactness that is not achievable is the worst of both.

**B — its own chain before the display transform, emitting a scene-linear
gain.** Chosen. The chain maps luminance into a bounded proxy internally, runs
SEF there, and outputs a per-pixel gain multiplied onto RGB — the same shape as
the clarity node, which filters normalised log2 luminance and re-applies a
ratio. Zero cost when off, because a disabled node resolves to its first input.

**The proxy is a sigmoid over log2 luminance, not raw normalised log.** This
matters, and the failure it avoids is specific:

> In a raw log domain the shadow axis is stretched, so `median{u}` falls. The
> split `N* = ⌊(M−1)·median⌋` then allocates almost every simulated image to the
> *brightened* side, the weights read the sensor's noise floor as
> "underexposed content that needs lifting", and the fusion amplifies noise in
> the near-black. A sigmoid compresses the shadows back, which puts the median
> where the paper's constants were validated.

Two further reasons the same choice is forced: `f*(t,k) = α^{|k|/N}(t−1)+1` is
anchored at `t = 1`, which is meaningless in an unbounded domain; and
well-exposedness at 0.5 with σ = 0.2 means "within about ±0.4 of mid" — over a
raw 12-stop log window that is ±2.4 stops, a different preference shape from the
one the paper tested. AgX is itself a sigmoid in log2, so matching one at middle
grey and slope is a cheap, faithful proxy rather than an invention.

**The robust normalisation is dropped, not implemented.** [HM20]'s final 1%
stretch is a global histogram operation, and in an editor it is wrong three
times over: it fights the user's own exposure, whites and blacks sliders; it
makes a pixel's value depend on the current crop, since the crop changes the
histogram; and it destroys identity-at-zero, because stretching an unmodified
image is not the identity. AgX and the user's controls already own global
tonality. A fixed clamp on the gain replaces it. The reference implementation
in `pipe/ExposureFusion.h` keeps it, because comparing against the paper needs
it — it just is not in the pipeline.

**The slider is `gain^s`, which is exactly the identity at zero.** No published
parameter degenerates cleanly: α → 1 collapses the exposure factors to the
identity, but `ρ(k)` contains no α, so the simulated images remain differently
clipped copies and their blend is not the input. Raising the emitted gain to the
slider's power is a lerp in log-gain: `s = 0` gives `gain⁰ = 1`, bit-exact, and
the node is disabled there anyway, so the architectural guarantee and the
arithmetic one agree. α stays fixed at the paper's 8 internally.

⚠️ **Monotonicity is not guaranteed, and this is measured.** Fusion blends
Laplacian coefficients with spatially varying weights; [M07] §4.1 names a
"spurious low frequency brightness change" as a known artefact. On a synthetic
full-range ramp the worst tonal reversal scales cleanly with the amplification:

| α | worst reversal |
|---|---|
| 2 | 2.1 × 10⁻⁴ |
| 4 | 2.3 × 10⁻³ |
| 8 | **1.1 × 10⁻²** |

That scaling is what says the reversals come from the method rather than from a
mistake in the blend — and 1% at the recommended α = 8 is large enough to band a
smooth gradient. `testExposureFusionMath` guards it as a regression and prints
all three numbers on every run.

## Open, and to be recorded in UNSOURCED

- **The epsilon guarding the weight normalisation.** No paper specifies one, and
  the denominator can reach zero.
- **`M` is searched from 5 upward, not from 2.** [H279] Eq. (7) solves for the
  smallest overlapping set; the exact form of that constraint could not be
  transcribed with enough confidence to be the sole authority, so the search
  starts at the count [HM20] Fig. 10 reports for the recommended α = 8, β = 0.5.
  Starting at 2 is also actively wrong here: with one image to allocate, the
  median-derived split cannot give a bright frame a darkened image at all.
- **The proxy transfer function**, the dropped robust normalisation, and the
  `gain^s` strength control are all Orion's, argued above but not published.
- **CGF 2009** unread; nothing may be attributed to it.

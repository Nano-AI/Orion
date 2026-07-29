# Dehaze — the dark channel prior

Plan of record for M3's dehaze story. Every value below was read out of the
papers themselves, not recalled; the equation numbers are the papers' own.

---

## The papers

| | Paper | Where |
|---|---|---|
| The method | He, Sun & Tang — *Single Image Haze Removal Using Dark Channel Prior*, CVPR 2009 | [PDF](http://mmlab.ie.cuhk.edu.hk/archive/2009/dehaze_cvpr2009.pdf) |
| Extended version | He, Sun & Tang — same title, IEEE TPAMI 33(12), 2011 | [PDF](http://mmlab.ie.cuhk.edu.hk/archive/2011/Haze.pdf) |
| The refinement Orion will use | He, Sun & Tang — *Guided Image Filtering*, ECCV 2010 / TPAMI 35(6), 2013 | already cited in `tone-and-local-contrast.md` |

⚠️ **Equation numbers differ between the two haze papers.** The recovery
equation is (16) in CVPR 2009 and (22) in TPAMI 2011. Cite the version you
mean.

The same three authors wrote the guided filter, and they wrote it *for this* —
which is why Orion already owns the expensive half of this feature.

---

## The model and the four steps

**Haze imaging model**, Eq. (1) in both papers:

```
I(x) = J(x)·t(x) + A·(1 − t(x))
```

`J·t` is "direct attenuation", `A(1−t)` is "airlight" (their words). Eq. (2)
gives `t(x) = e^(−β·d(x))` — transmission falls exponentially with depth, which
is why this doubles as a depth cue.

**1 — Dark channel.** Eq. (5) in both:

```
J_dark(x) = min_{c ∈ {r,g,b}} ( min_{y ∈ Ω(x)} ( J_c(y) ) )
```

The prior, measured over 5,000 images: with a **15 × 15** patch, "about 75% of
the pixels in the dark channels have zero values, and the intensities of 90% of
the pixels are below 25". TPAMI §4.4: "In the remainder of this paper, we use a
patch size of 15 × 15."

**2 — Atmospheric light `A`.** CVPR §4.4 / TPAMI §4.3, verbatim: "We first pick
the top **0.1 percent** brightest pixels in the dark channel. These pixels are
usually most haze-opaque... Among these pixels, the pixels with **highest
intensity in the input image I** are selected as the atmospheric light."

Note the two stages — it is not "the brightest pixel in the image", and the
paper says so explicitly: "these pixels may not be brightest ones in the whole
input image". A specular highlight would otherwise become `A` and the whole
frame would be solved against a wrong constant.

**3 — Transmission.** Eq. (11), then Eq. (12) with the aerial-perspective term:

```
t̃(x) = 1 − ω · min_c ( min_{y ∈ Ω(x)} ( I_c(y) / A_c ) )        ω ∈ (0, 1]
```

Why `ω` exists, in their words: "even in clear days the atmosphere is not
absolutely free of any particle... the presence of haze is a fundamental cue for
human to perceive depth. This phenomenon is called **aerial perspective**. If we
remove the haze thoroughly, the image may seem unnatural and the feeling of
depth may [be] lost."

> "The value of ω is application-based. **We fix it to 0.95 for all results
> reported in this paper.**"

**4 — Recovery.** CVPR Eq. (16) / TPAMI Eq. (22):

```
J(x) = (I(x) − A) / max(t(x), t₀) + A          t₀ = 0.1
```

"A typical value of `t₀` is 0.1." The floor exists because `t → 0` divides by
nothing; the paper also notes the result "looks dim" afterwards and that they
raise the exposure of `J` for display.

---

## Refinement: soft matting, and what the authors replaced it with

CVPR §4.2 and TPAMI §4.2 refine `t̃` with Levin's closed-form matting Laplacian,
minimising `E(t) = tᵀLt + λ(t − t̃)ᵀ(t − t̃)` and solving `(L + λU)t = λt̃`.

**The 2011 haze paper does not mention the guided filter at all.** The
replacement is stated in the guided-filter papers, by the same authors:

> ECCV 2010 §4, *Single Image Haze Removal*: "In [9] a haze transmission map is
> roughly estimated using a dark channel prior, and is refined by solving the
> matting Laplacian matrix. **On the contrary, we simply filter the raw
> transmission map under the guidance of the hazy image.** The results are
> visually similar (Fig. 10)."

TPAMI 35 (2013) §5 adds the numbers — "**about 40 ms for this 600 × 400 image,
in contrast to 10 seconds using the matting Laplacian matrix**" — and the
parameters used in its Fig. 16: guided filter with **r = 20, ε = 10⁻³**. It also
notes they "first apply a max filter to counteract the morphological effects of
the min filter", which is easy to miss and is the difference between a
transmission map that lines up with the edges and one shifted by half a patch.

This is the whole reason dehaze is cheap for Orion: the guided filter is already
a node in the graph. What it needs is the **cross-guided** form — guide is the
image, input is the transmission — where Orion's existing instance is
self-guided on log luminance.

---

## Stated limitations — worth putting in the UI, not just the file

TPAMI §6, Discussion, near-verbatim:

- **The prior fails on objects inherently similar to the atmospheric light with
  no shadow cast on them** — their example is white marble. The dark channel is
  then not near zero, so the method "will **underestimate the transmission** of
  these objects and **overestimate the haze layer**". A white building or a white
  car is the everyday version.
- The constant-airlight assumption fails where sunlight is strongly directional.
- Transmission is wavelength-dependent, so distant objects under thin haze "may
  fail to recover the true scene radiance... and they remain bluish".

**Sky is explicitly *not* a failure case.** CVPR §4.1: the sky's colour is
usually very close to `A`, so `min_c(min_y(I_c/A_c)) → 1` and `t̃ → 0` — "Equation
(11) gracefully handles both sky regions and non-sky regions. **We do not need
to separate the sky regions beforehand.**" Any implementation that special-cases
the sky has misread the method.

---

## How this lands in Orion — decisions to make when building

**Scene-linear, not display-encoded — a deliberate departure.** The paper's
results are computed on ordinary photographs, which are gamma-encoded, but
Eq. (1) is a statement about *radiance*: `J·t + A(1−t)` is a physical mixture and
only holds in linear light. Orion is scene-linear at this point in the graph, so
applying it there is more faithful to the model than the paper's own inputs
were. The prior itself survives the change — "the minimum over a patch is near
zero" is preserved by any monotone encoding — but the *statistics* quoted above
(75% zero, 90% under 25/255) were measured on encoded images and should not be
re-quoted as if they held here.

Consequences to handle: scene-linear values are unbounded above, so `I_c/A_c`
can exceed 1 at a specular and drive `t̃` negative. Clamp.

**`A` needs a reduction over the whole frame**, which is the one part that does
not fit a per-pixel node. It also does not depend on any user control — only on
everything upstream of the node — so it wants computing once and caching, not
per slider tick.

**Placement: after the camera profile, before clarity.** Dehaze claims to
recover the true scene radiance; clarity is a look applied to it. Physically the
restoration comes first. Keeping both upstream of the tone controls preserves
the property that already holds for clarity and the guided filter — an exposure
drag recomputes neither.

**The min filter is separable**, like any rank filter over a box: a 15-tap
horizontal minimum followed by a 15-tap vertical one is exactly the 15 × 15
minimum, at 30 taps instead of 225. Same for the max filter the TPAMI guided
filter paper calls for.

---

## The node plan

Sixteen nodes, seven new kernels (`boxBlur` is reused). Written down before
building so that a session that loses its context can pick it up here.

| # | Node | Kernel | Out |
|---|---|---|---|
| 1 | `dehaze:chan` | `dehazeChannelMin` | R16F, `min_c(I_c/A_c)` — with `A = (1,1,1)` this is the plain per-pixel channel minimum |
| 2–3 | `dehaze:dark h/v` | `dehazeRank` | R16F, separable 15-tap **min** → the dark channel |
| 4 | `dehaze:peak` | `dehazePeak` | RGBA16F at ¼, `(maxDark, R, G, B)` of the block's argmax — read back to find `A` |
| 5 | `dehaze:chan/A` | `dehazeChannelMin` | R16F, now with the real `A` |
| 6–7 | `dehaze:min h/v` | `dehazeRank` | R16F, separable 15-tap min |
| 8–9 | `dehaze:max h/v` | `dehazeRank` | R16F, separable 15-tap **max** — TPAMI 2013 §5's counteraction of the min filter's morphological shift, applied to the same quantity the min was |
| 10 | `dehaze:prep` | `dehazePrep` | RGBA32F at ¼: `(g, g², t̃, g·t̃)`, where `t̃ = 1 − ω·(node 9)` |
| 11–12 | `dehaze:blur h/v` | `boxBlur4` | RGBA32F, the four moments together |
| 13 | `dehaze:coeffs` | `dehazeAb` | RG32F, the guided filter's `a` and `b` |
| 14–15 | `dehaze:blur2 h/v` | `boxBlur` | RG32F — the existing kernel, unchanged |
| 16 | `dehaze` | `dehazeRecover` | RGBA16F: lift `(a, b)` bilinearly, `t = a·g + b`, then Eq. (16) |

**One kernel does both rank passes.** A 15 × 15 minimum is a 15-tap minimum
along each axis, and the same kernel with a mode flag is the maximum, so six
nodes share one shader.

**The strength slider is ω, not a blend.** Eq. (12) already has a parameter for
"how much haze to remove", the paper fixes it at 0.95, and `ω = 0` gives
`t̃ = 1` and `J = I` exactly — so zero is the identity by construction rather
than by a lerp bolted on the end. The slider maps `0…1 → ω = 0…0.95`, and its
top is the paper's own value rather than a number chosen here.

**`A` is the one part that is not a per-pixel node.** It is a reduction over the
whole frame, and it depends only on what is upstream of dehaze — not on the
slider — so it is computed once and cached, and recomputed when white balance
or the profile moves. `render()` does a readback of node 4 when it is stale,
picks `A`, pushes it, and renders again; the per-node cache means the second
pass only redoes what changed.

⚠️ **A deviation to state plainly when it ships.** The paper's percentile is
over *pixels* — the top 0.1% of the dark channel. Node 4 max-pools 4 × 4 blocks
first and the percentile is taken over block maxima, so the set selected is the
most haze-opaque part of the frame but is not literally the paper's top 0.1% of
pixels. Max-pooling is the right pooling here — it preserves exactly the
haze-opaque extremes the step is looking for — but it is an approximation and
belongs in `UNSOURCED.md` until measured against the exact percentile.


---

## Where a dehaze drag goes — profiled 2026-07-29

`orion-bench` prints this per node on every run. Unlike clarity, **there is no
hotspot**: the cost is spread almost evenly.

| Node | ms | share |
|---|---|---|
| `dehaze:min h` | 4.51 | 7.2% |
| `dehaze:dark h` | 4.50 | 7.2% |
| `dehaze:max h` | 4.48 | 7.1% |
| `dehaze:dark v` | 4.40 | 7.0% |
| `dehaze:max v` | 4.40 | 7.0% |
| `dehaze:min v` | 4.40 | 7.0% |
| `dehaze:moments` | 4.19 | 6.7% |

Six rank passes, **26.7 ms between them, 43% of the drag**, and each is within
2% of the others. That flatness is the finding: there is nothing here to fix
one of. The separability trick that took clarity from 70 ms to 58 does not
apply, because these passes are *already* separable — a 15 × 15 minimum is a
15-tap minimum along each axis, and that is how they were built.

At ~48 MB written and a cached read per pass, 4.4 ms is roughly 22 GB/s against
a 120 GB/s machine, so these are **tap-count bound rather than bandwidth
bound**: fifteen comparisons per pixel, per pass, six times.

### The published way to make that O(1)

A running minimum over a window of size k can be computed in a constant number
of comparisons per pixel, independent of k:

- **van Herk** — *A fast algorithm for local minimum and maximum filters on
  rectangular and octagonal kernels*, Pattern Recognition Letters 13(7), 1992.
- **Gil & Werman** — *Computing 2-D min, median, and max filters*, IEEE TPAMI
  15(5), 1993.

Both are standard and would take fifteen comparisons to about three.

⚠️ **But it is a sequential scan**, which is the opposite of what a GPU wants:
the algorithm builds running prefix and suffix minima along a line, and each
element depends on the one before it. Adapting it means one thread per line
segment with the segment length tuned to the window, and correctness at the
segment joins is the whole difficulty. That is a session's work with a real
chance of ending slower, which is exactly the shape of the change that already
backfired once on clarity's collapse kernel.

**Not attempted, and the reason recorded rather than the intention.** If it is
tried, the existing GPU test is the safety net: `testDehazeGpu` checks the
separable rank filter against a 15 × 15 patch computed directly, so any
replacement has to produce the same answer or say so.

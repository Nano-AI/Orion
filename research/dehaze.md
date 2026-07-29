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

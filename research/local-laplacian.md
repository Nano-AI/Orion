# Local Laplacian Filters — clarity

The plan of record for M3's first story. Read this before touching
`shaders/llf_*.slang`.

---

## The two papers

| | Paper | Where |
|---|---|---|
| The filter | Paris, Hasinoff & Kautz — *Local Laplacian Filters: Edge-aware Image Processing with a Laplacian Pyramid*, SIGGRAPH 2011 (ACM TOG 30(4)) | [PDF](https://people.csail.mit.edu/sparis/publi/2011/siggraph/Paris_11_Local_Laplacian_Filters.pdf) |
| What makes it affordable | Aubry, Paris, Hasinoff, Kautz & Durand — *Fast Local Laplacian Filters: Theory and Applications*, ACM TOG 33(5), 2014 | [PDF](https://imagine.enpc.fr/~aubrym/projects/llf/texts/2014-fast-laplacian-filter.pdf) |

MIT CSAIL / Adobe / NVIDIA. Both are the standard reference for edge-aware
detail manipulation; the 2011 paper's whole claim is that it is **halo-free by
construction**, which is the failure mode every clarity slider is judged on.

Pyramids are Burt & Adelson (1983), 5×5 kernels — the paper says so explicitly
in §5.1, and the separable `[1 4 6 4 1]/16` kernel is that construction.

---

## The algorithm, as published

For every output pyramid coefficient `(x₀, y₀, ℓ₀)`, take the input's own
Gaussian pyramid value `g₀ = G_ℓ₀(x₀, y₀)`, remap the **whole input image**
point-wise with a function centred on `g₀`, build a Laplacian pyramid of that
remapped image, and copy its `(x₀, y₀, ℓ₀)` coefficient into the output
pyramid. Collapse at the end. (Algorithm 1.)

The remapping splits at a user threshold `σr`, which is what separates detail
from edges — `r(i) = rd(i)` when `|i − g₀| ≤ σr`, and `re(i)` otherwise:

```
rd(i) = g₀ + sign(i − g₀) · σr · fd(|i − g₀| / σr)          (Eq. 1)
re(i) = g₀ + sign(i − g₀) · ( fe(|i − g₀| − σr) + σr )      (Eq. 2)
```

Both are constrained to be monotonically increasing, and continuous at
`|i − g₀| = σr` — `rd(g₀ ± σr) = re(g₀ ± σr)` — which is what stops the filter
from inventing edges that were not in the picture.

**Detail manipulation (§5.2)** is then exactly two choices:

```
fd(Δ) = Δ^α      α < 1 increases detail contrast, α > 1 smooths it
fe(a) = a        the identity — edges pass through untouched
```

### The noise term is part of the published filter, not a local addition

§5.2, *Reducing Noise Amplification*, verbatim in substance: increasing the
contrast of details makes noise and compression artefacts more visible, and
they mitigate it by limiting the smallest `Δ` amplified. When `α < 1`:

```
fd(Δ) = τ·Δ^α + (1 − τ)·Δ
```

with `τ` a smooth step equal to 0 below 1% of the maximum intensity, 1 above
2%, smooth in between. The paper states every result in it and its supplement
was computed with this function — so shipping `Δ^α` bare is *not* shipping the
published filter. It is the α < 1 branch that has unbounded slope at the
origin, which is the mechanism: without the term, the finest, lowest-amplitude
signal in the frame — which is the noise — gets the largest gain of anything in
the picture.

### Colour: the intensity channel, with ratios kept

§5.3, and Figure 9 is the side-by-side. The paper offers both: remap the RGB
vector (Eq. 3a/3b, with `unit(i − g₀)` and `‖i − g₀‖`), which also boosts
*colour* contrast, or "treat only the luminance channel and reintroduce
chrominance after". Their own tone-manipulation implementation takes the second
— intensity `Iᵢ`, colour ratios `ρ = I / Iᵢ`, filter applied to `log(Iᵢ)`.

Orion takes the luminance path, for the reason Figure 9 shows: filtering the
channels separately moves hue, and a clarity slider that shifts colour is a
clarity slider that fights the grading wheels.

### Scale selection is published too

§5.2: detail manipulation is applied at all scales in its basic form, "but one
can also control which scales are affected by limiting processing to a subset
of the pyramid levels" — Figure 7c (all levels), 7d (lowest 2), 7e (level 3 and
higher). And, importantly for a slider rather than a checkbox: *"while this
control is discrete, the changes are gradual, and one can interpolate between
the results from two subsets of levels if continuous control is desired."*

That sentence is the licence for a per-level weight rather than a per-level
switch, and it is what a **Texture** control would be built from later — same
pyramids, fine levels weighted instead of coarse.

---

## Why the 2011 algorithm cannot ship, and what 2014 does about it

A pyramid per output coefficient. The authors measure their own implementation
at **about a minute per megapixel** single-threaded, four seconds on eight
cores. At 24 MP that is not a slider.

Aubry et al. §3 replaces "a pyramid per pixel" with "a pyramid per *intensity*",
which is the whole trick:

1. Compute the Gaussian pyramid of `I`.
2. Regularly sample the intensity range with values `{γⱼ}`.
3. Compute the remapped images `{rⱼ(I)}` and their Laplacian pyramids.
4. For each coefficient `(ℓ, x, y)`: read `g` from the input Gaussian pyramid,
   find `j` and `a` with `g = (1−a)γⱼ + a·γⱼ₊₁`, and **linearly interpolate**
   `L_ℓ[O] = (1−a)·L_ℓ[rⱼ(I)] + a·L_ℓ[rⱼ₊₁(I)]`.
5. Collapse.

Linear in the pixel count, because the number of precomputed pyramids is fixed.

### How many γ levels — the paper answers this directly

Their sampling argument is a Nyquist argument on `r` viewed as a function of
`g`: only `(i − g)·f(i − g)` varies, `F[x f(x)] ∝ F[f]′`, so if `f` is
band-limited so is `r`. Then, verbatim:

> *"In practice, a Gaussian function G_σ is used for f, e.g., for the detail
> enhancement, we recommend sampling the intensity range every standard
> deviation σ."*

So **Δγ = σr** is the published sampling rate, and Orion built that first —
eight γ levels, with `kSigmaR` defined as `kGammaStep` so the two could not
drift apart.

They report the approximation is "above 30 dB" and "the differences are
invisible in practice", at about 50× the speed of the 2011 heuristic; on a
GeForce 480 GTX, 49 ms for one megapixel and 116 ms for four.

**Measured here, that rate is not enough, so Orion samples twice as finely.**
Against Paris et al.'s exact Algorithm 1, at the strongest setting the slider
reaches (α = 0.25):

| samples per σ | γ levels | mean error | max error | PSNR |
|---|---|---|---|---|
| 1.0 | 8 | 0.354 EV | 1.359 EV | **28.0 dB** |
| 2.1 | 16 | 0.151 EV | 0.696 EV | **35.6 dB** |
| 4.4 | 32 | 0.159 EV | 0.408 EV | — |

One sample per σ lands under the paper's own stated accuracy. Doubling recovers
it; doubling again buys nothing, and that plateau is the useful part — it says
the remaining error is the linear interpolation standing in for a sinc
reconstruction, which more γ levels cannot fix. Sixteen is the knee, not a round
number, and `kSigmaR` is therefore a constant in its own right rather than an
alias for `kGammaStep`.

The gentler settings never needed it: α = 0.5 measures 42.0 dB and α = 4
measures 49.0 dB at sixteen levels. It is the strongest detail boost — where the
power curve is least smooth in γ — that sets the requirement, which is exactly
what the Nyquist argument predicts.

`testLocalLaplacianGpu` prints that table on every run, so the evidence moves
if the constants do.

### The memory trade, also named in the paper

§3, Discussion: a straightforward implementation stores every precomputed
pyramid at once; alternatively "one can instead compute one such pyramid at a
time and add directly its contribution to the output pyramid", which needs more
updates of the output pyramid but only one pyramid resident.

Orion takes the first. The second wants read-modify-write accumulation into a
shared target, and Orion's DAG gives every node its own output texture — the
accumulating form would need one node per (γ, level) pair chained in series,
which is *more* nodes and more bandwidth, not less. See "packing" below.

---

## What Orion does, and where it departs

| Piece | Value | Why |
|---|---|---|
| Working quantity | normalized log2 luminance, Rec.2020 weights | Paris §5.3 filters `log(Iᵢ)`. Orion's whole pipeline already reasons in log2 stops — `guide_prep.slang` computes the same thing for the guided filter |
| Window | −10 … +2 EV, 12 stops, clamped | Puts `σr` and the noise thresholds in EV units that mean something |
| `σr` | 1/7 of the window, 1.714 EV | The detail/edge threshold. A local swing under about a stop and three quarters is texture |
| γ levels | **16**, Δγ = 1/15 | 2.14 samples per σ. Measured, not recommended — see the table above |
| Pyramid depth | 6 levels | Coarsest is ~190 px on a 6024 px frame — beyond that is global contrast, not clarity |
| `α` | `2^(−2·clarity)` | Endpoints are the paper's own Figure 7b and 7c values, 4 and 0.25. ⚠️ The curve between them is ours — `UNSOURCED.md` §10 |
| `fe` | identity | §5.2, detail manipulation. Edges are not touched — clarity is not a tone control |
| Noise `τ` | smoothstep over 1%…2% of the window | §5.2 as published, in the window's own units — 0.12 to 0.24 EV |

**Luminance weights.** Paris §5.3 uses `Iᵢ = (20·Ir + 40·Ig + Ib)/61`. Orion
uses Rec.2020's `(0.2627, 0.6780, 0.0593)`, which is the working space's own
luminance and what every other node in the pipeline already means by the word.
Using two different definitions of luminance in one pipeline is a bug waiting
for a frame that exposes it.

**Packing, and why the node count is what it is.** The sixteen γ pyramids are
stored four to a texture in `RGBA16Float` — four textures per level — so one
dispatch produces four of them and one fetch reads four. Thirty-two nodes hang
off one slider, which is the pyramid's arithmetic and not the code's: seven
kernels, none over a hundred lines. Half float rather than 32-bit: the quantum in
this normalized window is ~0.0005, which is 0.006 EV — an order of magnitude
below the noise floor `τ` already declines to amplify, so it cannot survive
into the output as anything visible.

**Placement: before the tone controls, after the camera profile.** Same
position, and the same reasoning, as the guided-filter chain. Exposure is a
multiply, so in log2 it is an additive constant — and the Laplacian of a
constant offset is zero, so computing clarity before exposure gives *bit for
bit* what computing it after would, while leaving the whole chain cached for
the slider people actually drag. Highlights, shadows, contrast and the curve
are not additive in log2, so for those the placement is a genuine choice: it
means clarity describes the scene, not the tone mapping applied to it.

---

## What it costs, measured

Sony ILCE-7M3, 6024x4024, M4. Clarity at zero switches all thirty-two nodes
off, so **the M0 gate does not move**: 10.61 / 10.18 / 9.79 ms p95 on the three
sample frames, and an exposure drag still recomputes three nodes.

Dragging clarity itself is **70 ms across 35 nodes**, which is correct and not
yet interactive. Where it goes, one command buffer per node (`orion-bench`
prints this on every run):

| Node | ms | share |
|---|---|---|
| `clarity:remap 1.0` | 12.07 | 16% |
| `clarity:collapse 0` | 12.01 | 16% |
| `clarity:remap 1.1` | 8.47 | 11% |
| `clarity:remap 1.3` | 7.47 | 10% |
| `clarity:remap 1.2` | 7.46 | 10% |
| `clarity` (apply) | 4.70 | 6% |
| `clarity:collapse 1` | 4.03 | 5% |

The four remap nodes are 47% of it between them. Each reads the full-resolution
luminance with a 5x5 footprint at half resolution and remaps every tap for four
gammas — a hundred remappings per output pixel, of which the `|i - g| > sigmaR`
early-out skips roughly three quarters.

**A hunch was tried first and was wrong, which is why the profile exists.** The
collapse kernels fetch all four packed stacks at all nine expansion taps while
only two of the sixteen gammas are ever used, so they were changed to fetch
only the stacks a pixel needs. Output was bit-identical and it ran **slower** —
78.9 ms against 71.6 — because the branch diverges more than the saved fetches
were worth. Reverted. `Pipeline::setProfiling` was written instead, and it
pointed at a different kernel entirely.

**Candidates, in the order worth trying:**

1. **Separable halving in threadgroup memory.** The Burt kernel is separable, so
   a tile loaded once and blurred in two passes turns 25 taps into 10. It cuts
   the remapping count by the same factor, which is the dominant term. One
   kernel, no new textures, and it applies to all three downsampling nodes.
2. **Remap at full resolution into its own texture**, then halve it. Takes the
   remapping count from 25 per full-resolution pixel to 1, at the cost of
   776 MB of intermediates and the traffic to write and read them. Estimated a
   wash on this machine and worth measuring rather than assuming.

Neither is a change to the filter, only to how it is evaluated, so both are
covered by the existing reference tests.

## What is checked, and where

- **`orion-tests`, CPU.** Paris Algorithm 1 implemented literally — remap the
  whole image per output coefficient, build the whole pyramid, take one
  number — on a small synthetic frame, and the GPU's fast approximation
  measured against it as PSNR. The gate is Aubry's own claim, 30 dB.
  This is the check that says the approximation is the paper's approximation
  and not a different filter that happens to look sharper.
- **`orion-tests`, GPU.** Clarity at zero is the identity; a step edge does not
  gain a halo; a flat patch does not move; the noise term measurably declines
  to amplify a low-amplitude ramp that a bare `Δ^α` would.
- **`orion-bench`.** A probe with a magnitude floor, on the `Detail` metric —
  mean luma is the wrong instrument for a local-contrast filter for the same
  reason it was the wrong instrument for sharpening.

# High ISO - what the wavelet chain leaves behind, and four answers to it

Written 2026-10-18 after Dhruv asked for frames at very high ISO to stop looking like very high ISO once denoised.
`research/detail.md` holds the chain that ships; `research/denoise-masked.md` holds its second pass (#299-#301).
This file is about what a 12800 frame still shows after both, and the pieces that answer each part.

**Status:** plan of record, implemented in the session that wrote it; the decision rows are named at the end as they land.

---

## 0. What a high-ISO frame looks like after the chain, and why

Four scales of the starlet reach a tap spacing of 8 px, so the chain removes grain up to roughly the 16 px support of the coarsest B3 spline.
The Poisson-Gaussian model `var = a·x + b` makes the luma threshold right at every tone, and the Detail factor (#299) keeps texture that stands above it.
What survives is not grain.
It is three other things, each with a different cause, and one of them is not even in the picture yet.

| What the eye reads as "high ISO" | Cause | Which piece |
|---|---|---|
| Coarse colour blotches, strongest in the shadows, 20-60 px across | Demosaicing band-limits chrominance to low spatial frequencies, so chroma noise is *coarse* by construction and four scales do not reach it | §1 Smoothness |
| A waxy, mushy surface where fine texture was | The garrote removed the texture that sat near the noise floor along with the noise; nothing puts micro-contrast back | §2 Texture |
| Clarity makes it worse | The local Laplacian's noise guard is 1-2% of its 12-stop window, fixed, so on a frame whose noise is 0.5 EV in the shadows Clarity amplifies grain | §3 Clarity's noise floor |
| Residual grain with the wavelet's own blotchy shape when Luminance is pushed past 3 | Shrinkage leaves a residual at every scale; a method that averages *similar* pixels instead of *nearby* ones leaves a different residual | §4 Non-local means, measured |

And the one that is not a filter: a hard denoise reads as plastic because real photographs have grain.
Film grain (`research/film-grain.md`) already ships and is what every editor's manual tells the photographer to add after a hard denoise.
It needs no code, only a place in the recipe (§5).

---

## 1. Smoothness: chroma at the scales the chain does not reach

### Why chroma noise is coarse

> D. Alleysson, S. Süsstrunk and J. Hérault, *Linear demosaicing inspired by the human visual system*, IEEE Transactions on Image Processing 14(4), 439-449, April 2005.
> E. Dubois, *Frequency-domain methods for demosaicking of Bayer-sampled color images*, IEEE Signal Processing Letters 12(12), 847-850, December 2005.

Both papers derive the same picture of the Bayer spectrum: luminance occupies the baseband, and the two chrominance channels are modulated out to the corners and edges of the spectrum, where they alias with luminance.
Every demosaicing method therefore reconstructs chrominance through a low-pass estimate, and the noise that rides on it comes out low-pass too.
That is the whole reason the colour blotches on a high-ISO frame are large while the luma grain is fine, and it is why extending the *chroma* shrinkage to coarser scales is the right lever where extending luma would flatten gradation.

### What is built

Two more starlet scales, **chroma only, at quarter resolution**.
The starlet is the same transform (`detail.md`, Starck, Fadili & Murtagh 2007), so nothing new is being cited for the decomposition; the scales are `j = 4, 5`, tap spacing 16 and 32 full-resolution pixels.

- `denoise:chroma down` takes **`c_4`, the full-resolution chain's coarsest blur**, averages 4x4 blocks, and keeps the block's full colour in `.rgb` with its luma in `.a`; the shrink splits luma from chroma itself.
  ⚠ The plan said `denoise:shrink 0`'s output. `c_4` is right: by definition it holds only the scales the chain did not reach, so the quarter chain continues the same transform, where the shrunk picture would have mixed already-handled scales into scale 4.
- Two `atrousBlur` and two `atrousShrink` nodes run unchanged on that texture with `strengthLuma = 0` and steps 4 and 8, which at quarter resolution are 16 and 32 full-resolution pixels.
- `denoise:chroma up` bilinearly expands `shrunk − unshrunk` and adds it to `denoise:shrink 0`'s output (its first input, so a disabled chain resolves straight past).
  The difference is what is expanded, not the picture, so the full-resolution luma and fine chroma are untouched to the bit; only the coarse chroma correction is resampled, and it is smooth by construction.

Scale norms: `‖W_4‖₂ = 0.0204` and `‖W_5‖₂ = 0.0102`, recomputed from the B3 taps in `testDenoiseSmoothnessGpu` and asserted.
⚠ `detail.md` printed the fifth term as 0.0202, off in the third figure; the constant in the header is checked rather than typed.

**The coarse shrinks read a chroma noise model, not the luma one.**
The plan reused `aLum`/`bLum`; on the test's card that removed 37% of the coarse chroma energy at Color 3 against a 67% plateau.
`coarseChromaNoise` carries the same fit through the chroma split (`Σ_c (δ − w_c)² var_c`) and divides by each channel's Bayer sampling density (R and B a quarter, G a half), which on that card is 3.0x the luma deviation and removes 66%.
It is derived, then measured, not sourced as a formula; `UNSOURCED.md` §20 says what would settle it (a MAD of the quarter chain's own `w_4` chroma over flat regions of a real 12800 frame).

**Measured (`testDenoiseSmoothnessGpu`):** coarse chroma energy 8.63e-7 → 2.94e-7 (66% removed) at Color 3, Smoothness 1; worst output luma move 0.0025 against 0.035 for Color's own full-resolution shrink; Smoothness 0 byte-identical; Color 0 runs nothing.
Six quarter-resolution nodes, about **5 ms** at 42.4 MP (`chroma up` 1.9-2.4 ms is the largest).
⚠ The garrote keeps anything well above `τ`: a blotch field ten times the model's coarse chroma deviation loses only 36%.
Real sensors add low-frequency read noise the Poisson-Gaussian fit does not see, so on a real 12800 frame the model may sit under the blotches; §5's by-eye step is where that shows.

**Control.** `denoiseSmoothness` 0..1, default 0.
The coarse strength is `smoothness × denoiseColor`, so Color 0 means nothing runs and the two never disagree about whether chroma is being denoised.
At 0 the six nodes are off and every render is byte-identical to today (`testDenoiseSmoothnessGpu`).

**Cost.** Six nodes at a sixteenth of the pixels: about 21 MB each at 42 MP, against 336 MB for one full-resolution scale, which is the reason `detail.md` stopped at four scales and the reason this does not stop there.

**Where it cannot go.** Chroma at coarse scales is where *small saturated objects* live: a red hold on a grey wall, a distant traffic light.
The garrote keeps a coefficient that stands above `τ`, and a hold is far above it, but a strong Smoothness on a strong Color will desaturate small colour the way every coarse chroma filter does.
It is a slider for that reason and not a constant.

---

## 2. Texture: multiscale contrast gain that knows the noise floor

### Source

> J.-L. Starck, F. Murtagh, E. J. Candès and D. L. Donoho, *Gray and color image contrast enhancement by the curvelet transform*, IEEE Transactions on Image Processing 12(6), 706-717, June 2003.

Their equation (1) is a gain applied to a multiscale coefficient `w` given the noise deviation `σ` at that scale:

```
y(|w|)  = 1                                          |w| <  c·σ
        = ((|w| − cσ)/(cσ))·(m/(cσ))^p + (2cσ − |w|)/(cσ)    cσ ≤ |w| < 2cσ
        = (m/|w|)^p                                  2cσ ≤ |w| < m
        = (m/|w|)^s                                  |w| ≥ m
```

`p` is the degree of non-linearity, `m` the amplitude above which nothing is boosted, `c` the noise multiple below which nothing is touched, `s` a saturation exponent (0 here, so strong edges pass at gain 1).
The function was written for curvelets and the paper applies it to the à-trous wavelet as well (their §IV); it is the multiscale form of the contrast-equalisation idea that goes back to Laine et al. 1994 and Agfa's MUSICA.
The property that matters here is in the first line: **below `c·σ` the gain is exactly one**, so the noise is not boosted because the noise floor is an input to the function, not a consequence of it.

### Why here and not in the local Laplacian

`research/local-laplacian.md` §"Scale selection" already licenses a Texture control as a per-level weight on the same pyramids Clarity uses.
That route was considered and is not taken, for two reasons.
It costs the whole filter, 70 ms at 42 MP, whenever Clarity is off; and its noise guard is a fixed fraction of the window (§3 of this file is about that), so a fine-level boost on a high-ISO frame would amplify the grain the chain just removed.
The starlet chain already holds `σ_j(x)` per pixel, per scale, from the frame's own fitted model.
A gain applied there costs a few lines and no nodes, and its floor is the measured one.

### What is built

The gain multiplies the **kept luma coefficient after the garrote** in `atrousShrink`, at the two finest scales only (`j = 0, 1`, level weights 1 and 0.5), with:

| Constant | Value | Why |
|---|---|---|
| `c` | 3 | Three deviations: the conventional "surely signal" line; the garrote's own `τ = β·σ` sits below it for every `β` the slider reaches |
| `m` | max(30·σ, 0.25·Y) | Where the boost has faded to one. 30σ alone made this a high-ISO-only control (measured below); a quarter of the local coarse luminance is an edge on any frame, and texture sits under it. Per pixel, no reduction pass, where the paper's `m` is an image quantile |
| `p` | `texture × 0.5` | Signed. Negative `p` gives `(m/|w|)^p < 1`, which attenuates weak fine texture and leaves edges at `m` alone: the skin-smoothing direction |
| level weights | 1, 0.5 | The two scales that carry "texture" at any sensor pitch; the third would be Clarity's territory |

These four are chosen, not sourced, and are entered in `UNSOURCED.md` §20.
The paper's `c`, `p` and `m` are free parameters there as well; what is sourced is the shape of the function and the floor.
⚠ One deviation from the printed equation: its middle branch ends at `(m/cσ)^p` where the third begins at `(m/2cσ)^p`, a jump of `2^p` that makes the transfer `w·y(w)` non-monotone; Orion's ramp ends at the third branch's value instead, which is continuous.

**Measured (`testTextureGpu`).**
Noisy card (the fitted model recovers the fixture's true `a`, `b`): Texture +1 lifts a 6σ period-4 ripple **x2.07** and −1 takes it to **x0.48**; flat grain rises **0.08%** at +1 and 0.01% at +0.5; mean shift 3.7e-6; a step away from its edge moves 6.8e-6; 0 is byte-identical; Texture 0.5 alone runs the chain.
Clean card (σ ≈ 7e-4, a ripple an eighth of the level deep): with `m = 30σ` alone the bench measured Texture +1 moving the two low-ISO sample frames by 0.0001-0.0002, under its "moved at all" line, because every real coefficient sat above `m` and passed at gain one.
With `m = max(30σ, 0.25·Y)` the same ripple lifts **x2.49** at +1 and **x0.30** at −1 with the flat mean held, and the noisy card's numbers are unchanged (there 30σ is the larger).
Cost: Texture alone runs the capture chain, about **46-48 ms** at 42.4 MP; with Luminance or Color already on it costs nothing more.
⚠ Texture −1 is non-monotone in the 3σ-6σ band (the gain falls from 1 to 0.45 while `|w|` doubles); that is inherent to eq. (1) with negative `p`.

**The chain runs when Texture is non-zero even with both strengths at zero.**
The garrote then passes every coefficient (`τ = 0`), the gain leaves everything under `3σ` at exactly one, and so Texture alone does not regrow grain.
The masked chain (#300) receives gain 1 and is unchanged.

**Control.** `texture` −1..1, default 0, byte-identical at 0 (`testTextureGpu`).
Unavailable, with a one-line reason in the panel, on a linear DNG source, where the capture chain is not built because there is no mosaic to fit a model to.

**Where it sits.** In linear camera RGB before lens correction and before sharpening, like the shrink it rides on.
Sharpening is a different operator (unsharp mask, scale-free, edge-shaped); Texture is coefficient gain on two known scales with a known floor.
A photograph can carry both, and on a high-ISO frame it should: Texture to put the surface back, Sharpening's Masking to keep the grain out of the edges.

---

## 3. Clarity's noise floor, from the frame instead of the window

### What ships

Paris et al. §5.2's noise term blends the remapping back to the identity below a threshold: `τ = 0` below 1% of the maximum intensity, 1 above 2%.
`LocalLaplacian.h` states it in the window's own units, 0.12 to 0.24 EV, and `research/local-laplacian.md` is right that this is part of the published filter.
It is also a *constant*, and the paper's own examples are well-exposed photographs whose noise is far under 0.12 EV.

On a frame at ISO 12800 the luma noise in the lower midtones is 0.3-0.6 EV.
Everything the chain did not remove sits above the guard, so Clarity at +0.5 takes the finest, lowest-amplitude signal in the frame, the grain, and gives it the largest gain of anything in the picture, which is the paragraph in the paper that explains why the guard exists.

### What is built

The guard's thresholds become per pixel:

```
σ_Y²(i)   = a·ΣA·Y + b·ΣB            the capture model carried into working luminance, k = Mᵀw, as DevelopDenoiseLocal.cpp does for the masked chain
σ_EV(i)   = σ_Y / (Y · ln 2)         the same deviation in stops at this pixel's own luminance
lo(i)     = max(kNoiseLo, kFloor · σ_EV(i) / kWindowEv)
hi(i)     = 2 · lo(i)
```

The pixel's own `i` is in hand at both places the remapping runs (`llf_remap_h.slang` and `llf_collapse0.slang`), so this is two parameters on each, the CPU mirror in `llf::remap`, and `testLocalLaplacianRemap` still agreeing at the branch points.
The `max()` keeps a clean frame byte-identical: where the measured noise is under the paper's constant, the paper's constant stands.

`kFloor` is a chosen multiple, entered in `UNSOURCED.md` §20; the two things that are sourced are the guard's shape (Paris et al.) and the model that feeds it (Foi et al. 2008, through the matrix as in #300).

**Measured (`testClarityNoiseFloorGpu`), and `kFloor` is 0.5, not the plan's 1.0.**
A card at a 30% deviation with a ripple at twice it, Clarity +1; the shipped guard raises the 3x3 grain RMS 0.0352 → 0.0587 and gives the ripple x1.278.

| `kFloor` | Grain rise, as a share of the shipped guard's | Ripple gain |
|---|---|---|
| 0.25 | 69% | x1.211 |
| 0.35 | 37% | x1.137 |
| **0.5** | **15%** | **x1.072** |
| 0.75 | 5% | x1.022 |
| 1.0 | 1% | x1.005 |

At one deviation the ripple keeps 2% of its boost, so Clarity on a noisy frame would do nothing; half is the knee.
A clean card renders byte-identical to the shipped guard; the CPU and GPU agree across the guard's branch point.
⚠ The floor binds on ordinary frames too: on `underexposedsubject` it changes the Clarity +1 render by 0.0024 mean |diff| against Clarity's own 0.0102, because Clarity reads pre-exposure values and a lifted shadow has a large deviation in stops.
That is the guard doing what the paper says it is for, on a frame the paper's constant could not see; it is also a render change on finished edits that carried Clarity on a noisy frame, stated here rather than hidden.

Zero nodes, zero cost, and it is the one piece in this file that changes a render with nothing new on the panel.
Only frames whose measured noise exceeds the constant move; the test renders one that does and one that does not.

---

## 4. Non-local means, built to be measured

### Sources

> A. Buades, B. Coll and J.-M. Morel, *A non-local algorithm for image denoising*, IEEE CVPR 2005, vol. 2, 60-65. doi:10.1109/CVPR.2005.38
> A. Buades, B. Coll and J.-M. Morel, *Non-Local Means Denoising*, Image Processing On Line 1 (2011), 208-212. doi:10.5201/ipol.2011.bcm_nlm

The second gives the pixelwise algorithm and its parameter table; it is implemented here **from the equations**.
⚠ The IPOL reference code is GPL and the article itself says some files "use algorithms possibly linked to patent [3]"; it is not read and not copied.

### Patent status, checked 2026-10-18

`denoise-masked.md` §4 passed NL-means over for cost and `CLAUDE.md` says a citation is not clearance, so the patent the IPOL article names was looked up before anything else.

| Document | Status |
|---|---|
| EP 1 749 278 A2, Buades, Coll & Morel, priority 2004-05-05 | **Withdrawn**; never granted |
| US 8,253,825 B2, same family, granted 2012-08-28 | **Expired 2020-10-05, fee related** (maintenance fee not paid); the adjusted term would have run to 2027-12-22 |

Read from Google Patents' legal-status pages, which is a database, not an opinion.
Freedom to operate remains **noted, not assessed**: nobody here has searched for other families, and a lapsed patent can in some jurisdictions be reinstated within a window.
What changed is that the method's *only known* patent is no longer in force, which turns the question from "may we" into "is it worth its cost", and the cost is what this section measures.

### The algorithm

For each pixel `p`, over a `(2r+1)²` search window `B(p, r)`:

```
û(p) = Σ_q w(p,q)·u(q) / Σ_q w(p,q)
w(p,q) = exp(−max(d²(p,q) − 2σ², 0) / h²)
d²(p,q) = mean over the (2f+1)² patch of (u(p+j) − u(q+j))²
w(p,p) = max over q ≠ p of w(p,q)
```

IPOL Table 1, grey images: patch 3×3 and search 21×21 with `h = 0.40σ` for `σ ≤ 15` on a 0..255 scale; patch 5×5, search 21×21, `h = 0.40σ` for `15 < σ ≤ 30`; larger windows above that.
The `2σ²` subtraction is what makes `h` a multiple of `σ`: two patches of pure noise sit at `d² ≈ 2σ²` and receive weight one.

### Orion's form

- **One node, `denoise:nlm`, between `hl:fill` and `denoise:blur 0`, luma only.** `out = in + (Ŷ − Y)`; chroma is untouched and stays the wavelet chain's business.
- **Profiled.** `σ²(p) = aLum·Y + bLum` at the centre pixel's 3×3 mean, the capture chain's own luma model.
  The `2σ²` subtraction then holds at every tone, which the 8-bit table's constant `σ` cannot.
- **Patch 3×3, search 11×11**, chosen by measurement below; the table's 21×21 / 5×5 row was the plan and is kept measurable.
- **Control** `denoiseNlm` 0..1 is `k` in `h = k·σ`, so the table's 0.40 sits at mid-slider. Default 0, node off, byte-identical.
- **With it on, the wavelet chain's luma threshold is overestimated** (its model describes the noise before NLM ran). The panel note and the agent vocabulary say so: NLM takes the grain, keep Luminance at 0 or low, Color and Smoothness still take the blotches.

### Cost, and why it is affordable at all

The gather is 441 offsets × 25 taps per pixel.
Read from textures that is ~11k fetches per pixel, which `denoise-masked.md` §4 rightly called a background pass.
Through **threadgroup memory** it is one 40×40 tile load per 16×16 group (apron `r + f = 12`, 6.4 KB of luma) and every patch difference reads shared memory.
⚠ This is the first `groupshared` kernel in the tree, so the first step is a smoke test that Slang's `groupshared` and `GroupMemoryBarrierWithGroupSync` come through to Metal threadgroup memory on this toolchain.
The fallback, if they do not, is the plain gather at search 11×11 and patch 3×3, which is also one of the two windows the bench measures.

### What "measured" means, in order

1. `testNlmGpu`: the GPU agrees with the CPU reference (`pipe/NonLocalMeans.h`, the `LocalLaplacian.h` pattern) within 1e-3 on a 96×96 noisy card; 0 is byte-identical; flat 3×3 RMS falls by at least half at `k = 0.4`; a 6σ period-4 ripple keeps at least 80% of its amplitude, and the test prints what the wavelet chain keeps at matched flat RMS, so the gain is a figure.
2. `orion-bench`, both windows, at 42 MP and at the preview scale, from `Pipeline::setProfiling`. The budget for a slider: preview ≤ 30 ms (it is upstream of every drag and the DAG caches it, so it reruns only when its own value or the white balance moves) and full ≤ 1 s on the settled render.
3. Dhruv's eye, on frames at ISO ≥ 6400, the §5 recipe with and without `denoiseNlm`.

### Measured, 2026-10-18

`groupshared` came through Slang to Metal threadgroup memory on the first try, so the fallback was not needed; the kernel takes both windows at run time and the bench measures both on one build.

**Synthetic card** (96×96, grey 0.25, σ 0.02, a period-4 ripple at 6σ in one half), `k = 0.4`:

| Window | Flat noise, before → after | Ripple kept | Wavelet chain at matched removal keeps |
|---|---|---|---|
| 11×11 search, 3×3 patch | 0.01908 → 0.00444 | **99.9%** | 85.9% (β 2.0) |
| 21×21 search, 5×5 patch | 0.01908 → 0.00250 | **99.7%** | 78.0% (β 2.5) |

Both windows keep the texture the chain smooths, which is the whole claim; the large window removes more noise for the same `k`.
⚠ A perfectly periodic ripple is non-local means' best case, since every period is an exact match; a real wall is not periodic, and the by-eye check in §5 is the honest one.

**Cost**, the two 42.4 MP sample frames, Apple GPU, `Pipeline::setProfiling`:

| Window | Full resolution | Preview graph (1/4 per axis) |
|---|---|---|
| 11×11 / 3×3 | 333-344 ms | 19 ms |
| 21×21 / 5×5 | 2,907-3,143 ms | 149-168 ms |

**Decision: it ships as a slider with the 11×11 / 3×3 window.**
The published window misses both budgets by five to eight times; the small one meets both.
`nlm::kPublishedRadius/kPublishedPatch` stay in the header and the bench prints both so the trade is re-measurable, and a background-pass form of the large window remains possible later without touching the kernel.

---

## 5. The recipe, which is where the grain node comes in

No button.
The recipe lives where an agent and a photographer both read it: `mcp/README.md`, the `describe_edits` notes, the Detail panel's section text, and `docs/agent-field-check.md`.

On a frame above ISO 3200:

1. Read `capture.iso` (new in `get_stats` and `current_photo`, see the decision rows) and `noise` on a flat patch, `shading` on a face.
2. `denoiseNlm` 0.4 **with `denoiseLuma` 1-1.5** for the grain (the bake-off below: alone it is the noisiest option; paired it is the cleanest).
3. `denoiseColor` 3; add `denoiseSmoothness` 0.5-1 if blotches remain in the shadows.
4. `denoiseDetail` 0.5 where there is texture to keep.
5. `texture` +0.5 or more to put the surface back where there is texture above the noise; on a wall that is all grain it has nothing to lift (bake-off, point 2).
6. `grainAmount` 0.01-0.02 at `grainSize` 2 if the result reads as plastic: fine, monochrome, midtone-weighted grain is what a clean photograph has and what a denoised one lacks.
7. Re-read `noise` and `shading` under `state: "proposed"`.

### Bake-off, 2026-10-18: four frames, four variants, Dhruv's eye pending

Four `climbing` frames found through the new `capture.iso` (DSC00709 at ISO 16000, DSC01157 at 10000, DSC00806 and DSC01261 at 8000), copied four times each; an Opus agent edited A, B and C through the MCP on one base look per frame (as-shot WB, contrast 1.5, highlights −0.2, shadows 0.1, vibrance 0.05, sharpen 0.3 / masking 0.85, Clarity 0, no masks or grain), and D was added after its report.
All read under `state: "proposed"`, flat patch `noise`; exports at full resolution in `highiso-bakeoff/exports/`.

| Variant | Denoise keys | 00709 wall (as shot 0.0463) | 01157 wall (0.0296) | 00806 mat (0.0308) | 01261 wall (0.0272) | At native resolution |
|---|---|---|---|---|---|---|
| A, today's controls | luma 2.5, color 4, detail 0.5 | 0.0094 | 0.0056 | 0.0052 | 0.0055 | soft; coarse green/magenta mottle on every wall and mat |
| B, recipe without NLM | A + smoothness 0.7, texture 0.3 | 0.0098 | 0.0058 | 0.0054 | 0.0058 | mottle gone, neutral walls, fine speckle remains; no colour lost (jersey saturation 0.823 both ways, pink hold 0.368 vs 0.370) |
| C, NLM alone | nlm 0.4, luma 0, color 4, smoothness 0.7, detail 0.5, texture 0.3 | 0.0172 | 0.0092 | 0.0087 | 0.0090 | crunchy, wormy luminance grain; plaster scratches and logo text a touch crisper |
| D, NLM paired | C + luma 1.5 | **0.0049** | **0.0036** | **0.0031** | **0.0027** | the cleanest by the number on every frame, 0.5-0.6 of A; the eye has not judged it |

Three findings, all measured, none yet judged by Dhruv:

1. **Smoothness does what §1 said**, on every frame, and desaturated nothing measurable.
2. **Texture at 0.3 is inert on these frames**: toggling it on 00709-C moved wall noise 0.01725 → 0.01723. The fine scales of an ISO 8000-16000 wall or jersey are noise to the model, so there is nothing above `3σ` to lift; the recipe now says +0.5 and up, where there is texture above the noise, and the clean-card test is where the control shows.
3. **NLM alone is the wrong pairing and NLM with Luminance 1.5 is the right one.** At `k = 0.4` with a 3×3 patch the small window removes less than Luminance 2.5 on its own and leaves a structured residual; behind it the wavelet chain at 1.5 takes that residual to half of A's grain. The research file said "keep Luminance at 0 or low" because the chain's threshold is overestimated behind NLM; "low" is right, "0" was not.
   ⚠ The overestimate is still there: the chain's `σ` describes the noise before NLM ran. A measured residual factor per `k` (the synthetic card gives 0.23 at 0.4) would let the chain's threshold follow; not built, a decision for after the eye has judged D.

---

## 6. Considered and not built

| Method | Source | Why not now |
|---|---|---|
| Texture as a fine-level weight in the local Laplacian | Paris et al. 2011 §5.2, `local-laplacian.md` | Costs the whole filter when Clarity is off; fixed noise guard. §2 explains; the route stays open |
| More full-resolution starlet scales for luma | `detail.md` | 336 MB a scale at 42 MP, and coarse luma shrinkage flattens gradation; the coarse problem is chroma's, §1 |
| BM3D | Dabov et al. 2007 | Non-commercial licence from Tampere; also GPU-hostile (`detail.md`) |
| Learned denoiser | `denoise-learned.md`, #111 | The domain argument stands; nothing here changes it |
| ISO-adaptive defaults (set the recipe from `capture.iso` automatically) | `FEATURES.md` "ISO-adaptive presets" | A preset is a patch; adaptivity is a new axis. The agent has the ISO now and can do it per frame; a product default waits for a decision |

⚠ Freedom to operate was not assessed for §1-§3 either.
Alleysson 2005 and Dubois 2005 are frequency-domain descriptions; Starck 2003's function has been in open astronomy software for two decades; Paris et al.'s guard already ships in Clarity.
That is provenance, not clearance.

---

## History

| Date | What |
|---|---|
| 2026-10-18 | Written. §1 coarse chroma, §2 noise-aware texture gain, §3 Clarity's measured floor, §4 NL-means with its patent status and measurement protocol, §5 the recipe |
| 2026-10-18 | Implemented, same day. §1 reads `c_4` with a chroma model; §2's `m` gained the brightness ceiling after the bench found 30σ alone reached nothing on a clean frame; §3's `kFloor` 0.5 from the sweep; §4 ships with the 11×11 window. Decisions #303-#308 |

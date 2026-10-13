# Denoise, second pass - texture-adaptive thresholds and masked denoising

Written 2026-10-17 for a photographer editing a competition shot at high ISO, where the climber wants less smoothing than the wall behind them.
`research/detail.md` §"Noise reduction" holds the entry for the chain that ships today.
This file is about three things found while reading it and one thing added beside it.

**Status:** implemented the same day, see the decision rows named at the end.

---

## 0. What was found before anything was designed

### 0.1 Color-only denoising smoothed luminance too

`DevelopCapture.cpp:716-720` handled "Luminance 0, Color > 0" by setting the shader's single `strength` to the color value and `chromaBoost` to 1, under a comment saying luma would be left untouched.
The shader (`denoise_accum.slang`) computed `tauLuma = strength * sigma`, so luma was shrunk at the color strength.
A photographer who raised Color alone to kill chroma blotches was also softening texture, with nothing on screen saying so.

**Fix:** the shader takes `strengthLuma` and `strengthChroma` as two numbers and derives nothing.
`tauLuma = strengthLuma * sigma`, `tauChroma = strengthChroma * sigma`.
Every render with Luminance above zero is unchanged by the two strengths themselves: the old `chromaBoost = color / luma` made `tauChroma = color * sigma`, which is the new form exactly.

One more line was needed, found by the test rather than by reading.
The three per-channel garrotes shrink the chroma components by different amounts, so their sum no longer has zero luminance, and Color alone still moved the luminance residual by 4% through that leak.
The kept chroma is now projected back onto the zero-luminance plane, `keptChroma -= luminance2020(keptChroma)`, which is what "chroma" meant in the split to begin with.
⚠ That line changes every render with Color above zero by a small amount (the leaked luminance), so a photograph finished with Color on does not re-render bit for bit; the suites' invariants hold and `detail.md`'s measured table is for Luminance and Color together, where the leak was already part of what was measured.

### 0.2 The threshold did not know texture from noise

The shipped threshold is `tau_j = beta * sigma_j(x)`, where `sigma_j` follows the sensor's Poisson-Gaussian model through the scale norms.
It adapts to brightness and to scale, and to nothing else: a coefficient of a given size is shrunk the same way on a blank wall and on rock texture.
That is the behaviour the research chose and it is correct for noise; it is why the control feels like a trade between grain and detail on exactly the frames that have both.

### 0.3 A mask cannot reach the denoiser where it sits

The chain runs in linear camera RGB, before the lens correction and the color matrix (`detail.md`, deliberate).
The mask fold reads `profile:hue/sat`, which is after the matrix: kinds 5 and 6 read the picture's luminance and color, and the guided refinement reads `guide:prep`, which reads the same node.
So a denoise node that took the fold as an input would close a cycle through the matrix.
Moving the chain after the matrix would reverse two measured decisions (resampling before denoising, and the model holding only in camera RGB), and was not done.

---

## 1. Texture-adaptive threshold: BayesShrink's shape, under the existing control

**Source:** S. G. Chang, B. Yu and M. Vetterli, *Adaptive wavelet thresholding for image denoising and compression*, IEEE Transactions on Image Processing 9(9), 1532-1546, September 2000.
Widely cited; the subband-adaptive threshold it derives, `tau = sigma_n^2 / sigma_x`, is the one most wavelet denoisers use when they adapt to content.
`deep-research-2026-07-27.md` §2 named it as the alternative to a fixed `beta_j` when the chain was first designed.

The derivation: for a generalized-Gaussian signal prior, the soft threshold that minimises Bayes risk is close to `sigma_n^2 / sigma_x`, where `sigma_n` is the noise standard deviation in the subband and `sigma_x` the signal's, estimated as `sqrt(max(E[w^2] - sigma_n^2, 0))` over the coefficients.
Where the signal is weak the threshold runs away and the subband is cleared; where it is strong the threshold vanishes and the texture is kept.

### How it is applied here, and why not verbatim

Verbatim BayesShrink has no strength control and estimates `sigma_x` over a whole subband.
Orion's threshold already carries the photographer's `beta` and a per-pixel `sigma_j(x)` from the sensor model, and neither is given up.
The adaptive part becomes a **factor on the existing threshold**, computed per pixel from a local window:

```
E_j(p)     = mean of w_j^2 over a 5x5 window spaced 2^j apart, luma only
sigma_x(p) = sqrt(max(E_j(p) - sigma_j(p)^2, 0))
factor(p)  = sigma_j(p) / max(sigma_x(p), sigma_j(p))      in (0, 1]
tau_j(p)   = beta * sigma_j(p) * lerp(1, factor(p), detail)
```

`detail` is a new global control, 0..1, **default 0**.
At 0 the factor is never applied and every existing photograph renders bit for bit as before, which is what lets this ship under the invariant tests.
At 1 the threshold is `beta * sigma_j^2 / sigma_x` wherever texture exceeds the noise, BayesShrink's form with the photographer's `beta` as the constant, and the fixed threshold where it does not.
The factor never exceeds one: Detail can only protect texture, never smooth more than the shipped chain did.

The window is spaced `2^j` apart like the transform itself, so the estimate at scale `j` reads the same support the coefficient was formed over.
Chroma keeps the fixed threshold: chroma texture is scarce and chroma noise is the ugly kind, so the control that protects texture should not also protect blotches.

### What to measure

The existing GPU test (`tests_tone.cpp`, `testDenoiseGpu`) proves a flat area's error halves and an edge survives.
The addition proves two more things on one frame: a fine stripe pattern under noise keeps more of its amplitude at `detail 1` than at `detail 0`, and the flat half of the same frame still loses most of its noise at `detail 1`.
The gate for the color-only defect is a noisy colored frame: `strengthLuma 0, strengthChroma 2` must leave the luminance residual where it was.

---

## 2. Masked denoising: a second, short chain where the mask can reach it

### 2.1 The rule it has to satisfy

`research/masking.md` §2b: an adjustment can be local when the coverage can scale its **parameter**, and the tone bands show the accepted shape of an exception, a non-pointwise filter whose parameter is per pixel and whose neighbourhood is read "as it stood".
A wavelet shrink's parameter is its threshold, and the threshold is already a per-pixel quantity (`sigma_j(x)`).
Scaling `beta` by the coverage is therefore well defined per pixel:

```
beta(p) = sum over layers L of  layerDenoise[L] * coverage_L(p)
```

The neighbourhood each coefficient is formed over is the picture after the capture chain, read as it stands, which is the same approximation the tone bands state for the guided estimate.

### 2.2 Where it runs

A second instance of the same two kernels, **after `profile:hue/sat`**, in scene-linear Rec.2020, with one node in front of it:

```
profile:hue/sat
  -> denoise:field                      (R16F x2: beta_luma(p), beta_chroma(p) from the eight folds)
  -> local denoise:blur 0..3
  -> local denoise:shrink 3..0          (reads the field; tau_j(p) = field(p) * sigma_j(p))
  -> dehaze -> clarity -> fusion -> develop:linear ...
```

`guide:prep` and the mask components keep reading `profile:hue/sat`.
That is the whole reason there is no cycle: the fold reads the picture before this chain, and this chain reads the fold.
The price is stated: a layer's highlights and shadows, and a kind-5 or kind-6 row, read the neighbourhood before the local denoise ran, the same shape as the guided estimate's approximation in §2b.

Every node is disabled unless the frame has a measured noise model, a mask, and a layer with a denoise above zero.
Disabled, the dehaze chain resolves straight back to `profile:hue/sat` and the graph is what it was; the texture pool (decision #219) holds nothing for a chain that did not run.
When it runs, the cost is the capture chain's again, which is the honest number for "denoise twice".

### 2.3 The noise model, carried through the matrix

The model `var = a*x + b` holds per channel in linear camera RGB.
The working-space luminance is `Y = w^T M x` for the normalised camera-to-working matrix `M` and the Rec.2020 weights `w`.
With `k = M^T w` and the white balance multipliers `m_c` already folded in the way `applyDenoise` folds them:

```
var(Y) = sum_c k_c^2 * (a * m_c * x_c + b * m_c^2)
```

The capture chain approximates every `x_c` by the coarse scale's luminance; this chain does the same with the working-space luminance, which equals the camera one for a neutral pixel because `M` has normalised rows.
So `aLum' = a * sum k_c^2 m_c` and `bLum' = b * sum k_c^2 m_c^2`, the capture chain's two sums with `k` in place of `w`.
The hue/sat map and sharpening sit between the two chains and reshape the noise slightly; that is accepted and recorded, as the Rec.2020 weights on camera RGB already are in `detail.md`.

### 2.4 Semantics the photographer sees

Each layer gets **Luminance** and **Color** denoise, 0..4, the global sliders' units, applied through that layer's coverage on top of whatever the global chain did.
A layer cannot un-denoise: the global chain has already run.
To denoise the background and leave the subject, set the global to zero and put the denoise on an inverted subject layer, or on a layer with the subject subtracted.
The agent surface says the same thing in `describe_edits`, and `sync_edits` carries the layers with the mask group.

---

## 3. A number for an agent to read

`get_stats` with `region` gains `noise`: the standard deviation of the patch's luminance against its own 3x3 box mean, in the same 0..1 units as `luma`.
It is a measure of fine grain and nothing else, flat on a smooth wall and high on noise or on fine texture, so it is read **before and after on the same patch**, never across patches.
`shading` measures the modelling a face keeps; `noise` measures the grain a wall loses.
Together they are the two numbers a denoise decision turns on, and before this the second did not exist.

---

## 4. Considered and not built

| Method | Source | Why not now |
|---|---|---|
| Generalised Anscombe transform with exact unbiased inverse | M. Mäkitalo and A. Foi, *Optimal inversion of the generalized Anscombe transformation for Poisson-Gaussian noise*, IEEE TIP 22(1), 91-103, 2013 | The per-pixel `sigma_j(x)` already does the job the stabilisation does, with the model fitted per frame. A gain would be at the darkest tones only and was not measured |
| Non-local means | A. Buades, B. Coll and J.-M. Morel, *A non-local algorithm for image denoising*, CVPR 2005 | A 21x21 search of 7x7 patches is ~21k reads per pixel. At 24 MP that is a background pass, not a slider, and `denoise-learned.md` §6 already sets that bar |
| BM3D | Dabov et al., IEEE TIP 16(8), 2007 | Rejected in `detail.md`: block matching is GPU-hostile |
| Learned (NAFNet-class) | `denoise-learned.md` | Costed to a decision point, by design nothing built; the domain argument there still stands |

⚠ **Freedom to operate was not assessed** for any of the above, including §1, which `CLAUDE.md` says to state rather than assume.
Chang, Yu and Vetterli is an academic result from 2000 and the form has been in open implementations for two decades, but that is provenance, not clearance.

---

## History

| Date | What |
|---|---|
| 2026-10-17 | Written. §0.1 defect, §1 texture-adaptive threshold, §2 masked chain, §3 the `noise` reading |

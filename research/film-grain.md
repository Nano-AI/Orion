# Film grain

**Status:** implemented 2026-07-31. `engine/shaders/grain.slang`, node `grain`.

## What grain is, and what it is not

Photographic grain is the developed silver: a random tiling of opaque particles
in the emulsion. Two properties follow, and both matter more than the amplitude.

- **It has a size.** Grain is spatially correlated — clumps, not per-pixel noise.
  Uncorrelated noise reads as a digital sensor, not as film, however it is
  weighted. This is the property a naive implementation misses.
- **It is strongest in the midtones.** A grain field with mean coverage `u` has
  Bernoulli variance `u(1 − u)`: zero where nothing is developed, zero where
  everything is, peaked at half. So the shadows and the shoulder are quiet by
  construction rather than by taste.

## The published model, and the one that is affordable

> A. Newson, J. Delon, B. Galerne, **"A Stochastic Film Grain Model for
> Resolution-Independent Rendering"**, *Computer Graphics Forum* 36(8),
> pp. 684–699, 2017. [doi:10.1111/cgf.13159](https://doi.org/10.1111/cgf.13159)
> Companion: *"Realistic Film Grain Rendering"*, Image Processing On Line, 2017.

Newson et al. model the emulsion as a **Boolean model** — discs of random radius
at the points of an inhomogeneous Poisson process, with local intensity set by
the image. It is the right physics and it is where the `u(1 − u)` variance law
above comes from.

⚠ **Its exact renderer is Monte Carlo over the disc process, per pixel**, and
that is not affordable here: the paper's own timings are orders of magnitude
outside a 16 ms budget at 24 Mpx, and a faithful implementation is several
hundred lines against this project's 50–150 line ceiling for a node. Both hard
constraints, broken at once.

The way out is the one the AV1 codec took for exactly this cost:

> A. Norkin, N. Birkbeck, **"Film Grain Synthesis for AV1 Video Codec"**,
> *Data Compression Conference (DCC)*, pp. 229–238, 2018.
> [doi:10.1109/DCC.2018.00031](https://doi.org/10.1109/DCC.2018.00031)

Synthesise a **correlated grain plate once**, then apply it per pixel with an
intensity-dependent scale. Orion takes AV1's architecture and Newson's
statistics: the plate supplies the spatial correlation, `√(Y(1−Y))` supplies the
variance law, and the per-pixel cost is one filtered texture fetch.

**Monochrome, added equally to R, G and B.** Luminance grain is what film looks
like; per-channel grain is what a noisy sensor looks like.

## Where it sits, and why not in scene-linear

Grain is applied **after the display transform**, in its own node between
`develop_display` and the geometry. Real grain lives in the negative's *density*
— a bounded, log-like quantity, far closer to a post-sigmoid display value than
to unbounded scene-linear light.

⚠ Adding it before the display transform breaks three ways, and all three are
the silent kind:

- The amplitude would have to be defined against unbounded EV, so one Amount
  setting would mean a different visible amount on every photograph and would
  change every time the exposure slider moved.
- `develop_display` opens with `max(c, 0)`, which **half-clamps** zero-mean
  noise near black and lifts the mean — grain that brightens the shadows.
- AgX takes `log2` of the input, which turns small symmetric linear noise into
  large asymmetric shadow noise.

The sigmoid does compress highlight grain differently from midtone grain. That
is not worth chasing on the wrong side of the transform; the rolloff belongs in
the weighting curve below, where it is explicit and bounded.

⚠ **The 8-bit dither moved with it.** `develop_display` used to add the Bayer
pattern as its last act. Grain has to be added to unquantised values, so the
dither block now lives at the end of the grain node — which is the last
pointwise writer before geometry. At Amount 0 the grain node applies only the
dither, so its output is bit-identical to what `develop_display` used to write.

## Coordinates: the frame, not the output

Because the node runs before geometry, the grain is keyed to the **frame** — the
uncropped, unturned sensor grid — for free, exactly as dust spots are.

A crop therefore *enlarges* the grain, which is what enlarging more of a
negative does. A straighten rotates it. It does not swim when the preview pans.

⚠ **Two exports at different sizes will show different grain per output pixel,
and that is correct.** A 4×6 and a 16×24 print from one negative differ the same
way. The promise this project makes is narrower and is kept: the same edit at
the same output size renders the same bytes.

## ⚠ Determinism, which is where this gets dangerous

The same edit must produce the same picture in the preview graph *and* the full
render — and those are **different pixel grids**, 1/16 apart.

**A hash of the pixel coordinate fails twice.** The grids differ, so preview and
export would be different realisations of the noise — a different picture, not a
coarser one. And the preview would show full per-pixel variance where the
settled render averages 16×16 of them, so the preview would read an order of
magnitude grainier than the thing it is previewing.

The plate fixes both. The host passes `gridStep` — frame pixels per node pixel,
1 at full resolution and 16 in the preview — and the shader samples the plate at
frame coordinates with an explicit level of detail, `log2(gridStep / size)`. Both
graphs then sample the **same field**, and the preview sees exactly the averaged
view a downscale would produce.

Four things that would silently break export-matches-preview, all avoided:

| Hazard | Why | What is done instead |
|---|---|---|
| A hardware sampler | Filtering precision is not specified across GPU families | Hand-rolled trilinear in plain ALU ops |
| `std::normal_distribution` | Its algorithm is implementation-defined; libc++ and libstdc++ differ | PCG32 + Box–Muller, written out |
| `generateMipmaps` | The filter is unspecified | CPU box filter, uploaded |
| Grain after the dither | Quantised input | Dither moved into this node, after grain |

> M. E. O'Neill, **"PCG: A Family of Simple Fast Space-Efficient Statistically
> Good Algorithms for Random Number Generation"**, Harvey Mudd College technical
> report HMC-CS-2014-0905, 2014.
>
> G. E. P. Box, M. E. Muller, **"A Note on the Generation of Random Normal
> Deviates"**, *Annals of Mathematical Statistics* 29(2), pp. 610–611, 1958.
> [doi:10.1214/aoms/1177706645](https://doi.org/10.1214/aoms/1177706645)

The seed is a compiled-in constant. Grain is unstructured, so nobody can tell
that two photographs share a field, and a per-photo seed would be state that can
disagree with itself.

## The weighting

    σ(Y) = amount · √(Y · (1 − Y))

on the **display** luma `Y` in [0, 1], Rec.709 weights. Straight from the
Boolean model's Bernoulli variance. It also disposes of the clamping question:
where `saturate` could truncate the noise — at 0 and at 1 — σ is already zero,
so there is no mean shift to hide.

## The controls

**Amount** and **Size**. Nothing else.

Roughness, which Lightroom offers third, is the plate's spectrum, and the fixed
band-limit already encodes it. A chroma-grain slider would be a control whose
correct value is always zero.

⚠ **Grain is judged at 100%.** At fit zoom the mip averaging is truthful and
shows little, which is every editor's behaviour and the honest one — a preview
that showed full-variance grain at fit would be lying about the print.

## What is tested

`testGrainGpu` in `apps/tests/tests_effects.cpp`, on a constant mid-grey card:

1. the standard deviation of the output matches the predicted `σ√(Y(1−Y))`
   within a tolerance — grain exists, at a calibrated amplitude;
2. the **mean shift against grain-off is under 1e-3** — this is the check that
   catches every clamping and asymmetry bug, which is the purple cast's class;
3. a box-downsample of the full-resolution render matches the 1/16 render —
   preview and export are the same field.

⚠ Check 3 alone passes with grain accidentally switched off entirely; check 1 is
what pins it on. Neither is worth much without the other.

**The bench probe measures mean absolute difference, not mean luma.** Grain is
zero-mean, so a luma probe reads zero on a working control — the same trap the
colour-cast probe records in `DECISIONS.md` #47.

# Black level - noise below black, and why a shadow must keep it

Written 2026-10-28 after the developer reported exposure looking "different when it previews and then when I let go", and chose to fix both halves: the preview must match the release, and the release must stop lifting noise into grey-purple shadows.
The night frame `_PIC8148` (Sony ARW, black 512, 14-bit) is the case; the daylight `_PIC8095` and `_PIC8220` are the controls.

**Status:** implemented in the session that wrote it. Proposed decision rows are in that session's report; `DECISIONS.md` carries them once accepted.

---

## 0. What was wrong

Read noise is zero-mean around the black level, so a deep shadow puts a large share of its samples *below* black.
In `_PIC8148`'s sky (x .30-.70, y .02-.15) the red channel sits 34 counts above black with a deviation of 81, and 31.8% of its samples are below black.
`linearize` subtracted black and then clamped at zero, so every one of those samples became zero and the shadow's mean became the mean of a rectified Gaussian:

    E[max(X, 0)] = mu * Phi(mu / sigma) + sigma * phi(mu / sigma)   >   mu

At red's mu/sigma = 0.42 that is x1.36; green and blue, with more light, rose less.
The lift is per channel and largest where there is least light, so a night sky's deep blue turned grey-purple, and nothing later could take it back out: the samples that would have pulled the mean down were gone.

Three more stages clamped at zero on the way to the camera profile (`rcd_green`'s output, `rcd_rb`'s output, and `lens`/`sharpen` when they run), each rectifying again whatever the stage before had left signed.

The preview hid this. It decimates the mosaic by four for speed and *averaged* each block, which shrinks the noise fourfold, so almost nothing in the preview fell below black: the preview showed the honest dark blue sky and the release settled on the lifted grey-purple one. That is the "different when I let go".

---

## 1. Sources

> **EMVA Standard 1288, Release 4.0 Linear**, European Machine Vision Association, 16 June 2021. §2.7 and §6.1 (measurement conditions).

§2.7: "The mean dark gray value µy.dark must be higher than zero so that no significant underflow occurs by temporal noise and the dark signal nonuniformity."
The saturation exposure is defined "in such a way that the clipping of the distribution of the digital signal at the saturation exposure is minor so that the computation of both the mean and variance are not biased."
The measurement conditions set the camera's offset "so that less than about 0.5% of the pixels underflow".
The standard's whole reason for an offset above zero is that clipping the noise distribution biases its mean. Camera makers follow it: that offset is the black level, 512 on this Sony. Clamping at black in software throws the offset's purpose away.

> A. Foi, **Clipped noisy images: Heteroskedastic modeling and practical denoising**, *Signal Processing* 89(12), 2609-2629, December 2009.

Models raw data as clipped heteroskedastic noise, derives the expectation and variance of the clipped observation (the expression in §0 is its zero-clip case), shows the clipped mean deviates from the true intensity wherever the signal is within a few deviations of the clip, and gives a practical procedure to estimate the true intensity back from clipped data.
Orion uses the model to explain and quantify the lift. **It does not implement the procedure**: the cheapest correct answer is not to clip in the first place (§2), and Foi's debiasing would only be needed where the clip is unavoidable (it is at white, §5).

> T. Plötz and S. Roth, **Benchmarking Denoising Algorithms with Real Photographs**, CVPR 2017, supplementary material.

Corroborates the same point for raw benchmark data: the mean of clipped noisy observations is not the clean intensity.

**Practice.** Not clipping below black before the demosaic is what serious raw pipelines do:
- LibRaw forum, thread on purple shadows, 25 May 2019 (MichelRouzic): "not clipping those values is the best way to avoid having purple haze". Weak source (a forum post), cited as corroboration only.
- darktable's raw black/white point module keeps negative values after subtraction (reported by its users on pixls.us). Weak source; darktable is GPL and its code was **not** read - this is about observed behaviour, nothing was implemented from it.

> Adobe, **DNG SDK**, `dng_reference.cpp`, `RefBaselineABCtoRGB` (as mirrored in AOSP `platform/external/dng_sdk`, commit 80a267e).

The DNG reference renderer pins the camera-to-ProPhoto result to [0, 1] (`Pin_real32 (0.0f, r, 1.0f)`) before its hue/sat map. This is the clamp Orion's `huesat.slang` already followed, and §3 is why it stays.

---

## 2. What is built

The mosaic keeps its sign from black subtraction to the camera profile.

| Stage | Was | Now | Why it is safe |
|---|---|---|---|
| `linearize` | `clamp(v, 0, whiteClip)` | `min(v, whiteClip)` | The ×1.36. Clipped above only: the white clip is a real sensor ceiling and the highlight nodes need it (§5) |
| `rcd_green` | output `clamp(.., 0, 1)` | `min(.., 1)` | Its ratio term reads the low-pass at `max(lpf, 0)` - see below |
| `rcd_rb` | `max(r/g/b, 0)` | unclamped | Colour differences; linear in signed input |
| `lens` | `max(c, 0)` | unclamped | A gain per pixel; sign-preserving. Off unless a profile matches |
| `sharpen` | `max(.., 0)` | unclamped | An unsharp mask; mean-preserving. Off at amount 0 |

**RCD's ratio guard is Orion's own.** RCD (`demosaic.md`) corrects each directional green estimate by a ratio of low-passes, `1 + (lc - ln) / (eps + lc + ln)`.
On non-negative data that factor is bounded to [0, 2]; on a signed mosaic around zero its denominator can vanish and the factor explodes.
The low-passes inside the ratio are read at `max(lpf, 0)`, which keeps RCD's own [0, 2] bound and makes the factor exactly 1 where both sides are dark; the neighbour the factor scales keeps its sign.
RCD's description assumes non-negative input, so nothing published says what to do here: `UNSOURCED.md` §21.

**Every node between the demosaic and the profile was checked for a rectifier.**
`highlights` and the `hl:` chain act only on channels at the white clip.
`denoise:nlm` reads its noise model at `max(mean, 0)` and does not rectify its output.
`denoise:chroma down/up`, `spots`, `cameraToWorking`: no clamp.
No NaN, Inf or |v| > 64 on any of the three frames at +1 or +4 EV.

**What the change does at each stage**, `_PIC8148`'s sky at +1 EV, app defaults (contrast 1.45). The reference column is the same frame's mosaic averaged 4x4 first, which shrinks the noise so far that almost nothing clips - the nearest thing to the clean answer the frame holds.

| Stage (R G B) | Before | After | Reference |
|---|---|---|---|
| linearize | .00569 .00624 .00820 | .00418 .00590 .00746 (19.5% below zero) | .00418 .00590 .00746 |
| rcd | .00588 .00600 .00828 | .00421 .00573 .00748 | .00419 .00582 .00745 |
| camera -> working | .00562 .00464 .00855 | .00399 .00473 .00762 | .00398 .00489 .00757 |
| hue/sat | .00552 .00636 .00851 | .00489 .00660 .00810 | .00393 .00540 .00752 |
| develop:linear | .02575 .02923 .03923 | .02326 .03030 .03743 | .01815 .02480 .03454 |
| display, 8-bit | 18.96 19.55 29.89 | 17.14 20.91 28.59 | 7.40 14.80 24.25 |

Up to the camera profile the release now equals the reference within RCD's own 1.5% on green.
The hue/sat row is §3. The display row is §4.

---

## 3. The hue/sat clamp stays

`huesat.slang` clamps ProPhoto at zero before its HSV lookup, as the DNG SDK does (§1).
With signed noise arriving, 23% of the sky's working-space values reach it negative, and the clamp lifts the sky's scene-linear mean again: red and green by about a fifth against the reference.

Passing the negative part through around the table (`out = map(max(pro, 0)) + min(pro, 0)`) was built and measured:

| `_PIC8148` sky, +1 EV, 8-bit | Clamp kept | Passed through |
|---|---|---|
| No noise reduction | 17.14 20.91 28.59 | 17.64 20.39 28.76 |
| Noise reduction 4 / 4 | 7.1 14.3 25.6 | 7.1 14.2 25.6 |

Under a level either way, because the display clamps the same pixels at zero anyway.
It also broke three tests (the harmonic highlight fill's wiring, the masked denoise's grain, Clarity's clean-frame guard) whose fixtures carry an sRGB matrix as `cam_xyz` and so render their grey as an imaginary colour well outside ProPhoto: the pass-through changed how every out-of-gamut colour renders, for no visible gain on the noise.
The spec's clamp stays; `develop:linear`'s and `grade`'s own clamps at zero, after it, stay too (they changed nothing at the display either way: the same table, rows "hue/sat" onward).

---

## 4. The display: noise through a curve, not a bias

After the change the sky still renders at 17.1 / 20.9 / 28.6 against the reference's 7.4 / 14.8 / 24.3.
That gap is not a bias to correct. It is what a noisy shadow looks like.

- **The signal is unbiased up to the profile** (§2 table), and in the synthetic test (`testPreviewShadowsMatchGpu`, a field 20 counts above per-channel blacks with a deviation of 80) the noisy frame's camera RGB is the clean frame's within 1%. The same test renders the clean field at 14.8 / 9.4 / 12.1 and the noisy one at 23.3 / 16.9 / 21.8: eight levels of lift from the deviation alone, with the mean exactly right.
- **The display is non-linear and clamps at zero** (`develop_display.slang`: `max(c, 0)`, the gamut matrix, a log2 shaper and a sigmoid). For a non-linear `f`, `E[f(X)] != f(E[X])` (Jensen): the release renders each noisy pixel, and the picture's mean is the mean of the rendered pixels.
- **Noise reduction removes it, and that is the test of which it is.** At noise reduction 4 / 4 the release lands on 7.1 / 14.3 / 25.6, on the reference. Before this change the same setting rendered 15.8 / 13.6 / 30.2, still purple: a lift baked into the mean survives denoising, a lift that is the noise's own rendering does not.

So the display is left as it is. A Foi-style correction there would be a noise-dependent tone curve, which is to say a denoiser that cannot see structure; the photographer already has the real one. And debiasing an offset measured between clipped and unclipped versions of an image is what Apple's US9686448B2 claims (§6).

---

## 5. The clip that stays, and the clamps out of scope

- **White.** `linearize` still clips at `whiteClip`. A blown photosite's value is not a noisy measurement of anything, and `highlights`/`hl:` reconstruct those pixels from neighbours precisely because they are clipped. Foi 2009 is the reference if that clip's bias ever needs undoing.
- **`linear_source.slang`** (linear and merged DNGs) still clamps at zero after its own black subtraction. A merge averages its brackets, so its shadows carry less noise below black; no frame here shows the lift, so it is left until one does, with the same one-line change.
- **`dehaze_recover.slang`** clamps its recovered radiance at zero, with its own stated reason (a mis-estimated airlight).
- **`estimateNoise`** reads raw counts, not `linearize`'s output, and is unaffected; its dark window excludes samples below black, which truncates the darkest bin's spread. Flagged, not changed.

---

## 6. Patents

CLAUDE.md: a citation is not clearance. Searched 2026-10-28 on Google Patents and by the authors' names, for the method built here (keeping signed values after black subtraction) and for the one rejected (debiasing a clipped mean).
Search terms: "image sensor black level subtraction negative values retained not clipped noise bias dark region color cast"; "raw image processing negative pixel values after black level subtraction preserved floating point demosaic"; "Foi declipping clipped noise variance stabilization raw image"; inventor "Alessandro Foi"; assignee Noiseless Imaging.
Status and expiry are Google Patents' own estimates, not a legal reading.

| Patent | Holder | Priority / granted | Status (Google) | What it claims | Bearing on Orion |
|---|---|---|---|---|---|
| US9686448B2, *Adaptive black-level restoration* | Apple | 2015-06-22 / 2017-06-20 | Active, est. expiry 2035-08-26 | Claim 1: an image with negative values; subtract a pedestal; clip; **determine an offset from the pedestal-subtracted and clipped images; adjust the original by it**; display | Covers the *debiasing* path, which Orion does not take (§4). Keeping negatives is not its claim |
| US8102444B2, *Post noise reduction black level correction* | Zoran, now Qualcomm | 2008-09-11 / 2012-01-24 | Active, adjusted expiry 2030-08-06 | Claim 1: the data **keeps the black level through processing, noise reduction runs, black-level correction runs after it** | The closest. Orion subtracts black *first*, as before; only the truncation is removed, and the order is unchanged. Whether that is equivalent is a question for counsel, not measurement: **flagged to the developer** |
| US9287316B2, *Mitigating image sensor pixel value clipping* | onsemi (Aptina) | 2013-04-19 / 2016-03-15 | Active, adjusted expiry 2034-01-02 | Adds per-colour black pedestals to avoid clipping | Orion adds no pedestal |
| US8953882B2, *Determining noise statistics* | Apple | 2012-05-31 / 2015-02-10 | Active, est. expiry 2033-04-02 | Noise statistics from image data; signed data appears in the description | Claims not reviewed (scanned PDF). Orion's noise estimate is unchanged |
| US12477250B2 | - | 2025 | Active | Remaps negative values into an unsigned range with a factor | Orion keeps floats; not this |
| Foi's declipping | - | - | No patent found | Released as software (ClipPoisGaus) | Not implemented anyway |

---

## 7. The preview

With the release fixed, how the preview is decimated decides whether it looks like it.
An averaged block has a quarter of the release's noise, and §4 says the rendered shadow depends on the noise as well as the mean, so an averaged preview is darker and bluer in the shadows than the frame it settles on - now for an honest reason rather than a clipped one.
A point-sampled block is one real sensor sample: the preview's per-pixel noise *is* the release's, and every pointwise stage downstream agrees in expectation.

Mean |16x16 block luma difference| between the preview and the release, 8-bit, app defaults, with this change's release (the averaged mosaic is not clamped, since the release no longer is):

| EV | `_PIC8148` averaged | point | `_PIC8095` averaged | point | `_PIC8220` averaged | point |
|---|---|---|---|---|---|---|
| -4 | 0.12 | 0.01 | 0.08 | 0.08 | 0.04 | 0.02 |
| -2 | 0.29 | 0.03 | 0.33 | 0.21 | 0.18 | 0.06 |
| -1 | 1.10 | 0.07 | 0.59 | 0.27 | 0.33 | 0.09 |
| 0 | 3.28 | 0.19 | 0.91 | 0.30 | 0.61 | 0.15 |
| +1 | 5.63 | 0.40 | 1.18 | 0.30 | 0.98 | 0.22 |
| +2 | 5.68 | 0.64 | 1.31 | 0.27 | 1.22 | 0.31 |
| +3 | 5.03 | 0.79 | 1.23 | 0.22 | 1.80 | 0.40 |
| +4 | 6.47 | 0.82 | 1.01 | 0.16 | 2.61 | 0.45 |

Averaging fails the one-level bar on the night frame from -1 EV up, and on the daylight frames from +1 EV (`_PIC8095`) and +2 (`_PIC8220`): the curve does to fine texture what it does to noise.
Point sampling is under a level everywhere (worst single block 3.3 levels, against 27.9 averaged). Its price is moire on fine periodic detail and about twice the per-pixel deviation from the release while a slider moves (`_PIC8148` +1 EV RMS 20.8 against 10.3 levels; `_PIC8095` 0 EV 10.2 against 7.5): the preview is noisier, as the release is.

---

## 8. What the exports change

`--batch-export` at defaults and `--agent proxy` at `_PIC8148` +1 EV, before and after. Blocks are 64x64; "shadows" are the blocks under 30 before.

| Export | Mean luma | Block change mean / max | Shadow R/G | Sky strip RGB |
|---|---|---|---|---|
| `_PIC8095` (daylight) | 152.50 -> 152.50 | 0.00 / 0.14 | 0.92 -> 0.92 | - |
| `_PIC8220` | 37.92 -> 37.93 | 0.01 / 0.15 | 0.94 -> 0.94 | - |
| `_PIC8148` (night) | 11.79 -> 11.98 | 0.19 / 0.41 | 0.88 -> 0.80 | 8.7 7.8 13.0 -> 8.1 8.4 12.6 |
| `_PIC8148` +1 EV | 28.01 -> 28.39 | 0.38 / 1.02 | 0.96 -> 0.84 | 19.8 19.6 30.4 -> 18.3 21.0 29.2 |
| `_PIC8148` +1 EV, noise reduction 4 / 4 | 23.74 -> 22.30 | 1.44 / 2.47 | 0.99 -> 0.49 | 16.0 13.5 30.4 -> 7.4 14.1 25.9 |

The daylight frames do not move. The night frame loses its red-over-green tint at defaults and keeps its level, because the level is the noise's rendering (§4). With noise reduction it goes from purple to the deep blue the averaged reference shows; before, denoising could not get there, because the lift was already in the mean.

# Black and white: the per-band gray mix

**Where:** `engine/shaders/ops/bw_ops.slang` (`applyBlackWhite`), called at the end of `develop_linear.slang`; Treatment (Color or Black & white) and the eight sliders in the Color tab (`ColorPanel.cs`), `blackWhite` and `bwMix` in the sidecar.

## The problem, and the baseline

Converting colour to gray is a projection: three channels become one, so colours of equal luminance but different hue land on the same gray.
Grundland and Dodgson state the problem and survey the standard answers in "Decolorize: Fast, contrast enhancing, color to grayscale conversion", *Pattern Recognition* 40(11), pp. 2891-2896, 2007.
Their baseline, and the one every photographic converter starts from, is the luminance of the working space: a weighted sum of the linear channels whose weights are the primaries' contribution to Y.
Orion's working space is Rec.2020, whose luminance weights are fixed by ITU-R Recommendation BT.2020 (2012): Y = 0.2627 R + 0.6780 G + 0.0593 B.
`luminance2020` in `ops/tone_ops.slang` is exactly that, and every slider at zero renders exactly it.

The luminance projection is what a panchromatic film or an unfiltered monochrome sensor would record.
Photographers have always changed it per hue with coloured filters over the lens: a red filter darkens a blue sky, a green one lightens foliage.
A per-hue gain on the luminance is the digital form of that filter, and it is what Lightroom's B&W mix exposes as eight sliders.
Grundland and Dodgson's own method (Decolorize) chooses the mapping automatically to preserve contrast; Orion does not implement it, because the mix here is a creative control the photographer sets, not an automatic one.

## What Orion computes

For a scene-linear pixel c with luminance Y, peak channel M and lowest channel m:

    chroma = (M - m) / M                        0 on the gray axis, 1 for a pure colour
    h      = the HSL hue of c                   the mixer's own, ops/hsl_ops.slang
    w_b(h) = (1 - d(h, centre_b) / 60)^2        the mixer's band membership, normalized over the eight
    gray   = Y * 2^(kBwStops * chroma * sum_b w_b(h) * s_b)      R = G = B = gray

with s_b the band's slider in -1..1 and kBwStops = 2.

- **Sourced:** the luminance baseline (BT.2020's weights, Grundland and Dodgson's framing of the problem).
- **Not sourced, and the same as the colour mixer's:** the eight band centres and the membership w_b(h). They are the colour mixer's, registered in [UNSOURCED.md §4](UNSOURCED.md#4-color-mixer-band-weighting), and reused unchanged (`hueBandWeight`) so a gray band and a mixer band are the same hues.
- **Orion's own:** the slider-to-gain mapping, registered in [UNSOURCED.md §33](UNSOURCED.md): the gain is exponential in stops, scaled by chroma, two stops at full travel.

## Why this mapping

- **Exponential, not 1 + s.** A linear gain 1 + sum w s reaches zero at -1, so a saturated colour of one band would go to flat black and lose its detail; it also needs a clamp. 2^x is never negative, needs no clamp and is symmetric in stops, the unit exposure already uses.
- **Scaled by chroma.** A neutral has no hue, and its HSL hue in noise is arbitrary. Weighting by (max - min) / max keeps a gray and a near-gray at their luminance exactly and grows the effect continuously with saturation, where a cut-off at some small saturation would print a step in a gradient. The ratio is scale-invariant, so a highlight above 1 is weighed like the same colour below it.
- **After every colour control, before the grade.** Applied last in `developLinear`, after the global and local colour adjustments, so the gray reflects what the photograph's colours had become and the output of the kernel is exactly neutral; `color_grade` and the display transform follow, so a grade can still split-tone the gray.

## The invariants tested

`testBlackWhiteMixGpu` (`apps/tests/tests_bw.cpp`) renders a chart of the eight band centres at chroma 1, 0.5, 0 and a 4x highlight through `developLinear` on the GPU:

- every slider at 0 gives exactly Rec.2020 Y, R = G = B;
- +1 on Green scales a pure green's gray by 2^2 = 4, a half-saturated green by 2, and a gray by 1 (green's neighbours are 60 degrees away, where membership is zero);
- +1 on Red weighs every band centre by its normalized membership (red itself by 1 / (1 + 1/4 + 1/9));
- every slider at -1 scales any fully saturated colour by 2^-2, whatever its band, because the normalized weights are a partition of unity.

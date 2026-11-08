# White balance from the picture: the selector, Auto and the presets

**Where:** `neutralFor` and `kWhiteBalancePresets` (`engine/src/pipe/WhiteBalance.h`, `.cpp`), `DevelopPipeline::neutralWhiteBalance` (`DevelopNeutral.cpp`), `Engine::whiteBalanceAt` (`EngineWhiteBalance.cpp`), `orion_engine_white_balance_at` and `orion_white_balance_preset`; the selector (W) and the menu in the Light tab (`WhiteBalance.cs`).

The temperature and tint model itself, temperature and tint to a white point and to camera multipliers, is [color-pipeline.md](color-pipeline.md#white-balance-temperature-and-tint-done-adobes-way--2026-07-29): Robertson's isotemperature-line method (A. R. Robertson, "Computation of Correlated Color Temperature and Distribution Temperature", *JOSA* 58(11), 1968), on the Planckian locus table of Wyszecki and Stiles, *Color Science*, 2nd ed., 1982, Table 1(3.11), with the tint as the DNG specification and Adobe's DNG SDK define it (`dng_temperature`, a displacement along the isotemperature line in CIE 1960 UCS, ±150 units).
Everything here is that model run backwards.

## The selector: a click on a neutral

A neutral surface is one whose camera response, after the white balance multipliers, has R = G = B.
The render applies per-channel gains g = asShot ⊙ M(T, tint) / M(asShot estimate) (`DevelopPipeline::applyCapture`), so for a patch whose white-balanced camera RGB is p under gains g, the gains that make it neutral are g'_c = g_c · p_G / p_c.
The temperature and tint whose gains are g' are found by the same two-dimensional search over the forward model that turns a camera's as-shot multipliers into a temperature and tint (`estimateFrom`).
That is the correlated colour temperature problem Robertson's method answers; Hernández-Andrés, Lee and Romero, "Calculating correlated color temperatures across the entire gamut of daylight and skylight chromaticities", *Applied Optics* 38(27), 1999, give a closed-form alternative.
Orion searches its own forward model instead of using either closed form, because the answer must also carry the tint, and must agree with the model the render uses to the last digit, or a pick would not render neutral.

The patch is read from the last render's reference image, the scene-linear working RGB after the camera profile, and taken back to camera RGB through the inverse of the camera matrix, whose rows sum to one so a camera neutral is a working neutral.
The profile's hue/saturation table follows the matrix; it leaves the neutral axis alone but moves a near-neutral a little, so the engine solves, renders the preview with the answer and solves again on that, up to four times, until the answer moves by less than 1 K and 0.001 tint.
A pixel within 5 % of the clip level, or with a channel at or below zero, says nothing about the light and is left out; a patch where more than half is left out is refused.

- **Sourced:** the white point model (above); the neutral condition is the definition of white balance.
- **Orion's own** ([UNSOURCED.md §34](UNSOURCED.md)): the patch size, the clip margin, the pass count and the convergence thresholds.

## Auto: grey world

Auto is the grey world assumption: the average reflectance of a scene is achromatic, so the illuminant is the mean of the image.
G. Buchsbaum, "A spatial processor model for object colour perception", *Journal of the Franklin Institute* 310(1), pp. 1-26, 1980.
Orion takes the mean camera RGB of every unclipped, non-black pixel of the preview graph's reference image and makes it neutral exactly as the selector makes a patch neutral, with the same passes.
Grey world is the simplest of the published estimators and fails on a scene dominated by one colour (a forest, a sunset); a better estimator is a follow-up, not a change of this mechanism.

## The presets

Lightroom's list for a raw file: As Shot, Auto, Daylight 5500 K, Cloudy 6500 K, Shade 7500 K, Tungsten 2850 K, Fluorescent 3800 K, Flash 5500 K.
The temperatures are Lightroom's; there is no published source for that list.
The tints are sourced where a standard illuminant defines one:

| Preset | Temperature | Tint (Adobe units) | Source |
|---|---|---|---|
| Daylight, Cloudy, Shade | 5500, 6500, 7500 K | +10 | The CIE daylight locus (CIE 15:2004 *Colorimetry*, 3rd ed., the x_D, y_D equations) lies above the Planckian: at 5500, 6500 and 7500 K it is +10.2, +10.0 and +9.8 units (computed through `whitePointXy`), the same +10 Lightroom shows |
| Tungsten | 2850 K | 0 | CIE illuminant A, a Planckian radiator at 2856 K, xy (0.44757, 0.40745) (CIE 15:2004) |
| Fluorescent | 3800 K | 0 | The CIE F-series lamps sit within a few units of the Planckian (F2 +6, F3 +2, F11 +1, F12 0, computed as above from the CIE 15:2004 F-series chromaticities); Lightroom's +21 matches none of them and is not reproduced |
| Flash | 5500 K | 0 | No standard illuminant; Lightroom's value |

## The invariants tested

`testWhiteBalanceNeutral` (`apps/tests/tests_white_balance.cpp`): a gray under four lights, picked under four balances (the light's own among them), renders neutral to 1e-3 with the balance solved for, through a camera whose own estimate is off the locus; Daylight, Cloudy and Shade lie within 1e-4 in CIE 1960 uv of the CIE daylight locus; Tungsten within 5e-4 of illuminant A.
`testWhiteBalanceAtGpu`: through the facade, a tungsten-cast gray block on a coloured frame renders neutral after one call, and a blown block is refused.

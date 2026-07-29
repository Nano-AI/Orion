# Camera profiles — what a 3×3 matrix cannot do

**Where:** `pipe/DevelopPipeline.cpp` (`kBaselineExposureEv`, the colour matrix),
`shaders/color_matrix.slang`, `pipe/HueSatMap.h`, `shaders/huesat.slang`.
Re-measure the fit with `tools/huefit.py --check`.

## Source

**Adobe, *Digital Negative (DNG) Specification*, version 1.6.0.0, December
2021** — chapter 6 and the profile tags. Published, dated, and the format every
raw converter on the market reads.

<https://paulbourke.net/dataformats/dng/dng_spec_1_6_0_0.pdf>

Supporting:

- Sandy McGuffog, *dcpTool documentation* — the DCP processing model and hue
  twists. <https://dcptool.sourceforge.net/DCP%20FIles.html> ·
  <https://dcptool.sourceforge.net/Hue%20Twists.html>
- Anders Torger, *Making a camera profile with DCamProf* — why a matrix fails on
  saturated blues, the matte-chart limitation, and deliberate deep-blue
  rendering. <https://torger.se/anders/photography/camera-profiling.html>
- Glenn S. Smith, *Human color vision and the unsaturated blue color of the
  daytime sky*, Am. J. Phys. **73**, 590 (2005).
  <https://pubs.aip.org/aapt/ajp/article-abstract/73/7/590/1056162>

⚠️ **Licence note.** Implementing the DNG specification is covered by Adobe's
royalty-free patent grant, but any implementation distributed under it must
carry, prominently in source and documentation:

> "This product includes DNG technology under license by Adobe."

That string goes in the about box and a `NOTICE` file **before any DCP-reading
code ships**. Adobe's own `.dcp` profile files are a separate matter and are not
redistributable — see DECISIONS #44.

## What a profile contains, and what Orion has

| Part | Tag | Orion | What it does |
|---|---|---|---|
| ColorMatrix | 50721/2 | ✅ | camera RGB → XYZ, linear |
| ForwardMatrix | 50964/5 | ❌ | better-conditioned, dual-illuminant |
| **HueSatMap** | 50938/9 | ✅ (fitted) | per hue/sat/value: hue shift, saturation scale |
| LookTable | 50982 | ❌ | creative look on top |
| **BaselineExposure** | 50730 | ✅ (fitted) | moves the zero point, in EV |

Orion had one of five, and now has three. The purple sky was what the missing
HueSatMap looked like; the flat, dark opening render was the missing
BaselineExposure. Both tables are *fitted* rather than read, because no profile
source for this body is redistributable (DECISIONS #44) — but both are fitted
into the spec's own structures, so a reader is the only thing missing.

---

## BaselineExposure

The spec:

> BaselineExposure specifies by how much (in EV units) to move the zero point.
> Positive values result in brighter default results.

Adobe applies it silently on open, which is why a file that has had it applied
still shows Exposure 0.00 in the panel. Orion does the same:
`kBaselineExposureEv` is added to the user's exposure inside `apply()`, so the
slider reads zero and Reset returns to the baseline rather than to darkness.

### The value is measured, not read

LibRaw does not carry this tag for native ARW, and no DNG Converter was
available, so the constant was **fitted against two independent references** —
the camera's own JPEG and Apple's RAW rendering — rather than read from a
profile.

Method: mean absolute luma error over six patches chosen per frame to span its
tonal range, swept over a 2-D grid of exposure against base contrast.

| Frame | best EV | best contrast | error |
|---|---|---|---|
| `_PIC8095` daylight cityscape | **+1.20** | **1.45** | 0.0171 |
| `_PIC8220` lit forecourt | **+1.20** | **1.45** | 0.0103 |
| `_PIC8148` night sky | +1.60 | 2.05 | 0.0068 |

Two of three agree exactly. The night frame's error surface is nearly flat —
0.0083 at the old defaults against 0.0068 at its own minimum — because a
near-black frame barely moves a mean luma, so its preference carries almost no
information. At (+1.2, 1.45) its error is 0.0150, still small.

Result on the daylight frame: mean error against the two references fell from
**0.1543 to 0.0194**, and Orion now lands *between* Sony and Apple on five of
six patches, which is the right place to be when two references disagree.

### What this value is not yet known to be

It fits **one camera body**. A per-camera `BaselineExposure` and a property of
Orion's own AgX zero point are indistinguishable from one body's data. The moment
a second body is supported, measure it again:

- if the number moves → it is per-camera, and belongs in a table
- if it does not → it belongs in the display transform, not here

Recorded in the code at the constant, so whoever adds the second body reads it.

---

## HueSatMap — built

The stage that corrects what the matrix cannot. From the spec, the table
encoding:

> Each entry of the table contains three 32-bit IEEE floating-point values. The
> first entry is hue shift in degrees; the second entry is saturation scale
> factor; and the third entry is a value scale factor. The table entries are
> stored in the tag in nested loop order, with the value divisions in the outer
> loop, the hue divisions in the middle loop, and the saturation divisions in
> the inner loop. All zero input saturation entries are required to have a value
> scale factor of 1.0.

`ProfileHueSatMapDims` gives HueDivisions ≥ 1, SaturationDivisions ≥ 2,
ValueDivisions ≥ 1. Adobe's usual grid is 90 hue × 25 saturation.
`ValueDivisions = 1` is the common case; the value axis is only needed for hue
twists proper.

### The colour space it applies in — the trap

`ProfileHueSatMapEncoding = 0` (the default) is explicit:

> 1. Convert linear ProPhoto RGB values to HSV.
> 2. Use the HSV coordinates to index into the color table.
> 3. Apply color table result to the original HSV values.
> 4. Convert modified HSV values back to linear ProPhoto RGB.

**Orion works in linear Rec.2020.** A node that indexes HSV built from Rec.2020
components puts every table cell at a different hue angle than a real DCP
expects — close enough to look right with a hand-fitted table, and silently
wrong the day an actual profile is loaded, which is the entire reason for
choosing the spec's shape. The conversion is two constant 3×3s; it belongs
inside the node. Skipping it is a `research/UNSOURCED.md` entry, because the
node would then not be the published algorithm.

`huesat.slang` does the conversion. `HueSatMap.h` keeps the three published
factors — Rec.2020→XYZ(D65), Bradford D65→D50, XYZ(D50)→ProPhoto, all
Lindbloom's — rather than one pre-multiplied product, so each is checkable
against its source. The round trip is asserted to be the identity to 1e-5, and
the working white is asserted to land on ProPhoto white; a composed matrix that
was wrong would tint every pixel, and no amount of table fitting would find it.

<http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html>

### Why the sky needs it and a ColorChecker does not fix it

No consumer sensor satisfies the Luther–Ives condition, so no fixed 3×3 is
colorimetrically correct across all spectra, and the error is worst for
saturated, spectrally narrow stimuli. Torger states the chart limitation
directly: the ColorChecker is a matte chart, so super-saturated colours cannot be
reproduced on it, and a CC24-optimised matrix may place saturated patches on or
outside the border of what the profile can reach. A chart improves the matrix; it
does not reach the sky.

And rendering deep blue away from violet is not one vendor's taste — Torger notes
most commercial profiles deliberately render these blues lighter than we see
them. Smith (2005) puts a number behind the developer's instinct that the sky is
not purple: skylight's spectral irradiance is a metameric match to **unsaturated
blue**, because the λ⁻⁴ intuition overweights violet where both the solar
spectrum and the eye's response fall away.

### The measured target

Sky patch on `_PIC8095.ARW`, after the BaselineExposure fix:

| | R/B | G/B |
|---|---|---|
| Orion | **0.622** | 0.678 |
| Sony JPEG | 0.458 | 0.713 |
| Apple | 0.441 | 0.671 |
| darktable | 0.270 | 0.586 |

G/B was on target after the exposure fix. The remaining error was almost purely
**excess red** — one axis rather than two, which is why the exposure fix had to
land first.

Target: **R/B ≈ 0.45**, the Sony/Apple consensus. Two implementations built
independently agreeing to within 0.017 is the evidence that this is a perceptual
correction rather than a house style.

### The fit

One hue region, four numbers, swept against the two references and scored as
mean absolute error in R/B and G/B over two sky patches — a saturated one high
in the frame and a hazy one near the horizon — with a foliage patch and a white
sign measured alongside to catch a correction that had spread outside blue.

| shift | sat ×0.95 | sat ×1.00 | sat ×1.05 |
|---|---|---|---|
| −8° | 0.0542 | 0.0262 | **0.0051** |
| −10° | 0.0550 | 0.0268 | 0.0158 |
| −12° | 0.0556 | 0.0270 | 0.0311 |
| −14° | 0.0557 | 0.0300 | 0.0464 |

Centre and half-width, checked at the chosen shift: 250° beats 235° (0.0072)
and 265° (0.0181); a 60° half-width beats 40° (0.0057). Shallow toward 235,
steep past 265 — the shape of a real hue region, not a spike the fit is
balanced on.

| patch | Orion | target |
|---|---|---|
| sky, upper (saturated) | 0.451 / 0.689 | 0.450 / 0.692 |
| sky, lower (hazy) | 0.636 / 0.816 | 0.647 / 0.822 |
| foliage | 0.858 / 0.903 | must not move (was 0.883 / 0.897) |
| white sign | 1.075 / 1.018 | must not move (was 1.075 / 1.018) |

One set of numbers covers two very different saturations because the correction
is weighted by saturation rather than applied flat across the hue. The foliage
patch moves 2.8% — it has sky visible between the leaves, and the white sign,
which does not, is unmoved to four decimals.

### What is pinned, and where

`orion-tests` (`testHueSatMapGpu`) holds everything that can be checked without
the sample frame: the matrix round trip, the spec's table structure (every
zero-saturation entry exactly (0, 1, 1), no entry scaling value), an identity
table leaving every pixel where it was, a grey ramp staying grey at every level,
and blue moving while foliage and skin do not.

`tools/huefit.py --check` holds the part that needs a photograph: it renders
`_PIC8095.ARW` and fails if the sky has drifted past 0.02 from the target. It
lives outside the suite because `samples/` is local-only — and it measures the
whole pipeline, so it also catches an upstream change that moves the sky
without touching this node.

---

## Dual-illuminant interpolation — specified, blocked on a source

DNG ≥ 1.2 requires linear interpolation in **inverse correlated colour
temperature** between two calibration illuminants, clamping to the nearer
calibration outside the range. So the algorithm is a ten-line citable function
the day Orion has a two-illuminant source. LibRaw's table carries one matrix per
body, so the open item is *obtaining a second matrix*, not designing anything.

Orion currently uses a single matrix, which means systematic error at every
temperature that is not the one it was built for. Small at 5130 K against D65,
and unquantified.

## Spectral sensitivity functions — mostly a dead end for this body

Jiang et al. (WACV 2013) covers 28 cameras, 400–720 nm at 10 nm, and the only
Sony body in it is the NEX-5N — nothing newer than about 2012, and no ILCE-7M3.
Measuring one requires a monochromator and an integrating sphere. As an
off-the-shelf lookup this route is closed.

The live alternative is estimation rather than measurement: Solomatov &
Akkaynak, *Spectral Sensitivity Estimation Without a Camera*, ICCP 2023 —
estimates a body's SSFs with no hardware access, code public.
<https://arxiv.org/abs/2304.11549>

With measured or estimated SSFs a matrix can be optimised against *chosen
spectra* — including real daylight sky — instead of being limited to what a
reflective chart can show.

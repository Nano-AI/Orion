# Response to the colour investigation — sources checked, two new leads

**Date:** 2026-07-28
**Responding to:** `2026-07-28-colour-investigation.md`
**What this is:** an outside review of that document, with the web research the
original session could not do (it ended at 200/200 searches). Every claim the
investigation left resting on judgement or memory has been checked against a
published source. Two of its open items now have concrete answers.

---

## 1. Verdict

The investigation's method is the strongest thing in this repository's feedback
folder: ratios not absolutes, three independent renderers as the reference
frame, hypotheses ruled out in order with an instrument each, and its own
mistakes kept in the record. The diagnosis — a 3×3-only profile failing on a
saturated narrow-band stimulus, correctable by the DNG spec's HueSatMap stage —
is **confirmed by every source checked below**.

Three things need to change before the fix lands, though. In order of cost if
ignored:

1. **Fix the exposure defect first, then fit the hue rotation** (§4.1).
2. **The HueSatMap node must apply its table in linear ProPhoto HSV, not
   Rec.2020 HSV**, or real profiles will never drop in (§4.2).
3. The target must be **pinned by a test the day it lands** (§4.4).

---

## 2. Claims checked

| Investigation claim | Verdict | Source |
|---|---|---|
| HueSatMap is the DNG-spec stage that corrects residual matrix error for saturated colour | ✅ Confirmed | DNG spec 1.6, ch. 6; dcpTool |
| Table entries are hue shift + saturation scale (+ value scale) | ✅ Confirmed, exact encoding in §3.1 | DNG spec 1.6, ProfileHueSatMapData1 |
| The "hue twist" (value-dependent hue shift) is how Adobe's newer profiles behave | ✅ Confirmed — `ValueDivisions > 1` is exactly Adobe's mechanism | dcpTool "Hue Twists" |
| A CC24 fit cannot fix saturated sky blue (§3.1 correction) | ✅ Confirmed independently — "the ColorChecker is a matte chart and therefore super-saturated colors can't be reproduced"; a CC24-optimised matrix "may put [saturated] patches on the border or even outside" LUT reach | Torger, camera-profiling |
| Rendering deep blue away from violet is a widespread deliberate choice, not one vendor's taste | ✅ Confirmed — "most commercial profiles choose to render these type of blues lighter than we see them" | Torger, camera-profiling |
| Sky's appearance target is not saturated violet (§3.2) | ✅ Confirmed, and stronger than the investigation put it — see §3.3 | Smith 2005, *Am. J. Phys.* |
| dcraw's `adobe_coeff` derives from Adobe DNG Converter (decision #45, OPEN) | ✅ Corroborated — dcraw updated "10–14 days after Adobe releases a new version of Adobe DNG Converter"; annotated source describes the table as "the Adobe-dng camera coefficients". Still hearsay-grade, but consistent hearsay from multiple independent documenters. #45 can stay OPEN with the evidence column filled in | LightZombie wiki; ninedegreesbelow |
| DNG implementations under the patent grant need an Adobe notice (decision #44) | ✅ Confirmed, exact string in §3.4 | Wikipedia / Adobe patent licence |
| Dual-illuminant interpolation needs a real profile source (open item 4) | ✅ Confirmed, and the algorithm is specified — see §3.5 | DNG spec 1.6 |
| SSF datasets "cover several dozen bodies" (open item 2) | ⚠️ True but useless for this camera — see §3.6 | Jiang et al. 2013 |

---

## 3. What the sources add

### 3.1 The exact table encoding — the node can be spec-shaped on day one

From the DNG 1.6 specification, `ProfileHueSatMapData1` (tag 50938):

> Each entry of the table contains three 32-bit IEEE floating-point values. The
> first entry is hue shift in degrees; the second entry is saturation scale
> factor; and the third entry is a value scale factor. The table entries are
> stored in the tag in nested loop order, with the value divisions in the outer
> loop, the hue divisions in the middle loop, and the saturation divisions in
> the inner loop. All zero input saturation entries are required to have a value
> scale factor of 1.0.

`ProfileHueSatMapDims` gives HueDivisions ≥ 1, SaturationDivisions ≥ 2,
ValueDivisions ≥ 1. Adobe's standard grid is 90 hue × 25 saturation (dcpTool);
`ValueDivisions = 1` is the common case, and the value axis is only needed for
hue twists proper. **Orion's minimal population — one documented rotation in
the blue region — fits in a `ValueDivisions = 1` table**, which is also the
shape DCamProf chose for its own correction LUT (chromaticity-indexed,
"2.5D"). Nothing about the minimal fit requires the third axis; leave it at 1
and the node still reads any real profile later.

### 3.2 The colour space the table lives in — a trap the plan hasn't hit yet

The spec is explicit about *where* the map applies (`ProfileHueSatMapEncoding`
= 0, the default):

> 1. Convert linear ProPhoto RGB values to HSV.
> 2. Use the HSV coordinates to index into the color table.
> 3. Apply color table result to the original HSV values.
> 4. Convert modified HSV values back to linear ProPhoto RGB.

(Encoding 1 additionally passes V through the sRGB curve for shadow precision.)

Orion works in linear Rec.2020. A node that indexes HSV built from **Rec.2020**
components will put every table cell at a different hue angle than a real DCP
expects — close enough to look right with a hand-fitted table, silently wrong
the day an actual profile is loaded, which is the stated reason for choosing
the spec shape at all. The conversion is two constant 3×3s
(Rec.2020 → linear ProPhoto → HSV and back); pay it inside the node. If it is
skipped as a deliberate simplification, that is a `research/UNSOURCED.md`
entry, because the node is then no longer the published algorithm.

### 3.3 The developer's instinct has a paper behind it

Smith (2005), *Human color vision and the unsaturated blue color of the daytime
sky*, Am. J. Phys. 73, 590, does quantitatively what §3.2 of the investigation
did by argument: skylight's spectral irradiance is a **metameric match to
unsaturated blue**, not violet — the λ⁻⁴ intuition overweights violet because
the solar spectrum falls off there and the eye's response does too. "The sky
clearly isn't purple when I look up" is not a taste judgement to be
accommodated; it is the measured appearance, with a citation. This is the
published anchor open item 1 said it lacked. The design decision Orion owns
shrinks from "what should blue sky look like" to "how far toward the
Sony/Apple consensus to go" — a tuning question, not a principle question.

### 3.4 The exact notice string for decision #44

> "This product includes DNG technology under license by Adobe."

Required "displayed in a prominent manner within its source code and
documentation" by the DNG patent licence. Copy verbatim into the about box and
a `NOTICE` file when DCP-reading code ships.

### 3.5 Dual-illuminant interpolation is fully specified (open item 4)

DNG ≥ 1.2 *requires* the algorithm: linear interpolation in **inverse
correlated colour temperature** between the two calibration illuminants
(recommended pair: Standard-A and D55/D65), clamping to the nearer calibration
outside the range. So the day Orion has a two-illuminant source, the
interpolation is a ten-line, citable function — the open item is purely "obtain
a second matrix", not "design an algorithm".

### 3.6 The SSF lead mostly dies on contact — and a replacement exists

Open item 2 called published SSF datasets the most interesting unexplored
suggestion. Checked: **Jiang et al. (WACV 2013) covers 28 cameras, 400–720 nm
at 10 nm — and the only Sony body in it is the NEX-5N.** No ILCE-7M3, nothing
newer than ~2012; measuring one requires a monochromator and integrating
sphere. As an off-the-shelf path for this camera it is closed.

The live replacement: Solomatov & Akkaynak, *Spectral Sensitivity Estimation
Without a Camera* (ICCP 2023) — estimates a body's SSFs with no hardware access
to it, code public. Worth one evening if the SSF route is ever wanted; until
then, downgrade item 2 from "most interesting" to "possible via estimation,
not via lookup".

### 3.7 The 1.3× darkness has a name: BaselineExposure (items 5 and 6b)

This is the largest new finding. The DNG spec defines a per-model tag for
exactly the measured symptom:

> BaselineExposure specifies by how much (in EV units) to move the zero point.
> Positive values result in brighter default results.

Adobe applies it silently on open; that is a documented, widely-analysed
behaviour ("Adobe's silent exposure compensation"), and Sony bodies at base ISO
typically carry values around +0.35 EV. Orion's measured midtone gap is
0.958 / 0.722 = 1.33× = **+0.41 EV** — the right order, the right direction,
and the mechanism every other converter uses to close it.

**Ten-minute check:** convert `_PIC8095.ARW` with Adobe DNG Converter, run
`exiftool -BaselineExposure` on the result. If it reads ≈ +0.35, task #18 stops
being a contrast-calibration question and becomes "apply the camera's baseline
exposure at the head of the linear pipe" — a sourced, per-model constant
instead of a tuned one. It also resolves §6b for free and more honestly than
retuning contrast would: +0.35 EV moves the sky from EV −0.5 to ~EV −0.15,
collapsing the shadow wheel's grip on it, which is precisely what the band
geometry predicted. LibRaw does not carry this tag for native ARW files, so the
value needs harvesting once per supported body from a converted DNG — one row
in a table Orion already needs.

---

## 4. Corrections and cautions on the plan

### 4.1 Sequence: exposure before hue fit

The investigation treats the hue rotation (§5) and the darkness (item 5) as
independent. They are — R/B is invariant under a global exposure scale in
linear — **but only while `ValueDivisions = 1` and the validation is
ratio-based.** The moment either changes (a value-dependent twist, or eyeball
validation against other renderers' renders), an exposure shift moves pixels
between value bands and re-opens the fit. Land BaselineExposure first, re-run
`pixstat` on the sky, then fit the rotation against the re-measured numbers.
Cheap insurance, and it avoids fitting a correction on top of a known defect.

### 4.2 The ProPhoto HSV requirement

Covered in §3.2. This is the one place the written plan, as it stands, would
produce a node that is spec-shaped in name only.

### 4.3 Broaden the fit's evidence for free

The one-frame limitation is honestly stated, but it is cheaper to loosen than
the doc implies: any daylight frame with sky in it gives another `pixstat`
patch against the same three reference renderers in minutes. Three or four
skies (different times of day, one overcast) turn "fitted to one frame" into
"fitted to one hue region with spread known" before any target chart enters
the picture.

### 4.4 Pin it or it will drift

The process finding — *the code was fine wherever it was measured* — implies
its own remedy and the doc stops short of committing to it. The day the node
lands, the bench corpus gains `_PIC8095.ARW` and the suite gains an assertion:
rendered sky patch R/B within a stated tolerance of the target (≈ 0.45 ± 0.02),
G/B likewise. Same pattern as the AgX neutrality test, which exists because the
last purple defect shipped without one. A second assertion — a neutral ramp
through the HueSatMap node stays neutral — enforces the spec's own
zero-saturation rule (§3.1) and guards the exact class of bug that built this
folder.

### 4.5 Promote `pixstat.swift` now

Every number in the investigation and every check proposed above flows through
a script that is still in a scratchpad. The investigation says "not yet
promoted"; this response says: before the next colour commit. A measurement
tool that can silently vanish is how a corpus goes back to two night frames.

---

## 5. Ready-to-paste citations

For the `research/` entry the HueSatMap node must have (sourcing rule):

- Adobe, *Digital Negative (DNG) Specification*, v1.6.0.0, Dec 2021 — ch. 6
  and tags 50937–50939, 51107 (ProfileHueSatMapDims/Data1/Data2/Encoding);
  BaselineExposure (50730); dual-illuminant interpolation by inverse CCT.
  https://paulbourke.net/dataformats/dng/dng_spec_1_6_0_0.pdf
- Sandy McGuffog, *dcpTool documentation* — DCP processing model and hue
  twists. https://dcptool.sourceforge.net/DCP%20FIles.html ·
  https://dcptool.sourceforge.net/Hue%20Twists.html
- Anders Torger, *Making a camera profile with DCamProf* — matrix limits on
  saturated blues, matte-chart limitation, deliberate deep-blue rendering.
  https://torger.se/anders/photography/camera-profiling.html
- Glenn S. Smith, *Human color vision and the unsaturated blue color of the
  daytime sky*, Am. J. Phys. 73, 590 (2005).
  https://pubs.aip.org/aapt/ajp/article-abstract/73/7/590/1056162
- Jiang, Liu, Gu, Süsstrunk, *What is the space of spectral sensitivity
  functions for digital color cameras?*, WACV 2013 — 28-camera SSF database.
  https://zenodo.org/records/3245883
- Solomatov & Akkaynak, *Spectral Sensitivity Estimation Without a Camera*,
  ICCP 2023. https://arxiv.org/abs/2304.11549 ·
  https://github.com/COLOR-Lab-Eilat/Spectral-sensitivity-estimation
- dcraw provenance corroboration: Doug Pardee, *ProjectDcraw* (LightZombie
  wiki) — https://github.com/Doug-Pardee/LightZombie/wiki/ProjectDcraw ·
  Elle Stone, *dcraw annotated* —
  https://ninedegreesbelow.com/files/dcraw-c-code-annotated-code.html
- BaselineExposure behaviour in practice: RawDigger, *Deriving Hidden Baseline
  Exposure Compensation* —
  https://www.rawdigger.com/howtouse/deriving-hidden-ble-compensation ·
  ExifTool DNG tag reference — https://exiftool.org/TagNames/DNG.html

---

## 6. Summary of recommended actions, in order

| # | Action | Closes |
|---|---|---|
| 1 | Convert one ARW per body with DNG Converter; read `BaselineExposure`; apply it at the head of the linear pipe | item 5, §6b, task #18 |
| 2 | Promote `pixstat.swift` into the repository | §4.5 |
| 3 | Re-measure the sky post-exposure-fix; fit the hue rotation | §5 of the investigation |
| 4 | Build the HueSatMap node with the spec's entry encoding, `ValueDivisions = 1`, applied in linear ProPhoto HSV | the fix itself |
| 5 | Add `_PIC8095` to the bench corpus; assert sky R/B and node neutrality | §4.4, process finding |
| 6 | Add the citations above to `research/` (new `camera-profiles.md` or extend `color-pipeline.md`); note the exact Adobe notice string against decision #44 | sourcing rule |
| 7 | Fill decision #45's evidence column with the corroboration in §2; leave OPEN | — |

The investigation ended by saying every serious defect lived in a state nobody
pointed an instrument at. The two findings this response adds — the ProPhoto
HSV requirement and BaselineExposure — were both sitting in the same
specification the fix was already citing. The instrument this time was simply
reading the rest of the document.

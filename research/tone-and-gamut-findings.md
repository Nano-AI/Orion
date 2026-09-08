# Tone and gamut — five-agent audit, 2026-09-07

Five agents ran in parallel against two threads #224/#225 left open: the AgX
roll-off's shadow side (mirrored, never measured — #223's own comment says
so), and the 0.83 chroma deficit #225 re-measured but did not attribute. A
sixth question — whether `fuse_apply.slang` reconstructs the way the rest of
this tree's guided-filter nodes do — came out of the same pass. Findings
below; decisions #226-#228 are the outcome. **Two magnitudes are unmeasured as
of this writing** and are marked where they occur, not guessed at.

---

## A. The contrast slope crops 2.48 of the shadow latitude's 8 stops, and does it asymmetrically

**Where:** `develop_display.slang:270` (the slope), `:154-157` (the axis
constants), `:176-183` (the roll-off thresholds, decision #223).

The display transform pivots contrast about middle gray on AgX's normalized,
two-piece log2 axis (decision #214's anchoring fix):

```
const float3 y = (xPre - kPivotNorm) * p.contrast + kPivotNorm;
```

Constants, verified against the file as it stands today: `kPivotNorm = 10.0f /
16.5f` (0.606061), `kBlackStops = 8.0f`, `kWhiteStops = 6.5f`,
`Engine.contrast` shipping default **1.45** (`app/Engine.swift:148`).

**Arithmetic, re-derived independently rather than copied from the brief:**

| quantity | formula | value |
|---|---|---|
| soft roll-off begins, shadow side | `kRollOffLo` → EV via `agxNormalize`⁻¹ | **-2.939 EV** |
| soft roll-off begins, highlight side | `kRollOffHi` → EV | **+3.674 EV** |
| hard clip, `rollOff` mode 0, shadow side | `-kBlackStops / contrast` | **-5.517 EV** |
| hard clip, mode 0, highlight side | `kWhiteStops / contrast` | **+4.483 EV** |
| shadow latitude lost to the mode-0 clip | `8.0 - 5.517` | **2.48 stops** |
| highlight headroom left unused, mode 0 | `4.483 - 3.674` (#221's measured real-photograph ceiling) | **0.81 EV** |

The two roll-off figures (-2.939, +3.674) and the two hard-clip figures
(-5.517, +4.483) match the brief to three decimal places; recomputed here from
`kRollOffHalf`'s own formula
(`kHighlightCeilingEv * kDesignContrast * (1 - kPivotNorm) / kWhiteStops`,
`develop_display.slang:178-179`) rather than taken on faith.

**⚠ `rollOff` mode 0 is no longer the shipping default.** Decision #224 moved
the default to mode 1, ACES RGC, whose identity zone is exactly the soft
figures above (-2.939 / +3.674 EV) — so the -5.517/+4.483 hard-clip numbers
describe a debug path (`ORION_ROLLOFF=0` / `--rolloff 0`,
`DevelopOutput.cpp:49-54`), not what a photograph renders through today. They
still matter: they are why the default changed (`testDisplayRollOffIsInjective`
exists because of exactly this crush), and the *soft* threshold's shadow side
inherits the hard clip's asymmetry by construction — see below.

**The asymmetry is deliberate, and the file says so itself.** `kRollOffHi` is
pinned to `kHighlightCeilingEv = 3.674f`, decision #221's measurement of the
brightest real content this pipeline's own photographs ever produce.
`kRollOffLo` is not an independent measurement of anything — it is
`kPivotNorm - kRollOffHalf`, the same half-width mirrored to the other side of
the pivot. `develop_display.slang:168-171` names this directly:

> "Symmetry about the pivot on this axis is a design choice (not a second
> measurement): it is the plainest way to place two thresholds from one
> number, and decision #223's validation confirms shadows keep enough
> latitude at the number this produces (~2.94 EV preserved below gray)."

So the highlight side is anchored to what the sensor and this pipeline
actually produce; the shadow side is anchored to the highlight side. Nothing
here says that is wrong — only that it is a different kind of claim, and the
file does not pretend otherwise.

---

## B. What the literature does with a toe/shoulder placement — Orion matches neither published pattern

Two patterns turn up, and Orion's mirror-one-side-to-the-other approach is
neither of them.

**Both ends placed by convention or by eye:**
- Troy Sobotka, *AgX* — <https://github.com/sobotka/AgX> and *AgX-S2O3*
  (2022–). Reference range is 10 stops below middle gray, 6.5 above —
  deliberately asymmetric, neither endpoint measured against anything; the
  sigmoid fit is credited to Jed Smith.
- John Hable, *"Uncharted 2: HDR Lighting"*, GDC 2010, and *Filmic Tonemapping
  with Piecewise Power Curves*, filmicworlds.com — hand-tuned toe/shoulder
  constants, openly presented as tuned by eye rather than derived.

**Both ends tied independently to a physical or display spec:**
- ACES Output Transforms tone mapping —
  <https://docs.acescentral.com/system-components/output-transforms/technical-details/tone-mapping/>
  — ties the curve to the target display's peak luminance.
- ACEScct's toe derives from Cineon/Kodak negative-film minimum density.

**Reinhard, Stark, Shirley & Ferwerda, *"Photographic Tone Reproduction for
Digital Images"*, ACM TOG 21(3), SIGGRAPH 2002, pp.267-276** — measures scene
log-average luminance, but to place middle gray (the exposure/key), not a
latitude endpoint. Does not transfer to this question directly; it answers
"where is gray", not "where does the toe run out".

⚠ **"Measure one side, mirror it to the other" — what `kRollOffLo` does — has
no published precedent found, either endorsing it or forbidding it.** Every
source above either places both ends by eye/convention, or ties both ends
independently to a measurement of their own side. Nothing found derives one
endpoint from the other. This is stated as **untested**, not as known-wrong:
mirroring is a reasonable-sounding simplification and the file's own comment
frames it that way, but "reasonable-sounding" is exactly the category CLAUDE.md's
sourcing rule exists to catch. `research/UNSOURCED.md` is the register for an
invented *constant*; this is closer to an invented *rule for deriving* one,
applied to a shadow threshold nobody has measured.

**What ACEScct's film-density claim rests on:** secondary sources only. No
AMPAS primary document stating the derivation was found in this pass — an
honest gap in what is available, not a claim resolved by omission.

---

## C. ⚠⚠ Patent note — read before touching the shadow threshold independently

CLAUDE.md's rule, restated because it is the reason this section exists: **a
citation is not clearance.** Decision #174 wrote that after decision #173 found
a different filter's alternatives were largely US patents; the same question
had never been asked about a toe/shoulder split, so it is asked here before
anyone writes code that gives the shadow side its own measured threshold.

**US Patent 7,158,686**, *"Enhancing the Tonal Characteristics of Digital
Images Using Inflection Points in a Tone Scale Function"*, Edward B. Gindele,
Eastman Kodak Co. Filed 2002-09-19, issued 2007-01-02.
<https://patents.google.com/patent/US7158686B2/en>

Claims independently-controllable highlight and shadow inflection points, split
at an 18% gray reference — the same reference point this pipeline already
pivots on, and close in shape to "give the shadow threshold its own measured
value instead of mirroring the highlight one." **Status: EXPIRED** (fee-related
lapse; a 2002 filing's term had run out by 2025), so this specific patent is not
a current obstacle.

**US Patent 5,541,028**, Eastman Kodak Co., *"Constructing Tone Scale Curves"*,
filed 1995. <https://patents.google.com/patent/US5541028A/en>. Also expired.
Uses a Michaelis-Menten curve — the same functional family ACES 2.0's tone
scale uses.

⚠⚠ **This was a web search for two patents, not a freedom-to-operate review.**
"Two found, both expired" is not "the area is clear" — it is "two specific,
findable instruments are not the obstacle." A real FTO review searches claims
systematically, checks continuations and family members, and is a legal
service this project has never purchased, on this filter or any other. If an
independently-measured shadow threshold is built, that gap does not close
itself by this section existing.

---

## D. The chroma deficit's mechanism: four uncoordinated per-channel clamps run ahead of the gamut compressor

Decision #225 re-measured the saturation ratio to the camera JPEG at **0.83**
(range 0.41-1.38) after fixing the warm-hue rotation, and left the cause open.
This pass finds the mechanism.

**Four negative clamps, all upstream of `kInset`:**

| site | code |
|---|---|
| `tone_ops.slang:112` (`applyTone`) | `return max(c * exp2(deltaEv), 0.0f);` |
| `tone_ops.slang:130` (`applyColor`) | `return max(y + (c - y) * amount, 0.0f);` |
| `develop_display.slang:256` | `float3 c = max(src[tid.xy].rgb, 0.0f);` |
| `develop_display.slang:259` | `c = max(mul(kRec2020ToRec709, c), 0.0f);` |

`kInset` — AgX's gamut-compression matrix — runs at `develop_display.slang:260`,
immediately after the second clamp above and downstream of all four.

⚠ **The first two are the *sole* effect of two stages that are otherwise the
exact identity at default settings.** `applyTone` at `deltaEv == 0` is
`c * 1.0`; `applyColor` at `vibrance == 0, saturation == 0` is `amount = 1.0`,
i.e. `y + (c - y) * 1 = c`. The only thing either function does to an
already-neutral pixel at Orion's defaults is clip it to non-negative — called
unconditionally, every photograph, from `develop_linear.slang:189-190`:

```
c = applyTone(c, guideEv, p.highlights, p.shadows, p.whites, p.blacks);
c = applyColor(c, p.vibrance, p.saturation);
```

⚠ **This directly contradicts `color_matrix.slang:30`'s own stated design
intent.** Immediately after the camera matrix multiply, that file says:

> "Negative values are legitimate here — they are colors outside the working
> gamut, and clipping them now would bake in a hue shift the tone mapper could
> otherwise resolve gracefully."

The graceful handling this comment promises never happens: by the time
`kInset` runs, two unconditional clamps (in `applyTone`/`applyColor`) and two
more immediately before it (`develop_display.slang:256`, `:259`) have already
zeroed every negative component the camera matrix legitimately produced. The
gamut compressor receives pre-clipped data, not the out-of-gamut values its own
sibling file says it is designed to resolve.

**Independent literature cross-check, pointing the same direction.** Mantiuk,
Mantiuk, Tomaszewska & Heidrich, *"Color Correction for Tone Mapping"*,
Computer Graphics Forum 28(2), Eurographics 2009, pp.193-202. Their
subjective-matching experiment over 8 HDR images found saturation correction is
"almost unnecessary" for mild tone-curve compression, `0.6 < c < 1.6`. Orion's
shipping contrast (1.45) sits **inside** that band — so the tone slope itself is
unlikely to be what is eating 17% of saturation, which points the finger back
at the clamps rather than at the contrast curve. Their eqs. 1-3 give three
published correction options (chroma-ratio-preserving, non-linear, and
luminance-preserving), none implemented here — recorded as the citable next
step, not built.

⚠ **Some desaturation here is intended, not a bug, and this section is not
claiming otherwise.** Per-channel tone mapping desaturating bright colors is
AgX's documented behavior: Blender's 4.0 release notes describe it as sending
"bright colors towards white, similar to real cameras"
(<https://developer.blender.org/docs/release_notes/4.0/color_management/>). So
*some* loss at high tone-curve compression is by design; only the *magnitude*
attributable to the four clamps above is in question, and nothing published
gives the right number for Orion's specific inset/outset matrices — this is an
adaptation, not a spec, same caveat `research/camera-profiles.md` already
carries for the HueSatMap fits.

⚠⚠ **MAGNITUDE IS UNMEASURED as of this writing.** Two measurement agents were
running concurrently with this write-up to isolate how much of the 0.83 ratio
the four clamps account for versus AgX's by-design per-channel desaturation.
Whatever number comes back belongs in this section, replacing this paragraph —
it is not guessed at here.

---

### ⚠⚠ Measured 2026-09-07 — the clamps are not the chroma deficit either

| probe | all 4 clamps | remove all 4 |
|---|---|---|
| Rec.2020 primaries + secondaries | 0.702 | **1.000** |
| CIE1931 spectral locus (31 pts) | 0.880 | 0.998 |
| **Synthetic ColorChecker24** | 0.467 | **0.467 — zero change** |

On real photographic subject matter the clamps cost **nothing**, to five
decimals. They engage only for colour outside Rec.709 but inside Rec.2020,
which is expected: AgX is defined against Rec.709 primaries.

On the three real ARWs the clamps fire on **~46% of pixels**, but at **mean
0.00004, max 0.0047** against a mid-gray of 0.18 — near-black rounding noise,
not saturated content being cut.

⚠ **Three of the four sites are inert.** Removing `tone_ops.slang:112`, `:130`
or `develop_display.slang:256` individually changes nothing measurable, because
`:259` re-clips whatever survives two lines later. Architecturally this is *one*
clamp with three redundant guards.

⚠⚠ **But they are load-bearing.** Removing all four takes `orion-tests` from
1029/0 to **7 failures**, and in those the *unmasked control patch* renders
byte-0 black — the entire frame collapses, not just the graded region. Not
root-caused. Anyone deleting a redundant-looking guard here should start from
that fact.

**So section D's hypothesis is wrong, and #225's 17% is still unexplained.**
What survives is the Mantiuk cross-check ruling out the contrast slope. Both
named suspects are now eliminated.

---

## E. Exposure fusion reconstructs an unguided upsample where the same tree does the guided one twice

**Where:** `fuse_apply.slang:39-77`.

`fuseSample` (`:39-51`) is a plain bilinear sampler. `fuseApply` (`:53-77`)
calls it on `fused` and `proxy` — both **quarter-resolution results** of the
fusion, not guided-filter coefficients — and reads the full-resolution pixel
`src[tid.xy]` (`:64`) only to multiply the reconstructed gain onto it at the
end (`:76`). The full-res pixel is never used to steer the upsample; a hard
edge that falls between two proxy texels is smoothed across the gap by plain
bilinear interpolation, exactly like any other naive upsample.

**The same tree gets this right, twice, elsewhere.** Both of the following
solve coefficients on a subsampled grid, lift *the coefficients* bilinearly,
and then re-evaluate the linear model against the **full-resolution guide**:

- `dehaze_recover.slang:46-61` — lifts `(a, b)` (`:46-58`), then
  `g = dehazeGuideOf(c.rgb, ...)` at full res (`:60`) and
  `t = clamp(ab.x * g + ab.y, ...)` (`:61`).
- `mask_guide_apply.slang` — lifts `(a, b)` via `liftAb` (`:39-58`), then
  `l = guide[tid.xy].x` at full res (`:72`) and `refined = c.x * l + c.y`
  (`:74`).

That is the Fast Guided Filter pattern as published: **Kaiming He & Jian Sun,
*"Fast Guided Filter"*, arXiv:1505.00996 (2015)** — solve `(a, b)` on a
subsampled grid, lift the *coefficients*, evaluate `q = a·I + b` against the
guide at full resolution. `fuse_apply.slang` skips the last step: it lifts the
already-evaluated *result*, not the coefficients, so nothing at full resolution
ever steers where the reconstruction follows an edge.

`kFuseScale = 4` (`engine/src/pipe/DevelopPipeline.h:503`) is the subsampling
factor the fusion proxy runs at — the same `s = 4` He & Sun report as visually
indistinguishable *when the guided reconstruction is used*
(`research/tone-and-local-contrast.md`'s own fast-guided-filter section
measures this for the highlight/shadow chain). Exposure fusion runs at the
same downsampling ratio without the guide step that makes that ratio safe.

**Predicted effect:** a halo on the order of one proxy texel's width — roughly
`kFuseScale` (4) full-resolution pixels — straddling any hard edge, where the
fusion gain should snap to the edge and instead blends smoothly across it.

⚠⚠ **MAGNITUDE IS UNMEASURED as of this writing.** A second measurement agent
was running concurrently with this write-up to render a hard-edge test frame
and quantify the halo directly rather than rely on the predicted width above.
That number belongs here once it lands.

---

### ⚠⚠ Measured 2026-09-07 — the halo is real, the diagnosis above is not

Driving the real shader chain (`fuseProxy` → `fuseSplit` → `llfDownPacked` →
`fuseBlend` → `fuseApply`) on a synthetic 6000×4000 vertical step edge of 6.64
stops:

| strength | 10–90% transition | excursion |
|---|---|---|
| 0.5 | **61 px** | 2.04 EV (~4.1× swing) |
| 1.0 | **95 px** | 4.08 EV (~16.9× swing) |

Four to six times the ~16 px this section predicted, so the halo is worse than
guessed. **But the upsample does not cause it.** Measuring the gain ratio at
proxy resolution — before any lift happens at all — already gives a 23–24
proxy-texel transition, **92–96 full-resolution px**. The halo is fully formed
in the low-resolution data. A prototype following `dehaze_recover.slang`'s
pattern exactly (coefficients lifted, evaluated against the true full-resolution
guide) measured **61 px and 96 px** — statistically identical to the version
called defective above.

So the pattern mismatch is a real code fact worth about **one proxy texel, 4 px
of 90+**. The width is set by running a 6-level Laplacian pyramid on a 4×
downsampled proxy, and `fuse_blend.slang:13-14` already quotes Mertens et al.
§3.2 calling that a deliberate trade: *"pre-smoothing the weights trades the
seams for halos."* A genuine fix means changing the pyramid's operating
resolution or level count — a much larger change than swapping a sampler.

⚠ **Fusion is also off by default**: `Adjustments.h:345` sets `fusion = 0.0f`,
the 32-node chain is disabled below `1e-4` (`DevelopLocal.cpp:434`), and Auto
Enhance zeroes it for any frame at or above mid-gray. A normally-exposed
photograph opened cold never runs this code. Priority: low.

⚠ **The lesson worth keeping** is the shape of the error, not the number. A
verified code-level inconsistency — one stage doing what two neighbours
document as wrong — read as a cause, and the measurement found it was a
correlate. The inconsistency was real; the causal claim attached to it was
inference, and inference is what the measurement was for.

---

## G. ⚠⚠ Measured against two references — #225's deficit is a look, not a defect

Sections D and E hunted a saturation defect. There isn't one. The measurement
that settles it needed no new sample frames, because **every RAW carries the
camera's own JPEG inside it** (`extractThumbnail`, `LIBRAW_THUMBNAIL_JPEG`) —
the reference was free and available the whole time.

Eight **sidecar-free** frames from the developer's own Sony shoot, three
renderings each — the camera's embedded JPEG, macOS ImageIO, and Orion.
Saturation here is the **mean of per-pixel** `(max-min)/max` over pixels above
0.05 luma, not §231's saturation-of-the-mean.

| frame | orion/camera | apple/camera | orion/apple |
|---|---|---|---|
| DSC09737 | 0.879 | 0.845 | 1.040 |
| DSC09738 | 0.901 | 0.912 | 0.988 |
| DSC09743 | 0.767 | 0.747 | 1.026 |
| DSC09744 | 0.789 | 0.764 | 1.033 |
| DSC09745 | 0.788 | 0.762 | 1.034 |
| DSC09746 | 0.777 | 0.759 | 1.024 |
| DSC09747 | 0.703 | 0.816 | **0.861** |
| DSC09749 | 0.622 | 0.761 | **0.817** |
| **mean** | **0.778** | **0.796** | **0.978** |

⚠⚠ **Apple sits as far below the camera JPEG as Orion does.** The camera JPEG
carries Sony's Creative Style and its own `Contrast: High` setting; a neutral
RAW render carries neither, and no RAW developer's does. #225's 0.83 was
measuring the gap between a camera JPEG and a RAW render, which every RAW
developer has.

**Exposure, measured in the same run, is sound.** Orion is within **±0.08 EV of
Apple on all eight frames**. `DSC09738` reads −0.54 EV against the *camera*
JPEG and −0.08 against Apple: Sony's DRO lifted that frame and neither RAW
developer did.

### The residual finding (#230)

Six frames put Orion at 0.98–1.04 of Apple. Two do not — **`DSC09747` 0.861 and
`DSC09749` 0.817** — and they are the two brightest (mean luma 0.69 and 0.75
against 0.33–0.66). So there is a real **high-key desaturation** relative to
Apple, keyed to bright content rather than to a hue. Cause undiagnosed;
candidates are AgX's shoulder compressing bright chroma harder than Apple's
transform, or `kOutset` under-restoring — §B records that AgX's inset/outset is
published with no derivation of how much the outset should give back.

⚠ **Do not let this grow back into #225's shape.** Two frames, one reference,
bright content only. It wants its own measurement across a wider luma range
before anything changes.

### ⚠ What the developer actually sees, and why it is not any of the above

Orion paints **the camera's embedded JPEG** as a placeholder on open
(`OrionApp+Files.swift:269`), and clears it when the render lands (`:296`),
~210.9 ms later (#151). So opening a photograph shows Sony's punchy rendering
and then replaces it with a neutral one at ~0.78 its saturation. The reported
symptom — *"it renders properly, then a quarter second later the colours
flatten"* — is that swap, and it is inherent to rendering RAW rather than a
fault. Closing the gap is a **default-look decision** (a camera-matching
profile, as Lightroom ships) and not a bug fix.

⚠ Two of the developer's own frames made it look far worse than it is:
**sidecars carrying extreme saved edits** — `DSC09734` `exposureEv −1.96`,
`DSC09742` `−2.73`, `DSC09752` `−3.22` with blacks *and* whites pinned at
±1.0000, the slider limits. Orion was applying them correctly. **A saved edit
was mistaken for a renderer defect twice in this session**, once by the
developer and once here, which is an argument for the edited state being
visible on open.

---

## F. What was not checked

Said plainly, per CLAUDE.md's own rule that a gap is recorded rather than
papered over:

- **(D)'s and (E)'s magnitudes are unmeasured** at the time this was written.
  Both sections are structured so a measured number drops in without
  rewriting the surrounding argument.
- **No freedom-to-operate review has been done**, on this filter or any other
  in this codebase (decision #174). Section C is a web search for two
  patents, not legal clearance.
- **The ACES documentation does not state a toe/shoulder derivation rule.**
  That is an honest gap in the source material, not something this pass
  filled in with a guess.
- **ACEScct's film-density-derived toe is secondary-sourced only.** No AMPAS
  primary document was found and read.
- **Whether mirroring one measured endpoint to place the other is wrong** was
  not established either way — section B's conclusion is "untested", and nothing
  here should be read as more than that.

---

## History

- **2026-09-07** — five-agent audit: the contrast slope's shadow/highlight
  asymmetry traced to its source (`kRollOffHi` measured, `kRollOffLo`
  mirrored, decision #214/#221/#223's own numbers, re-derived independently
  here); the toe/shoulder literature surveyed and found to contain no
  precedent for mirroring one side to the other; a patent search run before
  any code proposing an independent shadow threshold gets written; the chroma
  deficit's mechanism identified as four per-channel clamps ahead of `kInset`,
  cross-checked against Mantiuk et al. 2009's finding that saturation
  correction is unnecessary in Orion's own contrast band; and exposure
  fusion's upsample found to skip the guided-reconstruction step the same
  tree uses correctly twice elsewhere. Decisions #226, #227, #228. Chroma and
  halo magnitudes unmeasured as of this writing.

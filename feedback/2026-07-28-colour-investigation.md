# The purple sky — investigation, corrections, and what is still undecided

**Date:** 2026-07-28
**Trigger:** the developer opened `samples/_PIC8095.ARW` (Sony ILCE-7M3, daylight,
Space Needle) beside another editor and said the photo *"looks disgusting when
loaded in"*. Orion's sky rendered violet; every other renderer's rendered blue.

This file is the whole thread: the measurements, the two outside reviews that
corrected it, what I got wrong, and the decisions that came out. It is written to
be handed to a reviewer who has not seen the conversation.

---

## 1. What was measured

All numbers are mean sRGB over the same normalized sky patch, read with
`scratchpad/pixstat.swift` (a small ImageIO reader written for this, which
applies EXIF orientation — the first version did not and sampled foliage).

Ratios rather than absolute levels, because the renderers disagree on exposure
and contrast and the question here is *hue*.

| Source | R/B | G/B |
|---|---|---|
| Raw sensor, post-white-balance, black-subtracted (camera RGB) | 0.532 | 0.715 |
| **Orion** | **0.573** | **0.630** |
| Sony's own JPEG | 0.458 | 0.713 |
| Apple (ImageIO / Preview) | 0.441 | 0.671 |
| darktable 5.x, default | 0.270 | 0.586 |

**Orion is the outlier.** Every other implementation pulls red down relative to
blue; Orion pushes it up. Sony and Apple — independent implementations, different
companies — agree to within 0.017. darktable goes further still; its tone
rendering is different enough that the magnitude is not comparable, but the
direction corroborates.

Neutrals, by contrast, match almost exactly. On the white sign in the same frame:

| Source | R/B | G/B |
|---|---|---|
| Orion | 1.108 | 1.040 |
| Sony JPEG | 1.104 | 1.020 |

So white balance and the matrix's row-normalisation are correct. The error is
confined to saturated colour.

### What was ruled out, in order

1. **A persisted grade.** No sidecar existed for the file. This is the pure
   default render.
2. **An unknown camera body.** Same ILCE-7M3 as the two existing samples, so the
   same matrix path that renders the night frames acceptably.
3. **White balance.** `DevelopPipeline` applies
   `asShotMul * want / asShotRef`, which reduces algebraically to the camera's
   own `cam_mul` when the sliders are untouched. Corroborated by the neutral
   match above.
4. **The display transform.** AgX was bypassed entirely — a temporary path doing
   plain Rec.2020→Rec.709 plus an sRGB encode. R/B moved 0.573 → 0.554. The
   violet is already present in the linear working data.
5. **Sensor clipping.** `orion-rawstat` (written for this, now a permanent tool
   in `apps/rawstat/`) reports **0.00% of samples at the clip ceiling** in every
   CFA channel over the sky region. `linearize` clips to a common level after
   white balance (decision #29), so this was a live hypothesis — a clipped blue
   channel produces hue shifts that look exactly like profile error. It is not
   happening here.
6. **The matrix arithmetic.** Recomputed independently in Python from the
   published ILCE-7M3 matrix: invert `cam_xyz`, compose with XYZ→Rec.2020,
   row-normalise. Reproduces Orion's behaviour — for plausible blue-sky camera
   values it yields G−R of only +0.04 to +0.065 while mapping neutral to neutral
   exactly. The code is doing what the standard construction says.

---

## 2. The diagnosis

Orion applies **only the 3×3 colour matrix**. A camera profile has more parts;
the DNG specification defines them:

| Part | Orion | What it does |
|---|---|---|
| ColorMatrix (3×3) | ✅ | camera RGB → XYZ, linear |
| ForwardMatrix | ❌ | better-conditioned, dual-illuminant |
| **HueSatMap** | ❌ | **3D LUT: per hue/sat/value, a hue shift and saturation scale** |
| LookTable | ❌ | creative look on top |

No consumer sensor satisfies the Luther–Ives condition, so **no fixed 3×3 can be
colorimetrically correct across all spectra**. The error is worst for saturated,
spectrally narrow stimuli — deep blues especially. A least-squares matrix will
place deep blue between violet and magenta *while being the best available
matrix*. HueSatMap is the term that exists to correct exactly that residual, and
it is the part Orion lacks.

This was already recorded in `research/UNSOURCED.md` under "not unsourced, just
absent — DCP camera profiles: we use only the 3×3 matrix, no ForwardMatrix or
HueSatMap." It had never been connected to a symptom.

---

## 3. What I got wrong, and who corrected it

Two outside AI reviews (a Claude instance and a Fable 5 instance) were run
against my write-up. Both found real errors. Recording them because the
corrections are more useful than my original position.

### 3.1 I aimed the fix at the wrong step

I proposed shooting a ColorChecker and fitting a profile, and claimed it would
*"fix every hue, not just sky"*. **Wrong.** The CC24 blue patch is a reflective
sample nowhere near as saturated as zenith sky. Fitting to 24 reflective patches
and extrapolating to a saturated spectral stimulus is precisely the regime where
a 3×3 fails. A ColorChecker improves the **matrix** (step 3); purple skies live
in the **hue twist** (step 4). My plan would have cost a weekend and left the sky
violet.

### 3.2 I was too generous to "colorimetric is correct"

I offered "stay colorimetric and accept violet" as a defensible option. The
developer's response was the right one: *"The sky clearly isn't purple when I
look up, so why should I accept that?"*

Two different violets were being conflated:

1. **Genuine violet-leaning tristimulus.** Rayleigh scattering goes as λ⁻⁴,
   which peaks in violet; clear zenith sky really does sit further toward violet
   in absolute tristimulus than people expect.
2. **Matrix approximation error** on a saturated narrow-band stimulus, which
   happens to push the same direction.

**Orion's violet is mostly (2).** Calling it "colorimetrically faithful" dressed
an approximation error up as rigour.

And the deeper point: a raw renderer's job was never tristimulus match. A
10,000-nit sky cannot be reproduced on a 500-nit display in a dim room — the
adaptation and surround differ, so *appearance* match and *tristimulus* match are
different targets. That is why colour appearance models exist. **Rotating
saturated blue toward cyan is a partial appearance correction, not a "look."**
Sony and Adobe both do it because it is closer to what the photographer saw.

### 3.3 Provenance of LibRaw's matrices — an open question

LibRaw's `cam_xyz` table descends from dcraw's `adobe_coeff`, whose matrices were
widely understood to have been extracted from Adobe DNG Converter. If so, Orion
already ships Adobe-derived colour data while believing its stack is clean. This
is the same position as darktable, RawTherapee and every other dcraw descendant,
and unremarkable for an open-source project — but it is *already in the binary*,
which makes it more urgent than the `.dcp` question it was raised alongside.

Recorded as **decision #45, explicitly OPEN**, not settled.

### 3.4 Smaller catches

- Any DNG implementation distributed under Adobe's royalty-free patent grant
  must carry a **notice crediting DNG technology licensed from Adobe**. Needs to
  be in the about box before any DNG code ships. (decision #44)
- LibRaw is dual **LGPL-2.1 / CDDL-1.0**. Under CDDL, dynamic linking is not
  mandatory — a real escape hatch if static linking is ever needed for
  packaging. (decision #43)
- Recent DNG SDK downloads (1.7.1) ship without licence terms in the archive.
  Pin a version whose archive contains the EULA, if the SDK is used at all.

---

## 4. Licensing, settled

The developer settled the open business-model question during this thread:
**Orion is open source; paid distribution is out of scope.** That closes
STATUS's blocked item #2.

The existing rule — *avoid GPL, dynamically link LGPL* — is kept anyway. It costs
nothing today and it is what keeps the decision reversible.

| | GPL | LGPL (dynamic) | MIT/BSD/Apache |
|---|---|---|---|
| Open source (chosen) | ✅ | ✅ | ✅ |
| Source-available, paid binaries | ❌ | ✅ | ✅ |
| One-time purchase, closed | ❌ | ✅ | ✅ |

Current stack is clean under all three: LibRaw (LGPL/CDDL, dynamically linked),
lcms2 (MIT), OpenColorIO (BSD-3), SQLite (public domain), Slang (permissive),
lensfun (LGPL-3, planned). exiv2 avoided (GPL, decision #10). darktable and
RawTherapee never copied.

### Profile data specifically

- **Adobe's bundled `.dcp` files** — no published redistribution grant. Projects
  that derive from them instruct users to generate their own copies rather than
  share outputs. **Do not ship.**
- **RawTherapee's bundled profiles** — user-submitted, no stated licence of their
  own, inside a GPL-3 project. **Do not ship.**
- **The DNG specification** — royalty-free patent grant. Writing a DCP *reader*
  is fine, with the attribution notice above.
- **Profiles we generate ourselves** — ours. A profile is not a derivative work
  of the tool that made it, the same reason GCC's output is not GPL. DCamProf
  (GPL-3) can therefore be used to *produce* a profile without its licence
  attaching to the result; only its code must stay out of Orion.

Not legal advice. The developer's position is that this is moot while the project
is open source and unpaid.

---

## 5. The decision that was made, and how it is falsifiable

**Target: R/B ≈ 0.45, G/B ≈ 0.69 in this sky** — the Sony/Apple consensus.
Concretely, pull red down about 20% and lift green about 8% in saturated blue.

The argument for it is not "match Sony." It is that **two implementations built
independently landed within 0.017 of each other**, and a third moved the same
direction. That convergence is evidence of a perceptual correction rather than
one vendor's taste. If Orion sits outside the range every other renderer occupies,
Orion is the one that is wrong.

Taking the **hue rotation only** — not their saturation, not their contrast — is
what keeps this from making Orion a Sony clone. It matches where the hue lands,
not how the picture is graded.

### Implementation, chosen by judgement

A **HueSatMap-shaped node**, not a blue-only special case:

- it is the published shape from the DNG spec, so it is citable in `research/`
- any real profile drops straight into it later
- a hardcoded blue hack is exactly the sort of thing that quietly becomes
  permanent

It will be populated **minimally** — one deliberate, documented rotation fitted
to the target above — rather than pretending to be a full profile.

### Stated limitation

The fit comes from **one frame and one hue region**. It will fix skies. Every
other hue is unvalidated until there is a colour target shot or more frames, and
that will be written down where the correction lives rather than discovered
later.

---

## 6. Still open

1. **What blue sky should look like is now a design decision Orion owns.** The
   target above is measured and corroborated, but the moment Orion generates its
   own hue twists it is choosing an appearance. That deserves to be written down
   deliberately rather than tuned until it stops looking wrong. Currently the
   argument is "three renderers agree"; that is a reasonable anchor and not a
   principle.
2. **Camera spectral sensitivity functions.** The most interesting unexplored
   suggestion: published SSF datasets (e.g. Jiang et al.) cover several dozen
   bodies. With measured SSFs, a matrix can be optimised against *chosen spectra*
   — including real daylight sky — rather than being limited to what a reflective
   chart can show. Not chased: this session exhausted its web-search budget
   (200/200).
3. **A ColorChecker is still worth having**, reframed. It buys a defensible
   neutral axis and skin tones, which matter across far more images than sky
   does. It is no longer on the critical path.
4. **Dual-illuminant interpolation.** Orion uses a single matrix. LibRaw's table
   carries one matrix per body, so interpolating ColorMatrix1/ColorMatrix2 by
   estimated CCT is not available without a real profile source. Systematic error
   at every temperature that is not the one the matrix was built for; small at
   5130 K against D65, but unquantified.
5. **The frame is also ~1.3× darker** than the camera JPEG in the midtones (white
   sign 0.722 vs 0.958). Separate from hue, tracked as task #18. Some gap is
   correct — AgX is a neutral scene-referred transform and Sony's JPEG carries a
   creative look — but this much reads flat on first open, and the 1.15 base
   contrast was calibrated on one *night* frame using two summary metrics this
   session twice caught cancelling.

---

## 6b. The related complaint: "shadows colours everything"

Raised separately by the developer in the same thread, about the grading shadow
wheel. It was already fixed once this session — the zones were partitioned on
*linear* luminance at 0.0/0.5/1.0, which weighed middle gray 0.70 shadows; they
are now Gaussian bands on `log2(Y/0.18)` at −2.5 / 0 / +2.5 EV. It kept
happening anyway.

Measured on `_PIC8095.ARW`, sky patch, shadow wheel hard over and nothing else:

| Exposure | sky R/B plain | shadow-graded | shift |
|---|---|---|---|
| EV 0 (default) | 0.573 | 0.524 | **8.5%** |
| EV +2 | 0.734 | 0.729 | **0.7%** |

**The wheel's grip on the sky falls 12× when the frame is exposed up two stops.**
So the zones are placed correctly. What is wrong is where the picture sits: at
Orion's default rendering this daylight sky lands about half a stop *below*
middle gray in scene-linear terms, so it legitimately collects roughly 20% shadow
weight. Solving for the measured 12× ratio against the band geometry puts the sky
near EV −0.5, which is where the maths says it should behave that way.

This is not a second defect. It is item 5 above — the base render being ~1.3×
darker than the camera's own JPEG — surfacing as a grading complaint. Correct the
exposure the file opens at and the spill goes with it.

Worth stating because it is the second time in one session that a symptom was
filed against the wrong subsystem: the purple sky looked like a white-balance
problem and was a profile gap; this looked like a grading problem and is an
exposure default. Both were only separable by measurement.

---

## 7. Process finding

**The entire test corpus was two night frames.** Nothing in either suite, the
bench, or a full senior review ever exercised a bright daylight sky. The violet
survived all of it because no measurement was ever pointed at one.

`_PIC8095.ARW` and its camera JPEG are now the third sample and belong in the
bench corpus. The pattern is the same one that recurs throughout this repository:
the code was fine wherever it was measured.

---

## Tools written during this investigation

Both are permanent, in the repository:

- **`apps/rawstat/`** — per-CFA-channel raw statistics for a region: black,
  white, as-shot multipliers, raw mean, peak, and the fraction of samples at the
  clip ceiling. Written to rule out clipping; kept because a colour question
  should start at the sensor.
- **`scratchpad/pixstat.swift`** — mean sRGB over a normalized region of any
  image ImageIO can read, with EXIF orientation applied. Used for every
  cross-renderer comparison above. Not yet promoted into the repository.

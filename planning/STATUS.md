# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-07-30 (**landing page polish**; engine story before it: multi-selection in the filmstrip)
**Phase:** M0 done. M1 ~98%. M2 and **M3 complete**. **`research/masking.md` is
finished except sky** — primitives, groups, guided refinement, a raster
component, Vision filling it, and now a band on brightness. Six mask kinds. A mask is a *list* of components
folded per §6 (add/subtract/intersect), optionally feathered onto the
photograph's own edges, through the graph, the POD facade, the panel rows, the
sidecar, undo and the bench.

⚠️ **M3 is done — do not rebuild it.** Dehaze, creative LUTs, exposure fusion
and auto-enhance all shipped with research files, GPU tests and bench probes
(sessions `2026-07-28e` through `2026-07-29d`, and the cost table below). A
stale kickoff prompt naming those four has now arrived **five** times; the
answer each time is that they exist.
**Next story:** **sky masks** — and `research/masking.md` §5 is explicit that
Vision cannot produce one, so it is a model question (which model, under what
licence) before it is a code one. That is the thing to settle first.

⚠ **Nothing is reported and nothing carried forward loses work.** The gap table
below is down to three items, all of them either cosmetic or named-and-costed.

**Suites:** `orion-tests` **522 checks** · `orion-viewport-tests` **3390
checks** · **29 `repro/` scenarios, 143 checks** · all 0 failures. Bench exits 0
on all three frames: M0 gate **10.30 ms p95**, 127 nodes, 6427 MiB — plus a
preview graph at 1/16 that, about 400 MiB.

### Known gaps, carried forward

Small, named, and none of them blocking the next story:

| Gap | Where |
|---|---|
| **A matte is not saved with the photo.** It is a raster and the sidecar holds parameters, so reopening leaves a Subject or Person row empty until it is run again. Said out loud in the panel rather than left to be discovered. ⚠ It only *became* true this session — see below | `Sidecar`, `DevelopPanels` |
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand | `SubjectMatte` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **101 commits carry `Co-Authored-By` / `Claude-Session` trailers.** Developer approved stripping them; needs a history rewrite and a force-push to a public repo | whole history |

⚠️ **`samples/_PIC8095.ARW` has people in the plaza at its base.** Fine as a test
frame, but it must not be used for any published render — the landing site's
imagery was screened for this and twelve frames were rejected.

## Session 2026-07-30k — landing page polish (web/ only, no engine work)

Seven small fixes, all verified in a live browser: the reveal failsafe no
longer marks everything revealed 4 s after load (it now fires only if
IntersectionObserver never delivered — the old blanket `showAll` killed every
below-fold entrance for a reader who paused before scrolling); the hero
preload carries `imagesrcset`/`imagesizes` so small screens stop downloading
both the 2400w and 1200w files; the finder's EXIF theatre (`#vf`) is
`aria-hidden`; a fixed "Download alpha" chip surfaces after the hero on the
frame-counter's cue and bows out when the close's own CTA arrives; the
ledger's "written down too, in public" now links to `research/` on GitHub;
`SoftwareApplication` JSON-LD added; dead CSS removed (`.eyebrow`, `.mnote`,
`.hud__cue`, `.ledger em`).

## Session 2026-07-30q — independent layers

⚠ **Twenty-fourth arrival of the stale M3 prompt.** Not re-litigated.

Decision #75's stage 2, executed against the decomposition costed last session.
The subject can be graded one way and the sky another, in one render.

### A layer is a run of components

There is no separate layer list, because the row list already is one. Rows in a
run fold together into a single coverage and share one set of adjustments; a
break starts a new coverage with its own. One bool per component — no schema
restructure, and reordering keeps working because the grouping is read from the
rows rather than stored beside them.

⚠ **The layer index is derived, never stored.** Which layer a row belongs to is
how many breaks precede it, and that moves whenever a row is added, removed or
reordered. A stored index would be a second copy of the grouping and the two
would disagree the first time a row moved.

### ⚠ The constraint that shaped it

The graph is static, so a layer's coverage cannot be a node picked per render.
`develop:linear` binds **all four** component slots and `layerMask[L]` says
which one ends each layer. That is also why there are now **four refine chains**
rather than one — a layer's coverage is whichever component ends its run, so
every slot needs a chain and the parameter picks.

**Predicted 148 nodes and ~6565 MiB. Measured 148 nodes and 6878 MiB.** The node
count was exact; the memory ran 313 MiB over, because the estimate counted the
refine outputs and forgot each chain's own subsampled coefficient textures. M0
gate 9.70 → **10.85 ms**, still well inside 16.

### Three bugs on the way, all mine

- ⚠ **The kernel's `startsLayer` was never pushed.** The flag existed end to end
  — struct, facade, Swift, shader — and `apply` never wrote it into the
  parameter block, so the fold never restarted and every layer read one merged
  coverage. The scenario caught it; nothing else could have, because every unit
  test drives the kernel directly with parameters it sets itself.
- ⚠ **`1...upTo` traps when `upTo` is zero.** Selecting the first row crashed
  the process outright — Swift traps on an invalid range rather than yielding an
  empty sequence. Exit 133, no output, no message.
- ⚠ **The first scenario asserted a threshold picked by eye** (`left < 0.35`)
  and failed at 0.42 against a working stack. The claim is that two layers move
  in *opposite directions at once*, which is a statement about each region
  against its own unmasked value. Seventh time in this file that a first-draft
  check measured something other than its claim.

### What the scenario pins

Two radials over opposite sides, one pulled down two and a half stops and the
other lifted one and a half, **in one render** — which a single shared
adjustment cannot do. Then each layer's grade reaching only its own coverage,
both ways round, and the region between them bit-identical throughout.

Three mutations dead: ignoring the layer flag, pointing every layer at the last
coverage, and giving every layer layer-0's adjustments.

### Deliberately unchanged

The **four-component cap**. Layers do not change it: four components split
across four layers is the same 184 MiB as four in one group, because the cap is
a memory number. Per-layer clarity, dehaze and fusion stay refused (#75), and so
do blend modes over rendered frames.

## Session 2026-07-30p — the oldest gap closes, and stage 2 is costed rather than started

⚠ **Twenty-third arrival of the stale M3 prompt.** Not re-litigated.

### ⚠ Per-layer adjustments: costed, and deliberately not begun

Everything was in place for #75's stage 2 — #76 says what a layer carries, #77
means N layers need no new controls. So it was scoped properly first, and the
scoping is the reason it was not started.

**The constraint that shapes it:** the graph is static. Which component *ends* a
layer is a runtime property, so a layer's coverage cannot be a node picked per
render. The way through is that `develop:linear` binds **all four** component
outputs and a per-layer parameter says which index carries that layer.

The cost, measured rather than guessed: **four refine chains instead of one**
(+21 nodes), four coverage bindings, ~30 more floats of parameters, a sidecar
schema version, and layer boundaries in the row list. **127 nodes → 148**,
6427 MiB → about **6565 MiB**. Both affordable.

It is also ten files of coupled change, and starting it here would have left it
half-built — which this file has recorded as the wrong move twice
(`degrade-then-refine`, and the crop preview). The decomposition is in
`ROADMAP.md`, in order, with what must **not** be done along the way: raising
the four-component cap, per-layer pyramids, or blend modes over rendered frames.

### The brush has a bench probe, six sessions late

The oldest carried-forward gap in this file. A stroke is uploaded out of band,
so neither `Adjustments&` hook could reach it; the `prepare` hook added for the
matte probe has been sitting unused by anything else since.

⚠ **The dabs run on a diagonal**, not along an axis: they go through
`mask::toFrame` on the way in, so a stroke across the frame exercises the
transform as well as the kernel — an axis-aligned stroke still lands correctly
under a transform that had dropped or swapped a term.

Measured 0.205, 0.199 and 0.147 of reference; floor at half the smallest.

### ⚠ And the number the probe existed to find

**A 120-dab stroke costs 110–138 ms to render.** The kernel loops every dab at
every pixel with a bounding-square reject, so the cost is linear in the stroke's
length — a long stroke is not free the way a gradient is.

Degrade-then-refine hides it while the hand is moving, since the preview graph
has a sixteenth of the pixels; what this measures is the full render on settle.
Recorded rather than acted on: it is inside the budget today, and the answer if
strokes get longer is a bounding box per block of dabs rather than a faster
inner test. At the 16,384-dab cap this loop would be unusable.

That is what an unprobed control costs — the number was unknown for six
sessions, and nothing in the suite would have noticed it getting worse.

## Session 2026-07-30o — one catalogue, and the order made visible

Asked: *"Is there no way to have layers in the settings menu? Everything applied
before masking, then the masking section, then everything after. I want it clear
what all you can use, and I want those reusable so we aren't wasting UI
components or having different looks and feels."*

Two asks, and both were fair.

### The duplication was real, but not where it looked

There is already **one** `AdjustmentSlider`, used by all fifty-one controls, so
the look never differed. What was duplicated is the *description*: every control
was hand-listed at its call site with its range, unit, decimals and default —
fine for one panel, and not fine the moment the same adjustment appears twice.
Giving a mask more than exposure had just made the Local section a second copy
of the Light section's list with different numbers.

`AdjustmentCatalogue` is the one copy. `AdjustmentGroup` renders any subset in
any scope, so the global panel and a mask's panel are **the same specs through
the same view** and cannot drift in look, behaviour, or in what they offer.

⚠ **The binding table is a `switch` with no `default`.** Adding a case to
`AdjustmentID` without binding it is a compile error rather than a control that
draws nothing — which is exactly how `lutStrength` once shipped a dead slider.

### ⚠ Stage belongs to the scope, not to the adjustment

The first version put one `stage` on each spec, and it was wrong on the control
that matters most for the question being asked. **Global contrast runs in
`develop_display`, after the mask, on the combined result; local contrast runs
inside the mask node.** The same named control sits on opposite sides of the
mask depending on which is meant, and a single stage would have made the
ordering readout state one of them falsely.

Caught by writing the readout, not by reading the code. Pinned now.

### What the panel says now

The Local section is generated: the adjustments that *can* be local, then —
below the mask's own controls, because it is reference rather than control — a
**Where the mask sits** readout built from the same table, and a list of what a
mask cannot reach **with the reason for each**:

| Refused | Because |
|---|---|
| Temperature, Tint | applied before the demosaic — a local one means demosaicing twice |
| Highlights, Shadows | read the guided-filter chain, which runs once for the frame |
| Whites, Blacks | an endpoint, and an endpoint per region is not an endpoint |
| Clarity, Dehaze, Lift | 16–32 node pyramids; §2's rule is not even defined for them |
| Look | applied by the display transform, after the mask |

⚠ Generated rather than written out, so a control that stops being local-able
says so the day it changes — and a test fails if any refusal loses its reason,
because a refusal without one is just a control that has gone missing.

### Measured

Eight new checks in `orion-viewport-tests`, on the catalogue rather than on the
view. The load-bearing one asserts the catalogue's local set **equals the set
the shader actually applies**, written out rather than derived — deriving it
from the table it is checking would prove nothing. Three mutations dead:
claiming white balance is local, silently dropping a local control, and a
refusal losing its reason.

### ⚠ The first layout was wrong and the screenshot said so

The refusals went directly under the local sliders, where seven lines of prose
sat between a mask's adjustments and the mask's own geometry. Reference material
goes after the controls.

## Session 2026-07-30n — a mask does more than exposure

⚠ **Twenty-first and twenty-second arrivals of the stale M3 prompt.** Not
re-litigated.

The developer's standing complaint: *"I should be able to colour grade and do
all of that editing on a different mask instead of just doing certain presets."*
A mask could change exactly one thing — the local exposure — since masks
existed.

⚠ **Taken before stage 2 of the layer plan, and deliberately.** #75 staged N
layers first and widening the op set second; the reverse is the right order,
because it defines **what a layer is** before multiplying layers. It is also far
lower risk: no schema restructure, no graph change, and it delivers the actual
complaint today.

### `research/masking.md` §2b — what a mask may change

The test is whether the adjustment is a **function of the pixel alone**. §2's
rule is that the coverage scales the *parameter*; that is only well defined
pointwise. Four pass: exposure, contrast, saturation, and a colour cast.

⚠ **White balance cannot be local, and the reason is structural.** Temperature
and tint are applied in `linearize`, at the head of the graph, **before the
demosaic** — because the demosaic interpolates white-balanced data and the level
a channel clips at moves with its multiplier. A local white balance means
demosaicing the frame twice and choosing per pixel: not an adjustment, a second
pipeline.

So the panel offers **Warmth** and **Tint**, which are a pointwise colour cast,
and they are named differently from the global Temperature and Tint on purpose.
Calling them the same thing is the kind of lie only discovered when someone
tries to neutralise a cast with one and finds it cannot.

The pyramid operators are refused for the reason #75 records: not pointwise, so
§2's rule does not even define what a half-applied Laplacian decomposition
*means*.

### Two things caught in my own code

- ⚠ **The cast's luminance renormalisation did nothing.** It read the luminance
  on both sides of the multiply from the same already-cast colour, so the ratio
  was one. A line that looks like it is doing the work and is not — caught by
  writing the GPU check for it, and the mutation that reinstates it fails two.
- ⚠ **The scenario asserted the wrong ordering.** Saturation runs *before* the
  cast, so desaturating to grey and then casting gives a warm grey, not a grey.
  The first draft demanded the opposite and failed against a correct shader.
  Sixth time in this file's history that a first-draft check measured something
  other than its claim.

### Measured

10 GPU checks against exact numbers, not magnitudes. ⚠ The load-bearing one is
that **zero coverage is bit-identical**: every other check says "it moved", and
only this one says it moved *where the mask is*. Four mutations dead, including
scaling the result rather than the parameter, and pivoting contrast at zero
instead of at the display transform's −2.5.

Bench probe on **chroma rather than luma**, and that is the probe's point: the
cast is renormalised so it does not move brightness, so a luma floor would read
zero on a working control. 0.159, 0.210 and 0.049 of reference across the three
frames. ⚠ The daylight frame moves a third of what the night ones do — it is
already the most saturated, so a cast has proportionally less room — and a floor
calibrated on the dark frames alone would trip on it, which is the mistake
`DECISIONS.md` #47 records paying for twice.

M0 gate unmoved at 9.70 ms; the local set is four more terms in a node that was
already running.

### Still not done

**Stage 2: N independent layers.** One group with one adjustment set today, so a
subject can be graded — but not the subject one way and the sky another at the
same time. That is the next engine change, and #75 has its shape.

## Session 2026-07-30m — the brush erases, and a log that is a scenario

⚠ **Twentieth arrival of the stale M3 prompt.** Not re-litigated.

Four reports from the developer using the alpha, and one durable ask.

### The log, which is the durable one

*"Create logs so that I can do something, mess it up, and then ask you to
diagnose, create scenario, replicate, and fix."*

`app/InteractionLog.swift` writes **`Scenario.swift`'s own grammar**, so a log
*is* a reproduction: drop it in `repro/` and run it. File menu → Reveal Session
Log. One line per *committed* edit, taken where undo counts, so a slider drag is
one line and not sixty.

⚠ **What changed is found by diffing `DevelopState`**, not by calling a logger
from forty places — a field added to that struct is logged the day it is added.
The state deliberately *outside* `DevelopState` (the compare split, the overlay,
the tab) is recorded by its call sites, and that short list is the one part that
can rot. It is named as such in the file.

Two bugs in the log itself, both found by replaying it:

- A component created **and** a slider moved in one commit emitted only the mask
  line and dropped the slider. A missing line makes a replay diverge silently
  from the session it claims to reproduce — the one failure this must not have.
- `Int32 != Int32?` is always true, so every session grew a spurious `mask none`.

### The bug it was built for

**Compare could not be dragged after touching a mask.** `MaskOverlay` takes
`contentShape(Rectangle())` — the whole canvas, which it needs, since dragging a
radial's body or painting can start anywhere — and it sat **above**
`CompareOverlay`. Any live mask swallowed the divider's press.

Compare goes on top now. It claims a 28-point strip, so every press outside it
still falls through to the mask; reordering rather than disabling, because
editing through a split is a thing people do.

### The brush erases

*"There should be some kind of brush where I can add and subtract to the mask."*

⚠ **Polarity travels with the dab, not the stroke.** One component accumulates
every stroke ever laid on it, so the component has to remember which of its dabs
added and which took away. It rides in the dab texture's third channel —
RG32Float to RGBA32Float, 256 KB a component against 128.

⚠ **Erase is destination-out**, `a -= cov·a`, the exact inverse of paint's
source-over `a += cov·(1−a)`. So painting takes the alpha a fraction of the way
to one and erasing takes it the same fraction of the way to zero, and the two are
reversible against each other. Subtracting the coverage outright would drive the
alpha negative wherever a slow hand lingered, and the `saturate` at the end would
hide that as a hard hole in a soft brush. Three mutations dead, including that
one.

⚠ **Migration: a parallel array, not a third interleaved number.** `brushStroke`
is a flat list of floats in the sidecar; re-interleaving it would read every
stroke saved before erasing existed as garbage — silently, because a scrambled
stroke is still a valid stroke. `brushErase` is absent in those files, which
means "paints throughout", which is what they mean.

The scenario runner's `brush` verb now **appends** to the stroke already there,
as the overlay does, which is what makes a second pass build on the first and is
the only way to script painting and then erasing over it.

### Also, from the same reports

- **One Add menu**, grouped by how the mask decides what it covers: Draw (placed
  by hand), Detect (a model), Match (measures the pixels). It was three controls
  for one act. Subject and Person stay *actions* — choosing one adds the row and
  runs the model together, so there is still no way to select into an empty
  matte.
- **An eye on every row.** Hidden is a *disabled node*, not a zeroed coverage: a
  component's node takes the fold-so-far as its first input and a disabled node
  resolves to its first input, so hiding skips it exactly and costs nothing.
- **Size, Flow and Hardness already existed.** ⚠ There is no separate *opacity*
  and the panel now says why: Flow is per-dab buildup, not a ceiling, so
  overlapping passes build toward full coverage. A ceiling would need the kernel
  to track a per-stroke maximum; Erase is the honest way back down.

### ⚠ The layer question, answered and staged rather than started

Asked: should every edit be per-mask, with a master layer and blend modes?

The arithmetic decides it. **Pointwise adjustments can be per-layer** — exposure,
contrast, highlights/shadows, whites/blacks, vibrance, saturation, grading, the
mixer, the curve. **Clarity, dehaze and exposure fusion cannot**: they are 16–32
node pyramids each, and N copies destroys both the node count and the 6.4 GiB
cache that makes a drag 10 ms. The honest version of "clarity on a mask" is to
render the pyramid **once** globally and let each layer blend toward the input by
α × amount.

The shape: bake each layer's folded coverage to one R16F (46 MiB), one fused pass
loops over layers per pixel. **Eight layers ≈ 370 MiB**, under 6% of budget, and
the per-node cache survives — touching layer 3 does not recompute 1 and 2. The
existing parameter-scaling rule generalises: exposure collapses to `2^(Σ αᵢEᵢ)`,
order-independent. What must be **refused** is per-layer blend modes over
*rendered frames* — blending two tone-curved images is a different operation
needing N framebuffers. Layer opacity is a scalar into α.

Staged: (1) the shell — eye, `+` menu — **done this session**; (2) N
exposure-only layers with baked coverage and the fused pass, which is the
load-bearing engine change; (3) widen to the pointwise whitelist; (4) optionally
the masked blend of the single global pyramid.

## Session 2026-07-30l — a spot is a thing you drag

⚠ **Nineteenth arrival of the stale M3 prompt.** Not re-litigated; verified
against the tree three times in this session's own history.

Taken ahead of the sky story this file names, because sky is blocked on a model
question and this was reported by the developer using the alpha: *"spot and heal
should be like a dragable thing."*

### What was actually wrong with it

A spot was a **click**. It placed a disc, chose the source for you — one radius
and a bit to the right, or downward if that ran off the frame — and then both
were invisible. No handles, no way to move either, no way to say "take it from
*there*". The only correction available was Undo spot.

That is the wrong shape for the tool: healing a blemish is a judgement about
where the replacement comes from. The automatic source survives as the starting
position, because a click that immediately does something sensible is worth
keeping — it is a first guess now rather than the whole answer.

### ⚠ The story needed a transform that did not exist

A spot is stored in **frame** coordinates — dust is on the sensor, so it follows
the subject through a crop and a turn. Drawing one therefore needs the transform
the *other way*, and the program had only `toFrame`.

`mask::fromFrame`, and the risk in it is not the algebra, it is the **order**.
`toFrame` goes crop, then straighten, then turns; the inverse must go turns,
then straighten, then crop. Applying the three in the forward order with negated
angles is the mistake that looks right — and it is *exactly equivalent* whenever
at most one of the three is doing anything, which is every case anybody checks
by hand. So every test case turns on at least two at once, and the mutation that
reuses the forward order dies on six of them.

⚠ A round trip is also not enough on its own: two transforms each wrong in
mirrored ways round-trip perfectly. That is the trap `MatteGeometry.undoTurns`
had. One case is pinned against a hand-computed answer as well.

### Where the geometry lives

Hit-testing is in `CanvasLayout`, tested without a window, and the overlay draws
what it is told — the rule `MaskOverlay` already follows. Four mutations dead,
and two of them are about the ordering of a hit test:

- ⚠ **The source wins where the two discs overlap.** They overlap constantly at
  any useful radius, whichever is tested first is the one that can always be
  grabbed, and the source is the one with no other route to it — a destination
  can also be dragged by its body. Test destinations first and the source
  becomes unreachable exactly when the spot is large.
- ⚠ **Later spots beat earlier ones**, matching the draw order. Otherwise
  placing a spot on top of another makes the new one — the one being looked at —
  the only one that cannot be adjusted.
- **The handle has a floor of 11 points** though the disc it draws does not. Dust
  is *supposed* to be small: the size slider goes to 0.004 of the frame, about
  three points at fit zoom, and a three-point target cannot be hit.

### A bug the scenario found in the history layer

`EditHistory` coalesces consecutive entries carrying the same label — which is
what makes a slider drag one undo step instead of sixty. Placement and a later
move were **both labelled "Spot"**, so they merged, and undoing a move deleted
the spot. Two different acts need two different names; a move is "Move spot"
now.

The place-and-drag gesture is still deliberately *one* entry: `addSpot` has
already recorded, and the overlay skips its commit while the spot whose source
it is dragging is the one it just created.

### Also caught

The old click-to-place path was still in `ImageCanvas`. With the overlay
mounted, two handlers for one press is how a single drag places two spots — it
is gone, and the panel's copy describes the gesture that exists rather than the
one that used to.

⚠ And `contentShape(Rectangle())` on the overlay would have made it swallow
every press on the photograph — pans, the eyedropper, everything — for as long
as one spot existed anywhere in the frame. The hit region is the discs
themselves, plus the whole picture only while the tool is armed.

## Session 2026-07-30k — colour range masks

⚠ **Eighteenth arrival of the stale M3 prompt.** Not re-litigated; the evidence
table has been produced twice in this session's own history.

`research/masking.md` §4c, written before the code. §4b had deferred this
explicitly — "choosing between them is a decision worth its own session" — and
the answer was **neither of the two candidates it named**.

### ⚠ Neither CIE76 nor CIEDE2000. Oklab, on chromaticity only.

Three arguments, and the third is the one that decides it.

- **CIEDE2000 is scoped to small differences.** Fitted against near-threshold
  datasets, ΔE ≲ 5. A photographer dragging a tolerance works at 10–40, outside
  its validated range — and any *monotone* miscalibration is absorbed by the
  slider anyway, because the person is closing the loop with their eyes.
- **CIELAB's shape is wrong exactly where it matters.** Its blue-to-purple hue
  bend is long documented, and sky is the first thing anyone reaches a colour
  mask for. So CIE76's flaw bites and CIEDE2000's virtue does not.
- **CIEDE2000 is discontinuous.** Its mean-hue handling has genuine jumps
  (Sharma, Wu & Dalal 2005). A discontinuous distance feeding a smootherstep
  prints a *seam* across a smooth sky gradient. §4b already argued C² matters
  more for a range mask than for a gradient; that disqualifies it outright,
  before any argument about line count.

Oklab (Ottosson 2020, and normatively W3C CSS Color 4 as `oklab()`) fixes the
failure that matters at plain Euclidean cost.

### ⚠ The identity the whole design rests on

Oklab's nonlinearity is a **pure cube root** — no linear toe, no division by a
white point — so scaling the input by k scales L, a and b uniformly by k^⅓, and

>  **a/L and b/L are exactly invariant under exposure.**

Verified, not asserted: a sky patch at ×0.25, ×1, ×4 and ×64 gives
a/L = −0.081461 and b/L = −0.202005 at every one.

That is what makes the control well-defined on a scene-linear, **unbounded**
input. There is no honest `Yn` for a raw whose speculars run past 1.0, and every
grey anchor is a convention that would need defending. CIELAB cannot dodge the
question; Oklab makes it vanish.

So **lightness is excluded** and delegated to §4b's luminance band via intersect.
Two consequences, both wanted: a shade in shadow and the same shade in sun are
one colour, and every neutral collapses to the origin.

### The target is stored as RGB, and converted in the kernel

Converting once on the host would be a second implementation of the transform,
and this repository has been bitten by a duplicated colour transform twice. The
per-pixel cost is about twenty flops; what it buys is that the target and the
pixel cannot disagree about what Oklab is.

### Measured

12 GPU checks against an **independent CPU model** of the metric, not against
magnitudes. Five shader mutations dead: dropping the division by L (6 failures),
folding lightness into the distance (3), inverting the band's sense (6),
dropping the cube root (4).

⚠ **The sixth mutation survived, and it was the test's fault.** Deleting the
floor on L changed nothing, because every floor check compared the CPU oracle
against itself — and the oracle carries the same floor. The same shape as the
matte test's clamp: a check that cannot tell the code from its own stand-in. A
GPU-side case now asserts that two deep-shadow hues stay together, and the
mutation dies.

⚠ **And that case broke the one after it.** It re-uploads the reference texture
and did not put it back, so the *invert* check — the last in the function — ran
against a frame of near-black and passed for the wrong reason. A shared fixture
that one case mutates is a fixture every later case is quietly testing something
else against.

Bench probe: a **neutral** target, because the three sample frames share almost
no saturated colour and every photograph has near-neutrals. Measured 0.826, 0.226
and 0.221 of reference; the spread is the probe working, since the forecourt is
concrete and tarmac while the other two are not.

### ⚠ The photograph decided the repro, twice

The first scenario targeted the yellow Vantage and demanded the tarmac stay put.
Measured through this metric on a forecourt lit by sodium, the tarmac, the silver
car and the white building all sit within **0.10** of that yellow — everything
artificially lit shares a hue. That was a false assertion about the photograph,
not a defect. The target is the night sky now, which nothing else in the frame
shares (tarmac 0.217, car 0.314, foliage 0.555).

The second correction: an eight-percent patch of wet night tarmac is not one
colour — it carries reflections of the sky — so its mean legitimately moves when
the sky is selected. A region used to prove "nothing else was touched" has to be
uniform in what is being measured.

Also: the local exposure is **positive**, because negative exposure over a
near-black sky moves almost nothing. `mask-alignment.txt` records the same trap
from the other end.

### A bug found on the way, and committed separately

**A luminance range mask never survived reopening the photograph.**
`MaskComponentState` names its coding keys `Key` rather than `CodingKeys`, so
Swift synthesises the *encoder* from the stored properties while the decoder
reads the hand-written list — `rangeLo`, `rangeHi` and `rangeSoft` were written
to every sidecar and read back from none, from the session kind 5 shipped.

⚠ `DevelopState` had the identical defect and was fixed with a round-trip test.
That test could not see this one: **its fixture never filled the nested
component's range fields.** The lesson recorded then — a round trip is only as
good as the state it round-trips — applied to the outer struct and was not
carried into the inner one.

### Also this session: presets on their own tab, and a build

**Presets moved out of Light** — a hundred and twenty-three lines sitting in
front of the Exposure slider. Light opens on White Balance now. ⚠ The sixth tab
had the longest name of the six and `PRESETS` wrapped inside its plate; every
label is a point smaller and clamped to one line. Found by screenshotting, not
by reading.

**The export overlay guard finally has a test**, closing the oldest gap in the
table above. The runner gained `export` (through `Engine.export`, the call the
panel makes) and `identical`, which compares two files **byte for byte** — a
size comparison passes on two JPEGs that differ everywhere and compress alike.
Deleting the guard fails two of four.

**`v0.4.0-alpha.3` is published.** Version stays 0.4.0 per decision #64: M4 is
not closed while sky is open.

⚠ **Two claims in the previous release notes were stale and would have been
republished.** `8-bit TIFF` has been listed as a limitation since alpha.1 and is
false — exports are 16 bits per channel, checked with `sips` on this build
rather than trusted. The 256-dab truncation listed beside it was fixed a session
ago. A "known limitations" list is a thing that rots silently, because nothing
fails when it is wrong.

The dmg was verified the way the first one was: mounted, and run with
`build/shaders` and `data/lensfun` moved aside — **with the control**, which is
that the build-tree app fails on the same run. Without the control the test
proves only that something rendered.

⚠ And the landing page's version appears in **two forms** — `v0.4.0-alpha.3` in
the links and `0.4.0-alpha.3` in the JSON-LD `softwareVersion`. A replace over
the tag name misses the second, so the page advertised one version to readers
and another to anything parsing its structured data.

## Session 2026-07-30j — multi-selection in the filmstrip

The story this file has named for two sessions. ⚠ **Seventeenth arrival of the
stale M3 prompt**, answered with the table again — research file, shader, GPU
test section and bench probe for each of dehaze, creative LUTs, exposure fusion
and auto-enhance — and set aside.

### ⚠ One selected photo is not a selection

The whole feature rests on this. The photograph on the canvas is always in the
set, because every route to a new photo collapses the selection onto it — so a
selection of *one* is the resting state of the interface, not a decision anybody
made. `targets` therefore means **everything in view until there are two**.

The obvious rule — "act on the selection whenever it is non-empty" — makes
Export All silently export one photograph, for every user, on every folder, and
it would read as the button being broken. The alternative to counting is a flag
recording whether a selection was *deliberate*, which is a second piece of state
that can disagree with the first. Counting needs none, and the filmstrip shows
the count only once it means something, so what a batch will do is what the
strip says it will do.

### The bug the story uncovered, which is the part worth keeping

⚠ **Sync and batch export ignored the filter entirely.** Both read
`library.photos` — the whole folder — under a warning that said "every photo in
view" and a panel that offered to export "N photos". Cull to Rated, press Export
all, and every reject lands in the folder you just said was for the picks.

Two copies of "what this acts on" disagreeing, one in the code and one in the
sentence beside the button. `library.targets` is now the only answer to that
question and both callers ask it.

### Rules, and where they live

`PhotoSelection` is a pure value type in its own file — no AppKit, no facade
call — so `orion-viewport-tests` pins it without a GPU or a folder of raws. Same
split as `MatteGeometry` and `BatchExport`.

| Gesture | Does |
|---|---|
| click | selects one, opens it |
| ⌘-click | toggles, opens nothing |
| ⇧-click | range from the anchor, opens nothing |
| ⌘⇧-click | unions a second range |
| ⌘A / ⌘⇧A | all in view / back to the open photo |

⚠ **A modified click opens nothing.** Building a selection of forty frames is
not forty requests to look at one, and it would be forty raw decodes.

⚠ **The open photograph cannot be ⌘-clicked out of the set.** Its settings are
what a sync copies *from* and its panel is what is being read while the decision
is made; a sync that wrote the other thirty-nine and skipped it would be
indefensible, and nothing on screen would say so.

⚠ **A filter change confines the selection.** A selection is a set of URLs and
the filter is a view over a different list; nothing connects them unless
something does it on purpose. Without it, filtering to Rated and exporting
writes rejects the photographer cannot see, in a list they cannot check.

### Two marks, because there are two questions

The accent gate says *this is the photograph on the canvas*; a dimmer accent ring
says *this is in the set a batch will act on*. One mark for both would make a
forty-frame selection look like forty open photos. Rating and rejection follow
the selection from both the context menu and the Photo menu — two scopes for the
same key is how someone rates one frame from the menu and forty from the strip
and cannot say which rule they were under. ⚠ Rejection over a group is **set**,
not toggled, or a mixed selection flips into its own negative.

Modifiers come from `NSEvent.modifierFlags` rather than a stack of
`TapGesture().modifiers(_:)`: three gestures competing for one tap have a
resolution order, and getting it wrong fails silently — a command-click falling
through to the plain handler looks exactly like a plain click.

### Measured, and looked at

23 new checks in `orion-viewport-tests`, **six mutations, all dead** — including
the two that matter most: treating any non-empty selection as explicit (5
failures) and letting `confine` do nothing (2). Screenshotted rather than
assumed: the ring draws on the two chosen frames and the bar reads
`3 photos · 2 selected`.

⚠ **No repro scenario.** The runner drives `Engine`, `CanvasLayout` and
`TargetedAdjust`; it has no `Library`, and giving it one is its own story. The
rules are pure and fully pinned; the wiring is not, and that is stated rather
than papered over.

## Session 2026-07-30i — five defects, none of them reported

No story. The instruction was "fix the bugs", the report list was empty, and all
three suites plus fifteen scenarios were green — so the whole session was
finding things nobody had run into yet. **Four new scenarios, twelve checks,
every fix killed by reverting it.**

⚠ **Sixteenth arrival of the stale M3 prompt.** Not re-litigated.

### ⚠ The big one: degrade-then-refine only fanned out half its state

`Engine::setAdjustments` sends the adjustment block to both graphs and the
header says so in bold. **A brush stroke, a raster matte and a creative LUT are
not in `Adjustments`.** All three went through `developMutable()`, which is the
full graph and only the full graph — so the mask a photographer had painted
disappeared for the length of every drag and came back when they let go.

Worst on exactly what the preview exists for: local exposure *through* a brush
mask is a slider, so every tick of it rendered with no coverage at all.

This is the same shape as `matteDirty_` and `adj.exposureEv` before it — state a
kernel reads that lives outside the compared struct — arriving by a fourth
route. Not a missing dirty flag this time but a **missing recipient**. The rule
in this file wants widening: *anything that is not an `Adjustments` field needs
both a staleness answer and a delivery answer, and last session's architecture
added a second place for the second one to be wrong.*

`setWideOutput` is the one deliberate exception, and the reason is now at the
call site rather than in someone's head.

### A matte followed the graph instead of the photograph

`openRaw` keeps the compiled graph when the next frame has the same shape —
every frame of a folder from one camera — and `reload` re-pushed every parameter
block while leaving the two things that are not parameters exactly where they
were. So opening the next photo with a saved Subject row rendered **the previous
photograph's subject**: full coverage, right coordinate space, wrong picture.

⚠ Worth noticing that this file's own gap table asserted the opposite ("reopening
leaves a Subject or Person row empty"). It was true of the app, which re-sends
every stroke on open, and false of the engine, which is where the invariant
belongs. A claim that holds only because a caller happens to do the right thing
is not an invariant.

### The eyedropper read the wrong pixel on a cropped photograph

`sampleAt` returns two colours. The display one comes from the output texture
and was right. The scene one — which is what the colour-mixer band is derived
from — is looked up in the whole pre-geometry frame, and the code carrying the
point there undid the **quarter turn and nothing else**. Under a crop it read
whatever sat at that fraction of the uncropped frame: the yellow Vantage picks
band YELLOW at 58.8°, and through the bug the same click picked MAGENTA at
303.3° and moved a band the car is not in.

Now through `displayedToFrame`, the transform a mask already uses.

### ⚠ And the reason no scenario could see it

`repro/eyedropper-color-mixer.txt` passed throughout, for two independent
reasons, and the second is the interesting one:

- it never crops, and
- **the runner's `pick` derived the hue from the *display* colour while
  `ImageCanvas` derives it from the *scene* colour.**

Two different samples down two different code paths, and the runner was
exercising the one that could not go wrong. That is the `crop` verb's missing
`commitCropEdit` again, and it is now written down in `repro/README.md` as a
thing to check whenever a verb stands in for a gesture.

### Vision was handed the coverage overlay

`renderForAnalysis` neutralises the crop, the straighten and the rotation around
the render it gives a segmentation model, and left "Show mask" alone — so with
the overlay on, the model analysed a red-tinted photograph. `Engine.export` has
carried that guard since the overlay existed; this path never got it.

⚠ The common case rather than a corner: the overlay is on precisely when someone
is working with masks, which is precisely when they press Subject.

**This needed a fifth measurement surface.** `measure ... analysis` reads the
picture Vision is given, which nothing on screen ever shows — so the check is
about what the model is *handed* rather than what it does with it, and it does
not flake when Vision changes between OS releases.

### One latent, fixed for consistency

`orion_engine_preview_texture` was the only accessor in the facade without a
`try`/`catch`. `output()` throws on a graph that never compiled, and a throw
crossing that file terminates the process — which is the entire reason the file
exists.

### What the mutations said

Every fix was reverted and the matching scenario failed: the preview one on both
its brush and its matte halves, the matte-leak one on `emptyRowAfterOther`, the
crop one on all three of its picks, the overlay one on the analysis surface.
Each scenario also carries a positive control, because "nothing changed" passes
too easily when the thing being measured has quietly stopped happening at all.

## Session 2026-07-30h — the brush stops losing the end of a stroke

⚠ **Fifteenth arrival of the stale M3 prompt.** Not re-litigated.

The oldest carried-forward gap in this file, and the only one that silently lost
a photographer's work: everything past 256 dabs was dropped, with a warning on
stderr that the person painting the stroke would never see.

### ⚠ The recorded plan was reasoning from the wrong constraint

This file said, for four sessions, that the fix was **more nodes** chained
through the kernel's `accumulate` flag, and that it was "more nodes, not a
bigger buffer". That was wrong. The cap came from **Metal's four-kilobyte limit
on `setBytes`** — the parameter block was already two kilobytes, so an inline
dab list could hold 256 and no more.

Moving the stroke into an auxiliary **texture** removes the cap outright. It is
a binding the pipeline already supports for the mask matte, it costs 128 KB for
four components, and it needs no chain, no spare component and no second code
path. 256 × 64 texels is 16,384 dabs — about eighty frame-widths of stroke.

Worth recording as a pattern: a plan written next to a symptom, four sessions
before anyone tried it, described the shape of the *kernel* rather than the
shape of the *limit*.

### Two traps on the way

- ⚠ `dabStride` went **before** the range block in the C++ struct and **after**
  it in the shader. The two would have disagreed from offset 88 onward, and
  every field past it would have been read from the wrong place — a plausible
  mask rather than an obviously broken one. The offset asserts caught it, which
  is exactly what they exist for.
- ⚠ A zero-initialised `MaskComponent` leaves `dabStride` at zero, and the
  kernel's `max(stride, 1)` then puts dab 1 on row 1 of the texture, where
  nothing was written. A two-dab source-over check silently measured one dab.

### ⚠ One mutation needed a second level of test

Four mutations; three died against `orion-tests`. The fourth — reinstating the
cap in `DevelopPipeline::apply` — **passed the entire GPU suite**, because that
suite drives the kernel directly and never asks what the pipeline chose to
upload. `repro/long-brush-stroke.txt` paints ~360 dabs through `Engine` and
fails without the fix.

That is the third time this session a defect lived in the gap between two things
that were each tested on their own.

## Session 2026-07-30g — degrade-then-refine, and the report list empties

⚠ **Fourteenth arrival of the stale M3 prompt.** Not re-litigated.

The last open reported bug. Measured a day earlier and left named while M4's
feature list was finished, which turned out to be the right order — it meant
this session could spend all of itself on the architecture.

| tick | before | after |
|---|---|---|
| clarity | 57.2 ms, 17 fps | **5.1 ms, 195 fps** |
| dehaze | 115.4 ms, 9 fps | **7.1 ms, 141 fps** |

A second `DevelopPipeline` over a **quarter-linear** mosaic. Quarter, not half:
the target is 116 ms inside a 16 ms frame and four is the first power of two
that manages it — two would leave dehaze at 29 ms. Cost: intermediates at 1/16
the size (~400 MiB against 6427) and **16 ms** on the open of a photograph whose
shape needs a new graph, measured at 114 → 130 ms.

### The mosaic is decimated, and that is its own hazard

Committed separately. ⚠ Sample a Bayer mosaic on a stride that is not a multiple
of its 2×2 cell and the red samples land where the demosaic expects green;
`filters` still reports the pattern intact, so nothing downstream notices and
the demosaic gets the blame. Averaged rather than point-sampled, because a
mosaic point-sampled at stride four moires on fabric and a preview that shimmers
under a slider is worse than one that is soft.

⚠ **The ratio was wrong first time and the phase check did not catch it** — one
output *pixel* stands for `scale` input pixels, so one output *cell* stands for
`scale` input cells, not `scale/2`. The mosaic came out twice the intended size
with every phase assertion passing. A correct invariant is not a complete test;
the dimension check is what found it.

### Three properties, and only the first is about speed

- ⚠ **It settles to the full-resolution answer.** Every tick goes to the
  preview, so when the hand stops the full graph has never seen those values —
  stale by the whole gesture, not by a little.
- ⚠ **The preview shows the value being dragged.** This needed a *new
  measurement surface*. A version that stopped fanning adjustments out to the
  preview passed everything else: the settled picture was still right, and the
  only thing wrong was what the photographer saw *during* the drag, which
  nothing could see.
- ⚠ **Only the canvas reads it.** Export, the histogram, the eyedropper and the
  screenshot harness all go through `outputTexture`. A preview-resolution export
  is a mistake only the person receiving the file would find.

### Two caught before shipping

The blit computes its valid rectangle from `engine.imageWidth` — the *full*
graph's. Handed the preview it would have sampled a corner and blown it up over
the canvas, so the photograph would appear to zoom on every drag.

And the compare original is a full-resolution copy sampled through the same UVs,
so the split is suspended for the length of a drag. The line that does it was
first written **after** `setFragmentBytes`, where it did nothing.

### The control arms it, not a timer

Every slider calls `beginInteraction`/`endInteraction`, not only the slow ones.
Which controls are expensive is a property of the graph and it changes; a
hand-kept list of "the slow ones" is the shape of thing this file has recorded
being bitten by more than once. A timer would also render the *first* tick of
every drag at full resolution — the expensive one, since it dirties the graph.

The preview graph is built after the full one and **allowed to fail**: a machine
without room for it still edits, just without the fast path.

## Session 2026-07-30f — batch export, and M4's feature list closes

⚠ **Thirteenth arrival of the stale M3 prompt.** Not re-litigated.

The export path was built and tested in M1. What was missing is running it over
a list — and the interesting parts of that are not the loop.

### One engine, reused, and the trap in reusing it

`Engine.open` keeps its compiled graph when the next frame has the same shape,
so a batch is one engine opening files in turn. Measured over the three
samples, which are *not* all the same shape: **466 ms each, peak RSS 1.37 GB and
flat**.

⚠ **Each photograph must have its own sidecar restored before it is exported.**
The engine carries the previous photo's adjustments until something replaces
them, so a loop that only called `open` would export the second frame with the
first frame's grade. It would look plausible on a contact sheet, which is the
dangerous kind of wrong.

Verified with real files, not argued: a heavily edited sidecar on one photo gave
**12319.8 KB** against **9603.1** unedited, and the next photo in the same batch
came out **byte-identical** to exporting it alone. Deleting the restore call
drops the first back to 9603.1 — the mutation is visible in a file size.

### Nothing is overwritten

Export is the one operation here that writes files a photographer may already
have, and a batch is where both ways of losing one live: a target already on
disk, and two sources from different folders sharing a basename. Both get a
numbered suffix, the two rules compose, and `exists` is injected so all of it is
tested without a filesystem. One failure does not abandon the rest — a folder
will contain something the decoder cannot read sooner or later.

### Two small tools that made it testable

- `--batch-export <folder> <photos...>` runs a real batch. A feature shipped on
  the strength of its unit tests is a feature nobody has run.
- `save <path>` in the scenario runner writes a photo's sidecar, which is what
  set the leak test above up.

`BatchExport` is in both targets and `BatchExportDriver` only in the app — the
same split as `MatteGeometry` and `SubjectMatte` — so everything except the
twenty lines needing an `Engine` is checkable without a GPU.

⚠ **Honest about the threading:** the loop runs on the main actor because the
engine does, yielding between photographs so progress paints and Stop responds.
That is not the same as running off the main thread, and the interface is
disabled while it works. A folder of three hundred is about two and a half
minutes.

### Where this leaves the milestone

**M4's v1 feature list is complete.** Everything `ROADMAP.md` listed under it is
built: gradient masks, mask groups, guided refinement, the raster component,
Vision subject and person, luminance range masks, spot removal, presets,
copy/paste/sync and batch export.

⚠ What that makes next obvious: **degrade-then-refine**. It is the only open
item a user has actually complained about, it has been measured and costed since
`2026-07-29`, and there is no longer a feature story standing in front of it.

## Session 2026-07-30e — sync, and a sidecar bug it uncovered

⚠ **Twelfth arrival of the stale M3 prompt.** Not re-litigated.

### ⚠ The bug, which is the important part of this session

**Dust spots and mask refinement never survived reopening a photograph.** Both
were written to every sidecar and silently ignored on the way back in. Remove
some dust, close the photo, open it again: the dust is back.

`DevelopState` hand-writes its *decoder* against a private `Key` list and lets
Swift synthesise its *encoder* from the stored properties. **A field added to
the struct therefore joins the sidecar immediately and is read back never.** The
asymmetry produces no warning and looks like working code from both ends. Two
fields from this session's own earlier stories sat in that state.

`testEveryFieldSurvivesTheSidecar` is the guard: encode a state with every field
set, demand it come back identical. Testing the two that were broken would pin
today's bug; this pins the shape of it.

⚠ **And its first version could not see `maskRefine`**, because the fixture left
that field at its default. A round-trip test is only as good as the state it
round-trips, and a field the fixture forgets is a field the suite cannot see.
`busyState()` is exhaustive now and says so. Three mutations confirm it.

Found because a paste through the JSON path disagreed with the same paste
through the struct path — which is exactly what the asymmetry looks like from
outside.

### Sync does not open the photographs it writes to

Opening each target costs a quarter-second of RAW decode apiece and throws every
one away. The sidecar is the source of truth, so sync edits it directly — but
**not** by decoding it into a `DevelopState`.

⚠ A photograph with no sidecar has **no stored white balance**: its white
balance is whatever the camera recorded and is known only once the file is
decoded. Decode the sidecar into a struct and the missing keys come back as the
struct's defaults — 5500 K — and writing that back rewhite-balances every
untouched photograph in the selection to a number nobody chose.

So the patch is applied at the level of the **JSON keys**. A key the paste does
not mention and the target never had stays absent all the way to `Engine.open`,
which fills it from the camera.

`SyncSettings.keys(for:)` and `Preset.applied(to:)` are the same decision
written twice — against fields, and against key names. They cannot be merged
without decoding, so a test applies both to the same state for every group and
demands they agree.

### Confirmed, with the list in the question

Sync writes a sidecar for every photo in view without opening any, and there is
no undo across photographs. The confirmation names the count *and the groups by
name* — "sync settings" is the phrase that hides which settings.

### Scope, stated

"Across a selection" is across every photo **in view**: the library has no
multi-selection and building one in the filmstrip is its own story. The
`SyncSettings` half takes a list of URLs and does not care where it came from.

## Session 2026-07-30d — presets, and the runner was lying about crops

⚠ **Eleventh arrival of the stale M3 prompt.** Not re-litigated.

⚠ **No research file and no bench probe, deliberately.** Presets are not a
filter: there is no algorithm to cite and no floor to measure. CLAUDE.md asks
for both where a kernel is involved, and saying that plainly is better than
manufacturing a citation for copying floats.

### A preset is a patch, not a state

A preset stores a full `DevelopState` *and* the groups it may touch, and
applying it copies only those. Assigning the state wholesale is one line shorter
and wrong in a way that only shows up in use — a black-and-white look would
silently reset the photograph's exposure, its crop and its dust.

Excluded from **every** group: the crop, straighten and rotation; the spots; the
masks and their local adjustment. As-shot white balance is offerable but off by
default. `applied(to:)` lists fields explicitly rather than reflecting, so
adding a field to `DevelopState` is a decision instead of an accident.

### The test that earns its keep

Forty pure-logic checks, no GPU. The load-bearing one applies **each group on
its own** over a state with something set everywhere, and demands every other
group's witness field survive. A preset that assigned the whole state passes any
test that enables all groups at once; it fails this one 25 times.

⚠ **Two mutations survived and neither is a gap.** The lines re-asserting the
crop and the spots from `base` are a backstop over values that are already
`base`'s, so deleting them changes nothing today. They stay for the reason the
brush kernel keeps its radius cutoff, and the comment now says outright that a
test cannot distinguish a backstop from the thing it backs up.

### ⚠ A fidelity bug in the scenario runner

The `crop` verb called `setCrop` and stopped. **The interface does not** — the
overlay calls `commitCropEdit()` on drag end, because `setCrop` renders without
recording so that a drag is one history entry rather than sixty. So a scenario
that cropped left the crop out of history, and any `undo` after it stepped
*past* the crop instead of over it.

The runner's whole claim, stated at the top of `Scenario.swift`, is that it
drives what the interface drives. This was a place it did not, and twelve
scenarios were written against it. It surfaced only because a preset test used
undo after a crop — nothing else had that shape.

### And a scenario that failed on its own arithmetic

`repro/preset-is-a-patch.txt` first asserted that the rendered patch over a dust
spot was unchanged by a preset. That is false by construction: the preset moves
contrast and clarity, so the value there legitimately changes. It runs the other
path instead — the same crop and preset with no spot placed, which must differ.

## Session 2026-07-30c — spot removal, and a pattern worth naming

⚠ **Tenth arrival of the stale M3 prompt.** Not re-litigated.

`research/spot-removal.md`, written before the code. Sensor dust and blemishes,
which is `ROADMAP.md`'s scope and not modesty — the case that makes healing hard
is a blemish across a strong edge, and this deliberately does not solve it.

### What is cited and what is truncated

Pérez, Gangnet & Blake (SIGGRAPH 2003) is what healing *is*, and it needs a
sparse solve — refused here for the same reason `masking.md` §4 refused one.
Farbman et al. (SIGGRAPH 2009) is the published answer: mean-value coordinates
give the Poisson interpolant in closed form.

⚠ **Orion evaluates only that interpolant's zeroth-order term** — the mean of
the boundary difference, one number per spot. `UNSOURCED.md` §21 records it as a
truncation rather than a method, with the bounded failure it buys: across a hard
edge the correction is wrong on both sides by half the edge's contrast. That
limit is repeated in the panel rather than hidden.

### ⚠ Spots store frame coordinates. Masks store displayed ones.

Worth stating plainly because it looks like an inconsistency and is not:

| | Stored in | Because |
|---|---|---|
| Mask | displayed coordinates | placed *against* a subject; stays where you put it on screen |
| Spot | frame coordinates | dust is *on the sensor*; must follow the subject through a crop or turn |

Same transform, applied at a different moment — once at placement through the
new `orion_engine_to_frame`, rather than on every render.

### The third staleness bug of the session, and how it was found

A mutation removing the displayed-to-frame conversion **passed the whole
scenario**, because at zero rotation that transform is the identity. Chasing why
turned up a real defect: the spot parameters were re-pushed only when the
*spots* changed, so a rotation never re-transformed them.

⚠ **That is the third time this session** that state living outside the compared
struct went stale silently — after `matteDirty_` for the Vision matte and
`adj.exposureEv` for the range mask's bias. It is a pattern in this file's
`apply`, not three coincidences: **anything a kernel reads that is not a field
of `MaskComponentEdit` or of `Adjustments` needs either its own dirty flag or a
place in the comparison.** Converting spots at placement removes the staleness
path rather than adding a third flag to it.

The repro file now covers the case that discriminates — a spot placed *while*
the picture is turned, where the transform is not the identity.

### Also caught first-draft-wrong

- The research file's §4 argued for placing the node **before** the lens
  correction. Checking the graph settled it the other way: lens is the one stage
  that warps, so downstream of it a spot shares the space masks already use.
  Corrected in place with the reasoning, not quietly.
- The GPU test used flat fields throughout, so sampling the source once at its
  centre passed everything — **copying detail was never actually checked**,
  which is the entire reason clone exists. A striped-source case kills it now.

### Measured

Clone moved the night sky **0.0209** from where it started; heal moved it
**0.0031** — tone preserved about sevenfold, which is the whole distinction.
Seven GPU checks against exact numbers on a synthetic frame, six mutations dead.

The bench probe is four large clone spots rather than one dust speck: a real
spot is a few thousand pixels of twenty-four million. Clone rather than heal,
because measuring the operation whose purpose is to be invisible would be
calibrating a floor against a control working correctly.

## Session 2026-07-30b — a band on brightness

⚠ **Ninth arrival of the stale M3 prompt.** Not re-litigated; M3 has been
verified against the tree twice in this session's history.

`research/masking.md` **§4b**, written before the code. There is no algorithm to
cite for a luminance band and the section says so rather than dressing one up —
what it records are the three decisions that are easy to get wrong. Mask kind 5;
composed with intersect it refines another component, which is what Lightroom's
range masks do, and composed with add it stands alone. Both fall out of §6's
fold for free.

| Decision | Why |
|---|---|
| Reads the **reference** image | Read the edited result and raising exposure through a highlight band grows the band, which raises the exposure further |
| Measures in **stops** | Linear luminance is unbounded and logarithmic in its interesting range — a fixed linear band is enormous in the shadows and a sliver in the highlights |
| Rec.2020 luma, BT.2020-2 Table 3 | The coefficients `guide_prep` and `develop_display` already use |

### ⚠ The bias, which is the difference between usable and baffling

The band is measured before the tone controls, so the reference carries the
*scene's* luminance and not the screen's. On this night frame lifted 2.6 stops,
a band set by looking at the picture sat two and a half stops away from anything
and **selected nothing at all**. That is what the first version did, and it
looked exactly like the feature being unwired.

The measured stops are biased by the global exposure — one add, since exposure
is a multiply. The *measurement* still comes from the stable reference, so the
tone controls and the local exposure this mask drives leave it alone.

`adj.exposureEv` had to join the component-params comparison for the same reason
`matteDirty_` exists last session: the bias is not part of `MaskComponentEdit`,
so without it the band keeps the exposure it was created under and drifts off
the picture as the slider moves. **Second session running that state living
outside the compared struct went stale silently.** That is now a pattern worth
naming, not a coincidence.

### Two mutations survived, and both were the test's fault

- The C² check computed its ramp position from a **constant** rather than from
  the band being run. With `rangeLo` at -99 there is no ramp within sixty
  columns of where it sampled, so it measured a flat plateau and passed for a
  linear falloff.
- Product-versus-sum for the two edges differ **only where both are partial**,
  which needs a band narrower than twice its softness. A thin luminance slice is
  exactly what someone reaches for to isolate a tone, and it is a case now.

Both fixed, four mutations dead, and the log-versus-linear difference is
asserted rather than trusted: the band −2..+1 stops has its midpoint at −0.5,
while the same interval's linear midpoint is +0.17.

### The bench probe is a shadow band, deliberately

⚠ A highlight band measured **NO EFFECT** on the night frame, and correctly so:
it has almost nothing above middle grey — the same shape as dehaze finding no
haze in a clear sky. Widening the band until it moved would have "fixed" it by
selecting the whole picture, which measures nothing about a *band*. Every
photograph has shadows. Floors 2.15, 3.08 and 0.34 of reference; the spread is
the band working, since the two dark frames have six times more below middle
grey than the daylight one.

### A planning claim corrected

`FEATURES.md` said range masks were cheap **because M1 built a bilateral grid**.
⚠ M1 did not — there is none in the tree — and a range mask is pointwise, so it
would not have helped. The edge-aware part is §4's guided refinement, which a
range component composes with like any other.

## Session 2026-07-30a — M4 step 4, and three defects only running it found

⚠ **The stale M3 kickoff prompt arrived an eighth time.** M3 has been verified
against the tree twice in this session's history; not re-litigated.

Step 4a built the raster component last session. This session filled it.
`VNGenerateForegroundInstanceMaskRequest` for class-agnostic subject lifting and
`VNGeneratePersonSegmentationRequest` for people — macOS 14+, on device, no new
dependency, no licence question.

### The design question step 4a left open, answered

Vision wants an ordinary display-referred photograph, and the tempting way to
get one is a node that tone-maps the pre-geometry image. ⚠ **That is a second
copy of the display transform**, which this codebase has been bitten by before.

Instead the *existing* render is read back — already AgX-mapped, already eight
bits — taken with the crop reset, the straighten at zero and the user's rotation
at zero. That leaves exactly one difference from the frame coordinates kind 4
requires: the EXIF quarter turn, which is an exact permutation of pixels and
needs no resample. Neutralising the rest rather than correcting for it is the
other half of the argument — a crop would leave the matte with no data outside
the crop rectangle.

`MatteGeometry` is pure array logic so `orion-viewport-tests` can pin it without
a GPU, a window or a model. ⚠ Its load-bearing check is **agreement with the
point transform the parametric masks use**: reverse the raster's direction and
a matte and a gradient placed on the same subject land on opposite sides of the
picture, each internally consistent. The round-trip test passes under a
consistent reversal; only the agreement check catches it. Three mutations, all
dead.

### ⚠ Three defects, none of them findable by reading

**The matte was invisible.** Uploaded correctly, `select` reported 15.4%
coverage, and the render was untouched. `apply` skips a component whose
`MaskComponentEdit` has not changed — and a matte is not in that struct, so
`matteSize` never reached the shader and kind 4 read it as zero. Present,
correct, reported, and drawing nothing. The brush has the same problem and
answers it with `brushRevision`, a field the *caller* must remember to bump;
this is `matteDirty_`, the engine remembering instead, which is the version a
caller cannot get wrong.

**An empty person matte was silent.** The person request does not report "no
people" by returning no observation — it returns an observation whose mask is
entirely zero. The guard written for exactly this never fired, and on a
forecourt with no people the result was no error and no coverage, which is
indistinguishable from a broken feature.

**Nothing rendered at all on the first look.** That is what sent me looking, and
it is the third session running where the screenshot was the instrument. The
suites were green through all three defects.

### What is checked, given the model cannot be

Vision's output moves between OS releases and "did it pick the car" is not a
property this suite can own. `repro/subject-selection.txt` asserts the *wiring*:
the model runs, returns coverage, that coverage reaches the picture in the right
coordinate space, and it stops somewhere rather than covering everything. It
fails without `matteDirty_`.

Measured across the samples: subject **15.4%** on the forecourt and **10.8%** on
the plaza; person correctly finds nothing on either car frame and says so.
Looked at as well — with the overlay on, the coverage sits on the two foreground
Astons and stops at their bodies, leaving the white car behind, the building and
the tarmac untouched.

Subject and Person are **buttons, not picker entries**: they are actions, and a
picker entry would be a mode a photographer could select into an empty mask. The
panel was screenshotted rather than assumed.

## Session 2026-07-29w — M4 step 4a, somewhere for a raster to live

⚠ **The stale M3 kickoff prompt arrived a seventh time.** Answered with evidence
again — research file, shader, GPU test and bench probe present for each of the
four — and set aside.

`research/masking.md` §5 wants Vision subject and person selection, and Vision
produces a **raster**. Every mask component until now was parametric, evaluated
as a pure function of position, so there was nowhere for a matte to go. This
session built that place and stopped there: mask **kind 4**, sampled from an aux
texture, one per component slot so a group can hold a subject on one row and a
person on another.

### The decision the rest of step 4 hangs on

⚠ **The kernel samples in frame coordinates and does no geometry correction.**
That is a contract on whoever *produces* the matte. The alternative — a matte in
displayed coordinates — would need the crop, the straighten and the quarter
turns undone per pixel inside the mask kernel, and would carry no data at all
outside the crop rectangle. Keeping the kernel ignorant of geometry is what lets
a matte survive a crop and a rotation for the same reason a gradient does.

Allocated at **1024 on the long edge**, not the frame's: a segmentation network
runs at a fixed internal resolution far below 24 MP, and step 3's guided
refinement is what recovers the boundary. Four mattes cost about 4 MB together
against 48 MB for one at full resolution. A matte larger than the allocation is
**rejected, not downscaled** — silently resampling a boundary someone went to
trouble for, then calling the result edge-aware, is worse than refusing.

### What the tests pin, given Vision itself cannot be tested

Vision's output moves between OS releases and "did it find the subject" is not
a property this suite can assert. Everything *between* the matte and the picture
can be, and that is where the silent failures live. Ten GPU checks; the load
bearing one is the **half-texel convention** — a two-texel ramp is flat outside
the texel centres, linear between them, and exactly half way across, which every
plausible off-by-half convention breaks.

⚠ **Two mutation results worth keeping.** Swapping the interpolation order
survived *correctly*: bilinear is separable, so it is algebraically the same
filter and not a defect — the same shape as the brush's radius cutoff. Removing
the clamp on the sample coordinate **also** survived, and that one was a real
gap: the ramp's first texel was 0, which is exactly what an out-of-bounds Metal
read returns, so the test could not tell the two apart. A case with a first texel
of 0.25 was added and the mutation now dies. Five real mutations, all dead.

### A carried-forward gap closed on the way

The bench probe needed out-of-band state — a matte is uploaded through its own
call, like a brush stroke, and neither of `Probe`'s two `Adjustments&` hooks
could reach it. That is precisely why **the brush has gone unprobed since it was
built**. `Probe` gained a `prepare` hook; both are reachable now, and the matte
probe uses it.

`repro/matte-follows-the-frame.txt` drives it through `Engine`, and the
discriminating case is the half-plane: one quarter turn sends the frame's left
half across the displayed **top**, so a matte quietly stored in displayed
coordinates would stay on the left. Measured 0.9884 on the left before the turn
and 0.9883 across the top after it.

### Deliberately not reachable yet

The kind picker gains "Matte" when step 4b has a producer to fill it. An engine
feature nobody can select is not finished — but a control that can only produce
an empty mask is worse, so this is recorded as a gap rather than papered over.

### ⚠ Housekeeping: uncommitted work found in the tree

A landing-page **round six** — the hero's finder readout and a fix for the
statement highlighter cutting its letters — was sitting uncommitted when this
session started, along with its STATUS entry. A `git add -A` swept it into the
engine commit; that commit was split and the web work committed on its own
(`2cfab5c`), untouched and unreviewed by this session. Worth knowing the tree is
not always clean at the start of one.

## Session 2026-07-29v — M4 step 3, guided feathering

`research/masking.md` §4. The section predicted this would be "a second input
binding and nothing else" on the guided filter already in the tree, and that
held: the guide stays the log2 luminance the highlight and shadow recovery chain
computes, and only the *input* changes, from the image to the mask.

⚠ **The stale M3 kickoff prompt arrived a sixth time** and was answered with
evidence rather than assertion — research file, shader, GPU test and bench probe
each present for all four of dehaze, creative LUTs, exposure fusion and
auto-enhance — then set aside for the story this file actually names.

### What was built

| Kernel | Does |
|---|---|
| `mask_guide_prep` | gathers the four moments — mean I, I², p, I·p — straight onto the subsampled grid |
| `mask_guide_ab` | `a = cov(I,p)/(var(I)+ε)`, the general form the self-guided kernel is the p = I case of |
| `mask_guide_apply` | lifts the coefficients bilinearly and reconstructs `q = ā·I + b̄` |

Seven nodes on the **folded group**, not per component: the boundary a
photographer wants snapped is the one they can see. All seven disable at
strength zero and the consumer resolves straight past them to the fold, so the
M0 gate is unmoved at 9.59 ms. The chain costs **27 ms** when it runs, against
clarity's 66 and dehaze's 116.

The prep node does the subsample as well as the gather, unlike the self-guided
chain which spends a node on each: a full-resolution RGBA32F moment texture is
384 MB at 24 MP and would be read exactly once.

### ⚠ Caught before it shipped, and it would have been silent

`guide:prep` is **disabled whenever highlights and shadows are both zero**. A
disabled node resolves to its producer, so the refine chain would have been
handed `huesat`'s RGBA16F output through a `Texture2D<float2>` binding and read
colour components as a luminance and its square. Not a crash — a
plausible-looking wrong mask. That node is now enabled if *either* chain wants
it, and it is deliberately no longer in the other chain's enable loop.

### Neither constant is the paper's, and both derivations are written down

`UNSOURCED.md` §20.

- **r = maxdim/100.** The paper's r = 60 is 6–10% of its sub-megapixel figures;
  carried across as a fraction that is ~500 px here. What transfers is that r is
  a **search radius** — the local linear model can only pull a boundary onto an
  edge inside the window — bounded by how far the placed mask misses, which
  scales with the frame because the mask's *sources* do.
- **ε = 0.01 squared log2-exposure units.** The paper's 1e-6 assumes [0,1]
  intensity; converted faithfully it is 4e-5 stops². ⚠ That is unusable here
  because `mask_guide_prep` area-averages both moments — the house convention,
  since point-sampling aliases the variance — so `var` is the true
  full-resolution window variance and carries the photograph's noise at full
  strength. Deep shadows run to ~0.02 stops², so anything below that snaps the
  matte to noise. 0.01 follows a half-stop edge and ignores a tenth-stop one.

### The test, and the assertion that was wrong first

Eleven GPU checks, against the filter **computed directly on the CPU** rather
than against a magnitude. The subsampled chain reproduces the exact filter to
0.02 of coverage, worst at the discontinuity where s = 4 smears the lift.

⚠ **The first version asserted the refined boundary lands *on* the guide's
edge. It does not, and should not.** With the mask 40 px out and r = 60 the
exact filter puts the half-coverage crossing at 286; the GPU put it at 285. The
assertion was wrong, not the shader — sixth session running that a first-draft
check measured something other than its claim, and the first where an
independent CPU model was what settled it. What the filter actually owes is a
*discontinuity*: zero on the far side of the edge, then a jump of
1 − (d/2r)(1 + ln(2r/d)) = 0.30. That is pinned now, closed form quoted, and it
dies if the radius is ever quietly changed.

**The check that earns its keep is the flat guide.** With no edge to attract it
the boundary must stay exactly where it was placed and put no step anywhere —
without that, every other assertion is also satisfied by a plain blur of the
mask. Also pinned: a constant mask survives corners included (which is the box
passes normalising honestly), strength 0 is bit-identical, the complement is
symmetric because the filter is affine in p, and the jump at the edge
discriminates this ε from **both** wrong answers — the recovery chain's 0.04
loses the half-stop edge, the paper's 1e-6 snaps to a tenth-stop one.

Four mutations, all dead: the self-guided formula, `b` dropped from the
reconstruction, guide and mask swapped in the gather, and the bilinear lift
replaced by a direct index.

### Measured on photographs

The bench probe's context is the **same mask unrefined**, so it cannot pass with
the chain disabled. ⚠ Its floor is small — 0.008 of reference — and the reason
is structural, recorded at the call site: refinement only moves a ~120 px
boundary band and a whole-frame mean divides that by the rest of the frame. A
near-binary full-width gradient was tried as a more sensitive shape and measured
*lower*. What pins the behaviour is the GPU test; this line's job is that the
graph still delivers it and has not become a no-op.

Looked at as well as measured: with the overlay on, a hard radial across the
silver car has its circular arc cut straight through the tarmac at strength 0,
and at strength 1 the arc is gone and the coverage follows the car. The panel's
`REFINE` slider was screenshotted rather than assumed — a control inserted
without looking was silently not in the interface once before.

## Session 2026-07-29u — four more reported bugs, and the runner learns to see

The three things session `29t` left open, plus one reported mid-session. What
made all four findable is the same move each time: **teach the runner to measure
the surface the bug actually lives on.** `29t` closed by naming that as the next
step, and it was the whole session.

### ⚠️ A radial mask was misplaced on every odd quarter turn

Reported as "the mask is not aligned with the image at all", and the phrasing is
accurate rather than exaggerated — but it is not a rotation bug in the sense it
sounds like. **The EXIF turn counts**, so a portrait file was wrong with the
rotate control never touched. Landscape frames were fine until turned. Linear
gradients were never affected at all.

`mask::radiusToFrame` swapped the semi-axes on an odd turn. The reasoning was
that a semi-axis has an axis of its own, unlike a length, so it must swap when
the picture goes on its side. What that misses: **`toFrame` has already turned
the mask** — it subtracts k·π/2 from the angle, and the semi-axes are measured
along the mask's *own* axes, not the frame's. Rotating the axes and then
swapping the extents applies the turn twice. The algebra is in the header; the
short version is that a quarter turn in normalized coordinates is rigid, so it
contributes neither a swap nor a length change, and only the crop scales.

⚠️ **The unit test asserted the swap.** It had checked the transform against the
belief that produced it and never against a render, so it passed for as long as
the defect existed and would have gone on passing. Fifth session running that a
green check was not evidence — and the first where the check was not merely
weak but actively wrong.

### What found it: `maskcheck`, and why its shape matters

The runner now compares **the mask the interface draws** against **the coverage
the engine renders**. `CanvasLayout.maskAlpha` already existed as the overlay's
own transcription of the kernel — the thing the outline and the handles are
drawn from — so it is the oracle. `maskcheck` grids the frame, classifies every
cell by that oracle, and demands the render agree.

⚠️ **Two-sided, and that is the whole point.** Cells the interface draws clear
must come back **bit-identical**; cells it draws covered must move. A one-sided
"did something happen near here" check passes on a mask shifted by a tenth of
the frame — it still darkens roughly the right region and still looks plausible
in a screenshot. What a shifted mask cannot do is leave the clear cells
untouched. With the bug in place: **14 of 207 clear cells carried coverage, the
worst by 0.34 in luma.**

Two things the first version of the check got wrong, both its own fault rather
than the code's:

- **Positive local exposure over clipped highlights moves nothing**, so a
  covered cell on the blown dealership windows read as a failure. Negative
  exposure instead — nothing in these frames is at pure black.
- **"Clear" has to mean alpha *exactly* zero, not merely small.** At alpha 0.02
  a two-stop local exposure moves luma about 0.005, past the one-code tolerance
  and rightly so. Classifying that cell as clear reported a defect that was the
  classifier's.

`repro/mask-alignment.txt`: both frames at all four turns, plus a crop, a
straighten, roundness 4 and a linear control — 24 checks. Two mutations dead
(swap unconditionally; restore the original code exactly → 10 of 24 fail). The
engine test now asserts the transform is turn-independent and that the crop
scales each semi-axis along its own axis, and it dies under the same mutation.

**Also checked and found correct**, before suspicion landed on the radii: the
brush walks dabs to exactly where they are placed on both orientations at every
turn; linear gradients hold their angle at 0°, 45° and 90° through all four
turns; and the quadrant placement of a radial was right even while its *shape*
was wrong, which is precisely why a coarse test had never caught it.

### The compare split held an original of the wrong shape

The blit samples the edited texture and the held original through **one** set of
UVs, taken from the edited render's valid rectangle. Any geometry change under a
live split therefore read the held copy through the wrong window: a crop put
luma **0.7404** on the original side where 0.1432 belonged.

`rotate` carried its own `captureOriginal()` call and `setCrop` carried nothing,
so which of the two geometry controls worked was an accident of who remembered.
The engine records the geometry its original was rendered at and re-takes it
from `render()` when that moves — crop, straighten, quarter turn, crop preview,
and anything added later. The three hand-listed capture sites are gone.

Two supporting changes, both of which found something:

- **`measure ... canvas` renders through the real blit offscreen.** The shader
  and the transform moved to `CanvasBlit` so there is one copy. A CPU stand-in
  for the compositing would have been the one piece of code these tests cannot
  afford to fake.
- ⚠️ **`expect a == b` now compares saturation as well as luma.** A mean is a
  weak signature for a photograph: the rotate check passed against the wrong
  picture entirely because the two frames agreed on mean luma to 0.0035 — inside
  the tolerance — while differing by 0.28 in saturation.

And the other half of "compare shows different settings": **`assign` reset the
selected mask row to 0** on every undo, redo, history jump and compare capture.
The `mask…` sliders are views onto the selected row, so the panel then read a
different component's numbers. Clamped now.

### The eyedropper's lag was 50 ms of animation

**The engine read costs 2.4 µs** — measured with a new `time` verb, kept as
`repro/eyedropper-latency.txt`. A 60 Hz frame is 16 000 µs, so none of the
reported lag was in the sample path. It was `ColorLoupe`'s
`.animation(.linear(duration: 0.05), value: point)`: the loupe was told to take
50 ms to reach the pointer, so it was permanently behind and never arrived while
the hand was moving.

It was also **wrong rather than only slow** — the colour and the band updated the
instant the sample landed while the crosshair interpolated, so a crosshair
captioned "the exact sampled pixel" sat where no sample had been taken.

### Optics is a tab

The lens corrections sat second from the top of Detail and were reported
missing. Distortion, vignetting and fringing are properties of the glass, and a
photographer looking for them does not think "detail". The tab bar had already
made this argument once, when three of four tabs were a bare SF Symbol.
Screenshotted rather than trusted.

### Sliders: measured, and deliberately not started

`repro/slider-drag-cost.txt` is the number the report was missing — exposure
**9.4 ms** a tick, clarity **65.7**, dehaze **116.4**, each tick blocking the
main thread on `commitAndWait`. Not a defect in any one filter: session `29f`
already established dehaze's six rank passes sit within 2% of each other, so
there is nothing to fix in *one* of them.

**The fix is degrade-then-refine and it is a story, not a bug fix.** The graph
compiles at a single resolution, so the honest shape is a second `DevelopPipeline`
at a quarter-linear proxy (~380 MiB on top of 6092), both fed by `apply`, with
the full render scheduled on settle — plus the guarantee that export, the
histogram and the eyedropper never read the proxy. Half-building it would leave
the app worse than measured-and-slow. Left named, costed and unstarted.

## Session 2026-07-29t — seven reported bugs, and a runner that reproduces them

The developer used the alpha and reported thirteen things. Seven are fixed. The
lasting artefact is **`repro/`**: one text file per report, run by the app itself
(`--scenario`), so a report becomes a file that fails until it is fixed and then
stays as the regression test. `app/Scenario.swift` documents the grammar.

⚠️ **The runner drives `Engine`, `CanvasLayout` and `TargetedAdjust`** — the same
objects the interface drives — and never reaches around them into the pipeline.
One that poked the pipeline would exercise code already known to work and miss the
view-model layer, which is where every one of these failures actually was.

### The brush was wrong in two independent ways

**Dab centres were never transformed.** The gradient's centre went through
`mask::toFrame`; the stroke's points were copied straight from displayed
coordinates into the shader. So a stroke ignored the crop and the rotation — and
because a **portrait file carries an EXIF quarter turn**, a stroke on one landed
mirrored and ninety degrees off *with the rotate control untouched*. The gradients
being right is exactly what hid it: `MaskGeometry` was built for them in session
`29j` and the brush was wired up in `29n` without being put through it, while this
file claimed strokes survived rotation. Measured after: under the stroke
0.3520 → 0.6405, away from it 0.2190 → 0.2190 bit-identical.

**The nib was an ellipse.** Measured in normalized coordinates, where one unit of
x and one of y are different pixel counts on any non-square frame — 3:2 on a 3:2
photograph, and Size stretched rather than grew it. It is a radius in frame pixels
now, off the displayed picture's shorter side so it holds its on-screen size under
a crop.

### Two silent failures, both the same shape

**The eyedropper read an 8-bit texture as half float.** `Engine::sampleAt` used
`__fp16` whatever the format, and the screen tail is `RGBA8Unorm` — which does not
fail, it reinterprets four bytes as two halves and returns **NaN**. The
consequence was not a wrong colour but a silent one: `TargetedAdjust.hue`
correctly refuses a pixel with no hue, and NaN reads as no hue, so the pick did
nothing and said nothing. Same trap the bench's `output16` already records.

**Auto recorded no history entry.** Its five sliders were set by bare assignment
under a comment claiming that kept them on an ordinary edit's path. `edit(_:_:)`
is what records history; an assignment records nothing. So undo stepped *past*
Auto to the edit before it — reported as "can't undo auto, gets rid of all
changes". Decision #67 makes the rule explicit, because the failure is invisible:
the picture updates, the slider shows the new value, only the undo stack is wrong.

### Auto-enhance was not idempotent (decision #66)

Two causes. It derived its look from the frame **as it currently stands**, so the
second press measured the frame the first had corrected — +2.25 EV with lift 0.16,
then +2.99 EV with lift 0.00 — which also falsified the code's own comment about
the look responding to the photograph rather than to the correction. And the
solver ran a flat six passes when every step undershoots by construction, so a
frame far from the anchor ran out of passes short of it; the sample frames need
6, 11 and 17. It resets its five owned controls before measuring and stops on
arrival now.

### ⚠️ What the runner cannot see, and why two reports are still open

It measures `engine.outputTexture`, the *edited* render. **Compare composites two
textures in the canvas view**, so a compare bug living in that compositing is
invisible to it — the two passing compare scenarios are **not** evidence the
reported behaviour is fine. Teaching it to measure through the canvas is the next
step, and is what the remaining reports need.

### Still open from the report

| Report | Note |
|---|---|
| Compare shows wrong settings; rotating while comparing breaks | Needs the runner to see the canvas composite |
| Sliders slow | Real: **adjustments render at full resolution.** M1's Interaction epic named degrade-then-refine and it was never built, so dehaze (108 ms) and clarity (58 ms) run at 24 MP every tick. Only the crop has a preview path |
| Eyedropper latency | Separate from the NaN; unmeasured |
| Lens panel discoverability | It is in the Detail tab and nobody found it |

### v0.4.0-alpha.2

Cut because alpha.1 shipped every brush bug above. ⚠️ **`releases/latest`
excludes prereleases**, so the site's download button had been redirecting to the
releases *listing* rather than a download — found by following the redirect
instead of trusting the URL. It points at the tag now, which also keeps the
right-click-to-open instructions in front of a visitor; a direct `.dmg` link would
skip them.

## Session 2026-07-29s — the interface reads as an instrument, and there is a build

Two things, both asked for directly.

### The design pass (decision #63)

Brief settled by asking rather than guessing: hardware-literal in the register of
**Halide and Capture One**, density unchanged — the problem was hierarchy, not
packing — and film-rebate amber as **structure only, never near the photograph**.

**The histogram carries the identity.** It was a grey blob with three words under
it. It is a recessed plate now, with an engraved rail whose marks are ranked long
at the named divisions and short at the quarters, and clipping flags at both ends
that fly with the real percentage beside them. Position says which end, so both
flags are cut in one amber instead of inventing a second ink.

⚠️ **The flags found a real bug in the curve.** Its ceiling was the 99th
percentile over every bin, justified in a comment as stopping one blown bin from
flattening the curve. It does not: with 3 × 256 bins the 99th percentile *is* the
eighth-largest value, so a night frame with 10% of its pixels at black kept a
ceiling set by the clipping spike and squashed the photograph into a band along
the bottom — the grey blob. End bins are excluded from the ceiling now, which is
honest rather than a fudge precisely because the flags report what sits in them
as a number.

⚠️ **No EV scale on the rail, and the reason is in the file.** The obvious
instrument engraves stops. The output is AgX-mapped with **no sRGB encode**, so a
code value becomes stops only by inverting the AgX polynomial in the interface —
a second copy of the display transform, drifting from the shader the first time
either is touched. If stops are wanted the engine should report the mapping.

Hierarchy elsewhere: section names became engraved nameplates with a hairline to
the panel edge and a mark when anything inside has moved — **reported upward by
the controls as a SwiftUI preference, not listed at the call site**, because a
hand-kept list of what a section contains is exactly how `lutStrength` shipped a
dead slider. Three of four tool tabs were a bare SF Symbol with no label; all
four are named. The canvas hint moved off the photograph into the footer.

**No bundled font.** San Francisco carries expanded widths and tabular figures,
so the engraved register costs no license file and has no fallback to worry about.

### v0.4.0-alpha.1 — the first build outside the source tree

**https://github.com/Nano-AI/Orion/releases/tag/v0.4.0-alpha.1** · 3.0 MB dmg,
linked from the landing page.

`tools/package-app.sh`. The development bundle was not runnable by anyone else
and none of the reasons were visible in it:

| Blocker | Fix |
|---|---|
| `ORION_SHADER_DIR` / `ORION_DATA_DIR` are absolute paths into the build tree | `src/ResourcePaths.cpp` prefers `Contents/Resources`, falls back to the compile-time path (decision #65) |
| Homebrew `libraw` by absolute path, pulling `libomp`, `libjpeg`, `liblcms2` | Dependency graph **walked**, not listed — that list is a property of how Homebrew built libraw |
| Rewriting a Mach-O voids its signature; unsigned does not launch on arm64 | Re-signed ad-hoc *after* `install_name_tool`; bundle rpath added at link time |
| LibRaw is LGPL-2.1 | License texts copied verbatim from the installed packages, never retyped |

**The verification is the part worth keeping.** Any reference outside the bundle
is a hard error in the script, and then the *published* dmg was downloaded,
mounted, and run with `build/shaders` and `data/lensfun` moved aside — it
rendered. The control run, the build-tree app under the same conditions, failed
on a missing metallib. That control is what says the test was real rather than
the fallback quietly working.

**Version is 0.4.0, not 1.0.0** (decision #64). Minor tracks the milestone in
flight; `FEATURES.md` still lists range masks, spot removal, presets, sync and
batch export as v1, and a build calling itself v1 promises them.

⚠️ **Still true of the release:** arm64 only, 8-bit TIFF, and the 256-dab
truncation. All three are named in the release notes rather than left to be
found.

## Session 2026-07-29r — mask groups reach the interface

The other half of step 2. A mask was a list in the engine; now it is one in the
app, so **an engine feature nobody can select is finished at last**.

`DevelopState` carries `maskComponents`, and **each component holds its own
stroke** rather than strokes living in a parallel array — that arrangement is
how a reorder puts someone's paint on the wrong component. The panel gains a
row list (number, kind, op, dab count), Add and Remove, and a compose picker
that appears **only on rows after the first**, because the fold starts from
zero: add is the identity there and subtract or intersect gives an empty group.

**The `mask…` properties on `Engine` became views onto the selected row**, so
the sliders, the canvas overlay and the screenshot harness bind exactly as
before — the churn stayed in one file. The kind picker keeps its old meaning at
both ends: a kind on an empty group adds a component, `No mask` on a row
removes it.

⚠️ **Removal has to re-send every stroke.** The engine indexes strokes by
component, so removing row 1 of three shifts row 2's paint down with it and the
vacated tail must be cleared — otherwise the surviving component renders the
removed one's stroke, and the next component added inherits paint nobody drew.

### The migration is the load-bearing part

Every photo finished between gradient masks and groups has flat `maskKind` keys
and no `maskComponents` — and **`localExposureEv` kept its own name through the
change**. So dropping the mask would not open those photos unedited. It would
open them with the local exposure applied to the **whole frame**, which reads as
a working editor and is worse than a crash. Legacy keys are read and never
written, alongside `denoiseColour`.

**A component list that is present wins over legacy keys.** A file holding both
came from a newer build, and preferring the flat keys would silently discard
rows two and up. Pinned, with the off-row drop and a three-component round trip
including every op: 22 new checks in `orion-viewport-tests`.

## Session 2026-07-29q — mask groups, the engine half

`research/masking.md` §6, decision #62. A mask is now a **list of components**
folded left in listed order — add is `max` (no buildup where two overlap, which
is the section's first sentence), subtract is `α₁(1−α₂)`, intersect `α₁·α₂` —
and the adjustment is applied once through the combined coverage.

**The two mask kernels became one, and the merge found a fourth dead control.**
Decision #55's shape — a gradient node with a brush node chained after it, one
always passing through — existed only because the graph cannot swap a kernel per
render. `mask_component.slang` branches on `kind` instead, one node per
component, and the pass-through contortion is gone. What it had been hiding:
**invert never reached a brush** — the gradient node held the invert and the
brush node discarded that node's output. Same class as session `2026-07-29n`'s
three. Pinned by a GPU test now, and the mutation restoring the old behaviour
dies.

**The fold starts from a node that writes zero** — the additive identity — so
the first component needs no special case and a group of one runs the same code
as a group of four. Consequence stated in the header docs: subtract or
intersect on the *first* row is always zero, so the interface should not offer
an op there.

**`kMaxMaskComponents` is 4, and it is a memory number, not a concept.** Each
live component is a full-resolution R16F pass; unused ones are disabled — their
texture is the cost, none of their time. 118 nodes, 6092 MiB (was 5907);
the M0 gate is unmoved at **9.13–9.39 ms p95**.

**Found on the way: mask placement went stale under a crop.** The old
`maskMoved` staleness had no geometry fields, and the geometry block never
re-pushed the mask params — so after a crop or straighten the coverage kept its
old placement until a mask slider happened to move. The rewrite computes the
shared frame geometry once and re-places every live component when it moves.

**Verified, in three registers:**

- **The pre-merge numbers reproduce exactly.** The two GPU mask test suites now
  run against `maskComponent` with every pinned number unchanged — smootherstep
  against the closed form, source-over at partial flow (0.75 not 1.0), the
  airbrush series `1 − 0.998⁴⁰`, R16F resolving all forty steps.
- **The compose algebra is checked against the kernel's own parts**, not a
  reimplementation: each op on two overlapping radials, add measurably *not*
  screen (differs 0.095 where max matches to 2e-3), subtract-then-add order
  sensitivity, and a subtracted stroke erasing a gradient under the dab while
  leaving it exactly alone elsewhere.
- **On photographs:** zero-coverage bit-identity through the whole new chain on
  `_PIC8220` (`--measure`, luma 0.3856 → 0.3856 to four decimals beyond the
  ramp; 0.3767 → 0.1716 past the full line). A new bench probe drives a
  **group of two** through the real pipeline — linear +2 EV with a radial
  subtracted — the only thing that would catch the chain miswired; floors
  0.47/0.44/0.60 of reference across the three frames, floored at 0.22.

**Facade:** `OrionMaskComponent[4]` plus `mask_count` in the adjustments block;
`orion_engine_set_brush_stroke` takes a component index (out of range is
`BAD_ARG`, not a clamp — paint in the wrong component is worse than nothing).
Swift's `cAdjustments()` became named field assignment rather than the
80-argument positional init, which a transposed pair of same-typed floats would
have survived silently.

Also: the falloff was **two identical copies each commented "shared"** —
`ops/mask_ops.slang` now actually holds the one copy, next to the compose ops.

## Session 2026-07-29p — the landing page, redesigned around one idea

`web/` only; no engine or app code touched. The idea: **scrolling is the
slider drag.** The hero opens on the flat, undeveloped raw and the visitor's
scroll develops it — filter scrubbed from washed-out to the graded frame, a
readout ticking to the *real* values from the real edit (Exposure +2.60 EV,
Contrast 1.45, 3635 K — the same numbers the Local panel shows in the
interface screenshot further down, of the same photograph), and a literal
slider rail filling on the right. A second pinned scene sweeps a linear
gradient mask down over a sky, drawn as the editor draws it: three dashed
guide lines and a handle, ticking to −1.60 EV.

- **Display type is Bricolage Grotesque**, variable, self-hosted at 77 KB
  (decision #60). Headlines reveal line-by-line through clipped masks. Copy
  cut hard everywhere — headlines carry the page; body text is one or two
  lines per section. The working-today / not-yet register stays, verbatim
  claim discipline included.
- **The static page is the finished page** (decision #59). The script's
  first act is adding `html.js`; every hidden-until-revealed rule is gated
  on it. No JavaScript or reduced motion = the developed photo, the placed
  mask, the final numbers. The old page hid `.rv` blocks unconditionally
  and only JavaScript could show them — a no-JS visitor got a blank page.
  Verified with a screenshots pass at `javaScriptEnabled: false`.
- Frame counters number the sections in film-rebate amber (the app's `star`
  token) — the one new ink; teal remains live-numbers-only.
- Screenshotted at 14 desktop scroll positions, 4 mobile, plus no-JS and
  reduced-motion. Two fixes came out of looking rather than trusting: the
  Revuelto scene was mud under its wash (the photo is a stop darker than
  the others — it gets a CSS lift, stated in a comment) and the hero's
  teal text-glow read as a rendering artifact at full strength (halved).

**Round two, same day — show, don't tell.** The developer's review: too much
text, not enough motion. Body copy cut again (most sections are a headline
and one line now), and the page gained four things, all in the same
finished-page-rewound contract:

- **The proxy wipe.** The speed section no longer *says* other editors show
  you a proxy — the frame *is* a blurred proxy until a sweep line drags full
  resolution across it, labeled FULL RESOLUTION / PROXY at the line.
- **The statement lights up word by word** as you scroll through it. The
  words are wrapped in spans by the script, so without it the line is
  simply lit.
- **Every scrub is smoothed** — outputs chase their scroll-derived targets
  at 0.16/frame, so a stepping mouse wheel reads as one continuous motion;
  the rAF loop runs only while something is settling.
- **A live frame counter** in film-rebate amber sits fixed bottom-right
  (01 · Speed … End of roll), replacing the static per-section eyebrows.
  Plus: parallax inside the two flowing photo sections, the app screenshot
  lands like a print settling flat, the lens count ticks up on arrival,
  static grain over the two darkest scenes.

**Round seven — the wordmark was measured, not argued about** (decision #71).
The developer said twice that ORION and its line had little contrast. Sampling
the plate behind the type settled it: max luminance **1.000**, 15% of the box
above L 0.5, worst-case contrast **1.0:1**. The name lands on the blown
showroom — white on white, genuinely invisible, not a matter of taste. The
subtitle measured 16:1 and was never the problem.

The fix is a centre-spot ND, the filter a photographer would screw on for
exactly this. Neutral black at 0.56 alpha composites to a multiply by 0.44,
so it scales light instead of adding ink: the clipped highlight drops about a
stop and a half and the asphalt at L 0.008 does not move, which is why the
photograph keeps its shape and the yellow and white cars keep theirs. A flat
wash would have flattened all three. The wordmark's two text-shadows — a 34px
and a 70px blur, which light the area around a letter without defining it,
and one of them teal — became the thin dark keyline a finder's glyphs carry.
Worst case 1.0:1 → **3.64:1** (past the 3:1 large-text floor), mean 4.6 →
13.6. Two follow-ons from looking: the first ND core was too tight and left
the N standing on the white car unfiltered, and the new `A7 III` status
corner was washing out on the lit showroom at .52 opacity.

Not verified: the narrow layout after this change. The browser window was
stuck maximised and `resize_window` reported success while `innerWidth` stayed
1500. The ND is proportional so it should scale, but that is reasoning, not a
screenshot.

**Round six, same day — the finder gets a readout, and the highlighter stops
eating the line above.** The developer sent a Nikon Z5 product shot of an EVF
and asked whether it could be used. The image itself cannot — it is Nikon's
marketing photography, and a Nikon body in Orion's hero also implies an
endorsement — but a finder's *readout layout* is functional convention, so it
was rebuilt, same rule as the Hasselblad gesture. The hero now stacks two
lines at the foot of the frame the way a finder does: the shot above
(`24mm · f/1.4 · 1/80 · ISO 3200`, read out of this photograph's own EXIF —
a Sony A7 III at 24 mm — so nothing on the line is invented), then a ±3 EV
scale with a centre post and a teal needle the develop drives to +2.60, then
CONTRAST / TEMP / RENDER underneath as before. A status line pins `A7 III`
and `ARW · 24.2 MP` to the top corners, and EXPOSURE left the lower row
because the scale now says it. Two looking-not-trusting fixes, both from
screenshots: the scale first sat marooned mid-line while its number was
flush right, so the pair now travels right together; and on a phone the unit
lost its space to a negative-margin hack tuned for the desktop gap, now a
wrapped span instead.

The statement's highlighter was cutting the letters, and the cause was not
the highlighter. `line-height: 1.04` is tighter than SF Pro's content box
(~1.17em), and an inline background fills the content box — so the amber
band on line two reached up and sliced the descender of "got" on line one.
Line height is 1.22, with the reason in a comment so it does not get tuned
back down.

The band also faded from transparent to amber, which spends its whole
transition as half-opaque amber over near-black — a murky olive block — so
it became a stroke: a gradient grown 0 → 100% width, always fully inked and
simply not arrived yet. That traded one bug for a subtler one the developer
caught in a zoomed screenshot: the *text* colour was still flipping per
word, and a per-word colour flip cannot stay in step with a band sweeping
across that same word, so mid-stroke a letter was half ink-on-amber and
half ink-on-black. Fixed by removing the colour change entirely rather than
timing around it. A marker passes over writing that is already dark; the
unlit words are already dark, so they now hold #262c30 throughout — 8.9:1
on the amber — and only the band moves. Verified by freezing the stroke at
52% across "software" and looking at it. The overshoot moved into the
band's own geometry too (`background-position: -0.06em`, width `100% +
0.12em`), because a box-shadow is the shape of the whole border box and so
could only snap in at full width, which it did.

**Round five, same day — through the eyepiece, and the developer's notes.**
The hero now opens OUTSIDE the camera: black screen, the wordmark over a
small glowing 3:2 ocular, and the scroll opens the eyepiece to the full
frame before the finder wakes and the develop begins — the entry the
Hasselblad X2D page earns before its EVF view (their site refuses this
sandbox's browser; the gesture is rebuilt from its structure, not copied).
Four notes from the developer, all in: the wordmark drops Michroma for
Space Grotesk, tracked caps — modern, minimal, kin to the readout mono;
text contrast stepped up across the page; the statement's second sentence
is run over by a film-rebate amber highlighter word by word as the scroll
reaches it; and the color section is cut by a frosted-glass slash between
the words and the print, with the header a size up. The ocular clip
computes from the viewport so a phone gets an eyepiece, not a slit.

**Round four, same day — the hero is a viewfinder** (decision #61). The
sentence headline read as a generic hero, so it is gone. The page opens
inside a camera: frame brackets, a thirds grid, a mode line up top, an AF
point at the upper thirds intersection that turns teal the moment the
develop lands, and ORION set in Michroma — the wide engraved-on-the-body
lettering cameras use — where the eyepiece display sits. Scrolling develops
the raw as before; keep going and you push through the finder, the whole
overlay scaling past the eye while the photograph stays. The lede and the
instrument cluster moved to a strip below the hero so the finder stays
clean. Screenshotted at load, at lock, mid-push, and on mobile; the AF
point and the mode line were both repositioned because the first
screenshots showed them lost behind the wordmark.

**Round three, same day — subjects in frame, nothing generic.** Three notes
from the developer, all acted on:

- **Headers cut to two or three words** ("Instant updates." "Local light."
  "No lock-in." "Color you can check.") and every line of copy passed
  through a de-slop edit: no em dashes, no capsule phrasing, nothing that
  reads machine-made.
- **The pill chips and stock favicon are gone.** The hero numbers are now
  an instrument cluster (value over label, hairlines between), the GitHub
  button wears focus-peaking viewfinder corners that reach further on
  hover, and the favicon is Orion's Belt — three stars, the app's own
  namesake. Space Mono (self-hosted, 9 KB a weight) replaces the system
  mono so the readouts have a face of their own.
- **Every subject now fits its frame.** Measured the images instead of
  eyeballing: the M5 is 0.92:1 and the glasshouse 0.67:1, and both were
  being butchered by wide full-bleed crops. The mask demo moved to the
  night sky (a graduated sky darken, nothing to cut), the glasshouse hangs
  whole as a print beside the color copy, and the close anchors the M5 to
  its foot so the whole car reads on any screen. The hero also gained a
  scroll-driven settle: the frame eases from 1.09 to 1.0 as the grade
  lands.

## Session 2026-07-29o — see the mask you are painting

`Show mask` paints the coverage over the picture in red. Drawn in
`develop:linear`, last, so it sits above every adjustment and goes through the
same tone transform the photograph does. Some image is kept underneath rather
than flooding flat red — the mask is placed *against* the subject, so the
subject has to stay legible through it — and a constant floor keeps it visible
in deep shadow, where a purely proportional tint vanishes exactly where coverage
most needs checking.

| region | mask only | with overlay |
|---|---|---|
| zero coverage | sat 0.3908 · luma 0.4703 | **identical** |
| full coverage | sat 0.5262 · luma 0.1981 | **sat 0.8828** · luma 0.2388 |

Zero coverage bit-identical is the invariant: the overlay is strictly
proportional to alpha, so it cannot imply coverage that is not there.

**Deliberately not in `DevelopState`.** It is how you are *looking* at the
photograph, not an edit — so it never reaches the sidecar, never enters undo,
and never follows the photo to another machine. `export()` forces it off around
the write and restores it after, including when the export throws.

⚠️ **That export guard is untested.** It is correct by construction — set
synchronously and pushed before `orion_engine_export` — but nothing asserts it.
An export with the overlay on would write a red-tinted photograph with nothing
in the file to say why. Worth a test when the export path is next touched.

**This had to come before mask groups**, which is the next story: nobody can
debug add, subtract and intersect against an invisible alpha.

## Session 2026-07-29n — the brush is reachable, and there is a website

### Brush masks, end to end

`maskKind == 3`. Kernel → pipeline node → C facade → Swift → panel → painting on
the canvas. **Verified on a photograph, not by eye:**

| region | no mask | brush |
|---|---|---|
| under the stroke | 0.3892 | **0.5643** |
| far from the stroke | 0.0859 | **0.0859** — bit-identical |

The second row is the one that matters. A mask leaking a faint edit across the
whole frame still looks right.

**One node serves all four mask kinds**, which falls out of a property the suite
already pins — a pass with no dabs is the identity:

| kind | gradient node | brush node | result |
|---|---|---|---|
| 0 none | writes 1.0 | passes through | full coverage |
| 1 / 2 | the gradient | passes through | the gradient |
| 3 brush | ignored | starts empty | the stroke |

The alternative was swapping a node's kernel per render, which the graph cannot
express, or writing into a node's output from outside it.

⚠️ **The real risk was never the brush** — it is that every existing gradient
mask now reaches `develop:linear` through a new node. Measured against the
numbers taken before it existed: 0.4703 / 0.1981 / 0.4110, all three exact.

**Dab centres are deliberately not in `Adjustments`.** That struct is compared
field by field on every slider tick; carrying a stroke through it would make
every tick walk the stroke. It holds `brushRevision`, a single int.

**Spacing is walked, not stamped per event.** A pointer reports a handful of
positions a second, so per-event stamping draws a dotted line at speed and a
solid one when slow — the same gesture laying different paint depending on how
fast it was made. `carry` continues the spacing across event boundaries;
restarting clusters dabs wherever the hand slowed, which is at the corners of a
gesture. The test walks one line through a 3-event stream and a 60-event stream
and demands the same dabs in the same places.

⚠️ **A stroke over 256 dabs is truncated and says so on stderr.** The kernel
chains; the graph holds one brush node. Fixing it is more nodes, not a bigger
buffer.

### Three dead controls found, all the same class

- **Feather did nothing to a linear mask.** The shader reads it only in the
  radial branch — a linear ramp runs zero-line to full-line, so Length *is* the
  feather. Measured 0.50 against 0.02: bit-identical. Hidden.
- **The radial branch was a bare `else`,** so it also caught the brush: four
  dead sliders under a stroke that reads none of them. Now `else if kind == 2`.
- **The brush had no picker entry at all.** Kernel, node, facade and gesture all
  built, and `maskKind` could never be 3. An engine feature nobody can select is
  not finished.

### Mutation testing, twice, and what it caught

Fifteen mutations across the canvas geometry and the brush kernel. Thirteen
died. Two did not, and only one was a real gap:

- **Pinning the canvas map's origin — ignoring panning entirely — passed all
  3100 checks**, because `point(unit(p)) == p` holds for *any* invertible map.
  The round trip proved invertibility, not correctness. Now pinned against
  `ImageCanvas.transform`'s own `uvMin`.
- Deleting the brush's radius cutoff correctly changed nothing: `brushFalloff`
  saturates, so it is a performance early-out, not a correctness guard. Said so
  in the shader.

⚠️ **Four sessions running, the first version of a test measured something other
than its claim.** This time: an empty-pass check that never uploaded its input,
and an R16F banding claim asserted at flow 0.03 where banding cannot occur —
one dab moves alpha ~0.02, five to seven whole 8-bit codes. Banding needs flow
below 1/255. Re-asserted at 0.002 against `1 − 0.998⁴⁰` in closed form.

### ⚠️ Brush masks were cut from v1, and that was reversed

ROADMAP and FEATURES both said **"No brush masking in v1 — deliberately cut."**
The developer reversed it. `DECISIONS.md` #54 records why the original estimate
was wrong: of the three costs it named, storage is a list of centres rather than
a raster, and edge-aware snapping is the guided filter already built for dehaze.
Only stroke capture was ever real work.

### The landing site

`web/`, deployed to **https://nano-ai.github.io/Orion/** by
`.github/workflows/pages.yml`. Dependency-free static files; Pages is enabled
with `build_type: workflow`.

Dark only, no theme toggle, nothing interactive, American spelling. The chrome
is near-black plus the app's teal, teal reserved for numeric values; **all color
comes from six full-bleed photographs**, which is how "more color" and "match
the app" reconcile.

⚠️ **Every photograph was screened at native resolution for people, and twelve
frames were rejected** — including `_PIC8095`. **That frame is a repo sample and
has people in the plaza at its base; it must not be used for any published
render.** `_PIC8220` and `_PIC8148` are clear.

Three deploy traps hit and recorded:

- **`.gitignore`'s `orion-*.jpg` silently swallowed `web/img/*.jpg`.** The push
  succeeded, the deploy went green, every image 404'd. Negated for `web/img/`.
- **Vite's `base` must be `/Orion/`** if a build step is ever added — the
  default emits absolute paths that 404 on a project page while the deploy still
  reports success. React + Framer Motion was scaffolded and reverted: with
  interactivity banned there is no state for React and no gestures for Framer
  Motion, so it reduces to one `IntersectionObserver`.
- **A green deploy is not proof the page is right.** Verify the page *and every
  asset* returns 200, and that the content actually changed.
## Session 2026-07-29m — brush dabs, the last third of step 1

One kernel, `mask_brush.slang`. Normalized coordinates in, R16F alpha out, no
new dependency. **The maths and the GPU kernel are in and tested; nothing is
wired to the interface yet** — no node in `DevelopPipeline`, no facade field, no
painting on the canvas. That is the next story.

A stroke is a **list of dab centres**, stored parametrically and rasterized on
demand — a few kilobytes instead of the 24–120 MB a raster mask costs at
24–60 MP, and exact under a crop or a rotation because each centre goes through
the same transform the image does rather than being resampled. Re-interpolating
an already-feathered raster mask compounds blur; this cannot.

**One radius for the whole mask, not one per dab**, per the research's own shape.

**A long stroke is not capped at 256 dabs.** The kernel accumulates into the
alpha it is handed, so a stroke is several dispatches chained nose to tail.
Capping would either leave gaps or silently resample the photographer's stroke
into something they did not draw.

### What is pinned

- **Dabs compose source-over, not additively.** Two at full flow are full
  coverage, not two — adding lets a slow hand over one spot drive alpha past 1
  and clip, which reads as the brush getting stronger the longer you hover. The
  check uses *partial* flow, where the two rules differ measurably: over gives
  0.75, addition gives 1.0.
- **A dab is smootherstep in the radius**, checked against the function computed
  independently — not "the centre is bright and the outside is dark", which
  passes on any blob. The falloff is *shared* with the gradient masks rather
  than reimplemented.
- **Chaining works**: a second pass builds on the first rather than replacing
  it, and an empty pass leaves the stroke exactly as it was.

### Seven mutations, six dead — and the one that correctly survived

Deleting the radius cutoff changes no output, because `brushFalloff` saturates
and a pixel past the rim already contributes nothing. It is a **performance
early-out, not a correctness guard**, and the shader now says so — otherwise the
next reader assumes it load-bearing. Real defects (radius doubled, flow ignored,
addition instead of over, accumulate ignored, hardness ignored, smoothstep for
smootherstep) all die.

### ⚠️ Both first-run failures were the tests, not the shader

Fourth session running. The pattern does not change: the check asserted
something weaker than, or different from, its claim.

- The empty-pass check **never uploaded the second result into the source**, so
  it compared a pass-through of the first against the second — reporting a
  shader bug that was a missing line in the test.
- **The R16F claim was asserted at a flow where it is false.** At flow 0.03,
  source-over moves alpha about 0.02 per dab — five to seven whole 8-bit codes —
  so all forty steps resolve at eight bits and the check demonstrated nothing
  while reading like proof. Banding needs one dab to move alpha *less than one
  code*: **flow below about 1/255**. Asserted at 0.002 now, an ordinary airbrush
  flow, with the buildup checked against the source-over series `1 − 0.998⁴⁰` in
  closed form rather than against a range.

## Session 2026-07-29l — a gradient you place with your hands

The overlay and the dragging. Nothing about the mask maths changed; what changed
is that the geometry is reachable without reading a number off a slider.

**No geometry went into the view.** Handle positions, hit testing and what a
drag means are all `CanvasLayout` — the one copy of where the picture is —
so they are tested without a window. `MaskOverlay.swift` draws what it is told.

### The fact the whole design turns on

`mask_gradient.slang` is isotropic in **normalized** coordinates, and a
photograph is not square. So:

- a radial mask's boundary **is not a screen ellipse**;
- a linear gradient's iso-alpha lines **are not perpendicular on screen**;
- a stored 45° angle **is not 45° to the eye**.

Outlines are therefore sampled in the mask's own space and mapped out point by
point, and every angle a drag takes is measured in normalized space. That second
choice is also what keeps a dragged handle exactly under the cursor: the handle
is redrawn through the same map, so the round trip is an identity. Taking
`atan2` on screen instead and converting back makes the handle slide out from
under the finger by an amount that grows with how far the frame is from square.

The screenshot shows it: the linear gradient's endpoints are visibly *not*
square to its three lines.

### Handles

| Kind | Handles | Notes |
|---|---|---|
| Linear | centre, two endpoints | an endpoint sets angle *and* length, so no rotate handle and no mode |
| Radial | centre, four axis, rotate lollipop | axis handles resize **only** — one that also rotated would drift the angle on every size tweak with nothing on screen explaining it |

One gesture for the whole overlay, not one per handle. A small radial mask
stacks its centre, both axis handles and the lollipop within a few points of
each other, and stacked SwiftUI gestures resolve by **draw order, not
distance** — `CanvasLayout.maskHit` decides on distance instead.

### A drag cannot leave a state the panel cannot show

The sliders and the canvas write the same variables, so every drag clamps to the
slider's own range. Otherwise the two disagree about the state and the next
touch of a slider snaps the mask somewhere nobody put it.

### Verified on a photograph, not by eye

`--measure` on `_PIC8220`, a linear mask at −1.6 EV local:

| region | mask off | mask on |
|---|---|---|
| zero side | 0.4703 | **0.4703** — bit-identical |
| full side | 0.4110 | **0.1981** |

Identity where coverage is zero is the invariant that matters; a mask that laid
a faint edit across the whole frame would still look right.

### Eight mutations, and the one that got through

The suite was checked by breaking the code on purpose. Seven died immediately.
The eighth — **pinning the map's origin to a constant, so the overlay ignores
panning entirely** — passed all 3100 checks, because `point(unit(p)) == p` holds
for *any* invertible map. The round trip proved the map was invertible, not that
it was the right map. `testPictureMapFollowsThePan` now pins the origin against
`ImageCanvas.transform`'s own `uvMin`, and all eight die.

⚠️ **This is the third session running where a green suite was not evidence.**
The pattern is the same each time: the test asserted a property weaker than the
claim. Mutating the code is what exposed it, and it cost about ten minutes.

### Three things only the screenshot could catch

- **`arrow.trianglehead.clockwise` is SF Symbols 6**; the app's floor is macOS
  14. It draws on this Mac (26.4) and blank on a user's. Now `arrow.clockwise`.
  A screenshot on one machine cannot catch this either — only knowing the floor
  can.
- **The overlay was clipped to the canvas, not the picture**, so a mask's lines
  ran out across the letterbox as though the gradient continued into the black
  bars. Handles stay unclipped: a handle at the edge must remain grabbable.
- **The Feather slider does nothing to a linear mask.** The shader reads that
  field only in its radial branch — a linear gradient's ramp runs from the zero
  line to the full line, so Length already *is* the feather. Measured before
  removing it: 0.50 against 0.02 gave **bit-identical** luma. Hidden for linear.

### And a measurement that was wrong before the code was

The first `--measure` run passed **pixel** coordinates where the flag takes
**normalized** ones. It clamped to a single corner pixel and reported
`sd 0.00000` for what was supposed to be a 700 × 700 patch of a photograph —
which is the tell, and the only reason it was caught. The flag prints the region
it actually measured; read that line.

**Suites:** `orion-tests` **374 checks** · `orion-viewport-tests` **2088
checks** · both 0 failures. `orion-bench` exits 0 on all three sample frames;
the M0 gate passes at 9.29 ms p95. 114 nodes, 5907 MiB.

## Session 2026-07-29k — straighten, read off the shader rather than guessed

The gap left open last session. The temptation was to write a plausible rotation
and move on; the risk with that is the same one this codebase has been bitten by
before — two implementations of one transform that agree today and drift later.

So `geometry.slang` was read first, and it settled the question:

- **It rotates in pixel coordinates of the rotated frame, not normalized ones.**
  The frame's aspect is therefore *part of the transform* — a rotation applied
  to normalized coordinates of a 3:2 frame is a different rotation. A test
  asserts the square and 3:2 cases differ, which is what would catch someone
  "simplifying" the aspect away.
- **It rotates after the crop and before the turns are undone**, so the mask
  transform does the same, in the same place.
- **The pivot is passed, not derived** — deriving it from the crop origin and
  size is what once made the preview turn about the frame centre and the
  committed render about the crop centre.

Also pinned: the pivot is a fixed point at any angle and aspect; rotating by an
angle and then its negative is the identity, which says the transform is a
rotation and not a shear; and the straighten enters the mask's own angle
directly.

## Session 2026-07-29j — a mask has to stay on its subject

Found while thinking about canvas dragging, which turned out to be the smaller
half of the problem. **Masks are placed on the picture the user sees — cropped
and rotated — but applied in `develop:linear`, which runs before the geometry
node and sees neither.** Handing displayed coordinates straight to the shader
means a mask slides off its subject the moment the frame is turned, and shrinks
away from it under a crop.

`pipe/MaskGeometry.h` is the transform, and it is the payoff for masks being
parametric rather than raster: nothing to resample, only a centre, an angle and
two radii to move.

Three things that are individually easy to get wrong, each with its own test:

- **The crop applies before the turns.** The displayed picture *is* the crop, so
  a point halfway across the visible image is halfway across the crop rectangle,
  not across the frame.
- **The angle turns with the picture**, or a gradient placed across the frame
  runs down it after a rotation.
- **Radial semi-axes swap on an odd quarter turn, a gradient's length does
  not** — a length is measured along its own direction, semi-axes have an axis
  each.

The invariant the suite leans on: place a mask where the subject appears, turn
that placement forward through the same rotation, and it must land back where it
was put. Holds for all four turns across three points.

✅ **Straighten is handled now** (session 2026-07-29k, below).

## Session 2026-07-29i — masks made reachable

An engine feature nobody can touch is not finished, so step 1 was wired all the
way out — facade, sidecar, history and panel — before starting step 2.

Both mask kinds now share a **centre and an angle**, which is what a person
manipulates; a linear gradient's endpoints are derived from those plus a length.
The shader still takes the two points, because that is the form the maths wants.

**Measured end to end:** a local +2 EV through a linear gradient recomputes
**4 nodes in about 12 ms** and moves mean luma 0.073–0.108 across the three
frames. Four, because the mask is a pure function of position and nothing
upstream of a slider ever redirties it.

⚠️ **Not built: dragging the gradient on the canvas.** Geometry is on sliders —
usable and testable, but not how anyone wants to place a mask.

### Two mistakes, both about verification rather than code

- The panel section was inserted against an anchor that no longer existed. The
  replace had **no assertion**, so it silently did nothing, compiled, and passed
  every test — the feature simply was not in the interface. Anchored edits
  without a check are the same failure mode as a test that measures the wrong
  thing.
- Having been caught by exactly this on the Auto button, the section was
  **screenshotted rather than trusted**. It renders where intended.

## Session 2026-07-29h — M4 step 1, gradient masks

`research/masking.md` §1 and §2. **A mask is its parameters, not an image** —
normalized coordinates in, R16Float alpha out, so it survives a resize and an
export matches the preview it was made on.

One kernel serves both gradients: they differ only in how a position becomes a
distance, and two shaders would be two places to fix a feather. R16 rather than
R8 deliberately — alpha is multiplied into parameters and, once brushes exist,
accumulated across many low-flow dabs, and eight bits bands under accumulation.
Same class of error as the resize that quietly cost the export its bit depth.

**The falloff is Perlin's smootherstep**, not smoothstep: C² against C¹, and the
difference is visible as a faint Mach band at the feather boundary on a clear
sky, because the eye finds discontinuities in the second derivative.

### The decision the research flags as most likely to be got wrong

The alpha scales the **parameter**, not two rendered results. The test pins it
because the two are measurably different: at coverage 0.5 with a one-stop local
exposure, scaling the parameter gives **2^0.5 = 1.414** and blending renders
gives **1.5**. Six per cent apart, and only one is a smooth multiplicative ramp
in linear light. Also asserted: zero coverage leaves the pixel *exactly* alone,
or every mask would lay a faint edit across the whole frame.

### A test that was wrong before the code was

It checked radial symmetry twelve pixels either side of centre 0.5 — which on a
64-pixel axis falls *between* pixels, so the two sides were not equidistant, and
on the steepest part of the feather that half-pixel is worth a quarter of the
alpha range. The shader was right; the sampling was not. Third time this session
that the first version of a check measured something other than what it claimed.

## Session 2026-07-29g — outside research, acted on

The developer had a second session research the unsourced register. Four
correctness fixes came out of it, and one new defect was found on the way.

**White balance tint was wrong three ways** — wrong space (the offset belongs in
CIE 1960 UCS, not the non-uniform 1931 plane), wrong direction (it runs along
the isotemperature line, whose slope turns with temperature), wrong scale. Now
implements Adobe's `dng_temperature::Get_xy_coord` with Robertson's 31-row
table. **Thirteen of Adobe's own (temperature, tint) → xy vectors assert to
2 × 10⁻⁵.** `research/color-pipeline.md`.

⚠️ **A typo is kept deliberately.** Row r = 325 ships u = 0.24702 in the DNG
SDK; recomputing the locus from Planck's law gives 0.247924, and the error there
is two hundred times any other row's — a genuine mistake in Wyszecki & Stiles,
copied verbatim by Adobe. Orion keeps it, because the point is agreeing with
Lightroom rather than with physics. Correcting it moves the white point 0.0011
in xy around 3080 K: about 23 K and 1.1 tint units, below visibility on its own
and squarely in tungsten territory when compared.

⚠️ **D65 does not sit at tint 0** — it lands near +9.77, being on the daylight
locus rather than the Planckian one. Illuminant A, which *is* a Planckian
radiator at 2856 K, reads 0.008. A test asserting `tint(D65) = 0` would be the
wrong test.

**Exposure fusion solves for its image count** rather than using a hardcoded
five. The subtlety: the edges are *input* intensities, so the exposure factor is
inverted, not applied. The paper's own table settles the reading — at α = 8 it
reports N = 6, 4, 3 for β = 0.4, 0.5, 0.6, and this reproduces all three.

**Vignetting interpolates across aperture, in the reciprocal**, as lensfun does.
The old nearest-stop behaviour rendered every aperture between two calibrated
stops identically and then jumped.

**Two claims of Orion's own were corrected rather than defended.** The HueSatMap
blue twist is a *look*, not a per-camera correction — it was fitted against two
already-rendered images, both carrying their makers' looks. And the tone bands
were measured: there is no partition-of-unity dip, because `applyTone`
normalises, but at middle grey Shadows and Highlights hold **half the authority
each**, so Shadows +1 moves middle grey +0.99 EV. Not changed, because sidecars
store slider values and moving the centres would silently re-render every edit
already made — a migration decision, not a tuning one.

### And one defect found and fixed on the way

**As-shot white balance did not round-trip.** Written as a test while changing
the locus, because changing it alters what every file opens at and nothing was
checking that. It is now **exact** — 0 K, 0.000 tint, 0.000 in the multipliers
across fifteen pairs — but it took three wrong answers to get there, and two of
them were plausible:

- **845 K out**, originally: it solved temperature with tint pinned at zero and
  then solved tint, which cannot work — tint moves the red/blue ratio the
  temperature stage matches on.
- **Alternating the two axes does not fix it.** The error surface is a curved
  valley and coordinate descent zigzags along it. Worth recording, because it is
  the obvious next thing to try.
- **120 K out** with a joint two-dimensional search, and this is where it was
  nearly left as a documented gap. The cause was the *refinement window*: one
  coarse cell either way, which assumes the coarse stage lands in the cell
  containing the minimum. Where the valley runs obliquely it does not.

**The diagnostic that settled it was printing which pair failed rather than the
worst error.** "0.026 worst" reads like a systematic accuracy limit and invites
loosening a threshold. "Fourteen exact, one 120 K out" reads like a bug and
points at the search. Same data, opposite conclusion.

## Session 2026-07-29f — dehaze profiled, and deliberately not optimised

The bench's per-node profiler now points at any control, not just clarity. What
it says about dehaze is the opposite of what it said about clarity, and it
changes what the fix would have to be.

| Node | ms | share |
|---|---|---|
| `dehaze:min h` | 4.51 | 7.2% |
| `dehaze:dark h` | 4.50 | 7.2% |
| `dehaze:max h` | 4.48 | 7.1% |
| `dehaze:dark v` | 4.40 | 7.0% |
| `dehaze:max v` | 4.40 | 7.0% |
| `dehaze:min v` | 4.40 | 7.0% |

**Six rank passes, 26.7 ms between them, every one within 2% of the others.**
There is nothing to fix *one* of. And the trick that took clarity from 70 ms to
58 does not apply: these passes are already separable — that is how they were
built, and `testDehazeGpu` checks the claim against a 15 × 15 patch computed
directly.

At ~48 MB written per pass they run at roughly 22 GB/s on a 120 GB/s machine,
so they are **tap-count bound, not bandwidth bound**: fifteen comparisons per
pixel, six times over.

The published fix is a running min/max — van Herk (1992), Gil & Werman (1993) —
which is O(1) in the window size and would take fifteen comparisons to about
three. **Not attempted**, and the reason is recorded rather than the intention:
it is a sequential scan, which is what a GPU is worst at, and adapting it means
one thread per line segment with correctness at the joins being the whole
problem. That is a session's work with a real chance of ending slower — the
same shape as the change that already backfired once on clarity.

## Session 2026-07-29e — clarity, 70 ms to 58

The Burt kernel is separable and the fused 5×5 remap node was not using that.
Split into a horizontal pass that remaps and halves, and a vertical pass that
only halves, the remapping is evaluated at five taps per output instead of
twenty-five.

| | Clarity drag | The four remap nodes | Intermediates |
|---|---|---|---|
| Before | 70 ms | 12.07 / 8.47 / 7.47 / 7.46 ms | 5491 MiB |
| **After** | **58 ms** | **~2.8 ms each** | 5861 MiB |

**The filter is unchanged, and the bench proves it rather than asserting it:**
`clarity +1` measures 0.0163 moved and +0.0095 detail both before and after, to
four decimals. A change to how something is evaluated should be invisible in
what it produces, and that is exactly what the reference tests exist to check.

The trade is **370 MB for 12 ms**. Worth taking here; the first thing to look at
on a smaller GPU.

`clarity:collapse 0` is now the largest single node at 11.96 ms — 20% of the
drag, reading four packed stacks at nine taps each at full resolution. Recorded
in `research/local-laplacian.md` along with the warning that the obvious fix for
it was already tried and made things slower.

**Suites:** `orion-tests` **356 checks** · `orion-viewport-tests` **2088
checks** · both 0 failures. `orion-bench` exits 0 on all three sample frames.

## Session 2026-07-29d — auto-enhance wired, and M3 closes

`Engine::autoEnhance` runs the measure/correct loop; the facade writes back only
the five controls auto-enhance may move and leaves the rest of the caller's
block alone. The Auto button sets ordinary sliders, so what it decides is
visible, adjustable and undoable.

### The check that matters, on real photographs

Everything else about auto-enhance is tested against a stand-in for the
pipeline. The bench probe runs the real one, and asks the only question worth
asking — did the median land where it was aimed. Not a magnitude probe with a
floor, because "it moved" is not the claim.

| Frame | median | exposure | lift |
|---|---|---|---|
| daylight | 0.617 → **0.473** | −1.16 EV | 0.00 |
| forecourt | 0.148 → **0.461** | +0.03 EV | 1.00 |
| night | 0.129 → **0.461** | +0.26 EV | 1.00 |

The two dark frames barely move exposure, because the shadow lift is derived
from the photograph *before* the solver starts — by the time it runs there is
little left to correct. That division of labour was the intent, and it is
satisfying to watch it happen rather than have to argue for it.

**One constant changed because the measurement said so.** The endpoint gain
started at 2.0 and railed the whites slider at its maximum on two of three
frames — an automatic control handing the user a setting with nowhere left to
go. At 1.0 the median still lands on the anchor and the endpoints stay
somewhere a person can argue with.

### M3's features, composed — the check that was missing

Every M3 feature was verified alone; nothing verified them **together**, and
they are exactly the kind that interact. Dehaze divides by a transmission,
exposure fusion divides one proxy luminance by another, clarity raises a
normalised amplitude to a fractional power, and the creative LUT indexes a grid
with whatever comes out of all that. A NaN from any one of them is invisible on
screen — it renders as one black or white pixel — and propagates downstream.

The bench now renders all four at full strength over a tone move, and counts
pixels pinned hard at either end, against the same frame with the four off. A
photograph legitimately contains black and white pixels; the question is whether
these features *added* them.

| Frame | four on | four off | nodes / time |
|---|---|---|---|
| daylight | 1.01% | 0.00% | 83 / 141 ms |
| forecourt | 2.59% | 0.13% | 83 / 140 ms |
| night | **0.57%** | 0.63% | 83 / 142 ms |

All three compose cleanly. The night frame is the pleasing one: it ends with
*fewer* pinned pixels with the features on than off, which is exposure fusion
lifting shadows back out of pure black — the thing it exists to do, showing up
in a number rather than in an opinion.

140 ms is the worst case in the product and it is a single render, not a drag.

## M3 — what it cost, in one table

| Feature | Nodes | Drag | Resolution |
|---|---|---|---|
| Clarity (local Laplacian) | 32 | 70 ms | full |
| Dehaze (dark channel prior) | 16 | 108 ms | full |
| Exposure fusion | 32 | 37–48 ms | quarter |
| Creative LUT | — | 7 ms | fused into the display node |
| Auto-enhance | — | ~6 renders, one click | — |

**The M0 gate never moved**: 8.8–9.9 ms p95 throughout, exposure drag still
three nodes, because every one of these disables to nothing when it is off.
109 nodes, 5491 MiB of intermediates — the number to watch on a lesser GPU.

**The two slow ones are slow for the same reason and it is written down.**
Clarity and dehaze run at full resolution; fusion does not, and costs half as
much with the same node count. `Pipeline::setProfiling` prints a per-node
ranking on every bench run, and `research/local-laplacian.md` names the two
candidate fixes in order.

## Session 2026-07-29c — auto-enhance: researched, policy built, not yet wired

`research/auto-enhance.md`. **The research turned up two negative findings that
would otherwise have become confident wrong constants**, which is the whole
argument for chasing sources before writing numbers down.

### What has no source, and now says so

- **Simplest Color Balance recommends no clipping percentage.** Not in the text,
  and its reference implementation takes the levels as mandatory arguments with
  no fallback. The widely repeated "0.5% per side" is a reading of figure
  captions calling 1% total *"optimal"* and *"moderate"*. Orion uses it and
  records it as inference in `UNSOURCED.md` §15.
- **There is no published value for the mean or median luminance of a
  well-exposed photograph.** It was looked for. What exists is CIPA
  DC-004:2004's `MAX × 0.461`, which is a target for a uniform grey card under
  controlled lighting — and the standard itself calls the choice conventional:
  *"there is no single and absolute point of definition as long as the tone is
  in the middle range."* Aiming a photograph's median at it is a judgement.

### What is sourced

The quantile definition and — usefully — the reason to work on luminance rather
than per channel, which is the paper's own sentence: per-channel stretching
*"provides both a white balance and a contrast enhancement"*, and it is blunt
that this *"is not a real physical white balance"*. Orion already has one the
photographer set. Also sourced: a published ceiling on how hard an automatic
stretch may push (Lisani, Petro & Sbert, IPOL 2012, `smax = 2`).

**A trap avoided:** Mertens' well-exposedness Gaussian at 0.5 is a per-pixel
blending kernel for a bracketed stack, *not* evidence about the mean of a
photograph. It is used correctly inside exposure fusion; citing it here would
have been exactly the wrong-but-cited constant this repository exists to stop.

### The damping was backwards, and the comment says why

The solver's step is `log2(target / median)` — the correction that would be
right if the rendered median moved in proportion to exposure. The display
transform is compressive, so the true response is *smaller* than that estimate
and every step already undershoots. Damping below 1 only slows it: measured
0.064 from the anchor after five passes at 0.7, and inside 0.02 at 1.0.

### What is left

The policy is a pure function of a histogram and is fully tested. Not built:
`Engine::autoEnhance`, the C facade entry point, the Auto button, and the bench
probe that verifies the outcome on real frames. That last one matters most —
everything so far is checked against a stand-in for the pipeline, not the
pipeline.

## Session 2026-07-29b — exposure fusion, finished

The GPU chain, built to the plan the previous session recorded. Thirty-two
nodes, **all at quarter resolution**.

| | Nodes | Drag | Resolution |
|---|---|---|---|
| Clarity | 32 | 70 ms | full |
| Dehaze | 16 | 108 ms | full |
| **Fusion** | **32** | **37–48 ms** | **quarter** |

Fusion is the cheapest of the three despite having as many nodes as clarity,
because only a *gain* reaches the full-resolution picture — the pyramid never
does. That is worth remembering when the other two get optimised.

Measured lift at full strength: mean luma **+0.105 / +0.245 / +0.257** on the
three sample frames. **M0 gate unmoved at 8.84–9.93 ms p95.** 109 nodes,
5491 MiB.

### The test that stops two implementations drifting

`ops/fuse_ops.slang` and `pipe/ExposureFusion.h` are the same equations written
twice, and every other exposure-fusion test measures against the C++ side — so
if the two ever disagree, all of those tests are pinning something the product
does not run. The GPU test compares them per pixel on both the simulated
exposures and the weights, and separately checks that **the weights sum to one
at every pixel**, because if they do not the blend is quietly a gain as well.

**Strength zero is checked bit-identical** against a deliberately violent proxy
gain. That is load-bearing, not decoration: no published parameter of this
method degenerates to the identity — α → 1 collapses the exposure factors, but
`ρ(k)` contains no α, so the simulated images remain differently-clipped copies
and their blend is not the input. It is why the slider is a power on the gain.

### Where the whole-frame reductions now live

Two features need a statistic over the entire frame, which is the one thing a
per-pixel DAG cannot express: dehaze's atmospheric light, and fusion's median.
Both are handled the same way — `render()` renders once when the value is
stale, reads back a small texture, and renders again; the per-node cache means
the second pass only redoes what the new parameter touched. Stale means the
image or white balance changed. **Neither is ever recomputed on a slider tick**,
so neither is on the interaction path.

## Session 2026-07-29a — M3 story 4, exposure fusion (part one)

Simulated Exposure Fusion — Hessel & Morel (WACV 2020 / IPOL 9, 2019) on top of
Mertens et al. (2007). `research/exposure-fusion.md` has every constant with the
quotation it came from. **The CPU maths, its reference implementation and its
tests are in. The GPU chain is not built yet.**

### The placement decision, and why it went the way it did

The method needs a bounded, display-referred `t ∈ [0,1]`; this pipeline carries
unbounded scene-linear light. The faithful option is to split `develop:display`
so fusion sees the AgX-mapped image the user sees — and it was rejected for a
reason worth recording: **a faithful full-resolution RGB fusion is 30–60 ms at
any placement**, six simulated exposures each with two pyramids, on a
bandwidth-bound GPU. Once the method must be approximated regardless, paying a
permanent ~4 ms structural tax on every render — including when the feature is
off — buys an exactness that was never reachable.

So fusion gets its own chain emitting a scene-linear gain, the same shape as the
clarity node, disabled to zero cost when off.

**The proxy must be a sigmoid over log2, not raw normalised log**, and the
failure it avoids is specific: in raw log the shadow axis is stretched, so the
median falls, so `N* = ⌊(M−1)·median⌋` allocates nearly every simulated image to
the brightened side, and the weights then read the sensor's own noise floor as
underexposed content that needs lifting. AgX is itself a sigmoid in log2, so
matching one is a cheap faithful proxy rather than an invention.

**The paper's final 1% global stretch is dropped.** In an editor it fights the
user's exposure, whites and blacks; it makes a pixel's value depend on the
current crop; and it destroys identity-at-zero. The reference implementation
keeps it so comparisons against the paper stay possible.

**The slider raises the emitted gain to its own power.** No published parameter
degenerates to the identity — α → 1 collapses the exposure factors but `ρ(k)`
contains no α, so the simulated images stay differently-clipped copies. `gain^s`
is a lerp in log-gain, exact at `s = 0`.

### Three defects found by writing the tests

- **The simulated-image search started at M = 2**, where the median-derived split
  has a single image to allocate — so a bright frame could never be given a
  darkened one and the asymmetry the whole method rests on silently never
  appeared. It starts at 5 now, the count the paper reports for its own
  recommended α and β.
- **Robust normalisation divided by an epsilon** when its two clipped percentiles
  coincided, mapping a flat field plus one outlier to solid black.
- **A monotone ramp gains tonal reversals.** Measured 2.1e-4 / 2.3e-3 / 1.1e-2
  at α = 2 / 4 / 8. That it scales cleanly with amplification is what says it is
  the method — [M07] §4.1 names the artefact — and not a mistake in the blend.
  But 1% at the recommended α is enough to band a smooth gradient, so it is
  guarded as a regression and all three numbers print on every run.

### What the tests actually pin

The clip is continuous *and* has slope one on both sides of its join — a value
discontinuity is an edge in the simulated image, a slope discontinuity is an
edge in the *weight*, which is worse because it moves. The contrast weight is
checked against a finite difference, because Hessel & Morel replace Mertens'
Laplacian filter with that derivative, so if it is not the derivative there is no
contrast measure at all. And a flat field must fuse to the weighted average of
its own remaps — computable in closed form, and the one check that catches the
pyramid, the expand, the weighting or the collapse being wrong in a way that
does not cancel.

### Also this session

- `pipe/Pyramid.h` — the Burt & Adelson helpers lifted out of
  `LocalLaplacian.h`, since fusion analyses over the same construction. They
  exist to be the reference the GPU is measured against, so one copy matters.
- The `research/` index had gone **eight files stale**. Fixed.

## Session 2026-07-28f — M3 story 3, creative LUTs

`.cube` files, applied last in the display kernel with tetrahedral
interpolation. `research/luts.md`.

**Cheap, because it is fused.** The lookup lives inside
`develop_display.slang` rather than in a node of its own, following the rule
this pipeline already had about pointwise passes. Measured: changing the look
recomputes **2 nodes and 7 ms**.

### Tetrahedral, and why the test is built the way it is

Trilinear and tetrahedral **agree exactly on anything linear across a cell**, so
a gentle LUT cannot distinguish them and "it looks right" proves nothing. They
diverge only where a LUT has a hard boundary — a key, a hue restriction, most
film emulations — because trilinear reads four corners from the far side of it.

So the test builds a table that is zero at every corner except (1,1,1), where
the two must disagree: tetrahedral returns the smallest fractional coordinate,
trilinear their product. 0.4 against 0.12.

### Two bugs written and caught, both by tests that exist for the purpose

- **The grid's row stride used the texture's width where the shader used the
  LUT's own edge.** The texture is allocated at 65 to hold any grid; the packing
  must be `b·size + g` in both places. Getting it wrong puts every blue slice in
  the wrong row, which renders as a plausible colour cast rather than as
  anything obviously broken. The identity-LUT check is the only thing here that
  would have found it.
- **`lutStrength` was missing from the display node's change detection**, so the
  slider did nothing at all. Not visible by inspection; the bench reported the
  control as dead with 0 nodes recomputed.

### The reader reads the format, not the feature

Comments anywhere, CRLF, quoted titles, mixed-case keywords, `DOMAIN_MIN`/`MAX`,
and a 1D LUT **lifted onto the 3D grid** — a 1D LUT is a separable 3D one, so
the lift is exact at the grid nodes and downstream there is one code path
instead of a branch only some files exercise. Errors name the line: a LUT that
will not load is the user's file being wrong, and they need to know which line.

Sizes above 65 are refused by name rather than truncated.

### The references, chased down — and one of them found a bug

Both were unverified when the code landed; both are sourced now.

**Adobe, *Cube LUT Specification, Version 1.0*, September 2013.** The Adobe URL
is dead; the Internet Archive has it. It settles the byte ordering outright —
§7.2 states red changes most rapidly and then writes out the C index,
`r + N*g + N*N*b` — and, usefully, **§8 requires tetrahedral interpolation for
three-dimensional tables**. So the choice of tetrahedral is sourced by the file
format itself, which is a better reason than an accuracy argument.

**The six-tetrahedra construction is Sakamoto & Itooka, U.S. Patent 4,275,413
(1981)**, col. 10 and Table 2 — the origin of tetrahedral colour interpolation.
All six of Orion's cases were checked term by term against it. ⚠️ That table has
a **printing error** in rows 3–6 of its first half (two column headers
transposed, producing geometrically impossible non-adjacent vertex pairs); the
second half is correct and disambiguates it. Recorded in `research/luts.md` so
the next person to check the source does not conclude the code is wrong.

**Reading the specification found a real defect.** Comments in `.cube` are whole
*lines*, not trailing text (§5.8) — so the parser had been truncating a look
called `Look #3` to `Look`. Fixed, with a test. This is the argument for
chasing references down rather than implementing from recollection: the code
passed nineteen checks and was still wrong about the format.

**Still open, and no longer load-bearing:** whether tetrahedral is *more
accurate* than trilinear, prism or pyramid. Usually credited to Kasson et al.,
*J. Electronic Imaging* 4(3), 1995 — citation confirmed against DBLP and
Crossref, but the paper is paywalled and was not read, so `luts.md` does not
assert the ordering. `UNSOURCED.md` §12. Two dead ends recorded there too: the
ICC specifications contain zero occurrences of "tetrahedr", and neither does
*GPU Gems 2* ch. 24, which is the Cube spec's only bibliography entry.

## Session 2026-07-28e — M3 story 2, dehaze

He, Sun & Tang's dark channel prior (CVPR 2009 / TPAMI 33(12), 2011), refined
with their own guided filter rather than the matting Laplacian they published
it with. `research/dehaze.md` carries every constant with the quotation it came
out of — the patch is 15 × 15, ω is 0.95, t₀ is 0.1, and the atmospheric light
is the brightest *of the top 0.1% of the dark channel*, which is not the same
thing as the brightest pixel.

**Sixteen nodes, seven kernels.** The graph is now
`profile → dehaze → clarity → tone`, both restorations upstream of the tone
controls so an exposure drag recomputes neither. 77 nodes, 5238 MiB.

### What is pinned

- **The atmospheric light is the haze, not the brightest pixel.** A specular
  four times brighter than the sky is offered to `airlightFrom` and rejected,
  because the paper's first stage ranks by dark channel and only then by
  brightness. Getting that order wrong hands a wrong constant to the whole
  frame, and the paper says so explicitly.
- **A separable rank filter really is the square patch.** The 15-tap minimum
  along each axis is checked against the 15 × 15 minimum computed directly, on
  random data with hard zeros and ones. Same for the maximum, which TPAMI 2013
  §5 calls for to undo the min filter's morphological dilation. That claim is
  what buys 30 taps instead of 225, so it is worth a test rather than a comment.
- **Eq. (16) as arithmetic**, with the transmission pinned to a constant so the
  recovery is checked against the equation and not against another
  implementation of the guided filter. Also: `t = 1` is exactly the identity —
  which is *why* the slider is ω rather than a blend, so zero is exact by
  construction — and `t` is floored at t₀ rather than divided by.

### The night frame legitimately does nothing, and the bench says so

`_PIC8148` measures 0.0000 movement at full strength. That is the method
working: the dark channel is near zero across a night shot, the atmospheric
light lands on a light source, and Eq. (12) returns `t = 1` everywhere — no
veil to remove. The probe is **waived with that reason printed on the line**
rather than floored, because a floor that failed there would be a floor
demanding the filter invent haze. The other two frames move 0.123 and 0.057 of
the reference and are floored at half the smaller.

### Deliberate departures, both stated

- **Scene-linear, not display-encoded.** Eq. (1) is a physical mixture and only
  holds in linear light, so applying it here is a closer reading of the model
  than the paper's own gamma-encoded inputs. The prior survives the change; the
  *statistics* quoted in the paper were measured on encoded images and are not
  re-quoted as if they held here. Consequence handled: scene-linear is unbounded
  above, so `I_c/A_c` is clamped or a specular drives the transmission negative
  and Eq. (16) inverts the pixel.
- **The percentile is over pooled 4 × 4 block maxima, not over pixels.** Max
  pooling is right for a step hunting extremes, but it is not literally the
  paper's top 0.1% of pixels. `UNSOURCED.md`.

### ⚠️ Cost

A dehaze drag is **108 ms over 15 nodes** — six full-resolution 15-tap rank
passes are most of it. Same shape of problem as clarity's 70 ms, and the
per-node profiler added last session applies directly. The M0 gate is unmoved
because dehaze at zero disables the chain.

`A` is a reduction over the whole frame, so it is not a node: `render()` renders
once when it is stale, reads back a sixteenth-resolution candidate texture,
picks `A`, and renders again. Stale means the image or white balance changed —
never a slider, so it is off the interaction path.

## Session 2026-07-28d — M3 story 1, local Laplacian clarity

`research/local-laplacian.md` is the plan of record and carries the working;
this is what happened.

### The measurement that set the design

Paris, Hasinoff & Kautz (SIGGRAPH 2011) with Aubry et al.'s fast approximation
(ACM TOG 33(5), 2014). Aubry recommend sampling the intensity range **every
standard deviation σ** — eight γ levels here, which is what got built first.

Then it was measured against Paris et al.'s exact Algorithm 1, implemented
literally in `pipe/LocalLaplacian.h` — one full pyramid per output coefficient,
no approximation of any kind — at the strongest setting the slider reaches:

| samples per σ | γ levels | mean error | max error | PSNR |
|---|---|---|---|---|
| 1.0 | 8 | 0.354 EV | 1.359 EV | **28.0 dB** |
| 2.1 | **16** | 0.151 EV | 0.696 EV | **35.6 dB** |
| 4.4 | 32 | 0.159 EV | 0.408 EV | — |

The paper's own stated accuracy is "above 30 dB", and one sample per σ does not
reach it. Two do; four buy nothing, and **that plateau is the informative
part** — it says what is left is the linear interpolation standing in for a
sinc reconstruction, which no amount of extra γ levels can fix. Sixteen is a
measured knee. σr is now a constant in its own right instead of an alias for
the γ step.

Milder settings never needed it: α = 0.5 measures 42.0 dB, α = 4 measures 49.0.
It is the strongest boost that sets the requirement, which is what the Nyquist
argument in the paper predicts.

### Two references, because one number cannot diagnose

"The GPU disagrees with the paper" has two causes that want opposite fixes. So
there are two CPU references, and the checks are separate:

- **`referenceFast`** runs the *same* approximation on the CPU. A gap between it
  and the GPU is a bug in a kernel. Worst disagreement across all three slider
  settings is under 5e-3 — the shaders run Aubry's algorithm.
- **`reference`** runs Paris's exact algorithm. A gap between it and
  `referenceFast` is the approximation being an approximation, and it has to
  shrink as γ levels grow. It does, until it plateaus.

Also pinned: **α = 1 collapses back to the input** to 2e-3. With `fd(Δ) = Δ` the
remapping is exactly the identity, so the whole chain reduces to "analyse into a
Laplacian pyramid, collapse it again". Every other check here would still pass
with a subtly wrong expand operator, because both sides would share the mistake.
That one would not.

### What is in that the paper says must be

**The noise term.** §5.2, *Reducing Noise Amplification*: when α < 1,
`fd(Δ) = τΔ^α + (1−τ)Δ`, τ a smooth step over 1%…2% of the range. The paper
states every result in it was computed with that function. It matters because
the α < 1 branch has unbounded slope at the origin — without the term, the
lowest-amplitude signal in the frame receives the largest gain of anything in
the picture, and on a photograph that is the noise.

**Luminance only, ratios kept** (§5.3, Figure 9). Filtering the channels
separately also boosts *colour* contrast, which for a clarity slider means
fighting the grading wheels.

### Placement, and why the gate did not move

Before the tone controls, next to the guided filter, for the guided filter's own
reason: exposure is a multiply, so in log2 it is an additive constant, and the
Laplacian of a constant offset is zero. Clarity computed before exposure is
therefore *bit for bit* what computing it after would give, while all thirty-two
of its nodes stay cached for the slider people actually drag.

**M0 gate: 10.61 / 10.18 / 9.79 ms p95** on the three frames, exposure drag still
three nodes. Clarity at zero disables the whole chain and a disabled node
resolves to its first input, so it costs nothing when unused.

### ⚠️ A clarity drag is 70 ms, and the profile says where

Correct, not yet interactive. `Pipeline::setProfiling` now times each node in its
own command buffer and `orion-bench` prints the ranking every run:

| Node | ms | share |
|---|---|---|
| `clarity:remap 1.0` | 12.07 | 16% |
| `clarity:collapse 0` | 12.01 | 16% |
| `clarity:remap 1.1` | 8.47 | 11% |
| `clarity:remap 1.2/1.3` | 7.46 each | 20% |

The four remap nodes are 47% between them: each remaps a 5×5 footprint for four
γ at once, a hundred remappings per output pixel.

**That tool exists because a hunch was wrong first.** The collapse kernels read
all four packed stacks at all nine expansion taps while only two of sixteen γ
are ever used, so they were rewritten to fetch only what a pixel needs. Output
was bit-identical and it ran **slower — 78.9 ms against 71.6** — the branch
diverges more than the saved fetches were worth. Reverted, profiler written, and
it pointed at a different kernel.

Next, in order: separable halving in threadgroup memory (25 taps → 10, and the
remapping count falls with it), then measuring whether a full-resolution remap
into its own texture is a win or a wash. Neither changes the filter, so the
reference tests cover both.

### Also this session

- Intermediates **4027 → 4567 MiB**. The number to watch on a lesser GPU.
- `PixelFormat::R16Float` added — the pyramids are normalized into [0, 1], where
  a half-float quantum is 0.006 EV, an order of magnitude under the noise floor
  τ already declines to amplify.
- Bench probes `clarity +1` and `clarity -1` on the `Detail` metric, floors at
  half the smallest ratio over all three frames. Mean luma is the wrong
  instrument for a local-contrast filter, as it was for sharpening.
- **Texture is not built.** Paris §5.2 and Figure 7d/e specify it as the same
  filter restricted to fine pyramid levels, and §5.2 explicitly licenses
  interpolating between level subsets for a continuous control. It needs a
  second set of pyramids at its own α; the mechanism is written up.

## Session 2026-07-28c — closing M2

Everything M2 listed is now built, and the outside review's P2/P3 findings are
closed with it. In the order the work landed.

### The purple sky, closed — the camera profile grew its second stage

`research/camera-profiles.md` diagnosed it last session: Orion had one of the
five parts of a DNG profile, and a 3×3 matrix cannot be right for a saturated
narrow-band stimulus. The fix is the specification's own **HueSatMap** stage,
built as a real 90 × 25 table with `ValueDivisions = 1` in the spec's entry and
loop order — not a blue-only special case, so a `.dcp` reader later is a reader
and nothing else.

**The trap the plan nearly walked into:** `ProfileHueSatMapEncoding = 0`
requires the table to apply in **linear ProPhoto HSV**. Orion works in linear
Rec.2020. Indexing HSV built from Rec.2020 components would have looked right
against a hand-fitted table and been silently wrong the day a real profile
loaded — which is the entire reason for wearing the spec's shape. The node
converts in and out, from Lindbloom's three published matrices kept as separate
factors so each is checkable.

| | R/B | G/B |
|---|---|---|
| Orion, before | 0.622 | 0.678 |
| **Orion, after** | **0.451** | **0.689** |
| target (Sony/Apple mean) | 0.450 | 0.692 |

Fitted at −8°, saturation ×1.05, centred on 250° over a 60° half-width, swept
against two independent renderings of the same frame with a foliage patch and a
white sign watched for spread. The hazier sky near the horizon lands at 0.636
against 0.647 from the *same* numbers, because the correction is weighted by
saturation rather than applied flat across the hue.

Costs one full-resolution pass, upstream of exposure, so the gate did not move.

### What is pinned, and where

The process finding that built the `feedback/` folder was *the code was fine
wherever it was measured*. So:

- `orion-tests` holds everything checkable without the sample frame: the matrix
  round trip is the identity to 1e-5, every zero-saturation table entry is
  exactly (0, 1, 1), no entry scales value, an identity table leaves every pixel
  where it was, a grey ramp stays grey at every level, and blue moves while
  foliage and skin do not.
- `tools/huefit.py --check` holds the part that needs a photograph — it renders
  `_PIC8095.ARW` and fails if the sky drifts past 0.02 from the target. Outside
  the suite because `samples/` is local-only, and it measures the whole
  pipeline, so it also catches an upstream change that moves the sky without
  touching the node.

### Lens database — the data, not the library

The maths was never the missing part. lensfun's models were already implemented,
tested and running on the GPU; the measured coefficients were sitting in XML.
Linking the library would have added an LGPL-3 dependency, a build step and a
second implementation of the same polynomials to obtain a number that can be
read directly.

`data/lensfun/` is the database vendored unmodified with its CC BY-SA 3.0
licence — **1,558 lenses**. `pipe/LensDatabase.cpp` parses it once per process.
The shader now evaluates ptlens, of which poly3 is the a = c = 0 case, so both
of the database's distortion models land in one kernel; `autoScale` evaluates
the same polynomial, with a comment saying why it must.

**Matching is deliberately conservative, because a confident wrong profile is
worse than none** — it distorts the frame and reports that it measured it.
Names below eight characters never match; a differently-spelled match is
flagged and the panel says so. The developer's own Sigma 24mm F1.4 DG DN is not
in the database and correctly reports nothing rather than borrowing the DSLR
DG HSM entry, which is a different optical design. That case is asserted.

`a`, `c`, p_b and p_c would all have shipped untested — a manual slider can
only ever set `b` and p_a — so each has its own GPU assertion that it pins the
corner and moves the interior.

### Broader camera support

- **Unsupported sensors are refused by name.** X-Trans (6 × 6 mosaic), Foveon
  and linear DNG, and four-colour CFAs each produce their own message instead of
  a scrambled picture that reads as a bug in the pipeline.
- **The 2 × 2 black-level pattern is exact rather than averaged.** LibRaw's
  pattern lines up with the CFA cell for cell; averaging it left the spread in
  the shadows as a colour cast on every frame, with no control that could remove
  it. Asserted on RGGB and BGGR.
- One extension list. The Open panel took eight and the folder scan ten, so a
  folder could show a file the dialog refused.

### The review's P2 and P3 findings

| # | Was | Now |
|---|---|---|
| 6 | Sidecar escaping compounded one layer per save (`R&D` → `R&amp;D` → …) | unescape on read, asserted over three round trips |
| 7 | **Every export published the photographer's GPS**, silently | three-way control, default strips location, and the default itself is asserted |
| 8 | Generated design tokens existed and nothing imported them | the mirror is deleted; the generator emits sRGB (it emitted P3 while the app built sRGB) and a numeric `Components` enum for Metal |
| 9 | Serial folder load; a 30 ms busy-poll | bounded six-wide task group; `open` is async and the poll is gone |
| 10 | Five copy/behaviour mismatches | all five, including the export dialog now defaulting to the photo's own name |
| 11 | Dead state, per-call curve re-sorting | `minimumRating` deleted, curve sort and tangents hoisted per channel |
| 12 | Grading wheels, curve and filmstrip were mouse-only | all three have keyboard and VoiceOver paths; wheels speak hue and strength, the curve walks its points, filmstrip cells are buttons |
| 13 | `OrionApp.swift` 1,321 lines against a hard 1,000 | 1,120, with the three tool panels lifted into `DevelopPanels.swift`. **`apps/tests/main.cpp` is 2,828 and still over — stated, not softened** |
| 14 | Two copies of "where we are", four wrong entries | ROADMAP's status section is a pointer to this file; UNSOURCED rewritten |

**Not done, on purpose:** finding 9's suggestion to detach `Engine.exportedSize`
from the main actor. It renders a full-resolution frame through the same
pipeline the canvas is using, so detaching it races the render rather than
moving it — the fix is a serialized engine queue, which is a change to the
facade's threading contract and not a one-line detach. The hitch stays until
then.

### Adobe, and what Orion actually depends on

`/NOTICE` now carries the string the DNG patent grant requires — implementing
the specification triggered it, and the HueSatMap node is that implementation.
**No Adobe data is shipped.** Both profile values are fitted from the camera's
own JPEG and a second independent rendering, which is why they are also in
`research/UNSOURCED.md` §9: the *stages* are published, the *numbers* are
Orion's own measurement of one camera body.

## Session 2026-07-28b — answering the outside review

`feedback/2026-07-28-senior-review.md` is a senior review with 17 findings. This session took the three
P1s, one P2, and the process finding underneath them.

**Suites:** `orion-tests` **237 checks** (was 211) · `orion-viewport-tests`
**2081 checks** (was 2067) · both 0 failures. `orion-bench` now exits nonzero
when a control is dead or weak; verified by forcing one.

| # | Finding | What it was | Now |
|---|---|---|---|
| 1 | Edits lost on quit | `saveDevelop` ran only on a photo switch | `app/Autosave.swift`, coalesced writes + `willTerminate` |
| 2 | Disabled guide fed garbage to whites/blacks | `whites +1` moved mean luma **+0.1105**; correct is **+0.0064** | flag + pixel-EV fallback |
| 3 | Lens killed incremental invalidation | 7 nodes per exposure tick with a vignette on | 3, asserted by the bench |
| 5 | Newest node untested, bench could not fail | no grading GPU test, no probe, exit code ignored the probes | all three |
| — | **Lens distortion smeared the frame edges** | found by the developer mid-session | autoscale, `pipe/LensGeometry.h` |

### The correction the git history needs

The commit `02ad412` **"Edits persist per photo" claimed more than it built.**
It wired the sidecar and called it on a photo switch, and nothing else — so
editing one photo and quitting lost the work, which is the ordinary case. The
gap was noticed in that session, not built, and then shipped under a title that
reads as solved. This paragraph is the correction; the code landed today.

The same overstatement is in `feedback/2026-07-28-performance-and-quality.md` §2's "exposure drag,
3 nodes, 11.5 ms", which held only with every lens slider at zero — the one
state the bench measured. Both are fixed in the doc as well as in the code.

### What each fix cost, measured

- **Guide chain.** `develop_linear` sampled `guideAb`/`guideRaw` unconditionally.
  With highlights and shadows at zero the seven guide nodes are disabled, and
  `Pipeline::resolve` walks a disabled node back to the last live producer — the
  colour matrix. So linear RGB was read as log2 luminance and as filter
  coefficients. The *offsets* were zero, but the four band weights normalize to
  a partition of unity and two of them came from that garbage, so they sat in
  the denominator and diluted the other two per pixel.
  GPU-measured: blacks −1 at its strongest was worth **0.758 EV instead of
  1.948 EV**. On a real frame `whites +1` moved mean luma **+0.1105** where it
  should move **+0.0064** — an endpoint control acting as a second exposure
  slider on every photo. Fixed by telling the shader (`guideEnabled`) and
  falling back to the pixel's own EV, which is the correct semantics anyway.
- **Lens invalidation.** `correctingLens ||` tested nonzero, not changed. One
  clause deleted. The bench now drags exposure with a vignette and distortion
  applied and asserts the node count matches the clean drag: **3 of 28, 11.7 ms.**
- **Lens autoscale.** poly3's `(1 − k₁)` pins `r_d(1) = 1`, so the corners stay
  put — but `r = 1` is the corner and the frame is a rectangle. The edge
  midpoints sit at r ≈ 0.83, where a negative k₁ multiplies by 1 + 0.31·|k₁|.
  At the slider maximum that fetches **325 px past a 6024 px frame**, and
  `sampleClamped` returned the border pixel for all of it. Measured before the
  fix on `_PIC8148.ARW`: three columns 18 px apart returned identical means to
  four decimals. After: they differ, as real content does. Written up in
  `research/deep-research-2026-07-27.md` §4.
- **A half-texel shift in the same shader**, found while fixing the above. `d`
  is measured from pixel centers and `sampleClamped` indexes texels, so every
  fetch landed exactly between two texels — a half-pixel shift and a bilinear
  blur over the whole frame the moment any lens slider left zero. It survived
  the identity test because that test reads a linear ramp, where the average of
  two neighbours is the value between them, and the tolerance was 2e-3 — which
  is exactly one half of the ramp's texel step. The tolerance is 1e-4 now.

### The class of bug underneath findings 2, 3 and 5

All three lived in the gap between *something happened* and *the right thing
happened*. Three changes, in order of how much they are worth:

1. **Every bench probe is judged against its own baseline.** A probe that lifts
   exposure 5.5 EV was being compared against an unlifted frame, so the lift
   was counted as the control's own effect — it flattered the highlight grading
   wheel by more than tenfold. Fixed by giving each probe a `context` and
   measuring context-versus-context+control.
2. **Every probe asserts a magnitude**, as a fraction of what a reference
   control moves on the same frame, and **the exit code honours it.** Floors are
   printed on every line, passing or not. Verified on both sample frames.
3. **Invariant probes, not just magnitude probes.** Two exact questions that the
   loose version passed while the code was wrong: blacks and whites must land
   identically with the guide chain on and off, and an exposure drag with lens
   corrections applied must recompute the same node count as a clean one.

Also: `sharpen` was measured by mean luma, which an edge filter barely moves by
construction — it read −0.0005 on `_PIC8220`, under every other probe's noise.
There is a `Metric::Detail` now (neighbour-to-neighbour luma), and denoise has a
probe for the first time.

### Found while doing the above — not fixed, filed

**Feedback #4 is worse than it reads, and now has numbers.** The grading zones
partition on *linear* luma at 0.0/0.5/1.0, and separately the offset is an
additive constant in unbounded scene-linear — so what a wheel is worth relative
to the pixel falls as 1/level, while `wh` only switches on past linear 0.5. The
highlight wheel is therefore enabled exactly where its authority has gone. On
both sample frames lifted 5.5 EV it measures **−0.0000 and +0.0001** mean
chroma: inert. Midtones manage −0.0007. The shadow wheel works (+0.0396).

Third effect, same root: the shader clamps at zero and `kStrength = 0.03` at
full radius is ±0.038 — larger than a deep shadow — so the negative channels
stick at zero, the offsets stop cancelling, and the wheel *brightens* what it
should tint. A 0.0096-linear patch comes back at 0.0124, **+29%**.

Written up as `research/UNSOURCED.md` §8 with the fix (perceptual zone weights,
level-scaled offsets). The two dead probes are `WAIVED` in the bench with that
number, so they are stated on every run rather than quietly absent. **This is
the next story.**

### The M0 gate: 12.98 → 9.61 ms p95

Asked whether locality or caching had anything left to give. The answer is a
number: the pipeline runs at **96 GB/s against the M4's 120 GB/s peak** — 81%.
Spatial locality inside a kernel is already maxed, temporal locality across
frames *is* the per-node dirty cache, and the only lever left is moving fewer
bytes. Full working in `feedback/2026-07-28-performance-and-quality.md` §2.

So: the tail of the graph is eight bits for the screen now. The drawable is
`bgra8Unorm`, so `rgba16f` through `develop:display` and `geometry` was buying
precision nothing could show. Export widens the tail around its own read and
narrows it again, so 16-bit output is untouched.

| Tail | median | p95 | intermediates |
|---|---|---|---|
| RGBA8 (screen) | 9.09 ms | **9.61 ms** | 3828 MiB |
| RGBA16F (export) | 12.07 ms | 12.64 ms | 4211 MiB |

That is the 2.6 ms `feedback/2026-07-28-performance-and-quality.md` said 16-bit export had cost,
handed back, with the capability kept.

**Two process notes, because both nearly cost more than the change was worth:**

- **The first measurement was wrong and said "no gain at p95".** It compared a
  build from ten minutes earlier against one taken now, and this machine
  throttles hard across a long bench session — the same wide configuration read
  12.58 ms cool and 22.68 ms warm. The bench measures both tails **in one
  process, interleaved, and repeats the first configuration as a drift check.**
  If the two matching runs disagree, the comparison is noise and the numbers
  say so.
- **The bench's own readback was still asking for half float.** Downloading an
  `RGBA8Unorm` texture with a stride computed for `__fp16` does not fail, it
  returns nonsense — mean luma read 0.0023 instead of 0.0714 and every probe
  went with it. Four readers had the same assumption baked in (`Engine`'s
  histogram, `Engine::readOutput16`, the bench, the screenshot harness). All
  four ask the texture what it is now.

The display node dithers on the way down (ordered, Bayer 4×4). Not decoration:
geometry *resamples* those values and quantises a second time, and two roundings
of a smooth gradient is where contouring comes from — a night sky is the case.
The bench asserts the screen and export paths agree to better than one 8-bit
step; measured **0.00004 luma, 0.00005 chroma** with exposure, blacks and a 3°
straighten applied.

**Not done, and why.** Fusing `geometry` into `develop:display` would save
another ~2 ms, but `geometry.slang` resamples display-encoded pixels on purpose
— averaging unbounded scene-linear blooms a specular edge, which is why film
and VFX resample in log rather than linear. Fusion forces scene-linear
resampling and merges two small shaders into one large one. With 6.4 ms of
headroom that trade is not worth taking. Decision #40.

Also worth knowing: **the 4.2 GiB of intermediates is not waste, it is the
cache.** Resource aliasing would cut it to ~600 MB, but a cached node's output
has to stay resident, so aliasing and per-node caching are mutually exclusive.
Decision #39, written down because somebody will try.

### The flat, dark opening render — closed, and it took the shadow complaint with it

Two complaints from the developer, one root. *"Looks disgusting when loaded in"*
and *"shadows literally colours EVERYTHING"* were both the same defect: Orion
opened a daylight frame **1.3× darker** than the camera's own JPEG, which reads
as flat, **and** put the whole picture half a stop below middle gray — where the
grading shadow band legitimately catches it.

The mechanism has a name: the DNG specification's **`BaselineExposure`**
(tag 50730), *"by how much (in EV units) to move the zero point"*, which Adobe
applies silently on open. Orion had none.

LibRaw does not carry the tag for native ARW and no DNG Converter is installed,
so it was **fitted, not read**: mean absolute luma error over six patches per
frame, swept over a 2-D grid of exposure against base contrast, against two
independent references — the camera's JPEG and Apple's RAW rendering.

| Frame | best EV | best contrast | error |
|---|---|---|---|
| `_PIC8095` daylight | **+1.20** | **1.45** | 0.0171 |
| `_PIC8220` forecourt | **+1.20** | **1.45** | 0.0103 |
| `_PIC8148` night sky | +1.60 | 2.05 | 0.0068 |

Two of three agree exactly. The night frame's surface is nearly flat (0.0083 at
the old defaults against 0.0068 at its own minimum) because a near-black frame
barely moves a mean luma — its preference is noise, and at (+1.2, 1.45) its error
is still 0.0150.

Applied as `kBaselineExposureEv`, added inside `apply()` so **the Exposure slider
still reads 0.00** and Reset returns to the baseline rather than to darkness.
Base contrast 1.15 → 1.45. Daylight mean error **0.1543 → 0.0194**, and Orion now
lands *between* Sony and Apple on five of six patches — the right place to be
when two references disagree.

⚠️ It fits **one body**. A per-camera `BaselineExposure` and a property of
Orion's own AgX zero point cannot be told apart from one camera's data. The
caveat is written at the constant so whoever adds the second body re-measures.

**Bench floors recalibrated across three frames.** Adding the daylight frame
tripped six probes — a bright picture genuinely has no deep blacks, little noise
and few shadows, so those controls move less in it. Not regressions. One frame
had tripped four probes on the second; two frames tripped six on the third.
Floors are half the minimum ratio over all three now, and the reason is written
where the numbers are. All three frames exit 0; the gate passes on all three
(9.70 / 9.76 / 10.89 ms p95).

`apps/pixstat/` is in the repository rather than a scratchpad, with its
orientation handling rewritten as a pixel remap — the CGContext version was
vertically flipped, which is why the first "sky" measurement sampled foliage.

**Still open: the sky is still violet.** After the exposure fix its G/B is on
target (0.678 against Apple's 0.671) and the remaining error is almost purely
excess red (R/B 0.622 against a target of ≈0.45). One axis instead of two, which
is exactly why the exposure had to land first. `research/camera-profiles.md` has
the HueSatMap specification, the ProPhoto-HSV requirement, and the target.

### Grading regraded — feedback #4 closed

The developer reported it independently while this was being fixed: *"the color
grading for the shadows feels like it takes over the entire photo... it might
actually be pulling from the raw image instead of what's currently being
viewed."* Right in spirit. It reads the current scene-linear state, not the raw
— but it decided which zone a pixel was in using **linear** luminance, which
does not correspond to anything you can see on screen. Middle gray is Y = 0.18,
so it weighed 0.70 shadows.

Shadow-zone weight, before → after:

| Pixel | Linear Y | Old ws | New ws |
|---|---|---|---|
| Middle gray | 0.18 | **0.70** | 0.19 |
| A daylight sky | 0.30 | **0.35** | 0.08 |
| Two stops down | 0.045 | 0.87 | 0.77 |

Zones are Gaussian bands on `log2(Y/0.18)` at −2.5 / 0 / +2.5 EV, σ = 1.6 —
the same partition-of-unity construction the tone bands use, so a photograph has
one idea of where its shadows are. And the offset now scales with the pixel's
luminance, so a wheel is a constant chromaticity shift at every exposure instead
of an additive constant whose authority fell as 1/level. `k = 0.25` is derived
rather than tuned: `saturation = 1.5k/(1+k)`, so full travel is 30% from neutral.

`testColorGradeGpu` pins the property that matters: the same wheel measures
**0.1077 relative chroma at −3 EV and 0.1079 at +3 EV**, six stops apart. All
three bench probes pass on both frames; both waivers are gone.

### The instrument was wrong three times over

Worth recording, because it cost more than the fix did. A grading wheel rotates
hue at roughly constant saturation. **Mean luma, mean chroma and mean saturation
each reported a working wheel as doing nothing** — three different instruments,
same blind spot, because a frame mean cancels a rotation.

The bench gates on **mean absolute per-pixel movement** now. The summary metric
is still printed, for insight into *what* changed; movement decides *whether* it
did. It immediately paid for itself elsewhere: `tint +0.5` moves 0.0090 while
its mean-luma delta is −0.0014, so the old gate was reading a sixth of what that
control actually does.

Floors are half the *smaller* ratio measured across both sample frames.
Calibrating on one was not enough — four probes tuned on the night sky tripped
on the lit forecourt, because how far saturation, temperature, sharpening and
denoise move depends on how saturated, warm, detailed and noisy the picture
already is.

### Filmstrip: the frame line was 3 pt away from its own picture

Reported by eye, and a screenshot answered it. `.padding(3)` was applied
*before* the border overlay, so the line was drawn on the padded bounds and a
strip of film base sat between the frame line and the photo on every side — the
picture read as floating in a hole rather than as part of the film. The overlay
goes on the picture now, and the padding is horizontal only: on real stock the
rebate *is* the frame's top edge, while sideways the base is what separates one
negative from the next. New scenes `lens-barrel` and `lens-pincushion` in the
harness.

### Why the pipeline still runs at full resolution — asked, and worth recording

Not a stance, a deferral. The preview-ROI path in `ARCHITECTURE.md` is designed
and unbuilt because the budget passes without it: exposure drag is 11.9 ms p95
against 16 on this machine. Three separate things get conflated under "preview":

- **Tiling / chunk-by-chunk** does not reduce the work, it spreads it. It helps
  a first paint and does nothing for a slider drag, where a half-updated frame
  is worse than a whole one 12 ms later.
- **A downscaled proxy** is the real saving and the real risk. Every
  scale-dependent filter needs a scale-aware parameterization — the noise
  profile is per-pixel, the sharpen radius is in pixels, the guided filter's
  radius is `max(4, longest/200)` — and any mismatch means the preview lies
  about the export. That is the worst bug class in an editor: you find out after
  you have finished editing.
- **ROI — render only the visible region at the zoom you are at** — is the one
  that pays and the one that is designed. At fit the screen is ~2 MP against
  24 MP, roughly a tenfold saving, and it does not need a second parameterization
  because the pixels are the same pixels.

So the trade being taken is: one render path, no possible preview/export
disagreement, and 100% zoom shows real pixels with no re-render — against
carrying 4.2 GiB of intermediates and no headroom on a lesser GPU. The trigger
to build ROI is already named and already measured: temperature and tint at
43–53 ms, which no amount of caching fixes because white balance rewrites the
head of the graph.

## Overnight run — 2026-07-28

Working agreement for this run: commit and push per feature, screenshot every
major feature, measure the engine's output rather than eyeballing it, and only
reach for Gemini if `research/` genuinely does not cover something. It has not
been needed so far.

**Done**

| | Commit |
|---|---|
| Crop constrained to the turned frame; straighten opens to ±90; pivot is the frame center; corner marks in a fixed box; culling moved to a Photo menu | `28ca074` |
| Tone curve panel — the engine's spline had been unreachable through the facade since M2 | `829e565` |
| Profiled wavelet denoise, with a per-frame Poisson–Gaussian fit | `bb06700` |
| Highlight reconstruction; fast guided filter (90 ms → 19.6 ms); a bench that stops crying wolf | `a4ac2fa` |
| Lens corrections — distortion, TCA, vignetting | `bd8c23c` |
| Export panel: measured file size, typed dimensions | `06fff34` |
| 16-bit output end to end; red/blue swap in the screenshot harness | `a50908c` |
| Edits persist per photo; keys work; compare survives a rotation | `02ad412` |
| **A blown highlight came out magenta** — linearize never clipped | see below |
| Every adjustment resets from its own readout | see below |
| Compare came apart on zoom; the top/bottom split was upside down | see below |
| Analog track controls; American spelling; a sidecar that survives a rename | see below |
| Export color space, EXIF and rating; a resize that keeps its depth | see below |

### Export, finished

sRGB, Display P3 and Adobe RGB, converted by ColorSync rather than by a matrix
typed in here — CLAUDE.md's "prefer mature libraries", and a hand-rolled
chromatic adaptation is a cast waiting to happen. The pixels are tagged sRGB
where they are made, because that is what they are, and converted from there;
tagging them as the destination would relabel without moving them, which is how
a file comes to open oversaturated.

EXIF, lens, date and the star rating are carried onto the file, read with
ImageIO rather than exiv2 — DECISIONS #10, and it reads the RAW's own blocks
straight out of the container. Orientation and the RAW's pixel dimensions are
dropped deliberately: the geometry node has already applied the rotation, so
copying the tag would tell every viewer to turn the picture again.

Verified end to end on `_PIC8220.ARW`: Make SONY, Model ILCE-7M3, lens
"24mm F1.4 DG DN | Art 022", ISO 3200, 1/80 at f/1.4, the capture date, Software
Orion, rating 4, and the three profiles each landing on the right file.

**A resize was dropping to eight bits.** The 16-bit output path shipped the
night before survived exactly as far as the first resize, and only for exports
with a size limit — the ones nobody re-checks. The test now reads the written
file back, because a PNG of a smooth ramp compresses to almost nothing at either
depth and byte counts cannot tell them apart.

The bench was passing a bare options struct, so it measured a write the product
never performs. It builds the same options the app does now.

### Compare and zoom

The split happens across the drawn quad in the canvas shader; the panel was
drawing the divider, the labels and the grab band against the *fit* rectangle.
Same rectangle only at fit. `CanvasLayout.drawnRect` is where the picture
actually is. The top/bottom split was also upside down — the fragment shader
recovered its position by unpicking `uv`, which is flipped in y and scaled into
a sub-rectangle of the texture. The quad coordinate is a varying now.

### The sidecar could not survive a rename

Swift's synthesised decoder throws on a missing key rather than falling back to
a property's default, and `Engine.restore` swallows it with a `try?`. Renaming
`denoiseColour` would have silently discarded **every** adjustment in **every**
sidecar on disk, and the photo would have opened unedited with nothing said —
and that was already true of adding any field at all. Decoding is field-by-field
and forgiving now, and still reads the old spelling.

### The magenta highlights

The one that mattered. `linearize` scaled each channel by its white-balance gain
and clamped only at zero, so a blown pixel — which the sensor delivers as
(S, S, S) — left the node as the gains themselves, about (2.2, 1.0, 1.6) on a
warm frame. Everything downstream preserves ratios, so the tone curve, the
color matrix and AgX all carried it faithfully to the screen. Every clipped
light in a night shot rendered magenta.

Clipping all three to one ceiling is dcraw's default, and it belongs in the
mosaic for dcraw's reason: RCD interpolates across an unclipped neighbour, so
clipping afterwards leaves a fringe instead of a clean edge. Written up in
`research/color-pipeline.md`.

Measured over the blown sign in `_PIC8220.ARW`: mean saturation **0.242 → 0.015**,
R/G/B 0.878/0.677/0.896 → 0.800/0.809/0.811.

A side effect worth knowing: highlight recovery is now a measured no-op on that
frame (saturation 0.0146 → 0.0147 at full strength), because a fully blown pixel
is already white and there is nothing to correlate. It still earns its place
where one channel clipped alone. Left off by default, but the panel copy no
longer promises to fix a magenta the pipeline no longer produces.

⚠️ The clip moves with white balance, by design — the white point does. It also
spends the headroom a reconstruction could have used. Both are the right trade
against a cast that was on every frame.

**Still to do, in order**

0. **Masking** — the largest gap, and now the best-specified. `research/masking.md`
   is a full plan of record from a deep-research run: mask primitive maths,
   parametric-not-raster stroke storage, alpha applied to the *parameter* rather
   than blended, mask-group algebra, and Apple Vision for subject. The finding
   that matters most: **guided feathering is the guided filter's own named
   application** (He/Sun/Tang §"Matting/Guided Feathering", r = 60, ε = 1e-6), so
   auto-mask, feathering and AI-matte upsampling all come from the node already
   in the graph with one extra input binding. Steps 1–3 of that plan need no new
   dependency and no new licence position.

1. **A lens database.** The corrections are built and manual. lensfun would set
   the coefficients from what the EXIF names; the maths does not change. This is
   the largest remaining item — a dependency plus an XML database, not an
   afternoon.
2. **A real wide gamut.** The export picker offers sRGB, Display P3 and Adobe
   RGB, and converts correctly — but the display transform ends in Rec.709
   primaries and saturates there, so **nothing Orion renders yet falls outside
   sRGB**. Choosing P3 today buys correct tagging for a managed workflow, not
   more saturation. Widening it for real means giving `develop_display.slang`
   its output primaries as a parameter and moving the sRGB encode with them.
   The panel and the C header both say this plainly rather than implying
   otherwise.
3. **The EXIF read costs ~90 ms per export.** `writeImage` opens the RAW with
   ImageIO on every write to lift its metadata. Caching the property dictionary
   at open would give it straight back; export is off the interaction path, so
   it has not been worth doing yet.
4. **Temperature drag is 43 ms.** Structural: white balance rewrites the head of
   the graph, so the demosaic reruns. The fix is degrade-then-refine (a cheap
   demosaic mid-drag) or the preview-ROI path in `ARCHITECTURE.md`. Neither is
   built, and neither is small.

Verified 2026-07-28: a TIFF export reports `bitsPerSample: 16`, 6024×4024,
145 MB — which is exactly 6024·4024·3·2 bytes.

### Latency, re-measured 2026-07-28 (Sony ILCE-7M3, 6024×4024, M4)

**27 nodes, 4027 MiB of intermediates** — up from 16 nodes and 2.6 GiB. The M4
recommends a 17.8 GiB working set so this is comfortable, but it is the number
to watch on a lesser GPU.

| Drag | Nodes | Time |
|---|---|---|
| Exposure | 3 | 11.5 ms |
| Highlights / shadows | 10 | **23.6 ms** (was 90.1) |
| Color mixer | 5 | 19.2 ms |
| Temperature / tint / sharpen | 11 | ~50 ms |

**M0 gate passes at 11.67 ms p95**, against 16 ms. Re-measured 2026-07-28 after
the highlight clip: **12.70 ms p95** on `_PIC8220.ARW`. The clip is one
instruction in `linearize`; the difference is the frame, not the change.

⚠️ It was 9.04 ms before 16-bit output. Writing `RGBA16Float` from the display
and geometry nodes doubles the bytes those two move, and that is 2.6 ms of the
budget spent on a capability nobody sees on screen. It is a deliberate trade —
4.3 ms of headroom is still real headroom — but if the budget ever gets tight
this is the first place to look, and the fix is a second display path used only
for export rather than a wider one used always.

Temperature is over budget and always will be: it rewrites the head of the
graph, so the demosaic reruns. The fix is degrade-then-refine or the preview-ROI
path, neither of which is built.

### The screenshot harness

```
./build/Orion.app/Contents/MacOS/Orion --screenshot out.png --photo x.ARW \
    --scene crop-angle [--measure x,y,w,h] [--size 1680x1050]
```

Renders the real view hierarchy offscreen — no Screen Recording permission,
which a terminal does not have. Scenes live in `app/Screenshot.swift`; add one
there when you add a feature. `--measure` prints mean and standard deviation
for a region of the engine's output, which is the only way to tell whether a
filter did anything: noise that is obvious at 100% vanishes into a screenshot
scaled to fit a review pane. It is what caught the denoiser doing nothing, and the export panel's size
estimate reading twenty percent high.

`--measure` also prints mean saturation, which is what turned "there is purple
in my photo" into a number that could be watched going down.

A hover cannot be staged in an offscreen render, so `AdjustmentSlider.previewHover`
forces it for the `reset-hover` scene. Whether a control shifts sideways when its
hover state appears is exactly the kind of question a screenshot answers and
reading the code does not — this codebase has shipped that bug three times.

**What it does not prove:** the canvas is drawn as a still read off the GPU, not
through `MTKView` — AppKit cannot capture a Metal layer. Canvas geometry stays
the viewport suite's job.

**Second blind spot, found 2026-07-28:** a `rotation3DEffect` does not survive
`cacheDisplay` either. The mode dial renders every tab square-on in a
screenshot, and looks correct, while the live app shows the turn. Anything
relying on a 3D transform has to be checked by eye. The dial's horizontal scale
is deliberately redundant with its rotation so the part a screenshot *can* see
is still the right shape.

---

## Where we are

**M0 is done and the gate passed with room to spare.** A 24 MP Sony ARW goes
through a seven-node GPU pipeline and an exposure change re-renders in
**8.15 ms at p95 — at full resolution**, against a 16 ms budget. The preview-ROI
optimization the architecture assumes we would need is not needed yet.

```
Source          Sony ILCE-7M3, 6024 x 4024 (24.2 MP, RGGB)
  decode        48 ms   (504 MP/s, LibRaw)
Pipeline        7 nodes, 971 MiB of intermediates
  full render   45.8 ms  (every node)
  exposure drag  6.7 ms median, 7.9 p95  (2 of 7 nodes)
  curve drag     3.7 ms median, 4.6 p95  (1 of 7 nodes)
  WB drag       26.5 ms                  (7 of 7 nodes)  <- over budget
M0 gate         PASS
```

**The pipeline is bandwidth-bound, not compute-bound.** An `rgba16f` texture at
24 MP is 194 MB. Adding tone and color as separate pointwise nodes pushed
exposure drag to 19 ms and *failed the gate*; fusing all the scene-linear
pointwise work into one kernel, and AgX + curve into another, brought it back to
6.7 ms. Each operation still lives in its own function in
`shaders/ops/tone_ops.slang` — one file per adjustment, but one dispatch.

**Do not split pointwise operations back into separate nodes.** The
maintainability rule is about readable code, not one kernel per slider, and
every extra pointwise pass costs a 194 MB round trip for nothing.

⚠️ **White balance is over budget at 26.5 ms** and always will be: it rewrites
the linearize block at the head of the graph, so the demosaic has to rerun —
the demosaic interpolates white-balanced data. The fix is darktable's
degrade-then-refine (cheap demosaic mid-drag, full quality on release), or the
preview-ROI path. Neither is built yet.

Per-node caching works: moving exposure dirties only exposure + AgX, so
linearize, all three RCD passes and the color matrix are served from cache.

### Dev machine (measured, not assumed)
Apple **M4**, macOS 26.4.1, arm64 · Xcode 26.6 / clang 21 · 17.8 GiB recommended working set · 13.3 GiB max buffer · **unified memory** · Apple7 GPU family supported.

Unified memory is a real advantage: CPU↔GPU transfers are free, so LibRaw can decode straight into a shared buffer with no staging copy.

### Toolchain installed
`cmake` · `ninja` · `libraw 0.22.2` · `little-cms2` (was already present)
Still needed: **Slang** (S0.3, not in Homebrew — grab a GitHub release).

### Build
```
cmake -S . -B build -G Ninja
cmake --build build
./build/apps/probe/orion-probe
```

### What exists
```
engine/include/orion/orion.h        C facade — POD only, no exceptions cross it
engine/src/CApi.cpp                 exception firewall; guard() turns throws into status
engine/src/Engine.{h,cpp}           engine proper, RAII
engine/src/gpu/MetalDevice.{h,mm}   device + queue
engine/src/gpu/Resources.{h,mm}     Texture, Library, Kernel, CommandBuffer
engine/src/raw/RawImage.{h,cpp}     LibRaw decode -> untouched CFA mosaic
engine/src/pipe/Pipeline.{h,cpp}    the DAG: Kahn topo sort, per-node dirty caching
engine/src/pipe/DevelopPipeline.*   the standard 7-node graph + adjustments
engine/src/pipe/ShaderParams.h      host mirrors of shader structs, static_assert'd
engine/src/util/ImageWriter.mm      PNG out via ImageIO
engine/shaders/*.slang              7 kernels, one file each
app/*.swift                         SwiftUI shell, MTKView canvas, zero-copy
apps/probe, apps/bench              C-API smoke test, and the M0 gate
design/                             tokens.json -> CSS + Swift; darkroom mockup
```

### Bugs worth remembering
1. **Slang binding indices are cumulative across a module.** Compiling all
   kernels into one metallib gave kernel 2 textures at index 2/3 and kernel 3 at
   4/5, while the host binds from 0 every dispatch — so every kernel after the
   first read unbound slots and produced black. Fix: **one metallib per kernel**.
   Do not "optimize" that back into a single module.
2. **The camera matrix must be row-normalized.** Without it, white balance and
   the color matrix fight: the data is already neutral after WB, and an
   unnormalized matrix re-tints it (we had a magenta cast). dcraw normalizes
   rgb_cam for the same reason.
3. **One geometry, one function.** The renderer, the crop overlay and hit
   testing each computed the photo's on-screen rectangle for themselves, and
   drifted apart — handles landed on a rectangle the pixels were not drawn in.
   `app/CanvasLayout.swift` is now the only copy, and the *engine is given*
   the preview canvas rather than deriving a second one. Same class of bug in
   the shader: the straighten pivot was derived from cropOrigin/cropSize,
   which describe the canvas rather than the user's rectangle, so the preview
   turned about the frame center and the committed render about the crop
   center. Pass the pivot, do not derive it.
4. **The crop must stay inside the turned frame.** Nothing enforced it, so a
   straightened export had transparent wedges in its corners — the crop is
   what gets sampled, and it reached past the picture. `constrainedCrop`
   shrinks and recenters it, which is what Lightroom does. A fixed preview
   canvas could not hold a steep angle either: a 3:2 frame at 45 degrees
   reaches 1.77x its short side, so the old constant 1.42 clipped corners past
   about 17 degrees. The canvas is now computed per angle and aspect, and
   sampled into a frame-sized texture so its cost stays flat.
5. **A `Path` view takes the size it is offered.** An unsized one inside a
   `.position()` grows to the whole overlay, and `.position` then centers
   *that* — which threw the crop corner marks into the middle of the window.
   Give hand-drawn marks a fixed `.frame`.

## Settled

See `DECISIONS.md` for the full list with reasoning. Headlines:

- C++20 engine, Metal GPU, Slang shaders. **No Rust, no Vulkan.**
- Compute DAG, one shader per node, `rgba16f` linear Rec.2020, scene-referred, pixels stay on GPU.
- XMP sidecars = truth, SQLite = disposable rebuildable index. Folder-based, no catalog.
- macOS first. Sony ARW only for v1.
- RCD demosaic, AgX-family sigmoid tone mapper, profiled wavelet denoise.
- Maintainability is a hard constraint (solo dev): small shaders, 3-file feature changes.

## In flight

**Nothing in flight.** UI shell decision is closed — see `UI-DECISION.md`. Planning is complete enough to start coding.

⚠️ Session limit and the 200-call web-search budget were both exhausted on 2026-07-27. **Do research inline and sparingly** — the developer asked for fewer subagents, and they proved fragile at this scale.

## Blocked / needs a decision from the developer

1. ~~UI shell~~ ✅ **Resolved: SwiftUI/AppKit + C++ engine** (decision #25). Qt was picked then reversed — see `UI-DECISION.md` for why.
2. **License / business model** — undecided by choice. Building to keep both doors open: avoid GPL libraries, dynamically link LGPL ones. Revisit before v1 ships.

## Scope — locked 2026-07-27

Every feature now has a milestone. Notable calls:
- **Cut from v1:** card import (point at a folder instead), brush masking, keywords/search.
- **Local edits land in M4**, gradient + luminance/color-range masks + AI subject/sky. Spot removal kept.
- **Bilateral grid + BGU pulled forward to M1** — built before needed, as the escape hatch for the latency budget.
- No tethered shooting.

## M1 progress

Done: white balance (real Kelvin, as-shot on open), exposure, highlights,
shadows, whites, blacks, vibrance, saturation, contrast, tone curve, and
export (JPEG/PNG/TIFF with quality and resize). The app has all of them.

Remaining in M1: crop/rotate/straighten · XMP sidecars and the non-destructive
op stack · undo/redo and history · folder browse, filmstrip, ratings and
filtering · the SQLite index.

## M2 progress

1. ✅ **Tone curve** — `pipe/ToneCurve.{h,cpp}` evaluates the same monotone cubic
   Hermite spline as the mockup into a 256x4 LUT (master, R, G, B); the shader
   samples it. Runs after AgX, in display space.
2. ✅ **Color mixer** — eight hue bands with hue/saturation/luminance each,
   in `shaders/ops/hsl_ops.slang`. Weights overlap smoothly (60° falloff, squared)
   so a gradient crossing between bands does not band. Folded into the fused
   scene-linear kernel, so it costs no extra pass.
3. ✅ **Sharpening** — unsharp mask with detail masking, placed immediately after
   the demosaic. Upstream position is deliberate: dirt only flows downstream, so
   an exposure drag never recomputes it.
4. Profiled wavelet denoise + a per-camera noise profile
5. Lens corrections via lensfun
6. Before/after split — the mockup's Compare interaction

### Known gaps to close in M2
- Demosaic is **RCD-family, not a faithful RCD port** — directional +
  gradient-corrected + clamped, which is genuinely good but not the reference
  algorithm. Revisit against https://github.com/LuisSR/RCD-Demosaicing
- Highlight reconstruction is not implemented at all (clip only)
- The pipeline runs at full resolution; the preview-ROI path in ARCHITECTURE.md
  is designed but unbuilt. Not needed yet on an M4 — will be on lesser GPUs
- Black level ignores LibRaw's 2D cblack pattern (averaged instead)
- AgX output is sRGB-encoded; the EDR/P3 path is not wired up

## Culling — where the controls are

Rejection was reported broken three times and was never reproducible from the
code, because the failure was focus, not logic: the `x` handler was an
`onKeyPress` on the editor's root view, and the Metal canvas takes first
responder on any click. Culling now lives in a **Photo menu** (`PhotoCommands`
in `OrionApp.swift`), published through `@FocusedValue(\.cull)`. Menu shortcuts
route through the responder chain, so they work wherever focus sits — and the
shortcut is written next to its name instead of having to be known in advance.

R rejects · 1–5 rate · ⌘0 clears · ← → browse · 0 fits · 9 is actual size ·
⏎ applies a crop · ⎋ cancels one · ⌘R resets adjustments.

## Notes for whoever picks this up

- The developer wants **evidence, not agreement**. When they express skepticism about a technology, research it honestly — they explicitly asked to have their assumptions tested.
- Keep planning docs concise. Dense tables, not essays.
- The most important research finding is **Bilateral Guided Upsampling** (`RESEARCH.md` §4) — it is the general solution to "this algorithm is too slow to be interactive" and should be a DAG node type built in M1, before it's needed.

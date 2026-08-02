# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-08-01 (**the stroke stopped rebuilding the panel; the codebase reads American**)
**Phase:** M0 done. M1 ~98%. M2 and **M3 complete**. **`research/masking.md` is
finished** — primitives, groups, guided refinement, a raster
component, Vision filling it, and now a band on brightness. Six mask kinds. A mask is a *list* of components
folded per §6 (add/subtract/intersect), optionally feathered onto the
photograph's own edges, through the graph, the POD facade, the panel rows, the
sidecar, undo and the bench.

⚠️ **M3 is done — do not rebuild it.** Dehaze, creative LUTs, exposure fusion
and auto-enhance all shipped with research files, GPU tests and bench probes
(sessions `2026-07-28e` through `2026-07-29d`, now in `HISTORY.md`; the cost
table is immediately below). A stale kickoff prompt naming those four has now
arrived **36 times**; the answer each time is that they exist, and each of the
four now also has something that fails when its *wiring* breaks — see sessions
`31e` and `31f`.

**Next story:** the queue, in order, each with a cost:

1. **Dehaze's drag cost has roughly doubled** — 7.2× → 11.8–13.5× against
   exposure *in the same process*, so load cannot explain it. Needs a **bisect
   on a quiet machine**, not a theory. ~1 session.
2. **`reopen` grows 25–49 KB a cycle** where plain `open` is flat over 300
   iterations. ~240 MB across a 5,000-photo cull. ~1 session.
3. **Incremental brush accumulation.** ⚠ Now *located*: the host-side O(N) is
   gone and the slope did not change, so the residual is the **GPU dab loop**.
   That retires the three host-side candidates this table used to carry.
   Costed in `ROADMAP.md`. ~1–2 sessions.
4. **M1's library gap** — no SQLite index, no thumbnail cache, so every folder
   open rescans and re-reads every sidecar. Also a performance item. ~2 sessions.
5. **Export panel**: bit depth, metadata policy, output sharpening. 16-bit
   already exists in the engine and is not offered. ~1 session.
6. **Americanising the persisted keys**, if wanted — a schema migration with
   dual reads, not a rename. ~1 session, needs sign-off (#89).

⚠ **M5 is months, not sessions**, and saying otherwise would be a lie: it holds
an X-Trans demosaic (Markesteijn), a Windows port, Core ML denoise and
user-loadable DCP profiles, each a multi-week epic on its own.

Film grain is **finished and shipped**. All six canvas gestures arm. The rest of
the performance action item is in `ROADMAP.md`. `research/masking.md` is
**finished**; its leftovers are the fill leaking through smooth ground and the
per-layer decomposition beyond stage 2. The largest standing violation of a
stated hard constraint is `DevelopPipeline.cpp`, now **2,295 lines**.

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** `orion-tests` **569 checks** · `orion-viewport-tests` **3453
checks** · **33 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **11.39–14.13 ms p95** —
plus a preview graph at 1/16 that.

⚠ **That p95 is only meaningful next to one taken minutes away from it.** The
same binary measured 8.97, 16.75, 44.53 and 40.69 ms on this machine within an
hour, tracking GUI load rather than anything in the graph. HEAD measured
16.99/44.75/37.81 in the same window. Compare paired runs or do not compare.

### Known gaps, carried forward

Small, named, and none of them blocking the next story:

| Gap | Where |
|---|---|
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero. ⚠ It was **not** bounded until 2026-08-01 — on a photograph with no sidecar the sweep could never run at all, and 26 orphans had piled up beside one sample frame. Decision #87 | `MatteStore` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **101 commits carry `Co-Authored-By` / `Claude-Session` trailers.** Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history | whole history |
| **The 1000-line rule is broken six ways**, all in product code: `DevelopPipeline.cpp` **2,295**, `Engine.swift` 1,977, `OrionApp.swift` 1,433, `bench/main.cpp` 1,313, `DevelopPanels.swift` 1,135, `Scenario.swift` 1,080. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session. ⚠ Recounted 2026-07-31: `DevelopPipeline.cpp` and `bench/main.cpp` each grew again this session, and the `DevelopPanels.swift` figure carried here had been 30 lines stale | whole tree |
| **Nothing asserts that a gesture arms.** `Scenario` drives `Engine` and `CanvasLayout`, never a SwiftUI view, so the six `beginInteraction` calls are reachable only by reading them. They were found by `grep`, not by a red test. `repro/gesture-preview-agrees.txt` pins the *consequence* — the settled picture is identical armed or not — which is the strongest thing reachable from here | `Scenario.swift` |
| **The grading wheel's arming is unmeasured.** The wheels write three-component tuples and `Scenario`'s control table is scalar, so nothing can drive one. The only control of the six with no number against it | `Scenario.swift` |
| **The tick is timed whole, not attributed.** `EditHistory.record` copies the entire `DevelopState`, `InteractionLog.committed` diffs every field and formats strings, and `setBrushStroke` re-flattens the whole stroke — all per event, all O(size of the edit). ⚠ Candidates only: armed, a 784-dab stroke is 1.8 ms an event | `ROADMAP.md` |
| **`Engine.state` uses the memberwise initializer**, positional over eighty arguments. `cAdjustments()` in the same file refuses it in a comment for exactly that reason; `state` does not. Adding a field to `DevelopState` and forgetting this call compiles silently — it happened on 2026-08-01 with film grain and every suite stayed green | `Engine.swift` |

⚠️ **`samples/_PIC8095.ARW` has people in the plaza at its base.** Fine as a test
frame, but it must not be used for any published render — the landing site's
imagery was screened for this and twelve frames were rejected.


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

⚠ Those two figures are **as at the close of M3** and are kept that way, because
this table is what M3 cost. Masking has since taken the graph to 148 nodes and
6878 MiB; the current numbers are in the header above.

**The two slow ones are slow for the same reason and it is written down.**
Clarity and dehaze run at full resolution; fusion does not, and costs half as
much with the same node count. `Pipeline::setProfiling` prints a per-node
ranking on every bench run, and `research/local-laplacian.md` names the two
candidate fixes in order.


---

## The session log

The six most recent sessions are below. **Everything older lives in
[`HISTORY.md`](HISTORY.md)** — 56 sessions, moved there verbatim on 2026-07-31
in two passes, the second in the same breath as this update.

⚠ This file had grown to **4,643 lines across 56 sessions**, which broke the one
job it has. `CLAUDE.md` calls it the recovery point and says to read it first on
a fresh session; at that length nobody reads it, and in practice the last six
sessions each read only its opening fifty lines while adding another hundred to
the end. A recovery point that cannot be recovered from is not one.

⚠ The tail also held scaffolding from the first week that had gone plainly
false — a "Where we are" section still describing a **7-node, 971 MiB**
pipeline (it is 148 nodes and 6878 MiB) and an "In flight" section reading
"nothing in flight, planning is complete enough to start coding". Both are in
`HISTORY.md` now, marked as superseded rather than deleted.

The M3 cost table above was 3,392 lines down. It is the standing answer to the
kickoff prompt that keeps arriving, so it is now next to the thing it answers.

## Session 2026-08-01c — the stroke stopped rebuilding the panel

**Reported: painting takes ~155 ms a pointer event.** The harness said 0.9 ms.
That 170x was the whole problem, and nothing here could see it — `Scenario`
drives `Engine` and never renders SwiftUI.

### ⚠ It was `@Observable` invalidation, and it is now measured rather than argued

`Engine` is `@Observable`; Observation is property-granular. Every pointer event
wrote `maskComponents[i].brushStroke`, and `DevelopPanels` reads
`maskComponents` in **eleven places** including a `ForEach` over the mask rows —
so each dab rebuilt the whole develop panel, sixty times a second.

The `paint` verb counts invalidations with `withObservationTracking` now, which
is a check that can fail:

| | before | after |
|---|---|---|
| events invalidating the panel | **41/80** | **0/80** |

The stroke lives in `@ObservationIgnored` buffers for the length of the gesture,
appended to rather than rebuilt, and reaches `maskComponents` exactly once in
`endBrushStroke`. Decision #88.

### ⚠ The first draft skipped the adjustment block, and that broke the paint

Dabs travel by their own facade call, so skipping `orion_engine_set_adjustments`
looked free. But **`brush_revision` rides in that block** and is the only thing
the engine compares to decide the mask node is stale — it never walks a stroke
to find out. The dabs uploaded, the kernel never re-ran, the paint did not
appear, and it measured **0.0 ms an event at 45,000 fps**, which is what "did no
work" looks like when you are hoping for "fast".

`repro/gesture-preview-agrees.txt` caught it — written the day before, for a
different reason.

### ✅ Three gap-table candidates retired by measurement

A stroke is still linear (0.2 ms an event at 49 dabs, 1.5 at 490) and **the
slope did not change** when all the host-side O(N) work was removed. So it is
the GPU dab loop, not `EditHistory.record`, not `InteractionLog.committed`, not
the re-flatten. Incremental accumulation is the only fix, and the gap table
stops carrying three guesses.

### The codebase reads American

Two compile-checked stages: C++/Slang/C-header (58 files), then Swift (28).
Decision #89. Two bugs fell out of it — `maskKindName` had no case for kind 6,
so every **Color range row was labelled "Off"**, and a blanket rename hit
`Task.isCancelled`, which is Swift's, not ours.

⚠ **Persisted keys are frozen and that is the judgement in the change.** A
renamed sidecar key does not fail to parse; it yields a valid mask in the middle
of the frame. One unknown `PresetGroup` raw value loses every saved preset at
once. And the scenario grammar keeps **both** spellings permanently — renaming
collapsed four existing alias pairs into duplicate cases and fourteen repro
files failed at once.

### What was consulted

A Fable instance was asked for scope judgement and gave three things worth
keeping: check the report is not a stale binary before theorising; prove the
invalidation *fires* before claiming it *costs*; and freeze the persisted keys
rather than migrate them mid-investigation.

## Session 2026-08-01b — a leak the leak checker could not see

Asked for: leaks, a performance pass, and finishing what was unfinished. Two
agents ran read-only while the edits happened here.

### ⚠ The leak: ARC does not drain pools, and nothing here turns a run loop

**No `@autoreleasepool` anywhere in the engine's Metal layer.** Every autoreleased
temporary accumulated for the life of the process — 393 B a texture descriptor,
1.6 KB a library load, 2.3 KB a kernel — which is **~0.64 MB per graph built**
and ~1.3 MB per photograph opened, since a photograph builds two.

⚠ **`leaks --atExit` reports zero for this, on both binaries, and is right to.**
The blocks are still *reachable* from an undrained pool, so they are not leaks by
its definition. Only a footprint measurement finds it. LSan is unavailable on
macOS/arm64, so the tool that would have found it does not exist here.

⚠ **The app was shielded by accident**: `pushAndRender` runs on the main thread,
whose run loop drains each cycle. The bench, the tests and the scenario runner
are not — and moving a photo open to a background queue would have exposed it.

Six pools inside `Resources.mm`, decision #86. Verified on the harness that
found it: the same 15-iteration loop went **+8.92 MB, linear → +0.58 MB, flat
from iteration 13**.

Also: `Pipeline::compile` loaded a `MTLLibrary` **per node** — 149 nodes over 48
distinct metallibs, so ~101 redundant libraries per graph, doubled per photo.
Memoized by kernel name.

### ⚠ Orphan mattes accumulated forever on any photo that had never been saved

`MatteStore.sweep` ran only inside the successful-parse branch. That guard is
right about a sidecar which *exists and did not parse*. It is wrong about one
that is **absent** — a matte id lives only in a sidecar, so nothing can reference
those files. Measured: **26 orphans, 512 KB, beside one sample frame, oldest
three days old.**

Three cases now, in one function, because the policy had already been written
twice — the loader and the scenario runner's `reopen`, which claims in its own
comment to take the same steps. Decision #87. Mutations: the old two-case form
fails the absent check, an always-sweep form fails the unreadable check.

### Film grain finished — and it nearly shipped dead

Pieces 5 and 6: through `orion.h`, `CApi`, `DevelopState`, `Engine`, the
catalogue, two sliders, the sidecar, presets, sync, the log and the scenario.

⚠ **`Engine.state` builds `DevelopState` with the memberwise initializer**, which
is positional over eighty arguments. Adding the two fields to the struct and not
to that call compiled without a word: Swift filled them with the struct's
defaults, grain rendered on screen and reached the sidecar as **0**. 569 engine
checks, 3449 viewport checks and 31 scenarios all stayed green.

`cAdjustments()` in the same file already refuses the memberwise form, in a
comment, for exactly this reason. `state` uses it anyway.
`repro/grain-survives-a-reopen.txt` is the check that stands in for the compiler;
the mutation fails 2.

### What the stress pass found and I did **not** fix

Named in `ROADMAP.md`'s action item rather than guessed at:

- **Dehaze's drag cost has roughly doubled.** Normalised against exposure in the
  same process, so load cannot explain it: **7.2× → 11.8–13.5×**. The next step
  is a bisect, not a theory.
- **`reopen` grows 25–49 KB a cycle**; plain `open` is flat over 300 iterations.
  `InteractionLog` is capped at 2000 lines and ruled out.
- **Brush cost is linear in accumulated dabs, forever** — 16 ms at ~500 dabs
  unarmed, ~12,300 armed, then an unexplained 27 ms plateau at ~13,400.

### Held flat

Repeated opens (300×, +0.8 KB), export loops, 300 interact cycles, 2000 history
pushes, the spot cap at 64 and the mask cap at 4 — all measured, all flat, all
listed in the report rather than left implied.

**Suites:** 569 · **3453** · **33 scenarios** · bench exit 0 on all three frames,
M0 gate 11.39–14.13 ms p95.

## Session 2026-08-01a — the canvas never told the engine a gesture was happening

**Reported live: "I feel like there are forced updates per stroke, but this makes
things a lot slower."** Exactly right, and it named the mechanism.

### ⚠ The cause, which is not slowness anywhere

`AdjustmentSlider` arms degrade-then-refine through `AnalogTrack`. **No gesture
on the picture ever did.** So a slider tick rendered a quarter-linear preview and
a brush stroke rendered the **full graph at full resolution**, once per pointer
event, on a photograph that gets more expensive with every dab already laid.

Nothing was written badly. The preview graph has existed since 2026-07-30 and
the canvas simply never opted in.

### The instrument came first, and it had to

`scenario brush` hands the engine one finished stroke in a single
`setBrushStroke`. `MaskOverlay.paint` calls it again on **every pointer event**,
appending. Nothing in the repository issued a stroke the second way, so nothing
could see the cost — which is ROADMAP piece 1, now built as `paint <x,y> <x,y>
<n>`.

| Stroke, in dabs | Unarmed | Armed |
|---|---|---|
| 41 | 7.6 ms/event | **0.7 ms** |
| 123 | 15.2 | 1.1 |
| 246 | 27.3 — **37 fps** | **1.9 ms** |
| 784 | — | 1.8 |

And placing a radial mask that carries a local exposure, `drag maskCentreX`:
**13.0 ms a tick → 1.3 ms**.

⚠ **The verb deliberately does not arm the preview graph itself.** One that did
would report the same number whether `MaskOverlay` still called
`beginInteraction` or not — a measurement that cannot see the thing it exists to
measure, which is the defect this file has recorded three times.

### Fixed: two lines, in two gestures

Paint arms on the press and disarms in `onEnded`, after the history commit —
`endInteraction` renders the full graph once, and that is the first time the
full graph sees the paint rather than a refinement of it.

⚠ **The placement drag arms after the hit test, not before it.** A press that
grabs no handle falls through to the picture and pans it; arming first would
swap the canvas to the preview texture for a gesture that is not an edit.
`endInteraction` is called unconditionally, because it returns immediately when
nothing was armed and a grab that never moved still has to come off the preview.

### ⚠ Four more gestures do the same thing, and were left alone on purpose

`grep beginInteraction` finds them: `CropOverlay`, `SpotOverlay`, `CurveEditor`,
`ColorWheel`. All four still render the full graph per event.

They are **not** a sed. Each swaps the canvas to a differently-sized texture
mid-gesture with an overlay drawn over it, and this repository has already
shipped precisely that bug once — the compare split sampling two textures
through one set of UVs. The crop overlay is the worst of them, because its
rectangle *is* the geometry being changed. Each wants a before/after and a look
at the screen.

That, plus cold open, the library scan, memory on a lesser GPU, and the
measuring protocol itself, is the new **⚠ ACTION ITEM — a full performance audit
of the application** in `ROADMAP.md`, asked for in the same message.

### ⚠ What is still not covered

**Nothing asserts that a gesture arms.** `Scenario` drives `Engine` and
`CanvasLayout`, never a SwiftUI view, so the two lines added today are reachable
only by reading them — and the four above were found by grepping, not by a red
test. In the gap table rather than implied to be handled.

### Deliberately not optimised

Armed, painting is **still linear** in stroke length: 0.4 ms an event at 46 dabs,
1.8 at 784. The host-side reasons are known and named — `setBrushStroke`
re-flattens the whole stroke per event, `EditHistory.record` copies the whole
`DevelopState`, `InteractionLog.committed` diffs every field. At that slope the
dab cap lands around 5 ms an event, which is 200 fps.

⚠ Yesterday's own ROADMAP entry says not to optimise any of those rows before it
has a number. They have one now and the number says leave them alone.

## Session 2026-07-31l — the grain node ran when it was off, and the cursor was an oval

Two things reported live, one afternoon apart: **"it starts to get slow when I
adjust it"** and **"the circle is an OVAL"**. Both were real, both had a cause in
the tree, and neither was a hard problem once measured. Grain pieces 1, 3, 4 and
most of 7 landed along the way.

### ⚠ The slowdown was the grain node, running at Amount 0

`orion-bench` said so on the first run of the session — **exit 1**, M0 gate
**17.03 ms** against a 16 ms limit, and the exposure drag up from 3 nodes to 4.

The in-flight grain work had followed #81's costing literally: `develop:display`
to `RGBA16Float` unconditionally, and a new node after it. So every frame of
every drag paid a full-resolution pointwise pass *and* a doubled write on the
node feeding it, to multiply noise by zero.

Every other expensive thing in this graph disables to nothing when it is off.
The only reason grain looked different is that it is also the node that
**quantises** — so `retargetOutputChain` moves the two facts together, in one
place, and #82 has the table. The +194 MB is paid only while the slider is up;
the resting cost is the idle node's own **93 MB** (6878 → 6971 MiB).

| Exposure drag, `_PIC8220` | nodes | p95 |
|---|---|---|
| before grain | 3 | 13.63 ms |
| grain as first built | 4 | 17.03 ms — **gate FAIL** |
| after `retargetOutputChain` | 3 | 13.68 ms |

### ⚠ Then the fix looked like it had done nothing, and that was a second bug

The gate passed and the bench immediately reported the grain control as
**NO EFFECT**. `retargetOutputChain` pushed its parameters from `lastAdj_` —
which inside `apply` still holds the *previous* frame's values — so it switched
the node on and handed it Amount 0. The kernel ran and took its early exit.

Every test was green. Only the bench's control probe saw it, and only because it
measures the picture rather than the graph.

⚠ **And the probe had a floor of `0.0`**, which is not a floor: `0.0` is exactly
what it reads when the node was never dispatched. It is 0.06 of the exposure
reference now — measured at 0.127, 0.123 and 0.125 on the three sample frames,
which agree to a percent because grain's amplitude comes from the slider and not
from the scene.

### `testGrainWiring`, and one draft of it that was wrong

`testGrainGpu` dispatches the kernel with parameters it sets itself, so it can
never see the wiring — the same split that let dehaze be deleted with every
instrument green. The new test drives `DevelopPipeline` on a 64×64 synthetic and
asserts what actually broke: the node does not run at 0, it does run at 0.04 and
moves most of the frame, 0 is bit-identical to never having touched it, and an
exposure tick costs the same as before grain existed.

⚠ **The first draft compared 2 nodes against 12** and failed, correctly: it put a
warm render next to the cold first one. A drag is warm by definition.

⚠ **The fixture is a ramp, not a flat patch**, and the reason is the dither
check: on one flat value, whether a sub-LSB offset changes the rounded byte
depends on where that value happens to sit between two levels, so "dithered" and
"not dithered" can produce the same bytes. A ramp crosses every boundary.

**Mutations:** the node left enabled → 3 failures. The `lastAdj_` push → 2. The
dither flag dropped → 1.

⚠ **Two mutations survive and are written down rather than left green.** Leaving
`develop:display` on float with the node disabled passes everything — correctly,
because the offset is added in the shader whatever that node's own format is. It
costs 194 MB and a doubled write, which is a *latency* claim and the bench's job.
And dithering in both nodes at once with the slider up doubles the offset; real,
and not covered here.

### The oval was the brush cursor, and it was 1.497× wide

`CanvasLayout.brushCursor` built a circle in **normalized** coordinates and
mapped it out, so on a 3:2 frame it drew exactly the frame's aspect wider than
tall. The paint underneath is round: decision #62 folded `mask_brush.slang` into
`mask_component.slang` and moved the dab into frame pixels so the Size slider
would stop stretching it. The outline was left behind.

⚠ **What kept it alive is the interesting part.** The only thing tying the cursor
to the kernel was a *comment*, and the comment named `mask_brush.slang` — a file
that no longer exists. Nothing compiled against it, nothing tested it, and it
read as a considered decision rather than a leftover. Both copies of that comment
are corrected and `testBrushCursorIsRound` pins the shape *and* the radius
against `nibPx`; reverting it fails 8 checks.

⚠ **The radial mask is still an ellipse in normalized coordinates**, deliberately
and for now. Its semi-axes are per-axis and photographer-set, so unlike the nib
its shape is something you choose — and changing the convention changes what
`radius[1]` means in every sidecar already written. #83 records it as open rather
than as settled.

### ⚠ The M0 gate is not readable on a busy machine, and I nearly misattributed it

The final bench run failed at **49.49 ms** with no engine change since a run that
passed at 8.97. Three runs of the *same binary*: 16.75, 44.53, 40.69. Three runs
of **HEAD**, stashed and rebuilt under the same load: 16.99, 44.75, 37.81.

Identical distributions, so it is the machine — `WindowServer` was at 38% — and
not the change. Recorded because the honest comparison is the paired one: HEAD
and the fixed tree, back to back, which is 13.63 against 13.68. A single absolute
p95 from this bench means nothing unless it is paired with one taken minutes
either side of it.

### Still to do

Grain pieces 5 and 6 — the value through `orion.h`, `CApi`, Swift, the
catalogue, two sliders and the sidecar, about 20 files — and a `repro/` scenario
for the wiring.

⚠ **`grain.slang` is in `engine/shaders/CMakeLists.txt` and builds, but the whole
of this session is still uncommitted** at the time of writing. Session `31k` is
the standing argument for why that matters: a binary and a shader that disagree
produce nothing and say nothing.

## Session 2026-07-31k — every mask covered zero, and no test could see it

**Reported live**: "none of the masks are working" — brush, range, all of them.
Fixed by quitting the app. The interesting part is why that was the fix.

`perf: reject brush dabs a run of 64 at a time` (9546757) added a sixth texture
to `mask_component.slang`, moving that kernel's output from slot 4 to slot 5,
and changed `DevelopPipeline` to bind it. Both halves landed in one commit and
the suite passed. But an Orion process from the previous evening was still
running, and **`Pipeline::compile` loads metallibs from disk by path** — so
opening a photograph made the July 30 binary compile the July 31 kernel. It
bound five textures. The kernel wrote to the sixth.

⚠ **Metal does not call that an error.** An unbound slot is nil: reads give
zero, writes are discarded, no diagnostic unless the validation layer is on. The
kernel dispatched, completed, and wrote nothing — for every mask kind at once,
because they all run through that one kernel.

### What found it

The session log, in four lines. It dated the photo open at 12:38 against a
process start of 22:53 the night before. That is what `InteractionLog` was built
for and the first time it has paid.

### The guard

`Kernel::create` now takes Metal's reflection and records one past the highest
texture index the compiled shader refers to. ⚠ Highest **used** index, not the
declared argument count — an argument a shader never reads can be eliminated,
and counting declarations would refuse bindings that are in fact complete.
`Pipeline::compile` compares it against what it is about to bind and throws,
naming the kernel and the node.

`testBindingCount` runs first in `orion-tests`, because a shader and a binary
that disagree make every other GPU result a guess. It asserts the refusal *and*
that six bindings for six slots still compile — without that second half it
would pass on a guard that refused everything — and that the develop graph
itself satisfies the rule, which is the check that would have gone red the
moment the shader changed without the bind. Mutation-checked: disabling the
guard fails 3, an off-by-one in the slot count fails 4.

### ⚠ The lesson, which is not "rebuild more often"

This is the sixth instance of the class in `repro/README.md`: **a green suite
that was never in a position to fail.** The tests ran the matching binary, so
they could not observe the one thing that was wrong. What made it invisible was
not the mistake — it was Metal's silence about it. The fix is the assertion, not
the discipline.

**Still open**: `engine/shaders/grain.slang` is written but uncommitted and not
in `engine/shaders/CMakeLists.txt`. Grain pieces 1, 3–7 unstarted.


## Session 2026-07-31j — the grain plate, built and pinned

⚠ **Forty-first arrival of the stale M3 prompt.** Verified and set aside.

Piece 2 of `ROADMAP.md`'s film-grain decomposition: `GrainPlate.h`, the
precomputed field of correlated noise everything else hangs off.

⚠ **Scoped to the plate alone on purpose.** It is a self-contained unit with
properties that can be asserted on the CPU, where the shader and the node wiring
around it are not — and the last two sessions both recorded that starting a
multi-part change and leaving it half-built is the move this file has already
paid for twice.

### ⚠ The aux-texture API has no mip levels

The design needs a chain: a preview pixel covering sixteen frame pixels has to
see the *average* of sixteen, or the 1/16 preview reads an order of magnitude
grainier than the render it previews.

Adding real mip support would be a change to the GPU layer for nothing — the
shader has to filter **by hand** regardless, because a hardware sampler's
precision is not specified across GPU families and export could then differ by
device. So the chain is **stacked vertically into one 2048×4096 R32F**, 33 MB,
with `levelOffset(l)` a closed form that both sides compute from the same
expression. Two derivations of one offset is how a level gets read from the
wrong rows.

### What is pinned, and the check that matters

14 checks. The load-bearing one is that **the standard deviation falls down the
chain** — 1.0, then measurably less, then less again.

⚠ That is the property, not a defect, and it is the one an obvious "fix" would
destroy. Renormalising every level back to unit variance looks tidier and makes
the 1/16 preview exactly as grainy as the full render — the precise failure the
plate exists to prevent. The mutation that does it fails two checks.

Also pinned: neighbouring texels are **correlated** (0.3+), because uncorrelated
noise is a digital sensor rather than film — the mutation that skips the
band-limiting blur fails it — and two builds from one seed are **bit-identical**,
which is why PCG32 and Box–Muller are written out rather than taken from
`<random>`, whose algorithms differ between standard libraries.

### Still to do

Pieces 1 and 3–7: the shader, moving the quantisation boundary (`develop:display`
→ `RGBA16Float`, +194 MB), the adjustment through 20 files, two sliders, and the
GPU test. The design is settled in #81; none of it is guesswork now.

## Session 2026-07-31i — film grain, researched and costed rather than started

⚠ **Fortieth arrival of the stale M3 prompt.** Verified and set aside.

Nine sessions of tests, performance and maintainability, so this one went for a
feature: **film grain**, the last unbuilt item in `ROADMAP.md`'s M4 and listed
in `FEATURES.md` with no prior decision against it.

`research/film-grain.md` is written and decision #81 is logged. The code is
not, and the reason is the point of the session.

### The method, settled

Newson, Delon & Galerne (CGF 2017) model the emulsion as a Boolean process of
Poisson discs — the right physics, and the source of the `√(Y(1−Y))` variance
law. ⚠ Their exact renderer is **per-pixel Monte Carlo over the disc process**:
orders of magnitude outside 16 ms at 24 Mpx, and several hundred lines against
the 50–150 line ceiling. Both hard constraints, broken at once.

So AV1's architecture instead (Norkin & Birkbeck, DCC 2018) — one precomputed
correlated plate, applied per pixel with an intensity-dependent scale — carrying
Newson's statistics. Monochrome, after the display transform, keyed to the frame
rather than the output. #81 has the full reasoning, including why scene-linear
is the wrong side and what a hash-of-pixel-coordinate would do to the preview.

### ⚠ What costing it found, and why it stopped the session

**`develop:display` outputs `RGBA8Unorm`.** A grain node reading its output
would be adding noise to values that are **already 8-bit**. So this is not "add
a node": the quantisation boundary has to move — display becomes `RGBA16Float`,
the grain node inherits the Bayer dither and becomes the thing that quantises,
and `setWideOutput` retargets.

That is **+194 MB** of intermediates, a format change on two nodes, and an
Amount-0 path that must be bit-exact or every `identical` baseline silently
rebases. Plus a plate generator that cannot use `std::normal_distribution` or
`generateMipmaps` without export differing by toolchain.

Measured rather than guessed: a single new adjustment already touches **20
files** in this tree, and grain adds two of them plus an aux texture, a mip
chain, three GPU assertions and a probe that has to measure mean *absolute*
difference because grain is zero-mean.

⚠ That is two sessions of work, and starting it here would have left it
half-built — which this file records as the wrong move twice already
(`degrade-then-refine`, and the crop preview). The decomposition is in
`ROADMAP.md`, in order, with the memory number and the four things that must not
be done along the way.

### Note on the "3-file change" promise

`CLAUDE.md` says adding a feature should be a repeatable 3-file change. For a
new *node* that is roughly true. For a new *adjustment* it is 20 files, because
the value threads engine → `orion.h` → `CApi` → Swift → catalogue → sidecar →
presets → sync → log → scenario. Not a defect, but worth having counted, and
worth remembering the next time that sentence is used to size a story.

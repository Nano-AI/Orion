# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-08-01 (**the folder index and the thumbnail cache — M1's last real gap**)
**Phase:** M0 done. **M1 complete.** M2 and **M3 complete**. **`research/masking.md` is
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

1. **Dehaze's drag cost has roughly doubled** — ✅ *confirmed* 2026-08-01 by a
   paired re-measurement on a quiet machine, twice: exposure and clarity both
   within noise of their recorded figures, **dehaze 125 ms against 67.3**. ⚠
   Narrowed, not explained: the bench's per-node profile totals **73.6 ms over
   19 nodes**, so ~50 ms of the tick is *outside node dispatch*. `render()`'s
   own comment says the airlight readback cannot fire on the interaction path —
   either that is wrong or the time is elsewhere. **Bisect next**, one rebuild
   per step. ~1 session.
2. **`reopen` grows 25–49 KB a cycle** where plain `open` is flat over 300
   iterations. ~240 MB across a 5,000-photo cull. ~1 session.
3. **Incremental brush accumulation.** ⚠ Now *located*: the host-side O(N) is
   gone and the slope did not change, so the residual is the **GPU dab loop**.
   That retires the three host-side candidates this table used to carry.
   Costed in `ROADMAP.md`. ~1–2 sessions.
4. **Export panel**: bit depth, metadata policy, output sharpening. 16-bit
   already exists in the engine and is not offered. ~1 session.
5. **Americanising the persisted keys**, if wanted — a schema migration with
   dual reads, not a rename. ~1 session, needs sign-off (#89).

✅ **M1's library gap is closed** — SQLite index and persistent thumbnail cache,
2026-08-01, decision #91. 300 frames with the page cache warm: **454–688 ms cold
against 28–54 ms warm, 12.9–17.2×**. The leftovers are named and costed in
`ROADMAP.md` under *Library index — what is not done*.

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

**Suites:** `orion-tests` **569 checks** · `orion-viewport-tests` **3455
**Suites:** `orion-tests` **569 checks** · `orion-viewport-tests` **3538
checks** · **33 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **9.70–14.13 ms p95** —
plus a preview graph at 1/16 that. `Orion --library-open <folder>` is a fourth
gate: it opens a folder cold, warm and indexless in one process and fails when
the warm pass did not hit, or when any of the three disagree about a field.

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
| **The index's `SQLITE_BUSY` rule is reasoned, not pinned.** `SQLITE_CORRUPT`/`SQLITE_NOTADB` discard and rebuild the database; `BUSY` and `LOCKED` deliberately do not, because those mean a second Orion holds the file and discarding it would be deleting a live database. ⚠ The mutation that widens the list to every error code **passes every test** — reproducing lock contention needs a second process holding a transaction. Costed in `ROADMAP.md` | `PhotoIndex` |
| **Index rows for a folder you never open again are never collected.** The prune runs inside `plan`, against the listing it was handed, so it only cleans a folder you are looking at. ~200 B a row, so 5,000 dead frames is ~1 MB — untidy rather than a leak with teeth | `PhotoIndex` |
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

## Session 2026-08-01d — the reopen leak was the folder, not the photograph

**Reported: `open` × 300 is flat at +0.8 KB a cycle; `reopen` × 300 grows
25–30 KB a cycle, monotonic, and resumes at the same slope after the allocator
hands pages back.** So the leak is in the extra work `reopen` does: read the
sidecar, restore, upload mattes, sweep orphans.

### ⚠ It is `contentsOfDirectory`, and the rate is set by the folder

`MatteStore.sweep` enumerates the whole folder to find orphan mattes.
`contentsOfDirectory(at:)` returns **one autoreleased `NSURL` per entry**, each
with an `NSPathStore2`, a CoreServices `_FileCache` and three `CFString`s
behind it — **0.83 KB per file in the folder, per call** — and nothing released
them. Decision #90; the same shape as #86 one layer up, in Swift instead of
Metal.

**Paired runs, 300 iterations each, RSS polled at 150 ms, robust slope (median
of ten segment fits, so one allocator release cannot set the number):**

| Loop | Folder | Before | After |
|---|---|---|---|
| `open` (control) | 200 files | +1.0 | +1.3 KB/cycle |
| `reopen` | 2 files | +2.5 | +1.4 KB/cycle |
| `reopen` | 200 files | **+165.4** | **+1.5** KB/cycle |
| `reopen`, one radial mask | 200 files | **+162.0** | **+1.6** KB/cycle |

The reopen slope is now the open loop's, at every folder size. **The mask made
no difference either way** — the doubling in the report is the folder, not the
component; the original loop ran beside a folder that was filling up with the
measurement's own files while it ran.

### ⚠ What found it, and what could not

`leaks` reports **0 leaks for 0 bytes**, correctly and uselessly — the blocks
are reachable from an undrained pool. `heap` twice over a long run named the
classes (`NSURL`, `NSPathStore2`, `_FileCache`, `CFString`, growing 1:1:1:3),
and `malloc_history` on one live instance named the line:
`Scenario.step → MatteStore.sweepAfterLoad → sweep →
-[NSFileManager contentsOfDirectoryAtURL:...]`. Then the falsifiable step: the
same loop beside 2 files and beside 200 — +2.5 against +165, linear in the file
count, which is a directory enumeration and cannot be anything else.

The app is shielded by the run loop, as it was for #86. A batch or a scripted
loop is not, and neither is a folder with ten thousand photographs in it.

### The check, and the mutation

`testSweepDoesNotHoardTheDirectory` in `orion-viewport-tests`: 400 sweeps of a
300-file folder in one pool, asserting the process footprint grows under 8 MB.
It deliberately does not wrap the sweep in a pool of its own — that is the
caller it is written for. **Mutation: replace `autoreleasepool { }` in
`MatteStore.sweep` with an immediately-invoked closure — same scoping, no pool
— and it fails at 97.8 MB, 250.5 KB a sweep,** which is the 0.83 KB per file
again from the other end.

Gates: 569 engine checks, 3,455 viewport checks (3,453 before), 33 `repro/`
scenarios, bench exit 0 — all green with the fix in.
## Session 2026-08-01d — the folder index, and the two stamps it needs

M1's `Epic: Cull` has named a SQLite index since the first week and nothing had
ever been built. Every folder open re-opened every raw through LibRaw,
re-extracted every embedded preview and re-read every XMP sidecar. Decision #91.

**Paired, 300 frames, page cache pulled through first so the two differ by what
the index does and not by what the disk did:**

| Pass | Time | |
|---|---|---|
| cold — a database that has never seen the folder | **454–688 ms** | five runs |
| warm — the same database, seconds later | **28–54 ms** | **12.9–17.2×** |
| indexless — no database at all | **428–719 ms** | lands on cold, so the win is the index |

⚠ Absolute numbers on this machine are worthless alone; all three come from one
process, back to back, and the indexless pass is the control.

### ⚠ One stamp is the obvious design and it is wrong

The danger was never a miss. It is a **hit that is wrong** — a rating nobody set,
a thumbnail of a photograph that has since been replaced — because that looks
perfectly correct on screen.

**Rating a photograph does not touch the raw.** An index keyed on the raw's
`(mtime, size)` would therefore report the first rating it ever read, forever. So
each row carries **two** stamps and each half is validated against its own file:
the raw's for the dimensions, the camera and the thumbnail, the sidecar's for the
rating, the reject flag and the label. The mutation that collapses them to one
fails four checks.

And the mtime is whole **nanoseconds** from one `fstatat`, never a `Date`:
pressing 4 on a photograph rated 3 rewrites the sidecar to **the same length in
the same second**. The mutation that stores seconds fails two.

The thumbnail hangs off the raw and deliberately *not* the sidecar — it is the
camera's embedded preview, which no develop setting has ever affected. Keying it
to the sidecar too would throw away a good thumbnail on every star press.

### ⚠ Nothing is filed that was not read back off disk

`refreshMarks` stats, reads, and stats again, and files nothing when the two
disagree — so the contents of an old file can never be filed under the identity
of a new one. `Library.persist` calls it *after* the write rather than filing
what it holds in memory, because a merge onto a read-only card would otherwise
leave a rating in the index that is in no sidecar at all.

That race is unobservable from outside, so there is a seam — `marksReadWindow` —
purely so the check exists. An unobservable guard is one nobody can tell has
stopped working.

### ⚠ The probe's first draft could not fail, and it was written to catch that

Nothing in `orion-viewport-tests` can see whether `Library` calls the index at
all; the suite would stay green on a product that never consulted it. So
`Orion --library-open <folder>` drives the real `Library.open`.

Its first draft asserted **`misses == 0`**. A `Library` that never touches the
index satisfies that perfectly — zero attempts, zero misses, six green lines and
a four-times-slower open. Confirmed by actually removing the wiring: it passed.
It asserts **hits**, count for count, now, and the same mutation fails 2.

The third pass is indexless, and asserts field-for-field agreement with cold.
That is decision #9 as an executable check: delete the index and Orion behaves
identically, only slower.

### The tests, and what each one bites

Ten cases in `ViewportTests+Index.swift`, **85 checks**. Twelve mutations were
run against them; eleven fail, listed beside the test they fail. The surviving
one is written down rather than left implied: widening the
corrupt-code list from `SQLITE_CORRUPT`/`SQLITE_NOTADB` to every error code
passes everything, because simulating a `SQLITE_BUSY` from a second Orion needs
a second process. Costed in `ROADMAP.md`.

The corrupt-database case checks the thing that would be unforgivable: after
junk in the file, the photograph, its sidecar **and its matte** are all still
there, and the sidecar still says what it said. The mutation that points the
discard at the folder instead of the `.sqlite3` file fails 8.

### Not a dependency

`import SQLite3` from the macOS SDK. It is already on every Mac, it is public
domain, and the alternative was a package for something the platform ships.

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

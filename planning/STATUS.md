# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-08-01 (**perspective correction — M4's last geometry item**)
**Phase:** M0 done. **M1 complete.** M2, **M3 and M4's geometry complete**.
**`research/masking.md` is finished** — primitives, groups, guided refinement, a
raster component, Vision filling it, and a band on brightness. Six mask kinds. A
mask is a *list* of components folded per §6 (add/subtract/intersect), optionally
feathered onto the photograph's own edges, through the graph, the POD facade, the
panel rows, the sidecar, undo and the bench.

⚠ **Three duplicated blocks were removed from this header on 2026-08-01** — two
`Last updated` lines, two overlapping queues numbered 4/5 twice, and three
`Suites:` paragraphs, one of them two sessions stale. Four sessions had each
edited the top of this file without reading what was already there, which is the
same failure mode the 4,643-line prune was for.

⚠️ **M3 is done — do not rebuild it.** Dehaze, creative LUTs, exposure fusion
and auto-enhance all shipped with research files, GPU tests and bench probes
(sessions `2026-07-28e` through `2026-07-29d`, now in `HISTORY.md`; the cost
table is immediately below). A stale kickoff prompt naming those four has now
arrived **36 times**; the answer each time is that they exist, and each of the
four now also has something that fails when its *wiring* breaks.

**Next story:** the queue, in order, each with a cost:

1. **`reopen` grows 25–49 KB a cycle** where plain `open` is flat over 300
   iterations. ⚠ Partly answered — `MatteStore.sweep`'s directory enumeration
   was the bulk of it (decision #90) and the slope is now the `open` loop's at
   every folder size. Re-measure before spending a session on it.
2. **Incremental brush accumulation.** ⚠ Now *located*: the host-side O(N) is
   gone and the slope did not change, so the residual is the **GPU dab loop**.
   Costed in `ROADMAP.md`. ~1–2 sessions.
3. **A mask's extent under a perspective correction is first order.** Bounded
   and measured — exact up to a mask 0.28 of the frame across at vertical 0.45,
   degrading at the rim beyond that. Costed in `ROADMAP.md` under *Perspective —
   what is not done*. ~half a session.
4. **Snapshots / versions** — the last unbuilt line of M4 now that perspective
   has shipped. Unestimated.
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
the performance action item is in `ROADMAP.md`. The largest standing violation of
a stated hard constraint is `DevelopPipeline.cpp`, now **2,382 lines**.

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** `orion-tests` **641 checks** · `orion-viewport-tests` **3561
checks** · **35 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **8.88–14.83 ms p95** on
an idle machine — plus a preview graph at 1/16 that. `Orion --library-open <folder>` is a fourth
gate: it opens a folder cold, warm and indexless in one process and fails when
the warm pass did not hit, or when any of the three disagree about a field.

⚠ **That p95 is only meaningful next to one taken minutes away from it.** The
same binary measured 8.97, 16.75, 44.53 and 40.69 ms on this machine within an
hour, tracking GUI load rather than anything in the graph. Compare paired runs or
do not compare.
### Known gaps, carried forward

Small, named, and none of them blocking the next story:

| Gap | Where |
|---|---|
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero. ⚠ It was **not** bounded until 2026-08-01 — on a photograph with no sidecar the sweep could never run at all, and 26 orphans had piled up beside one sample frame. Decision #87 | `MatteStore` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **101 commits carry `Co-Authored-By` / `Claude-Session` trailers.** Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history | whole history |
| **The 1000-line rule is broken six ways**, all in product code. Recounted 2026-08-01: `DevelopPipeline.cpp` **2,382**, `Engine.swift` **2,163**, `bench/main.cpp` **1,506**, `OrionApp.swift` **1,461**, `Scenario.swift` **1,256**, `DevelopPanels.swift` **1,152** — every one of the six grew again, and four of the six figures carried here were stale. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session. ⚠ Recounted 2026-07-31: `DevelopPipeline.cpp` and `bench/main.cpp` each grew again this session, and the `DevelopPanels.swift` figure carried here had been 30 lines stale | whole tree |
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
[`HISTORY.md`](HISTORY.md)** — 61 sessions now, moved there verbatim on
2026-07-31 in two passes and again on 2026-08-01, which is what keeps this file
readable.

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

## Session 2026-08-01e — perspective correction, as a matrix and not a node

M4's last geometry item. Decision #96, `research/perspective.md`.

### The maths, and the citation

A keystone correction is a plane projectivity. **Hartley & Zisserman,
*Multiple View Geometry in Computer Vision*, 2nd ed., CUP 2004** — §2.3 for the
eight degrees of freedom, §4.1 for the Direct Linear Transformation, §4.1.2 for
the inhomogeneous solve with h₃₃ = 1, §4.4.4 for normalization. Implemented from
the description; **no GPL source consulted**, and neither darktable's `ashift`
nor RawTherapee's tool was opened.

Three controls — vertical, horizontal, aspect — reduce to four point
correspondences on the frame's corners, and the DLT through them is one 8×8
solve per geometry change. Vertical fills the destination's top row from a
*narrower* strip of the source than its bottom row, which is the whole
mechanism.

⚠ **§4.4.4 costs nothing here, and the reason is worth stating.** H&Z want the
correspondence centroid at the origin and the mean distance from it √2. The four
points are the corners of the centered unit square, so they already are — the
coordinates the problem is posed in *are* the normalizing transformation.

### ⚠ It goes inside the pass that was already resampling

`geometry.slang` composes orientation, quarter turns, straighten and crop into
one coordinate transform for exactly this reason (decision #40). A perspective
*node* is the obvious build and it samples the picture twice — a triangle filter
convolved with itself, and high frequencies nothing gets back.

So the shader gained **one homogeneous multiply** on a coordinate it already
had, between the straighten and the turns, and the five host-side pieces
(keystone, aspect, the auto-scale zoom, both coordinate conversions) are
multiplied into one 3×3 before the kernel sees anything.

Measured rather than argued: `testPerspectiveOneResample` runs the same
transform composed and split across two passes and compares acutance, and
`testPerspectiveWiring` asserts a perspective tick runs **one node**. The bench
agrees — `perspective 0.6  moved 0.1417 … 1 nodes` — and the graph is unmoved at
**149 nodes, 6971 MiB**.

### ⚠ Auto-scale knows nothing about the crop, and that is what makes it compose

`constrainedCrop` already keeps the crop inside the turned frame. If H maps the
frame into the frame, it maps anything already inside the frame into the frame.
Neither guarantee has to know about the other and the zoom never needs
recomputing when the rectangle moves.

Cheaper and more certain than `lens::autoScale`, which walks 64 points an edge:
a homography takes lines to lines, so **four corners bound the rectangle**, and
`fits` is an *interval* in the zoom (the image of a segment is a segment, and a
segment leaves a convex region once), so bisection is exact rather than
approximate. The one way that argument fails is w changing sign inside the
frame — w is affine, so w > 0 at the corners settles it, and the corners are
checked.

### ⚠ Three neutral guards, and only one of them is load-bearing

A zeroed control has to be **bit-identical** to a build without the feature, or
every baseline in every suite silently rebases. A flag in the parameter block
buys it in the shader. On the host there are three short-circuits, and removing
all three left **635 checks green** — because the DLT on ±1 correspondences
comes out bit-exact, and T·I·T⁻¹ came out exact at the fixtures' 96×64.

It does **not** at a real frame. `inTexels(identity, 6024, 4024)` is not the
identity in float; 4023×6021 is. The check names a real frame size now, and it
is the one that goes red when the guard is removed. A guard whose necessity no
test can demonstrate is a guard somebody deletes.

### The mask half: exact where it matters, first order where it does not

The same matrix bytes go to `mask::toFrame`, so masks, brush dabs and spots
follow the picture. A second derivation "in normalized coordinates" is how a
mask ends up plausibly wrong.

| Quantity | Under H |
|---|---|
| centre, brush dab, spot | **exact** |
| a linear gradient's direction | **exact** — H takes lines to lines |
| ramp length, radial semi-axes | **first order** — √\|det J\| at the centre |

⚠ **The last row has a measured bound rather than a hedge.** Through
`maskcheck`, which compares the render against the *overlay's* transcription and
demands every clear cell come back bit-identical: at vertical 0.45 a hard-edged
radial mask is exact at 0.10, 0.20 and **0.28** of the frame, and at 0.34 leaks 2
of 60 cells by 0.0105 luma; at vertical 1.0 by 0.0617; at vertical 0.2 it is
clean at 0.34. Never at the centre, always at the rim.

The fix is ~30 lines (the image of an ellipse under J is the eigen-decomposition
of a symmetric 2×2) and it is **costed in `ROADMAP.md` rather than bolted on**,
because it rewrites `mask::radiusToFrame`, whose derivation is load-bearing for
every quarter turn (#83) and pinned by `repro/mask-alignment.txt`.

### The tests, and the eight mutations

**55 new engine checks** in `tests_perspective.cpp` and **16 more** in
`repro/perspective-carries-the-mask.txt`. Eight mutations, each built and run:

| Mutation | Caught by |
|---|---|
| two rows of the homography swapped in the shader | 9 checks |
| the perspective divide dropped (`r = q.xy`) | 5 |
| row 2 read from `.w` — the padding word — instead of `.z` | 9 |
| `autoScale` always returns 1.0f | 8 |
| the vertical keystone's sign flipped | 4 |
| `mask::toFrame` handed nullptr instead of the matrix | `repro` — 2, and `orion-tests` stays **green**, which is the split that repro exists for |
| `displayedToFrame` handed nullptr — the spot path | 3 |
| all three neutral short-circuits removed | 1 — and only at a real frame's dimensions |

⚠ **The seventh and eighth are the interesting ones.** Dropping the homography
from the mask transform leaves all 641 engine checks green and fails only the
scenario, because `Scenario` drives `Engine` and `orion-tests` drives the
kernel. And the spot path is a *second* call site with its own argument list —
it was missing a check until the mutation found it, not the other way round.

### Gates

641 engine checks, 3561 viewport checks, **35** repro scenarios, all 0 failures.
Bench exit 0 on all three frames — 149 nodes and 6971 MiB, unchanged, M0 gate
**14.83 / 8.88 / 9.07 ms p95**.

⚠ **Then the gate went unreadable again, and it is reported rather than chased.**
Seven runs of *the same binary* on `_PIC8220` within twenty minutes:
**14.83, 8.88, 40.00, 29.24, 8.88, 34.98, 10.26 ms** — a four-fold spread with
`mds_stores` indexing in the background, and load average 3.0. This is the
fourth session in a row it has cost time (`2026-07-31l` has three runs of HEAD
under the same load: 16.99, 44.75, 37.81).

What is *not* load-dependent is what this change could actually have moved, and
it did not move: the gate times the **exposure** path, which the bench's own
named-node invariant reports as **3 nodes, clean 3 nodes** before and after, and
perspective adds **zero** nodes to it. The perspective probe passed on all three
frames in every run.

### Also done

`STATUS.md`'s header had **three duplicated blocks** — two `Last updated` lines,
two overlapping queues both numbered 4/5, and three `Suites:` paragraphs, one two
sessions stale. Four sessions had each edited the top without reading it. Removed,
and five more sessions moved to `HISTORY.md`.

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
## Session 2026-08-01d — dehaze redid the dark channel on every tick

**Queue item 1, root-caused and fixed.** `DevelopPipeline.cpp:1325` pushed the
whole dehaze parameter set whenever the slider moved. `Pipeline::setParams`
memcpys and calls `markDownstreamDirty` unconditionally — **it never compares
the bytes** — so pushing an unchanged block is indistinguishable from changing
it. Only `dehaze:moments` reads the slider (omega); the dark channel, the six
rank passes and the candidate pooling read the frame's size, the paper's
constants and A. Nine nodes, six of them full resolution over 24 MP, redone
per tick for a value none of them read.

The fix is a `hazeShapeValid_` latch: the size-derived blocks are pushed once
(and again on `first`, which is what a reload sets), omega is pushed on every
tick, and `pushAirlight` already had its own trigger.

**Paired, interleaved, two binaries, two rounds** — `Orion-before.app` and
`Orion-fixed.app` run back to back in the same machine state:

| round | before | after | ratio | exposure b/a | clarity b/a |
|---|---|---|---|---|---|
| loadavg ~3.5 | 127.1, 120.6 ms | 87.0, 87.7 ms | **0.71×** | 9.4/9.4 → 9.4/10.8 | 62.8/61.5 → 64.3/62.2 |
| loadavg ~1.85 | 147.3, 146.4 ms | 102.7, 100.6 ms | **0.69×** | 9.5/12.8 → 12.6/12.5 | 56.3/73.8 → 73.9/72.2 |

The two controls that were *not* touched move with the machine and not with the
build, which is what makes the dehaze column a result rather than a reading.
Normalised in-process, dehaze/clarity went **1.96–2.62× → 1.35–1.41×** and
dehaze/exposure **12.8–15.5× → 8.0–9.3×** (7.2× is what the file recorded).
All 33 control probes in `orion-bench` move the picture by *exactly* the same
amounts before and after: this is a cost change and nothing else.

### The test, and the mutation that proves it bites

`orion-bench`'s new `dehaze drag` invariant, beside `exposure drag, lens on` —
the same bug in the lens chain, found the same way. It drags dehaze twelve
times and asserts that **no node in a named list** ran, taking the *worst* tick
rather than the last one. Names, not a count: any nine nodes would satisfy a
count, and the point is which nine. Not milliseconds: this machine measured the
same binary at 8.97 and 44.53 ms p95 within an hour.

**Mutation:** restore the old guard —
`if (dehazing_ && (hazeMoved || !hazeShapeValid_))`. The bench prints
`dehaze drag  19 nodes, 9 of them slider-independent (dehaze:channel min ...)
DEHAZE REDOES THE DARK CHANNEL` and **exits 1**.

### ⚠ Two claims in this file were wrong, and both were about the fixture

1. **"~50 ms of the tick happens outside node dispatch."** It does not. That
   compared a 125 ms tick against the bench's *dehaze section alone* (73.6 ms,
   19 nodes). Clarity's input is `nDehaze_`
   (`DevelopPipeline.cpp:308`), so with clarity left at 0.9 by the line above
   it, **a dehaze tick runs the clarity chain too** — instrumented at **55
   nodes**: 16 dehaze plus the 39 that a clarity tick runs. 73.6 + 65.5 ≈ 139.
   The tick was always fully explained by dispatch.
2. **`estimateAirlight` was not the suspect.** Instrumented across a 40-tick
   drag it fires **once**, exactly as `render()`'s comment claims. The comment
   is right and stays.

### ⚠ What is still not explained, said plainly

The recorded baseline of **67.3 ms** does not reproduce at `6fd4e59`, the
commit that wrote it — built and measured there, dehaze/clarity is ~2.0, not
the 1.11 the header implies, and the bench's dehaze section is 19 nodes at both
ends of the window. So the header's three numbers were most likely taken
one control per run (dehaze with clarity still at zero costs about what the
header says), and the file that reports them drags all three in one process.
**The doubling was a fixture artifact; the waste it pointed at was real.**
No bisect was needed and none was run.

⚠ `orion-bench`'s **M0 gate is still unreadable under load** — it failed at
18.63 and 51.89 ms p95 with a stray `swift-frontend` at 99% CPU and passed at
9.10 ms on the same binary minutes later. It gates the *exposure* path, which
this change does not touch (3 nodes either way). Third session in a row this
has cost time; see `2026-07-31l`.
## Session 2026-08-01d — the export panel's last three controls

**Brief: the panel has four of its seven controls, add the missing three.** Two
of the three premises were wrong, and finding that out was most of the value.

### ⚠ Metadata policy was already built, wired and tested

Keep all / Strip location / Strip everything existed end to end — panel row,
`OrionMetadata`, `ImageWriter`, and three assertions in `tests_io.cpp`. Nothing
to build.

**But it had a hole, and it was the kind that matters.** GPS was dropped and the
IPTC dictionary was copied **whole** — so city, sub-location, province and
country survived a control whose entire purpose is that they should not. The
landing page advertises this feature by name ("Export with your location
stripped by default"), so the claim was false for any photograph that had been
catalogued.

It survived because the sample frames have no GPS — the bodies have no receiver
— so the one test aimed at this writes its own stand-in file, and that stand-in
carried coordinates and nothing else. **A fixture is a claim about what the
world looks like, and this one was narrower than the world.** The stand-in now
carries a city and a sub-location too. Decision #92.

### ⚠ 16-bit was not "in the engine and not offered" — it was the only mode

`Engine::exportImage` called `setWideOutput(true)` unconditionally. Every file
Orion had ever written was sixteen bits per component, so every PNG was about
twice the size it needed to be with nothing in the interface saying so, and the
ROADMAP's "blocked on the pipeline tail" note had been stale for a long time.

**The work was the narrow direction**, and it is not "the same pixels, rounded":
`ops/dither_ops.slang` puts a sub-LSB offset on whichever node writes the eight
bits, and CoreGraphics quantising a smooth 16-bit sky afterwards has no
equivalent. So the depth the file will hold now picks the graph, an 8-bit export
is byte-for-byte what the screen shows, and the writer's quantisation is
deliberately undithered because the graph already did it. Decision #90.

Two traps inside that. `readOutput16` assumed half float and would have read a
byte texture as noise. And the panel has to send `effectiveDepth`, not `depth` —
leaving the control at 16 and switching to JPEG would render the *undithered*
wide graph only for ImageIO to round it to eight anyway, which is banding in a
smooth sky and looks fine in a thumbnail.

### Output sharpening — the placement is sourced, the numbers are not, and it says so

Fraser's multipass model (capture / creative / output; Fraser & Schewe, 2nd ed.,
Peachpit, 2009) puts this pass **after** the resize, at final size, because
resampling is what softened the image. The resize is CoreGraphics', inside
`ImageWriter`, so the develop graph is the one place this pass cannot go — it is
~60 lines beside the resize, luminance-only on Rec.709 so an edge cannot pick up
a color fringe.

**The two amounts are mine and are in `UNSOURCED.md` §2** rather than dressed up
as derived. What is tested is the invariant a photographer would notice: None
does nothing at all, Screen overshoots a step edge, Print overshoots more,
brightness does not move, and all three channels move together. Replacing the
constants with measured ones needs no test rewritten. Decision #91.

### Every check was mutation-tested, and two did not bite

Eleven mutations, each built and run:

| Mutation | Caught by |
|---|---|
| `quantiseToEight` makes a 16bpc context | `orion-tests` — "a PNG asked for eight bits is that" (both plain and resized) |
| `toBitDepth` always returns Sixteen | `repro` — `depth8 == 8` |
| `effectiveDepth` returns `depth` | `orion-viewport-tests` — "JPEG is eight bits however the control is set" |
| Screen and Print constants swapped | `orion-tests` — "Print overshoots more than Screen" |
| `toSharpen` maps Screen to None | `repro` — `sharpNone < sharpScreen` |
| None sharpens, guard forced on | `orion-tests` — "None leaves the edge exactly as it was" |
| delta applied to green only | `orion-tests` — "moves all three channels together" |
| delta replaced by a constant | `orion-tests` — "does not move the overall brightness" |
| IPTC strip removed | `orion-tests` — "strip location removes the IPTC place name" |
| metadata policy ignored | `repro` — `metaNoneExif == 0` |

⚠ **Two mutations came back green, and both were my fault rather than the
product's.** "None sharpens" was inert because two independent guards stop it,
so it needed a two-part mutation to land. And "sharpen per channel" was green
against a **vacuous assertion**: the test edge was neutral grey, and on grey a
per-channel unsharp mask and a luminance-only one compute the identical answer.
The fixture was rewritten so the edge exists in **green alone** — red and blue
flat across the frame — which is the only shape that can tell the two apart.
That check now fails when the delta goes to one channel.

### What was built

- `app/ExportSettings.swift` — the model moved out of `ExportPanel.swift`, which
  imports SwiftUI. CLAUDE.md's rule that a view model has zero SwiftUI types in
  it was a promise rather than a fact while the two shared a file; the split
  makes the compiler hold it, and it is why the model is now testable in
  `orion-viewport-tests` at all. `ExportPanel.swift` went 436 → 275 lines.
- `app/ExportProbe.swift` — reads a written file back: depth, GPS, IPTC place,
  camera EXIF, acutance. All five properties exist because the control they
  serve fails invisibly.
- `probe` and `export key=value` in the scenario grammar, and
  `repro/export-depth-and-sharpening.txt` — 11 checks against files the real
  view models wrote.

### Not done, deliberately

- **HEIF.** In the ROADMAP's format table, not in the control table; it is a
  format question and a separate story.
- **A dither for the writer's own 16→8 quantisation.** Not needed while the
  graph picks the narrow path for 8-bit exports, and a second dither on top of
  the graph's would add noise twice. It is a landmine only if someone hands
  `writeImage` wide data and asks for eight bits, which is stated in the file.
- **Batch export's dropped settings.** Fixed in passing — it was sending
  neither the color space nor the new fields, so a batch wrote sRGB however the
  panel was set. Worth a mention because it is the same bug class the new
  controls would have walked into.

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

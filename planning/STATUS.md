# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-08-01 (**M3's last item renamed and solved; the brush bench was measuring itself — #96, #97, #98**)
**Phase:** M0 done. **M1 complete.** M2 and **M3 complete**. **`research/masking.md` is
**Phase:** M0 done. **M1 complete.** M2 and **M3 complete** — its last two open
items are now closed, one built and one refused (#96, #97). **`research/masking.md` is
finished** — primitives, groups, guided refinement, a raster
component, Vision filling it, and now a band on brightness. Six mask kinds. A mask is a *list* of components
folded per §6 (add/subtract/intersect), optionally feathered onto the
photograph's own edges, through the graph, the POD facade, the panel rows, the
sidecar, undo and the bench.
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

**Next story:** the queue, in order, each with a cost. ⚠ This list had grown two
overlapping copies of itself, numbered 1-5 and then 4-6; it is one list again.

1. ~~**Dehaze's drag cost**~~ — ✅ **done 2026-08-01, decision #92.** The cause
   was `DevelopPipeline.cpp:1325`: the dehaze chain's parameter blocks were
   re-pushed on every tick, and `setParams` dirties the whole downstream
   subgraph whether or not the bytes changed. **Only omega moves with the
   slider**; the dark channel, the six rank passes and the candidate pooling
   are functions of the frame's size, the paper's constants and A — nine nodes,
   six of them full-resolution over 24 MP, redone for a value none of them
   read. Paired A/B, two rounds, interleaved binaries: **147.3/146.4 → 102.7/
   100.6 ms** and **127.1/120.6 → 87.0/87.7 ms** — 0.69–0.71×, ~30% off the
   tick, with exposure and clarity unmoved in the same process. Pinned by the
   bench's `dehaze drag` invariant, which counts *named* nodes rather than
   milliseconds; reverting the guard prints `DEHAZE REDOES THE DARK CHANNEL`
   and exits 1. ⚠ Two claims in this file were **wrong** and are corrected
   below.
2. **`reopen` grows 25–49 KB a cycle** where plain `open` is flat over 300
   iterations. ~240 MB across a 5,000-photo cull. ~1 session.
3. **Incremental brush accumulation.** ✅ **Cause proved 2026-08-01** — it is
   `mask:0`, on **both** graphs, and the cost is `Σ blocks × box area`, not the
   dab count. Appending grows the block count and leaves the boxes their size,
   so it is linear. Host side is flat and three orders down (0.057 ms an event
   at any stroke length). ⚠ The bench's "the mask kernel is flat in dabs" was a
   **fixture artifact** and is withdrawn — it subdivided a stroke of fixed
   extent, which shrinks every box in exact proportion to the block count.
   Decomposed into two sessions in `ROADMAP.md`; **the predicate ships first,
   alone.** ~2 sessions.
4. ~~**M1's library gap**~~ — ✅ done 2026-08-01, decision #91. SQLite index and
   a persistent thumbnail cache; see the note below for the numbers.
5. ~~**Export panel**: bit depth, metadata policy, output sharpening.~~ ✅ done
   2026-08-01. ⚠ The premise was wrong in two ways: metadata policy had been
   built and wired for some time, and 16-bit was not "not offered" — it was the
   *only* mode, so every file Orion had written was 16-bit. The work was the
   8-bit path, output sharpening, and a location strip that also removes the
   IPTC place names. Decisions #90–#92.
6. **Americanising the persisted keys**, if wanted — a schema migration with
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
1. **Incremental brush accumulation.** ⚠ *Located*, not guessed: the host-side
   O(N) is gone and the slope did not change, so the residual is the **GPU dab
   loop**. Costed in `ROADMAP.md`. ~1-2 sessions.
2. **The grading panel's Balance** — the one thing split toning has that the
   wheels do not (#97). A signed EV offset on the three zone centres: ~5 lines
   in `color_grade.slang` plus one float through the usual twenty files.
   ~half a session.
3. **Americanising the persisted keys**, if wanted — a schema migration with
   dual reads, not a rename. ~1 session, needs sign-off (#89).
4. `DevelopPipeline.cpp` is **2,418 lines** against a stated ceiling of 1,000,
   and it grew again this session. Splitting product code wants its own session.

Closed since this list was last written, in the order they went:
**dehaze's drag cost** (#92), the **`reopen` leak** (#90), **M1's library gap**
(#91), the **export panel** (#93-#95), and now the **creative vignette** (#96)
with **split toning refused** (#97).

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

### ⚠ In flight right now — five agents, isolated worktrees, 2026-08-01

Recorded here because a session that ends mid-flight otherwise looks like a
session that stopped. Each verifies `orion-tests`, `orion-viewport-tests`, all
33 `repro/` scenarios and `orion-bench` before it commits, in its own worktree.

| Working on | Instruction that shapes it |
|---|---|
| Split toning + creative vignette | ⚠ May come back as a defended **"no"** on split toning — the grading wheels already do most of it, and a duplicated feature is worse than an argued refusal |
| Snapshots / versions | The hard part is that a snapshot's mask **matte** is a separate PNG the sweep can delete (#87), so a restored snapshot could silently cover nothing |
| Perspective correction | Must compose into the **existing** geometry matrix — a resample of a resample softens the picture — and masks/spots must follow it or they land plausibly wrong |
| The brush's preview-path linearity | Host side and the full-res kernel are both **already ruled out by measurement**. This is an investigation; a correct explanation with no code is a complete result |
| Segmentation highlight reconstruction | May also come back as a defended **"no"**, or as a costed decomposition with only its first piece built |

⚠ **Not started, and it needs the developer**: Americanising the *persisted*
sidecar keys (#89). It is a schema migration with dual reads and a
both-keys-present test, not a rename, and it changes files already on disk — so
it wants sign-off rather than an agent.

⚠ **M5 is not in flight and will not be finished by iterating**: X-Trans
(Markesteijn), a Windows port, Core ML denoise and user-loadable DCP profiles
are multi-week epics each.
the performance action item is in `ROADMAP.md`. `research/masking.md` is
**finished**; its leftovers are the fill leaking through smooth ground and the
per-layer decomposition beyond stage 2. The largest standing violation of a
stated hard constraint is `DevelopPipeline.cpp`, now **2,418 lines**.

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** `orion-tests` **586 checks** · `orion-viewport-tests` **3561
checks** · **34 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **11.39–14.13 ms p95** —
**Suites:** `orion-tests` **626 checks** · `orion-viewport-tests` **3561
checks** · **35 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **8.70–9.22 ms p95** —
plus a preview graph at 1/16 that. `Orion --library-open <folder>` is a fourth
gate: it opens a folder cold, warm and indexless in one process and fails when
the warm pass did not hit, or when any of the three disagree about a field.

⚠ **The three blocks above were each duplicated by a merge** — two `Last
updated:` lines, two numbered queues disagreeing about whether the export panel
was done, and two suite counts differing by 83 viewport checks. Reconciled
2026-08-01 against a run of all four gates rather than against either side.
**Suites:** `orion-tests` **586 checks** · `orion-viewport-tests` **3620
checks** · **36 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **11.39–14.13 ms p95** —
plus a preview graph at 1/16 that.
**Suites:** `orion-tests` **641 checks** · `orion-viewport-tests` **3561
checks** · **35 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **8.88–14.83 ms p95** on
an idle machine — plus a preview graph at 1/16 that. `Orion --library-open <folder>` is a fourth
gate: it opens a folder cold, warm and indexless in one process and fails when
the warm pass did not hit, or when any of the three disagree about a field.

⚠ **The M0 gate is not a reliable pass/fail on this machine, and it cost four
sessions and three agents hours on 2026-08-01.** Twelve consecutive runs of one
unchanged binary, nothing else on the machine (loadavg 2.9, `orion-bench` the
only process above 6% CPU):

    min 8.54 … 16.94   median 8.65 … 19.95   p95 9.70 … 31.30

⚠ **The whole distribution shifts, not the tail.** `min` alone varies two-fold,
so this is not a few stalled frames inside a run — the GPU is in a different
clock state from one run to the next. A p95 threshold cannot separate that from
a regression, and every red it produced tonight was environmental.

**Until that is fixed, treat the gate as advisory**: on a red, re-run and report
the spread. The load-immune checks are the ones to trust — node counts, which
`exposure drag, lens on` and `dehaze drag` both assert by name.
⚠ This block had **three** copies of itself carrying three different numbers.
One copy, measured this session.

⚠ **That p95 is only meaningful next to one taken minutes away from it.** The
same binary measured 8.97, 16.75, 44.53 and 40.69 ms on this machine within an
hour, tracking GUI load rather than anything in the graph. HEAD measured
16.99/44.75/37.81 in the same window. Compare paired runs or do not compare.

⚠ **The M0 gate is a wall-clock threshold and it therefore flakes, in both
directions.** Measured again 2026-08-01: one binary gave 8.97 / 16.60 / 18.57 /
23.73 / 20.05 / 20.54 ms inside half an hour, crossing the 16 ms line four
times, while an unrelated build gave 9.02 / 8.73 / 8.95 and then 18.67 / 23.64.
Interleaved five rounds, the two are **9.23 against 9.30 ms median** — the same
number. So a red bench run means "run it again beside a control", never "this
change regressed". This is the shape decision #92 already ruled against for
node-level probes; the top-level gate has not been converted yet and is the last
wall-clock assertion in the bench.

hour, tracking GUI load rather than anything in the graph. Compare paired runs or
do not compare.
⚠ It happened again on 2026-08-01e and the cause was named this time. Eleven
runs of **one binary**: 8.83, 8.92, 8.97, 9.00, 9.00, 9.22, 11.06, 16.86, 20.91,
21.47, **30.42** — a 3.4× spread with `CoreSpotlight` at 99% CPU indexing the
sample folder. It settled to 8.7–9.2 once the indexer finished, and the gate
measures the *exposure* path, which that session did not touch: 3 nodes and 149
nodes / 6971 MiB either way. **Do not chase this number on a busy machine.**

### Known gaps, carried forward

Small, named, and none of them blocking the next story:

| Gap | Where |
|---|---|
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero. ⚠ It was **not** bounded until 2026-08-01 — on a photograph with no sidecar the sweep could never run at all, and 26 orphans had piled up beside one sample frame. Decision #87 | `MatteStore` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **101 commits carry `Co-Authored-By` / `Claude-Session` trailers.** Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history | whole history |
| **The 1000-line rule is broken six ways**, all in product code. ⚠ Recounted 2026-08-01, every figure carried here was stale: `DevelopPipeline.cpp` **2,317**, `Engine.swift` 2,118, `OrionApp.swift` **1,445**, `bench/main.cpp` **1,623**, `DevelopPanels.swift` **1,152**, `Scenario.swift` **1,250**. The bench grew ~200 lines on 2026-08-01 profiling the brush on both graphs, which is the largest single jump on this list and is tooling rather than product. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session. ⚠ Recounted 2026-07-31: `DevelopPipeline.cpp` and `bench/main.cpp` each grew again this session, and the `DevelopPanels.swift` figure carried here had been 30 lines stale | whole tree |
| **The 1000-line rule is broken six ways**, all in product code: `DevelopPipeline.cpp` **2,295**, `Engine.swift` 2,118, `OrionApp.swift` 1,433, `bench/main.cpp` 1,313, `DevelopPanels.swift` **1,336**, `Scenario.swift` **1,250**. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session. ⚠ Recounted 2026-07-31: `DevelopPipeline.cpp` and `bench/main.cpp` each grew again this session, and the `DevelopPanels.swift` figure carried here had been 30 lines stale | whole tree |
| **The 1000-line rule is broken six ways**, all in product code. Recounted 2026-08-01: `DevelopPipeline.cpp` **2,382**, `Engine.swift` **2,163**, `bench/main.cpp` **1,506**, `OrionApp.swift` **1,461**, `Scenario.swift` **1,256**, `DevelopPanels.swift` **1,152** — every one of the six grew again, and four of the six figures carried here were stale. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session. ⚠ Recounted 2026-07-31: `DevelopPipeline.cpp` and `bench/main.cpp` each grew again this session, and the `DevelopPanels.swift` figure carried here had been 30 lines stale | whole tree |
| **The 1000-line rule is broken six ways**, all in product code, recounted 2026-08-01: `DevelopPipeline.cpp` **2,418**, `Engine.swift` 2,135, `bench/main.cpp` **1,510**, `OrionApp.swift` 1,445, `Scenario.swift` 1,254, `DevelopPanels.swift` 1,168. ⚠ Every one of the six was stale in this table by 20–200 lines; `bench/main.cpp` had grown 197. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session. ⚠ Recounted 2026-07-31: `DevelopPipeline.cpp` and `bench/main.cpp` each grew again this session, and the `DevelopPanels.swift` figure carried here had been 30 lines stale | whole tree |
| **Nothing asserts that a gesture arms.** `Scenario` drives `Engine` and `CanvasLayout`, never a SwiftUI view, so the six `beginInteraction` calls are reachable only by reading them. They were found by `grep`, not by a red test. `repro/gesture-preview-agrees.txt` pins the *consequence* — the settled picture is identical armed or not — which is the strongest thing reachable from here | `Scenario.swift` |
| **The grading wheel's arming is unmeasured.** The wheels write three-component tuples and `Scenario`'s control table is scalar, so nothing can drive one. The only control of the six with no number against it | `Scenario.swift` |
| ~~**The tick is timed whole, not attributed.**~~ ✅ **Attributed 2026-08-01.** One pointer event of paint is now three measured columns in `orion-bench` — `setBrushStroke` ×2, `apply` ×2, preview render. At 49 → 294 dabs: **0.001 / 0.057 / 0.77 ms → 0.001 / 0.057 / 2.82 ms.** Everything that grows is the GPU, and all of it is `mask:0` | `ROADMAP.md` |
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

The eight most recent sessions are below — every one of them 2026-08-01.
**Everything older lives in [`HISTORY.md`](HISTORY.md)** — 58 sessions, moved
there verbatim on 2026-07-31 in two passes and again on 2026-08-01.

⚠ The 2026-08-01 prune found `2026-07-31j` present in **both** files, byte for
byte: an earlier move copied it without removing it. The `STATUS.md` copy was
dropped rather than moved again. Worth checking on the next prune, because a
duplicated entry is invisible from either end.
The six most recent sessions are below. **Everything older lives in
[`HISTORY.md`](HISTORY.md)** — 61 sessions now, moved there verbatim on
2026-07-31 in two passes and again on 2026-08-01, which is what keeps this file
readable.
[`HISTORY.md`](HISTORY.md)** — 61 sessions now, moved there verbatim.

⚠ Pruned again on 2026-08-01: `2026-07-31k`, `2026-07-31l`, `2026-08-01a` and
`2026-08-01b` moved to `HISTORY.md`, and a **duplicate copy of `2026-07-31j`**
deleted — the previous prune had copied it across and left the original here.
The header block and the "next story" queue had each grown two or three copies
of themselves as well, carrying different numbers; they are one each again.

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

## Session 2026-08-01k — M3's last item was misnamed, and the name was the blocker

**Story:** "segmentation-based highlight reconstruction", the last unbuilt M3
line. Decisions **#96** and **#97**; `research/highlight-reconstruction.md`.

### ⚠ The name was wrong, and that is why it sat unbuilt

A connected-component pass is the one shape this project cannot take: union-find
is a CPU algorithm over a 24 Mpx buffer behind a readback stall, and the GPU
alternatives are iterative label propagation whose **pass count depends on the
picture**, against a static graph.

**Rouf, Lau & Heidrich (PROCAMS 2012) remove the requirement rather than
satisfying it.** Their §3.2 estimates a clipped region's color by solving
`∇²ρ = 0 over Ω^∪` with the region's own rim as a Dirichlet condition — and a
Dirichlet solve is **already region-scoped**. Nothing crosses a pixel outside
`Ω^∪`, so each connected blown region is solved on its own, from its own
boundary, without being labelled. Labelling would give the same answer more
slowly.

The roadmap item is renamed, not dropped. What was wanted was region-scoped
reconstruction with a propagated gradient. That is exactly what this is.

### ⚠ The gap is real, and it is measured rather than argued

`highlights.slang` reaches **12 pixels** (`kRadius`), and under decision #29 —
which clips every channel to one common ceiling before demosaic — its
`count == 3` branch is a **literal identity**, because a blown pixel already
arrives as `(clip, clip, clip)`. So a fully blown core is untouched at *any*
distance, and a blown lamp or window on a 6024×4024 frame is hundreds of pixels
across.

On a 140 px blown disc, asserted in the suite: `highlightRecover` returns the
core at **R/B 1.000**, its input unchanged. The new fill returns **4.091**
against a rim of **4.091**.

### What was built — piece 1 only, and not wired

`hl_pull.slang` (87) + `hl_push.slang` (82) + `pipe/HighlightFill.h`: the
Dirichlet fill as the pull-push interpolant of Gortler et al. (SIGGRAPH 1996
§3.5.1). Premultiplied storage makes the push exactly source-over, which makes
every value a convex combination of known pixels — the maximum principle for
free, where the window fit needs three explicit clamps to get it.

⚠ **`HighlightFill.h` carries a Gauss-Seidel reference run to convergence**, so
the approximation error against the harmonic solution Rouf et al. solve by
multigrid is **printed every run**: 0.0368, **6.1% of rim span**. Not a memory
of a session.

⚠ **Deliberately not wired to the graph.** Pieces 2–6 are costed in ROADMAP at
**+25 nodes and ~516 MB** for the pyramid alone — three or four sessions. Node
count is unchanged at 149 and the M0 gate is unmoved (8.92 ms p95 against 8.77
before, same machine, same hour).

### The mutations, including the one that did not bite

| Mutation | Effect |
|---|---|
| Drop half-texel centering in the push | **2 failures** |
| Source-over → plain add in the push | **2 failures** |
| Truncate the pyramid to 4 levels | **runs as a check every build** — the hole's centre must stay unresolved |
| Remove Gortler's `min(1, Σ)` weight cap | **nothing changed, to seven digits** |

⚠ The last one is the finding. The cap is **unreachable** here: the taps are a
partition of unity, so the pull is an average of weights already in [0,1] and
cannot exceed one. It was deleted — an unreachable branch reads as a guard
somebody is relying on. ⚠ Also worth carrying: the constant-rim check **survives
both of the first two mutations**, because a uniform scale and a half-texel
shift both leave a constant field constant. The cheap invariant proves the
normalization; only the host twin and the Gauss-Seidel reference prove the
filter.

### ⚠ A citation was wrong in five files

The cross-channel paper's third author is **M. F. Tappen**, not Tang. Verified
against the reference list of Rouf et al. and the paper's own listing; the
likely origin is He, Sun & **Tang**, cited correctly five times here for the
guided filter and the dark channel prior.

Nothing in any suite can catch this class — a citation that cannot be looked up
makes every constant under it uncheckable, and the only defence is reading the
source you claim to have read. Decision #97.

### Gates

595 engine checks (+9), 3561 viewport, 34 `repro/` scenarios, bench exit 0,
M0 gate 8.92 ms p95.

## Session 2026-08-01j — the brush bench measured a stroke nobody makes

**Reported: painting is linear in accumulated dabs (0.2 ms an event at 49, 1.5
at 490) and two hypotheses had failed to explain it** — the host-side O(N) was
gone with no change to the slope, and the bench said `mask:0` was *flat* in the
dab count (29.00 ms at 60 dabs, 21.96 at 960). Linear on the preview graph, flat
on the full one, and no mechanism that is both.

**There was no contradiction. The second measurement was wrong**, and it was
wrong in the fixture rather than in the kernel.

### What the kernel actually pays

    Σ over blocks of  (pixels inside that block's box) × 64 dab fetches

The bench grew the dab count by **subdividing a stroke of fixed extent** — the
same sine wave, sixteen times as many samples along it. That multiplies the
block count by sixteen and divides every box's area by sixteen, so the product
is invariant. It would have measured flat for any block size, any nib and any
frame. No hand makes that stroke: dab spacing is fixed by the nib, so appending
is the only way a stroke grows, and each new block of 64 arrives with a box the
same size as the last.

`orion-bench` now runs both shapes, on both graphs, on `_PIC8220.ARW`:

| Stroke shape | `mask:0`, full graph | `mask:0`, preview graph |
|---|---|---|
| refined, 60 → 960 dabs (the old fixture) | 24.16 → 19.35 ms | 1.54 → 1.31 ms |
| appended, 49 → 294 dabs (what `paint` lays) | 2.65 → **34.88 ms** | 0.17 → **2.23 ms** |

Six times the dabs is thirteen times the cost, on **both** graphs.

### What that rules out, with numbers rather than by elimination

- **Not resolution.** The preview is 1/16 the pixels and 1/16 the milliseconds
  at the same slope. The lead that the quarter-linear graph rejects differently
  was reasonable and is wrong — the full graph is linear too, and always was.
- **Not the host.** `setBrushStroke` ×2 is 0.001 ms and `apply` ×2 is 0.057 ms
  at 49 dabs *and* at 294. Uploading the whole dab list per event costs nothing
  measurable, which was worth measuring rather than assuming.
- **Not a stray dirty node.** Four nodes run per event at both lengths and only
  `mask:0` moves; the other three are a constant 0.60 ms of preview render.

### ⚠ A second thing defeats the boxes, and it is not the scribble

64 dabs is longer than most strokes, so a block straddles a pen-up and its box
spans the empty gap between two strokes. Six strokes, dab count and block count
and painted area all held identical, only the spacing moved:

| Gap between strokes | ms an event at 294 dabs |
|---|---|
| 0 (all six retraced) | 0.4 |
| 0.02 frame heights | 0.6 |
| 0.10 | 0.8 |
| 0.15 | 0.9 |

Monotone in the gap, tracking `(gap·H + 2r) / 2r`. Recorded in
`research/brush-acceleration.md` as a third term the "why 64" trade did not
have. **Not acted on** — padding strokes to a block boundary buys a constant
factor on a cost that is still linear, and spends dab slots and a third meaning
for the dab's `z` channel to get it.

### What shipped, and what deliberately did not

Shipped: the corrected bench fixture, the preview graph profiled beside the full
one, the host/GPU column table, and `Engine::kPreviewScale` made public so the
bench builds the preview graph from the engine's own constant instead of a copy.

**Not shipped: the fix.** Incremental accumulation is the only thing that
removes the slope, its risk is entirely in the host predicate, and the cheap
predicate renders a completely plausible brushstroke from a stale accumulator.
It is decomposed into two sessions in `ROADMAP.md` with named mutations, and
**the predicate ships first, alone, with no accumulator behind it** — including
the check that the fast path was *taken*, which is the failure this repository
keeps re-learning.

⚠ The bench addition is instrumentation, not an assertion: it prints numbers and
gates nothing, exactly like the `profileDrag` sections beside it. Making the
slope a *failing* check is session one's job, because a threshold on a number
this machine swings 3:1 on would be a flake, and a node-count invariant is only
meaningful once there is a fast path to count.

**Suites:** 586 · 3561 · 34 scenarios · bench exit 0.

## Session 2026-08-01i — perspective correction, as a matrix and not a node

M4's last geometry item. Decision #100, `research/perspective.md`.

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

## Session 2026-08-01h — the creative vignette, and a split-toning panel refused

M3's roadmap line had two items beside the grading wheels that were never built.
One is built. The other is refused, in writing, with the argument in
`DECISIONS.md` where it can be argued back.

### ⚠ Split toning is the wheels with fewer controls, and Adobe retired it

Decision #97. Split toning is a hue and saturation for shadows, the same for
highlights, and a Balance. Two of Orion's three wheels **are** those two tints,
each already carrying a luminance track split toning never had, and the third
grades the midtones, which split toning cannot reach at all.

And this is checkable rather than an opinion: **Camera Raw 13.0 / Lightroom
Classic 10.0, October 2020, deleted the Split Toning panel** and shipped Color
Grading — three wheels, hue/saturation/luminance each — in its place, documenting
the new Blending slider at 100 as giving "the same effect as the pre-existing
Split Toning feature". Building it here would mean adding the control the
reference implementation retired six years ago, beside the control they retired
it in favour of, which Orion already ships.

⚠ **One thing it has that the wheels do not, and it is named rather than waved
away: Balance.** The zones are Gaussians fixed at −2.5 / 0 / +2.5 EV; nothing
moves them. That is ~5 lines in `color_grade.slang` and one float through the
usual twenty files, it belongs on the grading panel, and it is item 2 of the
queue rather than smuggled into this session.

### The vignette: cos⁴, in stops, in scene-linear light, on the crop

Decision #96, `research/vignette.md`. `V(r) = 1/(1 + (r·T)²)²` — the cos⁴ law of
illumination (Reiss, *JOSA* 35(4), 1945; Kingslake, *Optics in Photography*,
1992) written through `cos² = 1/(1+tan²)` so the kernel has no trigonometry in
it. Normalized so that **Amount is the exposure change at the corner in stops**
and **Field angle is only the shape** — the half-diagonal angle of view of the
lens being imitated.

Both controls are physical quantities. Nothing here is a 0–100 strength.

Refused, each with a reason: **Roundness** (a lens's iso-illuminance contours are
circles about the optical axis, and a non-circular darkening is a radial mask,
which Orion has), **Feather** (the curve is the feather; the field angle moves
it), and Adobe's **three styles** (they exist to rescue highlights from a
display-referred blend — this is a multiply in scene-linear light before AgX, so
a bright corner rolls off instead of clipping).

### ⚠ It is not the lens correction, and that is asserted in both directions

`LensDatabase` already carries a vignetting *correction*, from lensfun's measured
polynomial, applied before the demosaic. This is its opposite. `testCreativeVignetteGpu`
asserts the creative control does not switch the `lens` node on **and** that the
lens control does not switch this one on, because the mistake looks roughly right
on screen and would run before the crop, before the demosaic, and would fight a
profile the day one loaded.

The mutation that wires the creative amount into `lens.vignetteA` fails that
check and the crop-symmetry check.

### ⚠ Post-crop, without the kernel ever learning about the crop

`geometry` crops last, so everything upstream renders the whole frame.
`DevelopPipeline::compositionCircle` hands the shader three numbers: the
rectangle's centre normalized in the unrotated frame, and its half-diagonal in
units of the frame's height.

**Only a circle, and that is the whole trick.** A rotation cannot change a
length, so the straighten and the quarter turns move the centre and leave the
radius alone — no second copy of `geometry.slang`'s inverse map, which is what
#70 is about. It also comes out resolution-independent, so the 1/16 preview
places the vignette identically without knowing it is smaller.

⚠ The half-pixel in `geometry.slang` — it rotates in *index* space against a
pivot given in continuous coordinates — is **mirrored rather than corrected**,
and the test asserts 0.751 rather than 0.750 because of it. A circle that agreed
with hand arithmetic and disagreed with the kernel would be the worse of the two.

### Fused, not a node

Into `color_grade.slang`, which is pointwise, adjacent and wants the same light.
Its own node would be a ~194 MB round trip at 24 MP for six lines of arithmetic —
the trade the creative LUT already lost inside `develop_display.slang`. The node
is renamed `grade + vignette` because it now does both.

At Amount 0 it disables to nothing: **149 nodes, 6971 MiB, M0 gate 8.97 ms p95,
exposure drag 3 nodes** — all exactly what they were before. The mutation that
leaves it always on takes the drag to **4 nodes and 12.81 ms**, which is grain's
#82 regression again, and fails 4 checks.

### The checks, and the six mutations that were actually run

**+40 engine checks** in `tests_vignette.cpp` (626 total), plus
`repro/vignette-follows-the-crop.txt` and a bench probe.

| Mutation | Fails |
|---|---|
| `compositionCircle` returns the frame's centre and full radius | **9** engine checks, and `cropMiddleOn == cropMiddleOff` in the scenario |
| `vignetteFalloff` drops the `1 − cos⁴(θmax)` normalization | 1 — "the corner is worth the same at 20 degrees and at 65" |
| `vignetteRadius` drops the aspect term | 2 — the edge-midpoint check and the same corner check |
| the creative amount wired into `lens.vignetteA` | 2 — "does not switch the lens correction on", and crop symmetry |
| the node never disables (`vignetting = true`) | 4, and the exposure drag goes 3 → 4 nodes |
| the params pushed from `lastAdj_` instead of `adj` | 6 |
| the quarter-turn `switch` cases transposed | 2 |

⚠ **Two of those came back green on the first attempt and both were the test's
fault.** Dropping the aspect term leaves the falloff an *ellipse* — still centred,
still four equal corners, still monotone — so every symmetry check passed; it
needed a check on the **edge midpoints**, which on a 4:3 frame sit at r = 0.8 and
r = 0.6 and must therefore differ. And the lens-wiring mutation was invisible
because §5 of the test re-applied the *same* amount from a fresh struct: `apply`
compares field by field and only re-evaluates a node's enable when something in
its own list moved (#92), so the graph never changed and the check could not see
anything. Every state in that section is now reached by *changing* the field that
owns it.

### The probe's floor, and why its three frames disagree

`vignette -2 EV`, on mean luma, floored at **0.35** — half the smallest of 0.79,
1.05 and 0.70 measured against the exposure reference on the three sample frames.

⚠ That spread is much wider than grain's, which agree to a percent, and the
reason is written into the probe rather than averaged away: grain's amplitude is
defined in *display* units, so it is the same wherever the scene sits; this one
is in stops of *scene-linear* light, and what two stops down is worth on screen
depends on where the corner started on AgX's curve.

### Also done, because the file demanded it

`STATUS.md` had **three** copies of its Suites block with three different
numbers, two overlapping copies of the "next story" queue, two Last-updated
headers, and a duplicate of session `2026-07-31j` that the previous prune had
copied rather than moved. All six file sizes in the 1000-line gap row were stale
by 20–200 lines. Recounted and de-duplicated.

## Session 2026-08-01g — the reopen leak was the folder, not the photograph

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

## Session 2026-08-01f — snapshots, and the matte that would have vanished

**M4's last unbuilt workflow item.** A photographer saves the edit under a name,
keeps working, and comes back to it. Neither undo (`EditHistory`: fifty deep,
coalescing, dies with the process) nor a preset (a **patch** carried *between*
photographs, deliberately excluding the crop, the dust and the masks) — a
snapshot is this photograph's whole `DevelopState` under a name. Decision #99.

### Where they live, and the argument that is *not* #79's

A sibling `PHOTO.orion-snapshots.json`. ⚠ #79's reason for refusing base64 in
the XMP — megabytes of text per autosave settle — **does not transfer**: a state
is a few kilobytes. Two reasons that do:

- **Autosave rewrites the sidecar 900 ms after any slider moves**, through
  `Sidecar.merge`, which is a read-modify-write over a hand-rolled string matcher
  rather than an XML parser. Every version kept would be decoded, re-encoded,
  re-escaped and rewritten on every settle, and a slider drag would get more
  expensive the more versions the photograph had.
- **Blast radius.** One bad merge takes the working edit *and* every version. A
  snapshot's whole job is to be the copy that survives when the working state
  does not.

Application Support was rejected on #79's own grounds: a path-keyed cache dies
when the photograph moves, and this is storage.

### ⚠ The matte, which is the part that would have failed silently

A `DevelopState` is not the whole edit. A raster mask is a sibling PNG named by
id (#79), and `MatteStore.sweep` deletes every matte the **sidecar** does not
reference. So the obvious version feature has a hole nothing on screen can show:
save a version with a Subject mask → delete the row → reopen → the sweep
collects the file → restore → the row is back, the raster is gone, **the mask
covers nothing**, and the picture changes with no error anywhere.

Fixed with a **pin, not a copy**, and it can be a pin only because matte files
are already immutable (#79 mints a fresh id and a fresh file on every
regeneration) — so an id inside a version always names the pixels it named when
the version was taken. `MatteStore.sweepAfterLoad` now keeps the union of what
the sidecar references and what every version does, computed in that one
function because #87's lesson is that a delete policy written twice stops
matching.

⚠ **The version file has #87's three states too.** Absent is `[]`, unreadable is
`nil`, and unreadable means both *collect no mattes at all* and *write nothing
over it*. Both halves are pinned by tests that go red when either is conflated.

What a pin cannot cover — a photograph copied without its siblings — is
**reported before the version is pressed**: the row names the selections it can
no longer find, in the amber the app uses for "look at this".

### ⚠ Restoring does not trap anybody, in two different timescales

`history.record`, not `history.reset` — a restore is one ⌘Z, like a preset.
Resetting would make it the one act in the program that cannot be taken back,
and it is the act most likely to have been a mistake. That covers the session.
For the quit that undo does not survive, `SnapshotStore.restore` keeps the
working edit as a single **automatic** version first, replaced rather than piled
up, and renaming it promotes it to an ordinary one. The order lives in one
function rather than at each call site, with the engine half handed in as a
closure — the seam `Autosave` and `BatchExport` already use, and what lets it be
pinned without an `Engine`.

### ⚠ No eighth tool tab, and that was measured rather than argued

Versions wanted a tab: they belong to one photograph where a preset belongs to
none. Seven plates divide 364 points into 48 each; eight leaves 42, and VERSIONS
needs about 51 at the bar's 9-point type. Rendered with the tab in place
(`--scene versions`), the bar came back reading **PRESE… VERSI…** — the new tab
cost the old one its name as well as its own. Versions sit at the top of the
Presets tab instead, first because the tab's other four sections are all
*between* photographs and this is the only one about the photograph in hand.

### What was checked, and the mutation for each

`orion-viewport-tests` **3561 → 3620** — 59 checks in twelve new functions in
`ViewportTests+Snapshot.swift` — plus two scenarios. Every one was run against a
mutant:

| Check | Mutation | Result |
|---|---|---|
| a matte a saved version names survives the sweep | drop `.union(pinned)` | 1 red |
| an unreadable version file collects nothing | `pinnedMattes` returns `[]` | 2 red |
| the working edit is kept | drop `keepWorkingEdit` | 3 red |
| unreadable is not overwritten | `read` returns `[]` on a decode failure | 6 red |
| renaming the automatic version keeps it | leave `automatic` set | 3 red |
| the ceiling refuses rather than evicts | evict the oldest | 2 red |
| a missing matte is named | `missingMattes` returns `[]` | 2 red |
| `snapshot-keeps-its-matte.txt` | drop `.union(pinned)` | **7 red**, and the failure printed is exactly the silent one: `leftAfter == leftBare` |
| `snapshot-keeps-its-matte.txt` | `Engine.restore(snapshot:)` skips `restoreMattes` | 6 red |
| `snapshot-survives-a-reopen.txt` | `history.reset` instead of `history.record` | 1 red |
| `snapshot-survives-a-reopen.txt` | drop `keepWorkingEdit` | 2 red |

⚠ **Two drafts of these checks could not fail, and both were caught before the
commit.** The date round trip was first written through a pair of encoders the
*test file* built, which pins ISO 8601 against ISO 8601 and stays green with the
store's two ends disagreeing — the one failure worth checking. And
`snapshot count 1` passed on the first run and failed on the second, because a
version file outlives the run; `snapshot clear` is now the first line of both
scenarios. Ninth and tenth instances of the class `repro/README.md` records.

### Not done

Nothing deferred from this story. Two things it deliberately does not do: a
version does **not** copy the matte files it names (it pins them, which is
correct only as long as #79's immutability holds — a future in-place matte
rewrite would have to revisit this), and there is no compare-two-versions view.

---

# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-07-31 (**the grain node ran when it was off, and the brush cursor was an oval**)
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

**Next story:** **finish film grain.** Pieces 1–4 and most of 7 are done and in
the build; what is left is the value through `orion.h` → `CApi` → Swift → the
catalogue → the sidecar (piece 5, ~10 files), two sliders (piece 6, ~4 files) and
a `repro/` wiring scenario. Nothing about it is guesswork now.

After that, `ROADMAP.md`'s **slider latency, end to end** is the named story with
the best reason to exist: a slowdown was reported from the app on 2026-07-31 and
found, but the instrument that found it stops at the C++ boundary and nothing in
this repository times a tick the way the app issues one. `research/masking.md` is
**finished**; its leftovers are the fill leaking through smooth ground and the
per-layer decomposition beyond stage 2. The largest standing violation of a
stated hard constraint is `DevelopPipeline.cpp`, now **2,295 lines**.

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** `orion-tests` **569 checks** · `orion-viewport-tests` **3449
checks** · **31 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: **149 nodes, 6971 MiB**, M0 gate **13.68 ms p95** on
`_PIC8220` — plus a preview graph at 1/16 that.

⚠ **That p95 is only meaningful next to one taken minutes away from it.** The
same binary measured 8.97, 16.75, 44.53 and 40.69 ms on this machine within an
hour, tracking GUI load rather than anything in the graph. HEAD measured
16.99/44.75/37.81 in the same window. Compare paired runs or do not compare.

### Known gaps, carried forward

Small, named, and none of them blocking the next story:

| Gap | Where |
|---|---|
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero | `MatteStore` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **101 commits carry `Co-Authored-By` / `Claude-Session` trailers.** Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history | whole history |
| **The 1000-line rule is broken six ways**, all in product code: `DevelopPipeline.cpp` **2,295**, `Engine.swift` 1,977, `OrionApp.swift` 1,433, `bench/main.cpp` 1,313, `DevelopPanels.swift` 1,135, `Scenario.swift` 1,080. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session. ⚠ Recounted 2026-07-31: `DevelopPipeline.cpp` and `bench/main.cpp` each grew again this session, and the `DevelopPanels.swift` figure carried here had been 30 lines stale | whole tree |
| **Nothing times a slider tick the way the app issues one.** The bench stops at the C++ boundary; `EditHistory.record` and `InteractionLog.committed` do per-tick work on the main thread that nothing measures. Named and costed in `ROADMAP.md` — and the reason it is named is that a slowdown *inside* the measured part still reached the developer before the bench did | `ROADMAP.md` |

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

## Session 2026-07-31h — both test files split

⚠ **Thirty-seventh through thirty-ninth arrivals of the stale M3 prompt.**
Verified and set aside; the story was named at the end of session `g`.

`apps/tests/main.cpp` was **7,656 lines** and `ViewportTests.swift` **3,297**,
against a limit `CLAUDE.md` calls a hard constraint. Both were files these
sessions kept adding to.

### What it looks like now

| | Before | After | Largest |
|---|---|---|---|
| `orion-tests` | 1 file, 7,656 | 15 files | **969** |
| `orion-viewport-tests` | 1 file, 3,297 | 13 files | **552** |

C++: twelve translation units by subject, `harness.{h,cpp}` for the counter and
the three helpers, and a `main.cpp` that is the running order and nothing else.
The counters move to `harness.cpp` behind `extern`, so every unit adds to one
tally. Swift: the file was already part extensions, so the split follows the
shape it had — `ViewportTests+<subject>.swift`, one `extension ViewportTests`
each.

### ⚠ Verified as a refactor, not asserted to be one

The pre-split binary was rebuilt from `git stash` and its **full stdout diffed**
against the new one, both suites. Identical line for line — not just the same
totals, the same output in the same order. A check count alone would have missed
a test that had stopped running and another that had started failing.

### ⚠ Two things worth recording

The first pass cut each chunk at its `static func`, which **orphaned every doc
comment** at the tail of the previous file — the comment explaining a test
ending up in a file that no longer contained it. Redone with boundaries walked
back over the attached comment block. Caught by reading the output, not by the
compiler, which was perfectly happy.

And the count went from seven violations to **six, not five**:
`app/Scenario.swift` crossed 1,000 during these same sessions, from the `reopen`,
`refuses` and `control` verbs I added to it. Splitting two files while growing a
third past the line is worth naming rather than rounding off.

### What is left

Six, all product code: `DevelopPipeline.cpp` 2,192, `Engine.swift` 1,977,
`OrionApp.swift` 1,433, `bench/main.cpp` 1,293, `DevelopPanels.swift` 1,165,
`Scenario.swift` 1,080. Splitting those is riskier than splitting tests — there
is no byte-identical-output check available for a library — and wants its own
session.

## Session 2026-07-31g — the recovery point could not be recovered from

⚠ **Thirty-sixth arrival of the stale M3 prompt.** Verified and set aside.

No code. This file was **4,643 lines across 56 sessions**, and `CLAUDE.md` opens
by calling it the recovery point and saying to read it first.

⚠ **Nobody was reading it, including me.** Across the six sessions before this
one I opened it, read the first fifty lines, and appended another hundred to the
end. Every session made the problem worse and none noticed, because the part
that matters is at the top and the cost is at the bottom. The working agreement
says updating this file is "what makes context loss survivable"; at that length
it was the opposite.

### What moved

50 sessions to `HISTORY.md`, verbatim. Six most recent stay. **588 lines now.**

Checked rather than assumed: 6 + 50 = 56 headings, and a line-by-line
comparison against `HEAD` shows the only original lines absent from the pair are
eleven I deliberately rewrote in the header.

### ⚠ Two things the move exposed

**The tail had been false for weeks.** A "Where we are" section still described
a **7-node, 971 MiB** pipeline — it is 148 and 6878 — and "In flight" read
"nothing in flight, planning is complete enough to start coding". Both kept in
`HISTORY.md`, marked as superseded, because they record what was believed at the
time. Neither was reachable by anyone reading from the top, which is exactly why
they rotted.

**The header pointed at "the cost table below", 3,392 lines below.** That table
is the standing answer to the kickoff prompt that has now arrived 36 times, and
it was the least findable thing in the file. It sits under the gap table now.

Also corrected while there: the suite counts had drifted (3430/30 against the
real 3437/32), and "arrived five times" was five sessions stale.

### The gap table gained a row

`CLAUDE.md` calls the thousand-line limit a hard constraint and the tree breaks
it seven ways, worst at `apps/tests/main.cpp` — **7,656 lines**. ⚠ Recorded
rather than fixed, and recorded pointedly: the two worst offenders are the two I
have added to nearly every session. Splitting them is a session of its own, and
`CLAUDE.md` now says to prune this file in the same breath as updating it, so
the same thing does not happen again by default.

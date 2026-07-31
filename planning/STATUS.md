# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-07-31 (**the grain plate, built and pinned** — piece 2 of the film-grain decomposition)
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

**Next story:** open. `research/masking.md` is **finished** — every kind it
plans is built, sky included. The named candidates left are the fill leaking
through smooth ground (a colour predicate alongside the gradient one), and
`ROADMAP.md`'s per-layer decomposition beyond what stage 2 shipped. The largest
standing violation of a stated hard constraint is `apps/tests/main.cpp` at
**7,656 lines** against `CLAUDE.md`'s thousand — see the gap table.

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** `orion-tests` **525 checks** · `orion-viewport-tests` **3437
checks** · **32 `repro/` scenarios** · all 0 failures. Bench exits 0 on all
three sample frames: 148 nodes, 6878 MiB, M0 gate **10.63 ms p95** on
`_PIC8220` — plus a preview graph at 1/16 that.

### Known gaps, carried forward

Small, named, and none of them blocking the next story:

| Gap | Where |
|---|---|
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero | `MatteStore` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **101 commits carry `Co-Authored-By` / `Claude-Session` trailers.** Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history | whole history |
| **The 1000-line rule is broken six ways**, all in product code: `DevelopPipeline.cpp` 2,192, `Engine.swift` 1,977, `OrionApp.swift` 1,433, `bench/main.cpp` 1,293, `DevelopPanels.swift` 1,165, `Scenario.swift` 1,080. ⚠ The two test files (7,656 and 3,297) were split on 2026-07-31 — but `Scenario.swift` crossed the line in the same run of sessions, so the count went from seven to six rather than to five. Splitting product code is riskier than splitting tests and wants its own session | whole tree |

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
[`HISTORY.md`](HISTORY.md)** — 50 sessions, moved there verbatim on 2026-07-31.

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

## Session 2026-07-31f — Auto writes five fields and nothing checked four of them

⚠ **Thirty-fifth arrival of the stale M3 prompt.** Verified and set aside.

Started by testing **my own** parting claim from last session: "the other three
M3 features have unwaived bench floors, so the bench is their wiring test." It
was asserted, not measured, which is precisely the habit these sessions keep
finding.

### ✅ The claim held for two of the three

Disabled at the host, one line each:

| Mutation | `orion-bench` | `orion-tests` |
|---|---|---|
| fusion dead | **exit 1** ✅ | 0 failures |
| creative LUT dead | **exit 1** ✅ | 0 failures |

The bench catches both. `orion-tests` catches neither, which is the same
kernel-versus-wiring split dehaze showed — worth having confirmed rather than
assumed.

### ⚠ Auto-enhance is not a filter, and the claim did not reach it

Auto is a *policy* that writes five sliders, so there is no pipeline gate to
disable and the bench's probe drives the C++ policy directly, never the button.
`repro/undo-after-auto.txt` covers the button — and covers it far more weakly
than it reads. Deleting one assignment at a time from `Engine.autoEnhance`:

| Dropped | `undo-after-auto.txt` |
|---|---|
| clarity | 0 failures |
| fusion | 0 failures |
| whites | 0 failures |
| blacks | 0 failures |
| **exposure** | 0 failures |

**Five for five.** The file asserts the frame moved and that one undo puts it
back; both are true, both worth keeping, and both are satisfied by whichever
fields remain. A button that had quietly stopped setting most of what it
computes looked exactly like a working one.

### The fix: assert the fields, not the outcome

A `control <name> <op> <value>` verb reads what a control *holds*.
`repro/auto-applies-every-field.txt` uses it, and now all five drops fail —
between 1 and 4 checks each, **5 for 5** against the old file's 0.

⚠ **Two frames, because no single frame exercises all five.** The policy is
scene-dependent and that is the point of it: the daylight plaza has blacks to
pull and no shadow lift to give (`fusion` 0.00); the night forecourt is the
reverse. A one-frame version would silently stop covering whichever field that
frame happens not to use.

### ⚠ And I nearly wrote a fifth number-about-its-fixture into a test

The first draft quoted `Screenshot --auto`'s figures for the night frame
(exposure −0.70) and the check failed: from a **clean open** the policy gives
**+0.09**. Neither is wrong — that harness applies a scene first and the policy
reads the state it is handed. Two paths, two answers, and I had pinned one path's
number into a test that takes the other.

⚠ The sign matters too. Exposure is *negative* on the bright plaza and
**positive** on the dark forecourt, so "Auto reduces exposure" would have been a
rule read off one photograph. The file says so where the bound is.

### Where this leaves the sweep

Every M3 feature now has something that fails when its wiring breaks: fusion and
LUTs via the bench, dehaze via `repro/dehaze-reaches-the-picture.txt`, auto via
`repro/auto-applies-every-field.txt`. **32 scenarios.**

## Session 2026-07-31e — dehaze could be deleted and everything stayed green

⚠ **Thirty-fourth arrival of the stale M3 prompt.** Verified and set aside — and
this session is about one of the four features it names, which makes the point
better than another evidence table would.

### ⚠ The measurement

`dehazing_ = false`, one line in `DevelopPipeline`. The node never runs, the
control does nothing on any photograph. Then:

| | |
|---|---|
| `orion-tests` | 525 checks, **0 failures** |
| `orion-viewport-tests` | 3437 checks, **0 failures** |
| `orion-bench` | **exit 0** |

The feature was gone from the product and every instrument said fine.

### Why each one missed it

**The GPU test dispatches the kernel directly**, with parameters it sets itself.
`testDehazeGpu` proves Eq. (12) and Eq. (16); it can never prove they are
reachable from the slider. (It does catch a dead *kernel* — mutating the recover
shader to pass through fails it. It is the wiring it cannot see.)

**The bench waives dehaze**, and it is the only waived probe in the table. The
reason is sound and stays: on a frame with no veil the dark channel is near zero,
the atmospheric light lands on a light source, Eq. (12) gives t = 1, and the
correct output is *no change* — a floor there would be a floor demanding a filter
invent haze. What was wrong is the blast radius. The waiver excused the control
on **every** frame, so "correctly does nothing here" and "does nothing anywhere"
were the same green.

**No scenario touched it.** Two mention dehaze; both measure cost, not output.

### The fix

`repro/dehaze-reaches-the-picture.txt`, which starts at `Engine` — the object the
panel drives. Three claims, and the pairing is what makes them sharp:

- a hazy frame **changes** (`_PIC8095`, 0.5485 → 0.3691 luma)
- a frame with no veil is **left alone** (`_PIC8148`, identical) — so a dehaze
  that always acted would fail as surely as one that never did
- zero is the untouched picture, and **0.35 is neither zero nor full**

⚠ **That last check was an afterthought and it earned its place.** There are two
mechanisms — a gate (`dehazing_`) and a strength (`omega`) — and at exactly zero
the gate alone decides, so the mutation making `omega` ignore the slider survived
both of the first two checks. Only a partial strength separates a dial from a
switch.

**Mutations:** never wired up → **3 failures**; omega ignores the slider → **1**.
⚠ A third, forcing the gate always on, **survives** — and it is not a defect: at
zero strength the chain computes an identity, so it is output-preserving and
merely wastes 15 nodes of GPU work. Only a timing check would see it. Said rather
than left as an unexplained green.

### ⚠ And I misread my own measurement on the way

The partial-strength check failed once on what I called a clean build, and I
went looking for a staleness bug in the dehaze chain. There was none: I had
restored the source and re-run the scenario **without rebuilding**, so the
previous mutation was still compiled in. The ramp measures monotonically —
0.5485, 0.5143, 0.4718, 0.4197, 0.3691 — and always did. Every mutation in this
session was re-run afterwards with a forced rebuild between each.

### The bench says so out loud now

A waived control prints a summary line naming the count. A waiver buried in one
row of thirty is a waiver nobody reads, and the next one should not be quiet.
The exit code is unchanged — the waiver is still correct, it is the silence that
was not.

## Session 2026-07-31d — the two mutations the register admitted survived

⚠ **Thirty-third arrival of the stale M3 prompt.** Verified and set aside.

`UNSOURCED.md` §23 named two claims as untested and one mutation as known to
survive. Both closed. One of them turned out to be hiding a wrong line.

### ✅ The four-connected fill, tested at last

§23 has said since the detector shipped: "the synthetic frames have no one-pixel
diagonal gap, and the mutation that adds diagonal neighbours survives."

The fixture is a wall of hard gradient between a calm sky and calm ground,
breached by two calm pixels touching **only at their corner**. Four-connected,
the fill reaches the first and stops — its four neighbours are wall, wall, sky,
wall. Eight-connected it steps diagonally into the second and floods everything:
the mutation now fills **384 of 384** ground pixels and fails two checks.

### ⚠ The eigenvalue proxy: the test found the code wrong, not the reverse

The energy took each covariance's largest **diagonal entry** as its largest
eigenvalue, justified in a comment as ordering candidates "the same way in every
case measured".

That was true only because every case measured had the same covariance
**shape**. My first fixture reproduced the same mistake — it varied one overall
spread, so every channel's variance scaled together, and then the *smallest*
diagonal entry orders the candidates identically too. The `min` mutation
survived it. Rebuilt with populations wide in different channels, the proxy
**reorders a pair in 21** against the true eigenvalue.

⚠ **And the stated reason for the approximation was false.** "A 3×3 symmetric
solve per threshold per frame is real work" — it runs 48 times in a whole
detection, against a Sobel over every pixel. Nobody had checked; the number was
plausible and wrong, which is the third time in four sessions.

It is Smith's closed form now (CACM 1961) — published, exact, non-iterative — so
this stops being a departure from the published method at all. Pinned against a
**Jacobi rotation**, deliberately a different algorithm and iterative where the
product's is closed form, agreeing to under 1e-9 across seven shapes.

⚠ **It changed no output.** Coverage on all three sample frames is identical to
the digit — 67.5%, 4.6%, 14.6%. The term really does only break ties and on this
corpus the ties do not arise. The value is that an unsourced approximation and a
false justification are gone, not that any photograph looks different, and
inflating it into more than that would be the thing this file exists to catch.

### The fixture guards itself

Both new tests carry a check on their own sharpness: that the covariances are
genuinely off-diagonal (or the agreement would be trivially true of the old code
too), and that the old diagonal shortcut still visibly reorders them (or the
fixture has gone bland). ⚠ Written after the first draft passed for the wrong
reason.

**+7 checks**, 3430 → 3437.

## Session 2026-07-31c — the sky check that could not fail

⚠ **Thirty-second arrival of the stale M3 prompt.** Verified and set aside.

Chasing the loose end session `a` flagged and did not follow: the screenshot
harness produced a sky matte on a night frame, where this file said both night
frames refuse.

### ⚠ First: last session's parting claim was wrong, and it was mine

Session `b` closed by naming the 12 ms fixed render cost as "75% of the frame
budget before any brush work happens… the next real lever". Measured, with
`interact on`:

| | Settled | Interactive |
|---|---|---|
| Exposure drag | 9.5 ms | **1.7 ms** (594 fps) |
| Brush, 2400 dabs | 64.7 ms | **5.1 ms** (197 fps) |

Degrade-then-refine already covers the drag; the 9.5 ms is paid **once when the
hand stops**, not per tick. There was no lever there. (Session `b`'s own work
still stands and reads better in this light: it took interactive painting from
about 134 ms a tick — 7 fps, unusable — to 5.1 ms.)

Nothing is now over budget on the interactive path, which is why this session is
not a performance story.

### ⚠ The check asserted nothing, and had asserted nothing since it was written

`repro/sky-mask.txt`'s refusal half read:

```
open samples/_PIC8148.ARW
set exposure 2.6
measure ... nightBefore
set localExposure -2.0
measure ... nightAfter
expect nightAfter == nightBefore
```

It never calls `select`. With no mask row on the photograph a local exposure
does nothing, so the two measurements are equal **by construction** — the check
passed whether the detector refused, accepted, or did not exist. Its own comment
explained the compromise: "`select` throws when the detector declines, so a
scenario cannot assert the refusal without failing."

That is the second time this project has shipped a green check that could not go
red, and both times the tell was the same: a check written *around* an
inconvenience rather than the inconvenience being fixed. The fix was one verb.
`refuses subject|person|sky` asserts a decline as the result it is.

### ⚠ What it was hiding: the answer depends on the edit

Same photograph, same detector, different exposure:

| Exposure | `_PIC8148` |
|---|---|
| 0 EV | **accepts**, 4.6% — and covers the **treetops**, sky unselected |
| +1 EV | accepts, same inversion |
| +2.6 EV | refuses |

So "both night frames refuse" was true only at the exposure the scenario
happened to set. At 0 EV it does not refuse — it **inverts**. Screenshotted and
looked at: the overlay sits on the treetops along the top edge and on the
top-right tree, and the entire starry sky is clear.

The cause is not a wrong line. The search is frame-relative by construction —
thresholds are percentiles of *this render's* gradient, the guard compares
against *this render's* mean — and on a grainy high-ISO sky over flat black
silhouettes the calmest region joined to the top edge genuinely **is** the
foliage. Lifting the shadows lifts the grain, which is what pushes it past the
guard at 2.6 and not at 0.

⚠ **Compounded by the gap table**: a matte is never regenerated when the edit
changes, so whichever answer the photographer was looking at when they pressed
the button is frozen into the edit.

### Not fixed, and why

The detector is left alone. Its failure list already covers this frame, #78
already argues a visibly wrong mask is acceptable because it is inspected as an
overlay and corrected with the brush, and the overlay does show it plainly.
Tightening the guard to make the old sentence true would mean inventing a
constant to fit a claim — which is the shape of the purple cast, not its cure.

What was wrong was the **test and the record**, and those are fixed: the
refusal is asserted for real at 2.6, and the inversion at 0 EV is pinned as
shipped behaviour — the treetops move under a local exposure and the open sky
comes back bit-identical.

**Two mutations dead:** a detector that never refuses (the `refuses` check goes
red) and one that always refuses (the daylight frame's `select` throws). ⚠ The
old check survives **both**, by construction — it never asked.

`research/sky-detection.md` and this file's session `u` entry both corrected.

## Session 2026-07-31b — the brush stops being quadratic

⚠ **Thirtieth and thirty-first arrivals of the stale M3 prompt.** Verified
against the tree once more and set aside.

`research/brush-acceleration.md`, decision #80. The largest measured latency in
the project, and the number this file carried for it was wrong.

### ⚠ The recorded figure was a number about the fixture

Session `2026-07-30p` wrote "a 120-dab stroke costs 110–138 ms" and called it
"inside the budget today". That came from the bench probe's own short stroke.
Measured across stroke lengths, with the fixed cost isolated:

| Dabs | Node re-render | Loop alone |
|---|---|---|
| 0 | 12.0 ms | — (a radial drag is 11.8 ms: same node, no loop) |
| 2 | 14.1 ms | ~2.1 ms |
| ~300 | 152.3 ms | ~140 ms |
| ~2400 | **2148.4 ms** | ~2136 ms |

Fifteen times worse than the figure on file at eight frame-widths, and the cap
allows seven times more again.

### ⚠ Two measurements that were of the wrong thing, one of them mine

The first attempt dragged **local exposure** and reported a flat 10.5 ms at every
stroke length — which is true, and says nothing, because that control does not
dirty the mask node so the dab loop never re-runs. The loop only re-runs when the
stroke or the nib changes. Dragging the nib is what produced the table above.

Worth keeping as a fact about the product, not just about the test: most drags
are unaffected. The cost lands on **painting**, where every appended dab re-runs
the loop over every dab so far.

### The fix, and why the obvious version of it is wrong

⚠ **The cost is the fetch, not the arithmetic** — one texture read per dab per
pixel is ~58 billion reads at 2400 dabs on 24 Mpx. So a faster inner test was
never the answer. One level of hierarchical bounding volumes (Clark 1976): a box
per run of 64 consecutive dabs, in a 4 KB aux texture, skipping 64 reads with one
test.

⚠ **Runs, never a spatial partition.** Paint is source-over, erase is
destination-out, and the two do not commute — an index range keeps application
order for free where a per-tile bin has to rebuild it with a stable scatter or a
sort, which is three kernels and a prefix sum inside a static graph.

⚠ **The boxes are unexpanded, and tested in the per-dab test's own expression
shape.** Growing each box by the nib radius on the host is the version that looks
right and is not: `fl(q·W) − fl(c·W)` is not `fl((q−c)·W)`, floating point does
not distribute, and the two tests would disagree on some pixel near some rim.
Written as the same comparison, monotone rounding does the work — when the block
test fires, every dab in the run takes the existing `continue`, which performs no
floating-point operation on the accumulator, so the skip runs exactly the
instruction stream the full loop would have.

⚠ And the boxes come from the **float32 texels actually uploaded**, not from the
values before the geometry transform, or a box can round tighter than what is
stored and skip a real dab.

### Measured

| Stroke | Before | After |
|---|---|---|
| ~2 dabs | 14.1 ms | 11.4 ms |
| ~300 dabs | 152.3 ms | **14.5 ms** |
| ~2400 dabs | 2148.4 ms | **65.6 ms** |

Bench probe 127.22 → **36.27 ms**, floor and coverage unchanged. M0 gate 10.63 ms.
148 nodes and 6878 MiB both unmoved — the boxes are 4 KB a component.

### ⚠ The oracle is the kernel itself, with rejection disabled

The new test renders the same 999-dab stroke twice: once with the boxes widened
to the whole plane, which disables every rejection exactly and *is* the
unaccelerated loop, and once with the real boxes. Bit-identical, not close. A CPU
model would have been a stand-in with its own bugs, and bit-identity is the one
claim a stand-in cannot support.

⚠ **And a third render, because the first two are circular on their own.** If the
kernel ignored the boxes, they would agree trivially. Boxes placed far from the
stroke must therefore return an *empty* frame — and the mutation that disables
rejection passes the identity check and fails exactly that one.

The fixture is 999 dabs (not a multiple of 64, so the partial run is exercised),
self-crossing so runs overlap, every seventh dab erasing so order decides the
answer, and eight centres off the frame edge.

**Three mutations dead:** the shader's block size disagreeing with the host's
(6549 texels differ, and it takes three older tests with it), dropping the
partial-block clamp (6 failures), and disabling rejection (the positive control).

⚠ **One mutation survives and it is not a defect**: `>` → `>=` in the block test.
At exact equality every dab in the run is `r` away, so `d ≥ 1` and all of them
hit the existing continue. `>` is kept because that argument leans on `sqrt` at
the boundary and the strict form needs no argument at all. Written down in the
research file rather than left as an unexplained green.

### Deliberately not done

**Incremental accumulation**, which would make painting O(1) in stroke length and
is the only fix for the frame-filling-scribble case that defeats the boxes
entirely. It needs a persistent ~97 MB R32F accumulator per brush component and a
host predicate deciding when the stroke's prefix is unchanged — and the cheap
version of that predicate ("did the count grow?") fails by rendering a completely
plausible brushstroke from a stale accumulator. Costed in `ROADMAP.md`.

## Session 2026-07-31a — a matte is saved with the photograph

⚠ **Twenty-ninth arrival of the stale M3 prompt.** Verified against the tree
again rather than re-litigated — research file, code, GPU test section and bench
probe for each of dehaze, creative LUTs, exposure fusion and auto-enhance — then
set aside, and the oldest entry in the gap table taken instead.

Every other mask kind is a handful of numbers in the sidecar. Kind 4 is a raster
of ~700k alpha values and was written **nowhere**, so a Subject, Person or Sky
row reopened present and empty. `app/MatteStore.swift`, decision #79.

### ⚠ The trap was colour management, and it is the purple cast's shape

CoreGraphics colour-manages greyscale. Write 0.5 through a Gamma-2.2 grey space
and read it back as linear and it comes back **0.735** — every feathered edge on
every reopened photograph shifted, nothing crashing, nothing to see. Both ends
name `CGColorSpace.linearGray` and the mutation that changes one of them fails
five checks.

⚠ **Which is why the fixture is a gradient ramp.** Every matte fixture this
repository had was binary — a disc, a half-plane — and a binary matte survives a
wrong colour space, a wrong bit depth *and* a wrong byte order, because 0 and 1
land on 0 and 1 however the curve between them is mangled. Only mid-values can
tell, and a feathered selection is made of mid-values.

### ⚠ The ramp could not see a flip, and a `CGBitmapContext` origin is bottom-left

The ramp varies along x only, so a **vertical flip leaves it identical** — and
the decode draws a top-down `CGImage` into a bottom-left-origin context, which
is exactly where a flip would come from. A flipped matte is not a broken-looking
mask; it is a plausible selection of the wrong half of the photograph. One
bright corner pins both axes, and the mutation that flips the draw fails it.

### ⚠ And the scenario's first draft measured something other than its claim

It asserted the reopened bands still differ *from each other*. They do that on a
photograph with **no mask at all**, because the picture itself varies across the
frame — measured with the matte deleted, that draft reported three of five
green. There is an unmasked baseline taken before any local exposure now, and
nowhere for an empty matte to hide. **Eighth time in this file's history** that
a first-draft check measured something other than its claim.

### What was decided, and what it refuses

Sibling `PHOTO.orion-matte-<uuid>.png` beside `PHOTO.xmp` — storage, not cache,
because Vision's models move between macOS releases and re-running on open would
silently change a finished edit on an OS update. Base64 in the XMP is refused:
autosave rewrites that attribute 900 ms after any slider moves. Files are
immutable and the **write precedes the reference**, so the sidecar can never
name a file that is not on disk and the only reachable crash window leaves an
orphan. Orphans are swept on open and **only after the sidecar parses** — an
unreadable sidecar yields no components, and sweeping against that would delete
every matte the photograph has.

⚠ **A missing file is reported, not swallowed.** `Engine.missingMattes` and a
panel caption; leaving the row with no coverage would change the picture with
nothing on screen saying why.

⚠ **`MaskComponentState` names its coding keys `Key`, not `CodingKeys`**, so the
encoder is synthesised from the stored properties while the decoder reads the
hand-written list. `matteId` failing that way would have been written to every
sidecar, read back never, and **swept away by this feature's own cleanup** on
the next open. Pinned.

### Measured, and looked at

24 new checks in `orion-viewport-tests` (3406 → 3430) on `MatteStore` alone — it
deliberately knows nothing about `Engine`, so the round trip is pinned without a
GPU. Plus `repro/matte-survives-a-reopen.txt`, 9 checks through the whole
loading path via a new `reopen` verb. **Five mutations dead:** the gamma space
(5 failures), the dropped clamp (2 — 1.5 wrapping to 0.5, a hole punched in the
most-covered region), 8-bit quantisation (5), the flipped draw (2), and saving
nothing at all (6). Both captions screenshotted.

M0 gate unmoved — nothing here is in the render path.

### Also: the panel says which of three states it is in

Saved, made-but-not-written, and file-missing are three different sentences.
⚠ `maskMatteSaved` is separate from `maskMatteSource` on purpose: the screenshot
harness makes a matte without writing one, and a caption that read the label and
then promised the file would claim a save that did not happen.

### One observation, not chased

The `sky` screenshot scene lifts exposure +1 EV and then **does** produce a
matte on `_PIC8220`, a night frame — where session `2026-07-30u` records both
night frames refusing. The detector's smoothness ratio is frame-relative, so a
lifted frame plausibly passes a check the unlifted one fails. Not this story,
not investigated, written down rather than left unnoticed.

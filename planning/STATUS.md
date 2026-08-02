# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-08-02 (**no file in the tree is over the 1000-line ceiling — `Screenshot.swift` was the last, #131**)

**Last updated:** 2026-08-02 (**both checks that named their own mutation and missed it now fail on it — #130**)
**Phase:** M0 done. **M1 complete.** M2 and **M3 complete** — its last two open
items are now closed, one built and one refused (#103 built, #101 refused). **`research/masking.md` is
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
3. ~~**Incremental brush accumulation.**~~ ✅ **done 2026-08-01, #102 and
   #108.** Both sessions shipped. A pointer event's cost is now flat in what
   is already painted — `mask:0` **5.20 ms appending 49 dabs to 294 against
   36.46 ms re-laying them**, same dab count and same host work, interleaved.
   One R32Float accumulator for the live component, **98.25 MiB** rather than
   the 393 a per-component one would have cost, decided from the arithmetic
   rather than assumed. Ten mutations; three passed and were defects in the
   checks. The first evaluation of a component is still linear, which is once
   after a reload rather than once an event. Original note follows.
   **Cause proved 2026-08-01** — it is
   `mask:0`, on **both** graphs, and the cost is `Σ blocks × box area`, not the
   dab count. Appending grows the block count and leaves the boxes their size,
   so it is linear. Host side is flat and three orders down (0.057 ms an event
   at any stroke length). ⚠ The bench's "the mask kernel is flat in dabs" was a
   **fixture artifact** and is withdrawn — it subdivided a stroke of fixed
   extent, which shrinks every box in exact proportion to the block count.
   Decomposed into two sessions in `ROADMAP.md`; **the predicate ships first,
   alone.** ✅ **Session one done 2026-08-01, decision #102** —
   `params::unchangedPrefix`, six mutations, no pixel moved and nothing reading
   its answer yet. **Session two is the accumulator behind it**, and its budget
   check (~97 MB a component at 24 Mpx, lazily allocated) comes before the
   code. ~1 session left.
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
4. ~~`DevelopPipeline.cpp` against a stated ceiling of 1,000~~ — ✅ **done
   2026-08-02, decision #113.** It had reached **2,896** lines and its header
   **917**; both are now five files, largest **757**. The seam is the graph's
   four regions and each file holds *both* halves of its region — the nodes the
   constructor adds and the blocks `apply` pushes into them, which used to be
   twelve hundred lines apart. Pure refactor, proved rather than asserted: nine
   canvas renders byte-for-byte identical, 173 nodes and 7186 MiB unchanged.
5. ~~`Engine.swift` against the same ceiling~~ — ✅ **done 2026-08-02, decision
   #117.** **2,331** lines, now eight files, largest **795**. Same seam as
   #113 — region of the problem — plus the constraint Swift adds: `Engine` is
   `@Observable`, so every **stored property** had to stay in the class body and
   only behaviour moved into `extension Engine`. 27 canvas renders byte-for-byte
   identical against `ab6f9b2`. ⚠ Two of seven mutations were green everywhere
   and were defects in the *checks*, both rewritten; and `lastFailure` turns out
   to have no oracle anywhere, which is recorded rather than fixed.
6. ~~`DevelopPanels.swift` against the same ceiling~~ — ✅ **done 2026-08-02,
   decision #122.** **1,366** lines, now seven files, largest **580**. The seam
   is the **tool tab**, because that is the unit a change arrives in: a slider
   belongs to exactly one tab, so the file named after the tab is the file you
   open. ⚠ **No SwiftUI state moved** — the file declared one property wrapper in
   1,366 lines and it travelled inside its own type; the panels are extensions
   on `Editor`, so they cannot hold storage. 41 of 42 interface renders
   byte-for-byte identical (the 42nd stamps the wall clock and is compared by
   eye), repro output identical, 800 / 3702 / bench 0. ⚠ **Its mutation found a
   hole the frame comparison cannot see: the Detail panel scrolls, and five
   whole sections below the fold — Grain, Vignette, Dehaze, Clarity, Sharpening
   — can be deleted with every check in the repository green.** Recorded, not
   fixed — and ✅ **closed 2026-08-02 by #125**, along with the Photo menu and
   the footer's failure line, which had the same cause.

   **The product's large files are down to `OrionApp.swift`**; the ceiling's
   survivors were then all in `apps/tests/`, which is a different argument —
   and #127 and #129 have since taken all four of those. The last file over the
   ceiling anywhere was `app/Screenshot.swift`, and **#131 took it — nothing in
   the tree is over 1,000 now**. See the gap table, recounted by sweep.

6. ~~`Scenario.swift` against the same ceiling~~ — ✅ **done 2026-08-02, decision
   #120.** **1,615** lines, **977 of them one `switch`**; now five files, largest
   **546**, and `Scenario.swift` itself **301**. Same seam again — region of the
   problem — cutting the *switch* rather than lifting helpers away from it, so a
   new verb is one edit in the family it resembles and a new slider is one edit
   in `Scenario+Controls.swift`. ⚠ A **pure move on an interface** (#89): all 158
   `case` labels are an identical multiset before and after, so no alias pair
   merged. All 40 scenarios byte-identical in *output*, and 51 rendered artefacts
   byte-identical against `9d9158d`. ⚠ **Mutation M4 found a check that cannot
   fail** — disable three quarters of the verbs and 39 of 40 scenarios still exit
   0, because 38 then run **zero checks** and the runner exits 0 on a run that
   asserted nothing. Recorded, not fixed.

Closed since this list was last written, in the order they went:
**dehaze's drag cost** (#92), the **`reopen` leak** (#90), **M1's library gap**
(#91), the **export panel** (#93-#95), and now the **creative vignette** (#103)
with **split toning refused** (#97).

✅ **M1's library gap is closed** — SQLite index and persistent thumbnail cache,
2026-08-01, decision #91. 300 frames with the page cache warm: **454–688 ms cold
against 28–54 ms warm, 12.9–17.2×**. The leftovers are named and costed in
`ROADMAP.md` under *Library index — what is not done*.

⚠ **M5 is months, not sessions**, and saying otherwise would be a lie: it holds
an X-Trans demosaic (Markesteijn), a Windows port, Core ML denoise and
user-loadable DCP profiles, each a multi-week epic on its own.

✅ **Two of those four are now researched, and neither was built.** Core ML
denoise (2026-08-01, #111) and **X-Trans (2026-08-02, #114)**. Both deliverables
were the write-up and a costed piece table; both tables have a **guess** column,
because `highlight-reconstruction.md`'s estimate was 16× out for want of one.

✅ **X-Trans — `research/demosaic-xtrans.md`, seven pieces in `ROADMAP.md`,
`UNSOURCED.md` §29, decision #114.** ⚠ **The line's own name is wrong.** It is
not "port Markesteijn": **that algorithm has never been published**, so the only
description is source code and the accessible copies (darktable, RawTherapee) are
GPL-3 — closed. And no port is needed, because **LibRaw already ships the same
code as `xtrans_interpolate` under LGPL-2.1 / CDDL-1.0** (`libraw.h:451`, read
off this machine), which Orion already links. ⚠ **The price is decision #29, not
the licence:** #29 clips between white balance and demosaic, so a demosaic that
leaves the GPU takes the temperature slider with it. Two surprises — the GPU
graph gets **smaller** (−5 nodes, −446 MiB, so 168 against 173), and a **40 MP**
X-Trans body puts the *existing* graph at **~11.8 GiB** before any of this.

✅ **Core ML denoise, 2026-08-01, decision
#111.** `research/denoise-learned.md` plus a six-piece table in `ROADMAP.md`.
**Nothing was built and that was the deliverable.** The line above is confirmed
rather than contradicted: it is **not a graph node** (one fp16 32-channel
activation at 24 Mpx is 1,480 MiB — exactly what the whole existing 8-node
denoise chain costs) and **not a slider** (NAFNet's own 65 GMAC is 48.1 TFLOP a
frame, ≥1.6 s on optimistic hardware against a 16 ms budget). ⚠ **The real
blocker is the domain, and it was mis-stated everywhere until now**: Orion's
noise *fit* is pre-demosaic but its *filter* runs post-demosaic in linear camera
RGB, which is neither the sRGB nor the Bayer domain any published checkpoint is
trained for. Pieces 1–3 are two measurements and a decision that is allowed to
say stop.

Film grain is **finished and shipped**. All six canvas gestures arm. The rest of
the performance action item is in `ROADMAP.md`. `DevelopPipeline.cpp` was the
largest standing violation of a stated hard constraint and **is not any more**
(#113, 2026-08-02); `apps/bench/main.cpp` was the second and **is not any
more** either (#118, same day, **2,289 → 85**). `apps/tests/tests_effects.cpp`
was the third and the largest file in the tree, and **is not any more** (#127,
same day, **1,716 → 555 + 865 + 331**, cut at the fixture). The gap row below has
been **recounted by sweep** rather than edited.

### ⚠ In flight — seventh wave, two agents, 2026-08-02

Both stories are **holes the splitting wave found by mutation and correctly left
alone**, because a fix inside a refactor is unreviewable. Neither is a feature;
both are coverage the project believed it had.

| Working on | Decision | The hole |
|---|---|---|
| A floor so a scenario that measures nothing cannot pass | #124 | ⚠ Make one verb family claim every verb and **39 of 40 scenarios still exit 0** — 38 of them run *zero* checks, print `orion: 0 checks, 0 failures`, and exit 0. This is the gate named in `CLAUDE.md`, in the working agreement and in every brief. Told to check how many of the 40 legitimately assert little before choosing a rule, and to mutation-test the guard itself |
| Three UI coverage holes with one root cause | #125 | `Screenshot.swift` builds `Editor` directly and drives only what is on screen at rest. So: the **Detail panel scrolls and coverage stops at the fold** (Grain, Vignette, Dehaze, Clarity, Sharpening — 60 lines of shipped controls — delete with everything green); the **Photo menu is in no capture** (`PhotoCommands` never reached, deleting Reset Adjustments reddens nothing); and the **footer's `lastFailure` display has no oracle** — today's `nofailure` verb pins the engine's *state*, not the line that shows it |

⚠ **#125 is told that "unreachable, and here is the consequence pinned instead"
is an acceptable answer** for the menu, because that is what #110.3 concluded
about gesture arming after genuinely trying, and it was right.

### ⚠ The first wave, and the wave that died — both in `HISTORY.md`

Five agents landed on 2026-08-01 and a second wave of four was killed by an
API session limit with nothing committed. Both accounts, and the one lesson
worth keeping — **an agent on a multi-hour task commits a skeleton early and
refines it, so a kill costs the last increment rather than the session** — are
in `HISTORY.md` under *Agent waves, 2026-08-01*. They are history now: the
first wave is merged and the second was relaunched and landed. ⚠ **Waves two
through six, and their write-ups, moved to `HISTORY.md` at the 2026-08-02 prune
(#132)** — this line used to end "and is the table below", and the table is
there now.

### ✅ 2026-08-02 — `Screenshot.swift` is five files, and the oracle did not move (#131)

**1,196 lines against a ceiling of 1,000 — the last file in the tree over it.**
It was **809** that morning; #125's three interface checks took it over the same
day, so the tool that closed three coverage holes became the only remaining
violation. **After this sweep, nothing in the tree is over 1,000.**

**The seam is the check/picture line, and #125's own header had already drawn
it.** Three scenes — `detail-tail`, `menu`, `render-failed` — assert rather than
pose, and a scene that asserts is a different kind of thing. So:

| File | Lines | What a change to it is |
|---|---|---|
| `Screenshot.swift` | 315 | the command line and the driver, in run order |
| `Screenshot+Scenes.swift` | 353 | **add a scene** — `apply(scene:to:)`, `tab`, `snapshots` |
| `Screenshot+Checks.swift` | 189 | **add a check-scene** — `checkMenu`, `requiredCommands`, `scrolls`, `minimumHeight` |
| `Screenshot+Measure.swift` | 315 | pixels out, as pictures and as numbers |
| `Screenshot+Render.swift` | 109 | a SwiftUI hierarchy to a PNG, offscreen |

Verbatim motion, extracted by line range with a script that proves **every one
of the 1,196 lines is claimed by exactly one destination**. The one non-blank
line not carried is `// MARK: Scenes`, replaced by the per-file headers.
⚠ #117's Swift tax again: `private` is file-scoped, so **eleven members widened
to internal** because `run` calls across every seam; four stayed private. In the
header of `Screenshot.swift` so nobody reads it as carelessness.

#### ⚠ The oracle was checked against itself before it was believed

This file **is** the interface oracle, so breaking it silently would blind the
checks that guard the UI. 45 scenes plus `menu`, rendered **twice from the
pre-split binary**: **44 of 45 agree with themselves, and `versions` does not** —
exactly #125's warning, its rows stamp `Date()` at minute resolution. So
`versions` is reported, not asserted around.

| Comparison | Result |
|---|---|
| pre-split vs pre-split (self-check) | 44/45 identical; `versions` differs |
| pre-split vs post-split | **44/45 identical; `versions` the only difference** |
| exit codes, 46 runs | identical |
| stderr, 46 runs (path normalised) | identical |
| 40 repro scenarios, full output | 36/40 byte-identical, **40/40 exit 0** |

`detail-tail` reproduces #125's measurement to the tenth — 570.0 points
scrolled, 1,701.0 of content past a 1,131.0-point window — and `menu` reports
the same 75-item bar with 26 of 26 present. The four repro logs that differ
differ **only in milliseconds and frames per second**: `slider-drag-cost`,
`gesture-preview-agrees`, `eyedropper-latency`, `dehaze-reaches-the-picture`,
every one a scenario that prints a latency.

#### The mutation table — all three of #125's, all three red

| # | Mutation | Check | Result |
|---|---|---|---|
| M1 | delete Detail's five below-fold sections (`DevelopPanels+Detail.swift` 126–185, exactly the 60 lines #125 counted) | `detail-tail` | **exit 1**, "nothing overflows the panel column", no frame written |
| M2 | delete the Reset Adjustments command | `menu` | **exit 1**, bar 75 → 74, 25 of 26, names the item and prints the whole bar |
| M3 | delete the footer's `else if let why = engine.lastFailure` branch | `render-failed` | exit 0, frame **differs at char 11,672,187** — the same offset #125 recorded — **8,671 pixels**, band x 2632–3359 / y 1790–1846 |

M3 confirmed by eye: the amber *Render failed — the compute pipeline could not
be built* is gone, replaced by the ordinary grey hint. Each mutation reverted
with `git checkout`, tree confirmed clean.

⚠ **M2 could not be written the way #125 wrote it.** Deleting only the
`Button("Reset Adjustments")` line does not compile — the `.keyboardShortcut`
and `.disabled` modifiers below it dangle onto `View` (`error: instance member
'keyboardShortcut' cannot be used on type 'View'`). The mutation is the
three-line item. Worth knowing before anyone re-runs it.

#### Gates

800 / 3708 / 40 of 40 / bench exits 0. ⚠ **The bench's first run said FAIL at
p95 32.13 ms and it was contention, not the build** — spread 5.65 ms across
rounds, run while this split's own rebuilds were still finishing. Rerun on a
quiet machine: **9.68 ms, spread 0.24**. The bench links no Swift at all, so
this split cannot reach it; #116's point about the M0 gate stands.

### ✅ 2026-08-02 — the two checks that claimed more than they checked (#130)

**Decision #130.** The two holes #127 and #129 found by mutation and left alone
are closed, and **a third of the same kind turned up while proving the first**.
Both fixes are in `apps/tests/`; no engine line changed.

⚠ **`testPerspectiveMaskExtent` check 6 named the `W⁻¹JW` conjugation and drove
a pure aspect squeeze, whose Jacobian is diagonal — so the conjugation
multiplied two zeros.** The squeeze block stays and now *asserts* that its
derivative is diagonal, so the blindness is on the record rather than implied.
Beside it, **6b drives a two-way keystone** (`{0.8, 0.6, 0}` on 600×400) at four
off-axis spots, whose least |off-diagonal| is **0.0674**, and checks the carried
derivative against **a central difference of `toFrame`'s own neighbouring
centres** — an independent answer, because a *position* never passes through the
conjugated matrix. Deleting the conjugation now prints **`worst 0.072389`**
(tolerance 1e-3) and **`1.483727 rad`** (tolerance 5e-3) and exits 1.

⚠ **The third hole was the file's own header**: *"every check below fails on the
isotropic version"*. Running that mutation reddens **five**, and four checks are
deliberately blind to it — check 4 preserves area on purpose, check 5 is the
neutral control. Counted from the run, not from the sentence.

⚠ **The highlight-fill comment was rewritten *and* the check strengthened.** *"A
constant rim fills with that constant"* claimed *"any weighting error, any lost
normalization, any half-texel drift … shows here"* and reached none of them:
with constant data every value in the pyramid is c·w for one c, so any blend
carrying colour and weight through the same arithmetic returns c whatever its
weights are. The naive un-premultiplied `lerp(up, f, f.a)` — the mistake
`hl_push.slang`'s own header warns about — leaves it green at **3e-7** and
reddens only the CPU-twin check. The comment now says what it does assert (the
*pairing*: the fill divides by the weight it accumulated). The one piece of the
old claim that could be made true here is now **a check**: both kernels promise
w stays in [0, 1] — the pull's taps are a partition of unity, the push's
w + (1−w)·w_up is convex — and neither said so in a test. Dropping the
premultiplied guard carries the weight to **7.12** and it now goes red at the
block itself instead of two files away.

Gates: **806** checks (800 + 6 new), **3708**, **40 of 40**, bench exit 0, M0
p95 **9.21 ms** against 16.

### ✅ 2026-08-02 — the last three oversize test files, split at the fixture (#129)

**Decision #129.** `tests_brush.cpp` **1,142 → 824**, `tests_perspective.cpp`
**1,110 → 837**, `tests_grade.cpp` **1,029 → 653**. Two new files
(`tests_spot.cpp` 246, `tests_linear.cpp` 390) and two existing ones grown into
(`tests_color.cpp` 324 → 414, `tests_mask_geom.cpp` 350 → 642). **`apps/tests/`
is now entirely under the ceiling**, and by sweep at `49c8a83` the only file
over it anywhere in 235 tracked sources is `app/Screenshot.swift` at 1,194,
which is another story.

⚠ **All three were the case #126 warned about — only just over the line — and
all three had the same shape underneath.** In each file the *subjects*
outnumbered the *fixtures*, and what came out was a whole test that shared
nothing at all with the rest of its file: no device, no kernel, no helper, no
frame. `testBayerDecimation` is a CPU Bayer mosaic in a GPU brush file, and it
went to `tests_color.cpp` because its assertion is `channelAt` on **the fixture
`testCfa` builds twelve lines above where it now sits**. `testSpotRemovalGpu`
dispatches two kernels nothing else in the tree touches. The two host-side
perspective mask checks read no GPU and none of their file's anonymous
namespace — they are `mask::toFrame` on the host, which **is** what
`tests_mask_geom.cpp` already is.

⚠ **Two pairs were deliberately kept together, on #127's grounds.** The brush
prefix predicate and its wiring test each name the other in their headers
(*"the unit test above cannot see the two things this one exists for"*), and
mutation M10 reddens checks in both at once. The tone bands and the local
adjustments share one `developLinear` dispatch, and the first has to explain a
mask binding it never samples while the second is the test that samples it —
so they are one new file rather than two.

⚠ **`testLensAutoScale` was left where it is**, and that is a decision: moving
90 lines to sit beside `testLensGpu` would take `tests_tone.cpp` to 948 to save
90 here. A split that does not clearly help is worse than no split.

⚠ **Verbatim, proved by index**: 276 + 377 + 82 + 238 lines sliced out by line
number and sliced back out of their destinations to compare with `HEAD` line
for line. `orion-tests` output **byte-identical**, `diff` clean, 800 checks.

⚠⚠ **Fifteen mutations, and the one that stayed green is the finding**:
deleting the W⁻¹JW conjugation from `mask::unperspective` — *the exact mistake
the check beside it names* — is invisible to every gate, because that check
drives a pure aspect squeeze and a diagonal Jacobian makes the conjugation a
no-op. Left unfixed and in the gap table. Two dangling doc comments (in
`tests_brush.cpp` and `tests_grade.cpp`, both strays from the 2026-07-31 split)
were also left alone, matching the one #127 recorded.

Gates: **800 / 3708 / 40 of 40 / bench exits 0**, p95 **9.37, 9.15, 9.27 ms**
over three runs against a 16 ms gate.

### ✅ 2026-08-02 — the dead function and the comments that lost their subject (#128)

The other half of what the splitting wave wrote down and did not fix. #122 named
two defects in `DevelopPanels`; both are fixed, and the sweep they invited found
**four more of the second kind and one comment that was simply wrong**.

| What | Verdict |
|---|---|
| `maskKindCell` | **Deleted.** One occurrence in the whole tree — its own declaration. `grep -rn maskKindCell` over everything but `.git`, `build` and `third_party` returns the decl plus two mentions in `STATUS.md`/`DECISIONS.md` |
| The doc above it | **Moved and rewritten.** It opens *"Masks — 'local' adjustments, on a tab of their own"*, so it is `maskPanel`'s; it sat where `maskPanel` used to be. Its last paragraph claimed *"the picker is a grid now"*, which stopped being true when the grid became `addMenu` — rewritten rather than relocated intact |
| `DevelopPanels.swift` → `OrionApp.swift` | **Rewritten.** Sent readers to `OrionApp.swift` for `section`, `slider` and the tab bar; they are in `OrionApp+Tools.swift` and `OrionApp+Chrome.swift` (#121) |
| `OrionApp+Tools.swift`, `OrionApp.swift` ×2 | **Rewritten.** All three still called `DevelopPanels.swift` the home of the panels. It holds one button (#122) |
| `Engine.swift` | **Moved.** *"Names the control being changed"* documents `edit(_:_:)` and sat on `log`, which had a doc of its own directly beneath it |
| `PhotoIndex.swift` | **Moved.** `refreshMarks`'s whole doc — stat, read, stat again — sat on the `marksReadWindow` test hook |
| `Screenshot.swift` | **Split three ways.** `measure`'s doc, `regionStats`'s doc and `Surface`'s own were one block on `enum Surface` |
| `MatteStore.swift` | **Split three ways.** `referenced`'s one-liner and the forty-line sweep policy both sat on `SidecarState`, which `a76ebfb` inserted above them. `git show` of the parent commit confirms the original attachment of each |
| `LocalRefusals` / `PipelineOrder` "kept and still tested" | **Corrected, not deleted.** Both views are unreferenced and deliberately so — an in-code note says restoring either is one line. Nothing tests them; what is tested is `AdjustmentCatalogue.refusedLocally`, the table they generate from |

⚠ **Five more unreferenced declarations found and left, on purpose.**
`removeSpot(_:)`, `jumpHistory(to:)`, `clearPlaceholder()`, `moveCrop(dx:dy:)`
and `allSyncableKeys` each have exactly one occurrence in the tree. All five are
the *named half of a pair whose other half ships* — `removeLastSpot`,
`undo`/`redo`, `showPlaceholder`, `setAspect`, `keys(for:)` — so each reads as a
feature that was never wired rather than a leftover, and `removeSpot`'s own doc
says which one: *"what a selected spot and a Delete key mean"*, and no spot
Delete handler exists. Deleting something that turns out to be used is worse
than leaving something dead, so they are listed here and left.

⚠ **How they were found, so the next sweep is cheaper.** Two scripts, both in
the session scratchpad rather than the repo. Dead declarations: extract every
`func`/`var`/`struct`/`enum` name in `app/`, strip comments from every code file
in the tree, count whole-word hits — one hit means the declaration only. Lost
comments: inside a `///` block, a **short** line ending in `.` followed
immediately by another `///` line means a paragraph ended early, which is what a
second doc pasted onto the first looks like. Fifteen candidates, nine of them
one subject written in two headlines and fine, six real.

Gates 800 / 3708 / 40 of 40 / bench 0. ⚠ Three of those are blind to Swift, so
`--scene detail` and `--scene detail-tail` were rendered from the pre-change
binary **twice first** — byte-identical, so the oracle is an oracle — and both
are byte-identical after the change by `cmp`. Which is the point: the only
executable line deleted was one nothing could reach.

### ✅ 2026-08-02 — `tests_effects.cpp` is three files, and every check kept its fixture (#127)

**1,716 lines, the largest file in the tree**, and the first split cut at the
**fixture** rather than the region — #126's seam, applied to the file it was
raised about.

| File | Lines | The fixture it is named for |
|---|---|---|
| `tests_display.cpp` | **331** | One row of scene-linear ramp through `developDisplay`, with a curve LUT and a cube bound. `testOutputDepth` + `testCreativeLut` |
| `tests_highlights.cpp` | **865** | A blown lamp in a warm surround, in three forms. `testHighlightHaloGpu` + `testHighlightFillGpu` + `testHighlightFillWiring` |
| `tests_effects.cpp` | **555** | Two operators that each build their own frame and read nobody else's. `testLocalLaplacianGpu` + `testDehazeGpu` |

⚠ **Seven test functions, four fixture families — fewer fixtures than subjects,
which is the whole reason the seam is not the subject.** Output depth and
creative LUTs are different subjects on the *same dispatch*, and the depth test's
own comment already has to explain the cube it never samples (*"or every texture
after it shifts by one, which is silent and total"*); a subject split puts that
comment in one file and its texture in another. The three highlight tests are one
scene, and the wiring test's checks are **written against the solver test's** —
*"`testHighlightFillGpu` asserts separately that it does not"* — so separating
them leaves each half asserting a premise it no longer establishes.

⚠ **Proved a pure move rather than asserted one.** 1,708 body lines were sliced
out of the original **by index** and compared back against `HEAD` line for line,
and `orion-tests` output is **byte-identical** before and after — `diff` clean,
**800 checks, 0 failures**, same names, same order, because the running order
lives in `main.cpp` and a translation unit has none.

| # | Mutation | Check it reddened |
|---|---|---|
| M1 | `develop_display.slang` quantised to eight bits | *the output resolves far finer than eight bits could* — 2 distinct values |
| M2 | red and blue swapped in `cubeCorner` | *an identity LUT leaves every pixel where it was* — worst 28/255 |
| M3 | premultiplied guard dropped in `hl_push.slang` | *the shader and its host twin agree* — worst 6.55 |
| M4 | `hl_apply.slang` §3.3 made a replacement | *it moves only the clipped channel* |
| M5 | Burt downsample shifted one texel | 9 red, led by *alpha = 1 collapses back to the input* |
| M6 | dark-channel stage skipped in `airlightFrom` | *the atmospheric light is the haze, not the brightest pixel* — A.r 3.0 |

⚠ **One finding, written down and left alone.** *"a constant rim fills with that
constant"* claims in its comment that *"any weighting error … shows here"*, and
it does not: M3 scales colour and weight together, so `v/w` is unchanged and that
check stays green while three of its neighbours go red. It is not unfailable — it
is narrower than its comment says. Also left: a doc comment for
`testExposureFusionMath` has been dangling at the end of this file since the
2026-07-31 split, describing a test that lives in `tests_fusion.cpp`.

Gates: 800 / 3,708 / 40 scenarios / bench exit 0.

### ✅ 2026-08-02 — three holes in the interface's coverage, and they were one hole (#125)

Two splits found, by mutation, that shipped UI could be deleted with every check
green, and left them: a fix inside a refactor is unreviewable. All three are now
closed, and **they had one cause** — `Screenshot.swift` builds `Editor` directly
and drives only what is on screen at rest. So the panel was covered as far as it
was tall, the menu was not covered at all because a `Commands` is not in
`Editor`, and the status line's warning branch was never in a state that draws
it.

| Hole | The check now | Shape |
|---|---|---|
| **The Detail panel scrolls, and coverage stopped at the fold** (#122 M9) | `--scene detail-tail` | scrolls the real `NSScrollView` to its end and **refuses to write a frame when nothing overflows** |
| **The Photo menu is unreachable from every check** (#121 M6) | `--scene menu` | clears the harness's hook, calls `OrionApp.main()`, reads `NSApp.mainMenu` — 26 commands by title, exit 1 naming any that is missing |
| **The footer's `lastFailure` branch has no oracle** (#121 M8) | `--scene render-failed` | plants the failure **and suspends the engine**, so the warning is in a frame |

**The mutations, each the exact deletion the two splits described.** Every check
in the repository was green on all three before this session.

| # | Mutation | Before | After |
|---|---|---|---|
| **M1** | delete Grain, Vignette, Dehaze, Clarity, Sharpening from `DevelopPanels+Detail.swift` — 60 lines | `detail-tail` exit 0 | ⚠ **exit 1**, *"nothing overflows the panel column"*. `--scene detail` byte-identical, 40 scenarios green, `menu` green — the old coverage is still blind, which is the point |
| **M2** | delete the `Reset Adjustments` command from `PhotoCommands` | `menu` exit 0, 26/26 | ⚠ **exit 1**, *"MISSING from the menu bar — "Reset Adjustments""*, 25 of 26, bar 75 → 74 items. All three frames byte-identical, 40 scenarios green |
| **M3** | delete the footer's `else if let why = engine.lastFailure` branch — 5 lines | `render-failed.png` | ⚠ **frame differs** (`cmp`, char 11,672,187). Looked at: the amber line is gone, the ordinary hint is back and only the 9-point `failed` readout remains — the reported bug exactly. `detail`, `detail-tail` and `menu` green, 40 scenarios green **including `nofailure`**, which pins the value and not the line |

⚠ **The scroll landed past the section it was written for, and looking at the
PNG is what caught it.** The Detail panel's content is **1,701 points** and the
default window gives its scroll view **681**, so at 1680×1050 the scene at rest
accounts for 0–681 and a frame scrolled to the *end* accounts for 1,020–1,701 —
and **Grain sits in the 339-point band between them**, seen by neither. The
scene asks for a 1,500-tall window (a resizable window on a taller display),
which holds 1,131, leaves the end 570 points down, and makes the two frames
overlap by 111 points with nothing between them. Confirmed by reading the
capture, not by trusting the offset.

⚠ **The planted failure was wiped by the layout, and the first capture
photographed the bug while claiming to photograph the fix.** `render()` clears
`lastFailure` on success and *laying the interface out renders*: the canvas's
`onAppear` assigns `engine.cropPreview`, whose `didSet` is `pushAndRender()`.
The frame came back showing the ordinary hint and `0.0 ms`. Suspending the
engine is what a failed one looks like from the panel's side — no successful
frame arrives to take the warning down.

⚠ **A defect nobody was looking for, found on the menu check's first run.** The
menu bar ships **`Compare Original  ()`**: `OrionApp+Commands.swift` writes
`"Compare Original  (\\)"`, but a `Button`'s string is a `LocalizedStringKey`
and a backslash is that grammar's escape character, so **the one item whose key
is spelled only in its title has lost the key**. Nothing in the repository could
see it until something read the real menu bar. Pinned as it ships and left
alone: the fix is a line in a file this story does not own, and a check that is
red the day it lands is not a check.

⚠ **Reaching `PhotoCommands` honestly cost a re-launch, and the alternative was
worse.** `CullActions` is reachable from a test and driving it would have been
easy — and **green on M2**, which deletes the *button* and leaves the action it
called sitting there. What was unchecked was whether the command is in a menu at
all, so the check reads the menu. The price is that a window really does open
for about a second, and that the check asserts presence rather than firing:
the items are disabled at launch and firing one needs a photograph, a key window
and focus. That is #110.3's shape — reach for the real thing, and say plainly
which link is still unpinned.

**Gates:** 800 / 3708 / 40 of 40 / bench exit 0, before and after. The three new
checks are **byte-stable across two runs of one binary** (self-checked before
being trusted — `versions.png` is not, and that is why).

### Where the counts stand, and the one gate that flakes

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** re-measured 2026-08-02 at the prune (#132), all four gates run
end to end on a clean worktree. `orion-tests` **806 checks** ·
`orion-viewport-tests` **3708 checks** · **40 `repro/` scenarios** · all 0
failures. Bench exits 0: **173 nodes, 7186 MiB**, M0 gate PASS at **9.02 ms
p95** — plus a preview graph at 1/16 that. ⚠ The previous copy of this block
said **800** and **3702**, which were the counts before #125 and #129 added
checks; the suites only ever grow, so a stale number here reads as a
regression. The bench prints **54 named checks** on `_PIC8220`
and `_PIC8095` and 55 on `_PIC8148`, the extra one being that frame's waiver
banner. `Orion --library-open <folder>` is a fourth gate: it
opens a folder cold, warm and indexless in one process and fails when the warm
pass did not hit, or when any of the three disagree about a field.

⚠ **This block had grown to *four* copies of itself** carrying 722/626/586/641
engine checks, 3620/3561 viewport, 39/35/36 scenarios and 149 nodes — none of
them current, all of them left behind by merges that added without deleting, and
the file had already flagged the same thing happening twice before. **One copy
now, measured 2026-08-01 against a run of all four gates**, and the node count is
173 rather than 149 because the highlight fill landed (#105/#106).

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
| ⚠ **The check floor says a file measured something, never that it measured the right thing.** #124 takes M4 (one verb family claiming every verb) from 39/40 exiting 0 to 3/40. Two survivors are the declared instruments. The third, `snapshot-keeps-its-matte.txt`, keeps 3 real checks under the mutation because `snapshot missing` is implemented by the very family that claims everything — a floor cannot see that. The oracles that *can* are the byte comparison of rendered frames and the full-output diff, and neither is what the gate runs. Raising the floor would not help; the residual is a coverage shape, not a threshold | `repro/` |
| ~~**The session log replay now exits 1.**~~ ✅ **already closed in the tree, found stale at the 2026-08-02 prune.** `InteractionLog.start()` writes `minchecks 0` into the header it emits, with five comment lines above it saying why (`InteractionLog.swift:65-78`) — so a replayed session log asserts nothing and exits 0, which is the one workflow it exists for. The row said the header "owes" that line and that it was left for whoever owns the file; whoever owns it had already written it | `InteractionLog.swift` |
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero. ⚠ It was **not** bounded until 2026-08-01 — on a photograph with no sidecar the sweep could never run at all, and 26 orphans had piled up beside one sample frame. Decision #87 | `MatteStore` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **363 commits carry `Co-Authored-By` / `Claude-Session` trailers.** ⚠ **Recounted at the 2026-08-02 prune — the row said 101, and it is 363**, because every agent in every wave since has added more. `git log --format=%B \| grep -c 'Co-Authored-By: Claude'`. Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history, and the longer it waits the larger the rewrite | whole history |
| **A check names the mutation it exists to catch and does not catch it, for the second time in two days.** `testPerspectiveMaskExtent` check 6 (now in `tests_mask_geom.cpp`) says *"getting the conjugation W⁻¹JW wrong on a 3:2 frame"* is what it is for — and **deleting that conjugation from `mask::unperspective` outright leaves 800 / 3708 / 40 / bench 0 all green** (#129, mutation M6). The fault is the fixture: the check drives a **pure aspect squeeze**, whose Jacobian is diagonal, and a diagonal `J` has `b = c = 0` — so the conjugation multiplies two zeros. It needs a keystone, which has an off-diagonal derivative. **Recorded rather than fixed**, per the brief. Its neighbour in the same session, #127's *"a constant rim fills with that constant"*, is the same shape | `MaskGeometry.h` |
| **The 1000-line rule is not broken anywhere.** ⚠ **Recounted by sweep 2026-08-02 at `191b451`, on a clean worktree with nothing else running** — `git ls-files` over all **239** tracked `.swift/.cpp/.h/.hpp/.mm/.c/.m/.slang` files, counted with `grep -c ''`, not a directory list, so nothing is out of scope by being somewhere nobody thought to look. **Over 1,000: none.** Largest anywhere: `ShaderParams.h` **905**, `tests_highlights.cpp` **897**, `tests_tone.cpp` **858**, `tests_perspective.cpp` **837**, `tests_brush.cpp` **824**, `ViewportTests+Index.swift` **809**, `Engine.swift` **795**, `tests_grain.cpp` **788**. Narrowing the sweep to `.cpp/.swift/.h/.mm` gives **177** files and the same head of the list. ⚠ **The previous copy of this row went stale within hours of being written, which is exactly what it warns about**: it recorded `tests_highlights.cpp` at **865** as at `6767716`, and `1a3083d` — *"bound the fill's weight, and say what the constant rim actually reaches"* — took that file to **897** on the same day. Every other number in it re-derived unchanged. ⚠ **`app/Screenshot.swift` was the last one over the line**, at **1,196** — 809 lines on the morning of 2026-08-02, taken over the line the same day by #125's three interface checks, and split five ways by #131 at the seam between a scene that *asserts* and a scene that *poses*. ⚠ A sweep is of **one worktree at one commit** and cannot see whatever is in flight elsewhere — it is a floor on the violation, not a ceiling. Eleven splits are done: `DevelopPipeline.cpp` 2,896→452 (#113), `Engine.swift` 2,331→795 (#117), `bench/main.cpp` 2,289→85 (#118), `tests_effects.cpp` 1,716→555 (#127), `Scenario.swift` 1,615→301 (#120), `OrionApp.swift` 1,557→299 (#121), `DevelopPanels.swift` 1,366→56 (#122), `Screenshot.swift` 1,196→315 (#131), and #129's three: `tests_brush.cpp` 1,142→824, `tests_perspective.cpp` 1,110→837, `tests_grade.cpp` 1,029→653. ⚠ **Recount by sweep before editing this row; never adjust the numbers in place** — it has carried up to four contradictory copies of itself at once, and three were collapsed into one on 2026-08-02 | whole tree |
| ~~⚠ **The whole Photo menu is unreachable from every check.**~~ ✅ **closed 2026-08-02, decision #125.** `--screenshot --scene menu` hands the process back to `OrionApp.main()` and reads `NSApp.mainMenu` — the shipping `Scene` building the shipping `PhotoCommands` — and asserts **26 commands by title**, exiting 1 and printing the whole 75-item bar when one is missing. Deleting Reset Adjustments now prints `MISSING from the menu bar — "Reset Adjustments"` and exits 1, with every frame and all 40 scenarios still green. ⚠ It asserts **presence, not firing**: the items are disabled at launch and firing one needs a photograph, a key window and focus (#110.3's shape). ⚠ It is not driven through `CullActions`, deliberately — that would be green on the mutation, which deletes the button and leaves the action | `Screenshot.swift` |
| ⚠ **The Compare Original menu item ships without its key.** The bar reads **`Compare Original  ()`**: the source writes `"Compare Original  (\\)"`, and a `Button`'s string is a `LocalizedStringKey` whose escape character is the backslash, so the one item that spells its key only in its title has lost it. Found by the menu check's first run, 2026-08-02; **pinned as it ships** so the check is green on the tree as it stands, and it will go red — printing the whole bar — the moment somebody fixes it, which is the right way round. The fix is `Text(verbatim:)` or a different spelling | `OrionApp+Commands.swift` |
| ⚠ **Three of the four command-line modes are checked by nothing in the repository.** `--screenshot`, `--batch-export` and `--library-open` each have their four-line dispatch in `OrionApp.init`, and deleting any one of them is green on all four gates (#121, M1/M3/M4). Only `--scenario` is exercised, because only it is what the 40-file sweep runs. ⚠ Structural, not a coverage oversight: `apps/tests` and `apps/bench` are pure C++ and name no Swift, and `orion-viewport-tests` compiles a Swift list with **zero** `OrionApp*` files | `OrionApp.swift` |
| ~~**`Engine.lastFailure` is pinned, the line that displays it is not.**~~ ✅ **closed 2026-08-02, decision #125.** `--scene render-failed` plants the failure **and suspends the engine** — laying the interface out renders, and a successful render clears the value, which wiped the first attempt and photographed the ordinary hint — so the amber "Render failed — …" line is in a byte-compared frame. Deleting the branch changes the frame; `nofailure` stays green on the same mutation, which is exactly the distinction: it pins the state, this pins the line | `Screenshot.swift` |
| ⚠ **The three interface checks are run by hand, like every other frame here.** `detail-tail`, `render-failed` and `menu` are `--screenshot` scenes, and **no gate in the repository runs `--screenshot` at all** (#121: the only mode the 40-file sweep drives is `--scenario`). Two of the three exit nonzero by themselves, so they need no reference image — but somebody has to invoke them. Same standing as the 42-frame comparison, which is also nobody's gate | `Screenshot.swift` |
| **One screenshot scene is not byte-stable, so it cannot be an oracle.** `--scene versions` builds its rows in `Screenshot.snapshots` from `Date()` and the panel prints an absolute clock time, so **two runs of the same binary disagree** — 37/38 scenes byte-identical, `versions.png` differing by 2,380 bytes purely in the timestamp glyphs. Found by self-checking the oracle before trusting it (#121); every other scene is stable across runs and across the split. ⚠ Left alone rather than fixed, because a behaviour change hidden inside a refactor is unreviewable. The fix is a fixed `Date` in the harness, not in the product | `Screenshot.swift` |
| **Nothing asserts that a gesture *arms*** — narrowed 2026-08-01, decision #110.3, and it is now the *first* link only. `repro/gesture-preview-agrees.txt` used to compare an armed run against an unarmed one and demand they agree, which is green when arming does nothing; it now also asserts arming has an effect (the preview surface goes 0.2323/0.2918 → 0.4814/0.2037 over the same eight ticks), so a no-op `beginInteraction` fails. What is still unreachable is a `DragGesture` closure calling it: **attempted** — `NSHostingView` off-screen lays the wheel out and hit-tests it, but `NSEvent.mouseEvent` through `NSApplication.sendEvent` never reaches the recognizer, and CGEvent-backed events need a real on-screen window and the real cursor. Deleting `ColorWheel`'s call is green across 744 / 3624 / 39, measured | `Scenario.swift` |
| ~~**The grading wheel's arming is unmeasured.**~~ ✅ **closed 2026-08-01, decision #110.2.** `wheel` and `dragwheel` drive a three-component control, added beside the scalar spellings rather than replacing them (#89). **9.6 ms per tick unarmed against 1.2 armed, 8.0×**, settled picture identical at luma 0.2268 / sat 0.5136 | `Scenario.swift` |
| ~~**The tick is timed whole, not attributed.**~~ ✅ **Attributed 2026-08-01.** One pointer event of paint is now three measured columns in `orion-bench` — `setBrushStroke` ×2, `apply` ×2, preview render. At 49 → 294 dabs: **0.001 / 0.057 / 0.77 ms → 0.001 / 0.057 / 2.82 ms.** Everything that grows is the GPU, and all of it is `mask:0` | `ROADMAP.md` |
| ~~**The index's `SQLITE_BUSY` rule is reasoned, not pinned.**~~ ✅ **Pinned 2026-08-02.** ⚠ The note said reproducing lock contention needed a second *process*. It did not — SQLite's locks are on the **file**, so a second **connection** in the same process contends identically, and that is the only reason this could be tested at all. Two checks now hold it: a busy *write* must not take the index out of service (`available` stays true, and the same instance still serves the row once the lock lifts), and a lock met *at open* must not destroy a database another process is holding — which is the case `init` can actually act on, since `discardable` is read there and nowhere else. ⚠ **The first version of the test could not fail**, and it is written down in the file rather than quietly fixed: it asserted the row survived into a *new* `PhotoIndex`, on the assumption that condemning deletes the file. Condemning only sets `live = false` on that instance. The mutation passed. Rewritten to assert the consequence that exists, the mutation (`guard code != SQLITE_OK`) now reddens **4 checks**, one of them reading **28,672 bytes became 4,096** — a live database, held by another process, truncated | `PhotoIndex` |
| ~~**Index rows for a folder you never open again are never collected.**~~ ✅ **Closed 2026-08-02.** `plan` prunes only the listing it is handed, so it can clean a folder you are *looking at* and never one you have stopped opening. `collectMissingFolders` now runs once per launch, **keyed on the folder rather than the file** — checking every path would stat thousands of files at launch to save a kilobyte, while one stat per distinct `dir` is cheap. ⚠ **An unplugged drive looks exactly like a deleted folder and this deliberately does not care**: nothing lives only here (#79), so the cost of collecting a folder that comes back is one re-scan, against a database that otherwise never stops growing. ⚠ **The test's first version could not fail** and is recorded rather than quietly fixed: it re-created the vanished file fresh and asserted it came back cold, which passes whether the row was collected or not, because a new mtime invalidates the row on its own. It was measuring staleness, not collection. Holding the stamp identical — same bytes, same nanosecond — is what makes a surviving row a *hit* and a collected row a *miss*; the mutation then goes red | `PhotoIndex` |
| ~~**`Engine.state` uses the memberwise initializer**, and adding a field to `DevelopState` and forgetting this call compiles silently.~~ ✅ **closed 2026-08-01, decision #110.1.** No stored property carries an inline default any more, so a field omitted from that call is `error: missing argument for parameter 'gradeBalance' in call` at both `Engine.swift:1669` and `DevelopState.init()`. ⚠ A field added *with* a default still compiles — that is what `testDevelopStateRoster` is for, and its second half found that `busyState()` had never moved eight of the fields it claimed to round-trip | `Engine.swift` |

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

The **six most recent sessions are above** — `#131`, `#130`, `#129`, `#128`,
`#127` and `#125`, all of them 2026-08-02. **Everything older lives in
[`HISTORY.md`](HISTORY.md)**, which is the archive and is deliberately *not* part
of the read order in `CLAUDE.md`.

⚠ **Pruned 2026-08-02, decision #132.** Twelve agents merged that day and every
one of them appended an entry, taking this file to **2,221 lines across 28
session and wave blocks** — the same failure the 4,643-line prune was for.
**Twenty-two blocks moved to `HISTORY.md`**: sixteen from the body of this file
(#124, #122, #120, #121, #123, #117, #118, #113, the silent-failure inventory,
the concurrent-sidecar note, two stale *In flight* tables, and the second, third,
fourth and fifth wave write-ups) and the six `## Session` entries that were the
tail of this log — `2026-08-02a`, `2026-08-01r`, `q`, `p`, `o` and `n`. Nothing
was edited on the way across, and **745 lines are left here**.

⚠ **A prune moves; it never copies.** The 2026-08-01 prune left `2026-07-31j` in
both files byte for byte, and a duplicated entry is invisible from either end
because each copy looks complete. So all **63** headings that moved — the
twenty-two blocks plus every sub-heading inside them — were checked afterwards by
exact-match grep: **present in `HISTORY.md`, absent here**, 63 of 63. That check
is cheap and is worth repeating on the next prune.

⚠ **Four claims in the gap table and the counts above were stale, and were
re-derived from the tree rather than trusted** — the ceiling row's largest test
file, a gap that the tree had already closed, the trailer count, and the suite
totals. All four are corrected in place with the measurement beside them. The
lesson is that a number written into this file goes stale in *hours* on a day
like 2026-08-02, so a prune re-derives; it does not proof-read.

⚠ **`HISTORY.md` itself carries duplicates, and they predate this prune.**
`2026-08-01b` appears three times; `2026-08-01a`, `2026-07-31l`, `2026-07-31k`
and `2026-07-31i` twice each. Left alone deliberately, because the brief for this
prune was append-only — but it is the next thing to fix in the archive, and it is
the same defect the paragraph above is about.

⚠ **The header of this file is still duplicated, and that was left alone on
purpose.** Two `Last updated` lines, two `Phase` paragraphs, and **three
overlapping copies of the "next story" queue** numbered 1-6, 1-5 and 1-6 with
different contents. Deciding which queue is the real one is a judgement about
what the next story is, which is not a prune's business; it is named here so the
next session does it deliberately rather than by accident.

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

*Sessions `2026-08-01m` and earlier were already in `HISTORY.md` before this
prune.*

# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-08-02 (**the two biggest files in the tree are now eight and five, and neither moved a pixel — #113 and #117**)
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
   **Four files are left over the ceiling** — `apps/bench/main.cpp` 2,289,
   `Scenario.swift` 1,596, `OrionApp.swift` 1,557, `DevelopPanels.swift` 1,366 —
   and they are the harder half, because two of them are what does the checking.

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
more** either (#118, same day, **2,289 → 85**). Every remaining violation is
in `app/`, and the gap row below has been **recounted** rather than edited.

### ⚠ The first wave, and the wave that died — both in `HISTORY.md`

Five agents landed on 2026-08-01 and a second wave of four was killed by an
API session limit with nothing committed. Both accounts, and the one lesson
worth keeping — **an agent on a multi-hour task commits a skeleton early and
refines it, so a kill costs the last increment rather than the session** — are
in `HISTORY.md` under *Agent waves, 2026-08-01*. They are history now: the
first wave is merged and the second was relaunched and is the table below.

### ✅ 2026-08-02 — `OrionApp.swift` is six files, and the four CLI modes are unchanged (#121)

**1,557 lines against a ceiling of 1,000**, the last of the three the sixth wave
was chartered against. Six files, largest **373**: `OrionApp.swift` 296 (the
`@main` entry, the four command-line modes, and `Editor`'s stored properties),
`OrionApp+Commands.swift` 323, `OrionApp+Chrome.swift` 373,
`OrionApp+Canvas.swift` 244, `OrionApp+Tools.swift` 163, `OrionApp+Files.swift`
262. The seam is #113's and #117's — **region of the problem** — and the brief's
proposed cut (entry-and-CLI / menus / root-view-and-chrome) was checked against
the file and **rejected**: it leaves the third file at about 1,350 and still over
the ceiling. Full reasoning in decision #121.

⚠ **Three of the four named gates cannot see this work at all, and that is
structural rather than an accident of coverage.** `apps/tests/CMakeLists.txt` and
`apps/bench/CMakeLists.txt` do not mention Swift; `orion-viewport-tests` compiles
a Swift source list containing **zero** `OrionApp*` files. Only the 40-scenario
sweep runs the app binary, and it only ever passes `--scenario`. So
`--screenshot`, `--batch-export` and `--library-open` are checked by nothing in
the repository, which the mutations below confirm one at a time.

⚠ **The oracle was checked against itself first, and it is noisy on one scene.**
Two full runs of the *pre-split* binary over 38 scenes agree 37/38;
`versions.png` differs between two runs of the same binary, because
`Screenshot.snapshots` builds its rows from `Date()` and the panel prints an
absolute clock time. Pre-split against post-split is **the same 37/38
byte-identical**, and the 38th was compared visually: the only difference is
`6:24 AM` → `6:34 AM`, the ten minutes between the runs. Recorded rather than
fixed — it is a defect in the harness, not the product, and a fix hidden inside
a refactor is unreviewable.

⚠ **All four command-line modes were exercised before and after and diffed.**
`--scenario` over all 40 repro files (**954 lines of report, 40/40 exit 0**),
`--batch-export` of three photographs, `--library-open` of `samples/`,
`--screenshot` over 38 scenes. After normalising wall-clock and the probe
database's UUID, **every one is identical**, and the three exported JPEGs match
by SHA-256 without any normalisation at all.

⚠ **Verbatim motion, checked rather than claimed.** Every body line is a
line-range slice; a multiset diff of all 1,426 non-blank lines accounts for every
difference: five `// MARK:` comments dropped because the filenames now carry
them, seventeen declarations re-emitted with `private` removed, and the file
headers. **No moved line was edited.** The cost is #117's exactly — Swift's
`private` is file-scoped, so five stored properties, eleven views and methods and
`PhotoCommands` widened to internal; nine members stayed `private`.

⚠ **Eight mutations, and the three that stayed green everywhere are the finding.**

| # | Mutation | What went red |
|---|---|---|
| M5 | a `@State` moved into `extension Editor` | **build** — `error: extensions must not contain stored properties`. The seam's constraint is a compile error, not a preference (#117's M1) |
| M1 | `--library-open` dispatch deleted from `init()` | the library probe only. **All four repo gates green** |
| M2 | `--scenario` dispatch deleted | the **scenario gate**, 3/3. The only CLI mode the repository itself checks |
| M3 | `--batch-export` dispatch deleted | the batch probe only. **All four repo gates green** |
| M4 | `--screenshot` dispatch deleted | the screenshot probe only, 3/3. **All four repo gates green** |
| M6 | the **Reset Adjustments menu command deleted** | ⚠ **nothing.** Three scenarios, three byte-compared frames, both other modes, all four gates |
| M7 | `--screenshot` moved ahead of `--scenario` in `init()` | ⚠ **nothing** |
| M8 | the footer's `engine.lastFailure` branch deleted | ⚠ **nothing**, including `nofailure` |

⚠ **M6: a menu item can be deleted from the product and nothing anywhere
notices.** `Screenshot.swift:211` builds `Editor` directly and never builds
`OrionApp`'s `Scene`, so `PhotoCommands` is in no captured hierarchy at all, and
no scenario verb opens a menu. The whole Photo menu is unreachable from every
check in the repository.

⚠ **M7: the `init()` ordering comment is defensive, not load-bearing.** It says
`--scenario` precedes `--screenshot` "because a scenario writes its own stills",
but `Screenshot.options` returns nil unless `--screenshot` is in `argv` and no
invocation passes both flags. The eight repro files that write stills use the
`shot` *verb*, which calls `Screenshot.writeCanvas` directly and never the CLI
mode. **The order is kept anyway** — it costs nothing and the next reader should
find the comment beside what it describes.

⚠ **M8: `nofailure` pins the engine's value, not the line that shows it.** The
verb added in `9d9158d` asserts `Engine.lastFailure` clears on a successful
render; deleting the footer branch that *displays* it leaves that green, and no
scene renders a failed frame so the byte comparison is blind too. #117 recorded
that `lastFailure` had no oracle anywhere; half of it now does, and the half
that reaches the photographer still does not.

⚠ **All three are written down and none is fixed**, because a behaviour change
hidden inside a refactor is unreviewable.

⚠ **The mutation harness itself was wrong twice and both were caught by
self-testing it before trusting it.** `timeout` is GNU and absent on macOS, so
the first run exited 127 everywhere and **every check went red under every
mutation**; the replacement then clobbered the caller's loop variable, so three
screenshot checks reported a scene called `180`. A check that cannot pass proves
as little as one that cannot fail. The harness now smoke-tests both verdicts
before it runs, and was confirmed all-green on the clean tree first.

### ✅ 2026-08-02 — the last two bench checks that could not fail (#123)

#118's split of `apps/bench` ran twelve mutations and found **three checks that
pass no matter what the code does**. One was fixed in #119. **These are the
other two**, and both are now demonstrated failing.

| check | the blind spot | the fix | mutation, before → after |
|---|---|---|---|
| `screen vs export path` | compared two frame **means**, and an ordered dither is **zero-mean** | mean **per-pixel** deviation between the two buffers | M9a (dither × 40): `ok` exit 0 → `DITHER MAGNITUDE` exit 1 |
| `converged` | the look landed the median inside `kSettled` before `refine` ran once, on `_PIC8220` | start the solve **two stops off the anchor** | M11 (`refine` returns its input): `yes` exit 0 → `NO` exit 1 |

**A zero-mean error needs a spread to see it.** M9a made the dither eighty times
too wide and the old line printed `ok` at -0.00313 luma against its 0.00392
bound — the gap only there at all because `saturate` clipped the ends off. The
new gate is `meanAbsDiff` between the screen and export buffers, against the
**same one-eight-bit-step bound stated per pixel** rather than per frame. Not a
new claim: the narrow path is `round(v + d)` with `|d| <= 0.5/255`, a resample,
then a second round, so the honest deviation is a fraction of a code and scales
**linearly** with the dither's amplitude.

| frame | honest | M9a | bound |
|---|---|---|---|
| `_PIC8220` | **0.00116** | 0.01398 | 0.00392 |
| `_PIC8148` | **0.00115** | — | 0.00392 |
| `_PIC8095` | **0.00119** | 0.01424 | 0.00392 |

3.3x of headroom, and near-identical on three very different photographs
because it measures the quantiser rather than the picture. An ordered dither is
a fixed table, so the number is the same on every run — nothing here is a wall
clock and nothing is sampled noise. The worst pixel (0.00604 honest, 0.08075
under M9a) is **printed beside it and deliberately not gated**: a max over
24 MP is a good thing to read and a poor thing to assert.

**A check that depends on which photograph you passed is not a check.** M11
gutted `auto_enhance::refine` and `converged` stayed green on `_PIC8220` — the
frame every brief names — because `look()` is set from the *first* measurement
and on that frame its fusion and clarity land the median at 0.4570 against a
0.4610 anchor, inside `kSettled`. The loop broke before the solver was consulted
once, and the reported set was `+0.00 EV, +0.00 blacks, +0.00 whites`. It was
red on `_PIC8095`, so the suite caught it by luck of the file name.

The fix is not "use the other frame". Measure at rest, then **displace the start
two stops away from the anchor** — `-2 EV` if the median is below mid-gray,
`+2 EV` if above. Two stops is far outside `kSettled` under any transfer
function and the response is monotonic, so the first measurement is off-anchor
on *every* frame and the loop cannot exit without `refine` having moved
something. It is also the stronger claim, because a fixed-point iteration must
not care where it started:

| frame | start | passes | lands | set |
|---|---|---|---|---|
| `_PIC8220` | -2.00 EV | 8 | 0.4570 | `+0.01 EV` |
| `_PIC8148` | -2.00 EV | 7 | 0.4570 | `+0.24 EV` |
| `_PIC8095` | **+2.00 EV** | 11 | 0.4648 | `-1.22 EV` |

`refines >= 1` is asserted alongside and prints `SOLVER NEVER RAN`, so deleting
the displacement later fails loudly rather than quietly restoring the blind
spot. M11 now exits 1 on **all three** frames, not one of them.

⚠ **The M0 wall-clock gate is still the noisy one.** On a machine with three
other agents building, `_PIC8220` measured p95 20.64, 18.32, 16.36 and 9.78 ms
from the same binary within the hour — the report prints the spread itself and
calls it machine noise. Every check added here is a pixel statistic or a count.
Attribution was taken from the named line and the run's exit code together,
never from a `grep` pipeline's status.

### ⚠ In flight — sixth wave, four agents, isolated worktrees, 2026-08-02

The 1000-line rule finished in one pass, plus the checks that cannot fail.

| Working on | Decision | Scope, and the trap named to it |
|---|---|---|
| Split `app/Scenario.swift`, 1,596 | #120 | ⚠ **Not ordinary code — an interface.** #89: renaming its verbs collapsed four alias pairs and **fourteen `repro/*.txt` failed at once**, and the app's session log is itself a runnable scenario. Add nothing, rename nothing. Told to diff the **full output** of all 40 scenarios, not the exit codes |
| Split `app/OrionApp.swift`, 1,557 | #121 | ⚠ Holds four CLI modes that **nothing else exercises** — `orion-tests` and `orion-viewport-tests` never invoke them. Told to run all four and diff their output. Also: `init()`'s ordering is deliberate, a scenario writes its own stills so it precedes `--screenshot` |
| Split `app/DevelopPanels.swift`, 1,366 | #122 | ⚠ Product UI, so a byte comparison of the canvas is **blind** to the failure mode — a panel that stops appearing, a slider that stops being bound. Told to use `--screenshot`, which renders the whole interface, and to open each tab it touched. And not to add an eighth tool tab (#99: the bar came back reading `PRESE… VERSI…`) |
| ✅ **Merged 2026-08-02** — the two remaining checks that cannot fail | #123 | The dither-magnitude check compares **means**, and an ordered dither is zero-mean, so it sees a *biased* dither and not a **40×** one. And `auto_enhance::refine` can be gutted with `converged` green on `_PIC8220` — the frame every brief names — while going red on `_PIC8095`. ⚠ **A check that depends on which photograph you passed is not a check** |

⚠ **Every brief now carries three warnings earned today**: do not pipe a run into
`tail` and read `$?` (I did that twice, and a caught mutation looked like a
pass); the M0 gate is noisy and gave a **false positive within an hour** of being
fixed; and self-check your oracle before trusting it — #117 rendered the same
scene twice from the *pre-split* build to prove a difference would be real.

### ✅ 2026-08-02 — `Engine.swift` is eight files, and no pixel moved (#117)

**2,331 lines against a ceiling of 1,000** — the largest violation left once
#113 took `DevelopPipeline.cpp` out, and its sibling. Same seam, same protocol,
one addition Swift forces.

**The seam is region of the problem**, so both halves of a change sit together:
`Engine+Geometry` (136) · `Engine+Spots` (146) · `Engine+Mask` (460) ·
`Engine+Brush` (249) · `Engine+Document` (244) · `Engine+Render` (297) ·
`Engine+Compare` (145), and `Engine.swift` falls to **795**.

⚠ **`Engine` is `@Observable`, so every stored property had to stay in the class
body** — mutation M1 confirms it is the compiler's rule, not folklore
(`error: extensions must not contain stored properties`). What is left in
`Engine.swift` is therefore *the values and the three lists that enumerate
them*: the properties, `state`, `assign` and `cAdjustments`. Deliberate, not
residue — those three are exactly the edits a new adjustment needs, so adding a
slider is now one file. **This is the shape any `@Observable` split will take.**

⚠ **It cost encapsulation and that is not glossed.** Swift's `private` is
file-scoped, so fifteen members widened to internal and **eighteen
`private(set)` properties lost their compiler-enforced "only `Engine` writes
this"**. There is no way to split a Swift type across files without paying it.

**Verbatim motion, asserted rather than trusted** — extracted by line range by a
script that proves every one of the 2,331 lines is claimed by exactly one
destination. **27 canvas renders byte-for-byte identical** by `cmp` against a
baseline built from `ab6f9b2`, the commit before any of this; the oracle was
first checked against itself (two runs of the pre-split binary, 27/27).
800 / 3702 / 40 green, bench exits 0. #110.1's trap still bites in **both**
halves, checked by adding a field and watching `DevelopState.init()` and then
`Engine.state` refuse independently.

⚠ **Two of seven mutations were green on everything, and both were defects in
the checks.** M3 (delete `pushStrokes()` from `assign`) was invisible because no
scenario painted and then reassigned state; M5 (`constrainCrop` a no-op) was
invisible because the geometry scenario cropped *after* straightening, and
`setCrop` runs the same clamp itself. Both scenarios rewritten, the baseline
re-taken from the old code, both now red. **The 40 repro scenarios stayed green
through both** — these were gaps in the repository, not just the session.

⚠ **Two things nothing can see.** The byte check is blind to **compare** —
`shot` reads the output texture and the split composite lives in `CanvasBlit` —
so M2 is green on all 27 frames and red only on the two compare repro files.
And **`lastFailure` has no oracle at all**: M7 deletes both success-path clears,
so the footer would show a stale failure forever, and 27 frames / 40 scenarios /
800 / 3702 / bench all stay green. It landed the same day. Written down, not
fixed — a behaviour change hidden in a refactor is unreviewable.

### ✅ 2026-08-02 — `apps/bench/main.cpp` is nine files, and no check was lost (#118)

**2,289 lines against a ceiling of 1,000**, ~700 of them added in one day as
feature after feature bolted a probe onto the end. Sibling of #113 and the same
protocol: verbatim motion, proved by mutation rather than by inspection.

**The seam is the report the bench prints**, because the test `CLAUDE.md` sets is
that the next edit is a small repeatable one — and for the bench that means
*adding a probe for a new node is one obvious file*.

| file | lines | what it holds |
|---|---|---|
| `main.cpp` | 2,289 → **85** | decode, device, pipeline, thirteen calls, the exit code |
| `bench_controls.cpp` | **669** | the probe table. ⚠ **A new control is one row here and nothing else** |
| `bench_highlights.cpp` | **424** | the fill's four invariants *and* the clip-set census (was `3c` + `3e`) |
| `bench_brush.cpp` | **386** | the accumulation invariant *and* the stroke profiles (was `3d`) |
| `bench_invariants.cpp` | **275** | the guide chain, lens, dehaze, Balance (was `3b`), screen vs export |
| `bench_compose.cpp` | **188** | the M3 features together, auto-enhance |
| `bench_gate.cpp` | **179** | the M0 gate, the wide tail, the curve drag |
| `bench_profile.cpp` | **152** | clarity and dehaze node by node, export timing |
| `bench_metrics.cpp` | **147** | the instruments |
| `bench.h` | **126** | the `Bench` context and the section list |

⚠ **The numbered blocks are gone and that was half the point.** `3b`, `3c`, `3d`
and `3e` were numbers assigned so concurrent agents would not collide inside one
file. They were never names. Files are.

⚠ **`Bench::cleanNodes` is the only value that crosses a section boundary**, and
it is deliberately not re-derived: the exposure gate measures how many nodes a
tick recomputes, and `exposure drag, lens on`, `exposure drag, fill off` and
`exposure drag, fill on` are all stated against it. A second derivation would
turn three checks into comparisons of a number with itself.

#### The named check set, before and after — **identical on all three frames**

54 / 55 / 54 checks on `_PIC8095` / `_PIC8148` / `_PIC8220`: same names, same
verdicts, same node counts, and the same rendered means on the guide and
screen-vs-export lines. Diffing the whole report with wall-clock blanked leaves
only the decode MP/s, one column width where a millisecond crossed 10, the
output prefix in the PNG paths, and tie-breaks in the hottest-N rankings — the
same classes #113 reported. A multiset diff of every non-blank line accounts for
all **seven** dropped lines: two `namespace` markers, two block braces, two
includes that moved, and the return statement.

#### The mutations — twelve, and **three of them found checks that cannot fail**

| # | mutation | what went red | exit |
|---|---|---|---|
| M1 | `perspectiveVertical` dropped from the geometry change list | `perspective 0.6` **1 node ok → 0 nodes NO EFFECT** | 1 |
| M2 | `grading` counts a nonzero Balance | `balance with no grade` **0 nodes → 3, grade ran 8 times** | 1 |
| M3 | `filling` hardwired true | `highlights off` **0 → 24 fill nodes on a full render** | 1 |
| M4 | the dehaze shape re-pushed every tick | `dehaze drag` **10 nodes / 0 → 19 / 9 slider-independent** | 1 |
| M5 | `correctingLens` back in the lens re-push condition | `exposure drag, lens on` **3 → 7 nodes** | 1 |
| M6 | the brush accumulator continues from a moved head | both brush rows, **`49 then 0` → `49 then 49`** | 1 |
| M7 | the M0 threshold 16.0 → 1.0 | **M0 gate PASS → FAIL** at 9.06 ms, best of three | 1 |
| M8 | the curve LUT built once and never rebuilt | `curve changed the image` **yes → NO — BUG** | 1 |
| M9b | the dither loses its zero-mean recentering | `screen vs export path` **PATHS DISAGREE**, +0.00738 | 1 |
| M12 | `hl_apply`'s hole predicate becomes a plain level test | census, **0 → 13,135 px the fill must not touch** | 1 |
| M9a | the dither made **40× too large** | ⚠ **nothing.** 0.00010 → 0.00313, inside the 1/255 bound — **fixed in #123** | 0 |
| M10 | `needsGuide` hardwired false | ⚠ **nothing** — see below; **fixed in #119** | 0 |
| M11 | `auto_enhance::refine` returns its input | ⚠ **nothing on `_PIC8220`**; red on `_PIC8095` — **fixed in #123** | 0 / 1 |

⚠ **All three of the bottom rows now go red**, in #119 and #123. The three
paragraphs below are the diagnosis as it was written on the day and are left
standing; the fixes and their re-run verdicts are in those two entries.

⚠ **M10 confirms #113's finding, unchanged and still unfixed.** With
`needsGuide` false the whole six-node guided-filter chain is dead, and both
`… guide off/on` lines still print `ok` — with the delta *improving* to
`+0.00000`, because with the chain off the "on" run **is** the "off" run. The
only trace is that `highlights -1` and `shadows +1` fall from **10 nodes to 3**,
which the bench prints and does not assert. **Not fixed here**, per #113: a
behaviour change hidden inside a refactor is unreviewable. The cheap fix is
visible from the mutation — assert those two probes' node counts, which are
load-immune — and it is the next job in `apps/bench`.

⚠ **M9a is a second one, and it is new.** `screen vs export path` says a wrong
dither magnitude "would show up here". It largely would not: an ordered dither
is zero-mean by construction and the check compares *means*, so 40× the
amplitude moves the gap to 0.00313 against a 0.00392 bound. What it does see is a
**biased** dither (M9b). The comment overstates the check by one of its three
named failure modes.

⚠ **M11 is a third, and it is about which frame the gate command names.** On
`_PIC8220` auto-enhance reports `exposure +0.00 EV, blacks +0.00, whites +0.00`:
the look's fusion and clarity land the median inside `kSettled` before
`ae::refine` is consulted once, so the solver can be gutted and `converged` stays
green. It is red on `_PIC8095` (median 0.6172 → 0.6133, off by 0.1523). The
`orion-bench` line in every brief runs `_PIC8220`.

#### Gates

`orion-tests` **800 checks, 0 failures**. `orion-viewport-tests` **3702, 0**. All
**40** `repro/*.txt` exit 0. `orion-bench` exit 0 on all three sample frames —
**173 nodes, 7186 MiB** unchanged, M0 gate PASS at 9.15 ms, which is advisory.
⚠ **No timing claim is made**: one of the mutation runs measured this unchanged
graph at p95 21.98–23.38 ms while the machine was busy and 8.87 when it was not.
The load-bearing numbers are the named checks and the node counts, and both are
unmoved.

⚠ **Nothing outside `apps/bench/` and the two planning files is touched.** The
engine edits the mutations needed were reverted with `git checkout` after each
run and the tree was confirmed clean before the gates.

### ✅ 2026-08-02 — `DevelopPipeline.cpp` is five files, and no pixel moved (#113)

**2,896 lines and a 917-line header, against a ceiling of 1,000.** The last
entry in the gap table that anyone had costed, held back until it could go
alone because a split landing beside two other agents is a merge nobody can
resolve.

**The seam is the graph's four regions, and each file holds *both* halves of
its region** — the nodes the constructor adds and the parameter blocks `apply`
pushes into them. That pairing is the whole decision. Adding dehaze meant
editing the constructor at line 400 and `apply` at line 1,628; those two edits
are now adjacent, and *which* file is answered by where in the picture the
feature lives.

| file | lines | region |
|---|---|---|
| `DevelopPipeline.cpp` | 2,896 → **452** | the spine: two call lists and little else |
| `DevelopCapture.cpp` | **715** | sensor to profile — demosaic, highlights, fill, denoise, lens, spots, sharpen, matrix |
| `DevelopLocal.cpp` | **686** | dehaze, clarity, fusion, guided filter — the multi-node operators |
| `DevelopMask.cpp` | **757** | the mask group, the brush, the feathering |
| `DevelopOutput.cpp` | **586** | tone, grade, display, grain, geometry |
| `DevelopPipeline.h` | 917 → **691** | + `Adjustments.h` **345**, `DevelopInternal.h` **45** |

⚠ **`ApplyContext` is the only thing that is new.** Ten values one stage derives
and another reads — the perspective, the crop and turns, `frameMoved`,
`needsGuide`. Everything else is verbatim code motion, extracted by line range
rather than retyped, so each moved body is byte-identical to what it replaced.
Each stage function opens with a preamble aliasing what it takes from the
context, which is why no moved line needed touching.

⚠ **Not one pixel moved, and that is checked rather than claimed.** Nine canvas
renders — tone, white balance, the three local operators, grading and the
vignette, spots and grain, four kinds of mask over two layers with feathering,
and the whole geometry stack including the crop preview — are **byte-for-byte
identical** before and after, by `cmp`. 173 nodes and 7186 MiB unchanged; the
whole bench differs only in wall-clock percentages and one tie-break in a
hottest-N list.

⚠ **Neither call list may be sorted.** Node construction order is load-bearing —
indices are held in members, so a reordered `build` compiles and changes which
node feeds which. `apply`'s order is preserved for a weaker reason, written on
the function: the stages push to distinct nodes so it very probably does not
matter, but "very probably" is a claim about 1,300 lines and a refactor is not
where to start making claims.

⚠ **Four stages cannot be driven from a scenario** — `highlightRecovery`,
denoise, the lens correction and capture sharpening have no `set` name in the
grammar, so the byte-check does not cover them and they rest on `orion-tests`'
real GPU renders instead. Stated rather than papered over; adding four names to
`Scenario.swift` would close it and is a small job for whoever is next in that
file.

#### The mutations — nine, and three of them are only visible to one check

Each breaks a seam the split created. ⚠ **The byte-check baseline is built from
`a15c7eb`**, the commit before any of #113 — comparing the split against its own
output would prove nothing.

| # | mutation | byte-check, 10 renders | `orion-tests` | `orion-bench` |
|---|---|---|---|---|
| M1 | `ctx.perspective` never reaches a stage | RED ×2 | RED, 3 | RED exit 1 |
| M2 | `ctx.frameMoved` stuck false | RED ×2 | RED, 2 | green |
| M3 | `ctx.visibilityMoved` stuck false | ⚠ **green → RED ×3 after the fix** | RED, 5 | green |
| M4 | `ctx.needsGuide` stuck false | RED ×8 | **green** | **green** |
| M5 | `ctx.refining` stuck false | RED ×3 | green | RED exit 1 |
| M6 | mask nodes built after output nodes | RED ×3 | RED, 3 | RED exit 1 |
| M7 | the grain plate is never uploaded | RED ×4 | RED, 2 | RED exit 1 |
| M8 | `applyTone` pushed after `applyOutput` | RED ×2 | **green** | **green** |
| M9 | the output chain never retargets | RED ×9 | **green** | **green**, 7186 → **7279 MiB** |

⚠ **M3 was a defect in this session's own check and is the row worth reading.**
The mask render added four rows and hid none, so every component was live —
which is exactly what `apply` does with the enable loop deleted. The one
predicate governing mask enablement could be hardwired false and all nine
renders stayed byte-identical. One `maskhide 1` line fixes it, and M3 now turns
three renders red.

⚠ **M4 exposes an existing check that cannot fail, and it is in the bench.**
`endpointPair` turns the guided filter on by setting `shadows = 1e-6` and
asserts the two runs agree — so with `needsGuide` hardwired false the "on" run
*is* the "off" run, and `blacks -1, guide off/on` passes trivially under the
one mutation it looks built to catch. The whole six-node chain can be switched
off across the product with 800 engine checks and every bench invariant green.
**Not fixed here** — a behaviour change hidden inside a refactor is
unreviewable — but it is the next thing anyone in `apps/bench` should do.

⚠ **M8 changed the answer to a design question.** The split preserved `apply`'s
push order on the cautious argument that "the stages write to distinct nodes so
it probably does not matter" is a claim about 1,300 lines. M8 moved `applyTone`
past `applyOutput` and **the rendered bytes changed** — so the order is
load-bearing, not merely unproven, and the comment on `apply` now says so. Why
those two frames and not the other eight is **not diagnosed**; the guess written
beside it is labelled as a guess.

⚠ **No timing claim is made.** Wall-clock on this machine spans 8.87–31.45 ms
across runs of one unchanged binary, so the comparison is node counts and
pixels: 173 nodes, 7186 MiB, every per-control node count in the bench sweep
unchanged, and ten renders byte-identical. Sixteen non-inlined member calls per
`apply` against a ~10 ms GPU frame is not a measurable cost and is not claimed
to be one either way.

### ✅ 2026-08-02 — the silent-failure inventory, finished by hand

The killed agent (#115) landed four fixes and never reached the sweep. Completed
inline, since it needs a grep and a judgement rather than an agent. **The finding
is mostly a negative one, and that is worth recording**: after this session's
fixes the `app/` write paths are in good shape, and manufacturing more fixes to
look busy would have been the wrong answer.

| Site | Verdict |
|---|---|
| `Engine.restore` on an undecodable blob | ✅ fixed by #115 — the data-loss one |
| `Autosave.flush` / `toSidecar` | ✅ already correct: the job is dropped and the baseline moves **only once the write has landed**, so a sidecar that will not write is retried rather than believed |
| `Snapshots.commit` on restore | **Left, and argued in place**: best effort by design, logged, and the *guard* is what is best effort rather than the restore — refusing to restore over an unwritable version list would punish the photographer for a disk problem |
| `MatteStore.sweep` delete | **Left**: `do`/`catch` that writes the failure to stderr and keeps going. Collecting is housekeeping; a file it cannot remove costs nothing |
| `MatteStore.write`, `Snapshots.save` | **Left**: both `throws`, so nothing is swallowed |
| `orion_engine_set_brush_stroke` ×3 | ✅ already reported — `noteBrushRefusal`, and the live path says a refused stroke reads as a dead hand |
| `Engine.setMaskMatte` | ✅ returns `Bool`; every caller guards it **except one**, below |
| `Screenshot.swift` sky matte | ⚠ **fixed here.** It was `_ =`. That harness renders the stills the landing page publishes under *"the interface as it runs today, not a mockup"*, so a matte that silently failed to upload would put a picture of a feature not working underneath a claim that it does. It now exits 1 rather than shipping a quietly wrong photograph of the product |
| `renderPreview` falling back to `render` | **Left, deliberately**: documented, and the fallback is the right behaviour on a machine with no preview graph |

⚠ **The rule this session settled**: a failure may be silent when the fallback is
genuinely correct and the photographer loses nothing. It may not be silent when
the result *looks* right and is not — that is the class that produced the black
canvas, the deleted mattes and the film grain that shipped at zero.

### ⚠ 2026-08-02 — concurrent agents were writing each other's sidecars

A one-off: `repro/grain-survives-a-reopen.txt` failed once in a suite of forty,
passed alone, and passed forty-for-forty twice more. Not a product bug, and not
left as a mystery — **a flaky gate nobody owns is worse than a red one**, and
this project has just spent a decision (#116) on exactly that.

The mechanism. That scenario does `save samples/_PIC8220.xmp` then `reopen`, and
`tools/worktree-setup.sh` gave every worktree **one symlink to the main repo's
`samples` directory**. So every agent running the scenario suite was writing the
same sidecar, the same matte PNGs and the same `orion-snapshots.json` as every
other agent and as the main checkout. The evidence was in the folder: another
agent's matte files landing there seconds apart while my suite ran.

**Fixed in the script.** `third_party` stays one directory symlink — a build only
reads it. `samples` is now a **private directory of symlinks to the raw files**,
so the 50 MB originals are still shared and never copied, while the sidecars,
mattes and snapshots each run writes belong to that worktree alone. Both branches
retested: refuses in the main repo, and a write inside a worktree no longer
reaches the shared folder.

⚠ **The three agents already running were started under the old script** and
still share the folder, so one more flake of this shape is possible before they
land. It is this, not the product.

### ⚠ In flight — fifth wave, two splits, isolated worktrees, 2026-08-02

The session limit reset, so the 1000-line rule gets its next two files. **Five
were over the ceiling** after #113 took `DevelopPipeline.cpp` from 2,896 to 451:
`Engine.swift` **2,331**, `bench/main.cpp` **2,289**, `Scenario.swift` 1,596,
`OrionApp.swift` 1,557, `DevelopPanels.swift` 1,366. These are the top two.

| Working on | Decision | Scope, and the trap named to it |
|---|---|---|
| Split `app/Engine.swift`, 2,331 lines | #117 | ⚠ **The Swift constraint decides the seam and has no C++ equivalent**: `Engine` is `@Observable`, so **stored properties must stay in the main declaration** — the macro only sees the class body. Behaviour moves to extensions. Told to copy #113's proof exactly: baseline rendered from `ab6f9b2`, byte-compared after, and the blind spots named |
| Split `apps/bench/main.cpp`, 2,289 lines | #118 | ⚠ **Tooling, so breaking a render is impossible and silently weakening a gate is the whole risk** — and the gates are what everything else here is verified against. Told to diff the **named check set** before against after, and to prove at least six can still fail by breaking what they assert |

⚠ **Neither may fix anything while it is in there.** #118 in particular is told
about `endpointPair` — the guided-filter check #113 found that cannot fail — and
told explicitly **not** to fix it, because a refactor with a fix hidden in it is
unreviewable.

### The fourth wave — all three merged, one of them cut short

⚠ **The silent-failure sweep (#115) was killed by a session limit mid-run**, before
it reached its mutation table. It had committed four fixes by then, which is the
whole reason commit-early is the first line of every brief. **Its inventory was
never finished** — the four landed, the sweep of every remaining swallowed error
in `app/` did not, and that is the open half.

⚠ **The worst of the four was a data-loss bug and is worth stating in full.**
`Engine.restore` returned silently on a develop blob that would not decode, and
two callers read *"the sidecar had a blob"* as *"and it is now in the engine"*:
`MatteStore.sweepAfterLoad` got the default empty component list and **deleted
every matte PNG beside the photograph** (not recoverable — the model has to run
again, and Vision's answer moves between OS releases), and `Autosave.begin` armed
with the default state, so **the first slider tick wrote a blank blob over a
sidecar that had only failed to parse**. The mutation was run at merge rather
than inherited: making `restore` report success on an undecodable blob takes
`repro/unreadable-sidecar-keeps-the-work.txt` to exit 2; restored, exit 0.


⚠ **Briefs now say `sh tools/worktree-setup.sh`, never a hand-rolled `ln -s`.**
That is the whole point of the script: it exits on the main repo before it can
touch anything, and it refuses to replace an entry that is not already a symlink.

| Working on | Decision | Scope, and the trap named to it |
|---|---|---|
| ✅ **Merged 2026-08-02** — split `DevelopPipeline.cpp`, 2,896 → 442 lines | #113 | Alone in `engine/` this hour, which is why it waited. ⚠ **Pure refactor; not one pixel may move**, and it is told to prove that by rendering a frame before and after and comparing **byte for byte** rather than asserting it. ⚠ Node construction order is load-bearing: a reorder that compiles can still change which node feeds which |
| **X-Trans — research and decomposition only, no build** | #114 | ⚠ The licensing question *is* the story: Markesteijn's implementations are darktable's and RawTherapee's, both **GPL**. The real question is whether a description exists **outside** GPL source, and **"it does not" is a valuable answer**. Told to check what LibRaw already gives us, since that may reshape the whole item |
| **Every other place the app fails silently** | #115 | Generalises the black-canvas bug fixed this session. ⚠ Told **not** to fix them reflexively — a dialog on every recoverable hiccup is its own bug — and to rank by blast radius, since a swallowed sidecar or export error costs work the photographer cannot get back |

⚠ **The three touch disjoint trees** — `engine/`, `research/`+`planning/`, and
`app/` — so the merges should not collide beyond the usual `DECISIONS.md` and
`STATUS.md` rows.

### ⚠⚠ 2026-08-02 — the build is down, and the instruction that did it was mine

**`third_party/slang` is destroyed and nothing can be compiled.** Every shader
fails with `Too many levels of symbolic links`. Spotlight finds no `slangc` on
the machine and there are no other volumes. `README.md:82` documents the
install: a macOS release from `github.com/shader-slang/slang`, extracted into
`third_party/slang/`. **Awaiting the developer's go-ahead to re-download it** —
that is the only thing standing between here and a green tree.

⚠ **Cause, stated plainly: a line in the agent brief.** Every worktree brief
carried this scaffolding, because `samples` and `third_party` are gitignored and
a worktree cannot build without them:

    ln -s /Users/grootbeat/Documents/Orion/third_party . && ln -s .../samples .

It is correct **inside a worktree** and destructive in the **main repo**, where
the target and the destination are the same path — the entry becomes a symlink
to itself. It was written into five briefs. One agent ran it with the main repo
as its working directory and both entries were replaced by self-links.

⚠ **The fix is not "tell agents to be careful."** The line is out of every
brief and replaced by **`tools/worktree-setup.sh`**, whose entire reason for
existing is the guard rather than the linking: a linked worktree has a `.git`
*file* and the main worktree has a `.git` *directory*, so the script exits on
the main repo before it can touch anything, and it refuses to replace any entry
that is not already a symlink. Both branches are tested — it refuses in the main
repo, links in a worktree, and leaves the main repo's entries alone.

**What was lost, and what was not:**

| | State |
|---|---|
| `third_party/slang` | **Gone.** Redownloadable, Apache-2.0, not source and not the developer's data |
| `samples/` | Directory gone; **all three originals safe** in `~/Pictures/July 25`, `~/Pictures/Rejects`, `~/Pictures/Cars july 25th`. Rebuilt 2026-08-02 as symlinks to those three, so sidecars land in `samples/` and the photo folders stay clean |
| Generated mattes and JPEGs in `samples/` | Gone; regenerable |
| ⚠ Any `.xmp` that lived in `samples/` | Gone, and not recoverable. Test-frame edits only |
| Source, planning, research, `repro/` | **Untouched.** All tracked |
| `origin/main` | **Untouched and green at `8ac1d62`** |

⚠ **`06543ed` (highlight piece 4) is committed locally and deliberately NOT
pushed.** No gate has been run against it. It exists so the tree is not left
mid-merge. Verify before pushing.

⚠ **Two agents were stopped mid-flight** to stop the command repeating: the
brush accumulator (#108, mid re-verification) and the persisted-key migration
(#112, mid mutation table — it never reached a sidecar write). Both are intact
on their `worktree-agent-*` branches and can be resumed.

### The third wave — two merged and pushed, one merged unverified, one stopped

⚠ **Decision numbers are assigned in the brief this time, not chosen by the
agent.** Five agents collided on numbers today — four of them all picked #96 —
and every collision cost a renumber across a dozen files at merge. #108 to #111
are spoken for.

| Working on | Decision | Scope, and the trap named to it |
|---|---|---|
| ⏸ **STOPPED mid-verification by the outage; branch intact** — brush accumulation, session two | #108 | The half behind #102's predicate, and the item that answers *"no brush stroke should take 155 ms"*. ⚠ The budget is part of the decision: ~97 MB a component at 24 Mpx against a graph already at 173 nodes / 7186 MiB, so six components would cost 580 MB and the number decides the design. ⚠ `unchangedPrefix` returning N means the texture is valid *up to* N — an accumulator out of sync with it renders a plausible stroke from stale pixels and nothing looks wrong |
| ⚠ **Merged locally as `06543ed`, UNVERIFIED, deliberately not pushed** — highlight piece 4. Reported at 173 nodes / 7186 MiB, both unchanged, against an estimated +23/+30 | #109 | Re-costed at **+23 nodes and +30 MiB** because it reuses piece 3's pyramid — told to verify that from the inside rather than trust it. ⚠ #106's maximum-principle argument covers the *fill* and may not cover a detail transfer, which can plausibly move a pixel outside the rim's range; if so it owes #29 a new argument |
| ✅ **Merged and pushed** — three Swift-layer gaps | #110.1–.3 | ⚠ The memberwise trap has now silently shipped **two** dead features (film grain, then Balance) and each cost a `repro/` scenario written afterwards. Told to fix the *shape* and prove it — add a field, show the compiler rejects it, quote the error. A fix that merely looks safer is the same trap with better formatting |
| ✅ **Core ML denoise — research only, merged 2026-08-01** (session `2026-08-01p`) | #111 | ⚠ Told explicitly not to build. Raw-domain vs sRGB-domain is the load-bearing question (most published denoisers are trained on JPEG noise, which a Bayer sensor does not produce), and the license of any **weights** matters as much as the paper's. "Not worth building, here is the arithmetic" is a first-class outcome |

⚠ **The denoise brief's premise was wrong and the agent corrected it.** I wrote
that Orion's noise handling is pre-demosaic. The **fit** is — `estimateNoise` takes
a `BayerImage`. The **filter** is not: `denoise:blur 0..3` sits at line 211 of
`DevelopPipeline.cpp`, after `rcd:red/blue` (134) and before `camera->working`
(260), in **linear camera RGB**, because `var = a·x + b` only holds there.
Verified against the source at merge rather than taken on trust. That gives Orion
a **third** noise domain — published denoisers are trained on sRGB or on Bayer,
and neither is where Orion filters. An sRGB checkpoint dropped in at the current
insertion point would look plausible, be wrong for an invisible reason, and no
check in either suite would catch it. Same class as the purple cast.

**Deliberately not deployed this wave, and why:**

- ~~⚠ **Splitting `DevelopPipeline.cpp`**~~ — ✅ **done 2026-08-02, decision
  #113**, alone and after the wave, exactly as this note said it had to go. It
  was 2,896 lines by the time it went.
- **The persisted-key migration** (#89) — **needs the developer's sign-off before
  anyone starts.** It rewrites sidecars on disk, and a renamed key does not fail
  to parse: it yields a perfectly valid mask sitting in the middle of the frame.
- **Windows** — multi-week and needs a machine to test on.

### ✅ The second wave, all four merged — 2026-08-01

| What it was sent for | What came back |
|---|---|
| Grading **Balance** | ✅ #104. Rigid shift of the three zone centres. Bit-identity at centre **measured against a rebuilt pre-change shader**, not asserted. Seven mutations red; one of its own checks could not fail and it rewrote it |
| Perspective's **mask-extent** term | ✅ #107. ⚠ **The brief's premise was wrong** — #100's leak table does not reproduce, and the bug it did find (aspect, 0.1461 luma, a mask staying round over a picture squeezed 2:1) is an order worse than the one it was sent for |
| Incremental brush accumulation, **both sessions** | ✅ #102 then **#108**. The predicate, then the accumulator behind it. A pointer event costs the dabs appended rather than all of them: `mask:0` 5.20 ms adding 49 to 294 against 36.46 ms re-laying them. One R32Float texture for the live component — 98.25 MiB, not the 393 four would cost. Ten mutations, three of which passed and were defects in the checks |
| Highlight fill, **pieces 2–3** | ✅ #105, #106. ⚠ It **measured** the subsampling question before spending the estimate: a quarter costs 0.8 points on a 6.1-point approximation, so 1/4 it is. ROADMAP's memory number was wrong by 16×. Real cost 149 → 173 nodes, 6971 → 7186 MiB, off by default |

⚠ **Two of the four found a check of their own that could not fail**, and said so
unprompted — Balance's zone ordering (normalized weights peak at the image ends
whatever the centres do) and the fill's disable test (upstream of exposure, so a
drag never re-runs it and the probe passed while the chain was wired always-on).
That is the habit worth keeping, and it is worth more than any of the four
features.

⚠ **Commit-early worked.** The first attempt at these four was killed by an API
session limit with nothing committed; relaunched with that as the loudest line in
each brief, all four landed 5, 5, 7 and 5 commits deep.

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** measured 2026-08-02 against a run of all four gates after #118.
`orion-tests` **800 checks** · `orion-viewport-tests` **3702 checks** · **40
`repro/` scenarios** · all 0 failures. Bench exits 0 on all three sample frames:
**173 nodes, 7186 MiB**, M0 gate PASS at **9.15 ms p95** — plus a
preview graph at 1/16 that. The bench prints **54 named checks** on `_PIC8220`
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
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero. ⚠ It was **not** bounded until 2026-08-01 — on a photograph with no sidecar the sweep could never run at all, and 26 orphans had piled up beside one sample frame. Decision #87 | `MatteStore` |
| The **nib's constants are uncited** — dab spacing, hardness clamp | `UNSOURCED.md` §17 |
| **101 commits carry `Co-Authored-By` / `Claude-Session` trailers.** Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history | whole history |
| ⚠ **The whole Photo menu is unreachable from every check.** Deleting the Reset Adjustments command is green on 800, 3702, all 40 scenarios, the bench and every byte-compared frame (#121, M6). `Screenshot.swift:211` builds `Editor` directly and never builds `OrionApp`'s `Scene`, so `PhotoCommands` is in no captured hierarchy; no scenario verb opens a menu. Every menu item, its title, its shortcut and its greyed-out state rest on reading the code | `OrionApp+Commands.swift` |
| ⚠ **Three of the four command-line modes are checked by nothing in the repository.** `--screenshot`, `--batch-export` and `--library-open` each have their four-line dispatch in `OrionApp.init`, and deleting any one of them is green on all four gates (#121, M1/M3/M4). Only `--scenario` is exercised, because only it is what the 40-file sweep runs. ⚠ Structural, not a coverage oversight: `apps/tests` and `apps/bench` are pure C++ and name no Swift, and `orion-viewport-tests` compiles a Swift list with **zero** `OrionApp*` files | `OrionApp.swift` |
| **`Engine.lastFailure` is pinned, the line that displays it is not.** `nofailure` (`9d9158d`) asserts the engine clears the failure on a successful render. Deleting the footer branch that renders "Render failed — …" is green on everything including that verb (#121, M8), and no screenshot scene produces a failed frame, so the byte comparison is blind as well. Half of what #117 reported is closed; the half a photographer actually sees is not | `OrionApp+Chrome.swift` |
| **One screenshot scene is not byte-stable, so it cannot be an oracle.** `--scene versions` builds its rows in `Screenshot.snapshots` from `Date()` and the panel prints an absolute clock time, so **two runs of the same binary disagree** — 37/38 scenes byte-identical, `versions.png` differing by 2,380 bytes purely in the timestamp glyphs. Found by self-checking the oracle before trusting it (#121); every other scene is stable across runs and across the split. ⚠ Left alone rather than fixed, because a behaviour change hidden inside a refactor is unreviewable. The fix is a fixed `Date` in the harness, not in the product | `Screenshot.swift` |
| **The 1000-line rule is broken six ways**, and ⚠ **the largest file in the tree was never in this row**: `apps/tests/tests_effects.cpp` **1,716**, `app/Scenario.swift` **1,615**, `app/DevelopPanels.swift` **1,366**, `apps/tests/tests_brush.cpp` **1,142**, `apps/tests/tests_perspective.cpp` **1,110**, `apps/tests/tests_grade.cpp` **1,029**. ⚠ Swept live across **every** tracked source at the 2026-08-02 merge, not just product code — the row had said *"three files, all in `app/`"* and *"the next file below the survivors is `ShaderParams.h` at 905"*, and both were wrong: four test files sit above it and the biggest of them is bigger than anything the splitting wave was chartered against. Scenario.swift was also listed at 1,596 when it is 1,615. ⚠ **Recount by sweep before editing this row; do not adjust the numbers in place** — it once carried four contradictory copies of itself, and every figure written into it today was stale within hours because agents write it before the others land. Four splits landed 2026-08-02: `DevelopPipeline.cpp` 2,896→451, `Engine.swift` 2,331→795, `bench/main.cpp` 2,289→85, `OrionApp.swift` 1,557→299. Two more are in flight | whole tree |
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

## Session 2026-08-02a — X-Trans, and the licence that turned out not to be the problem

**Story:** M5's "X-Trans support (Markesteijn)", on the roadmap since the first
milestone list and never once investigated. **Research and decomposition only.
Nothing was built, no `throw` was lifted, no build was run and neither suite was
run — there was nothing to test.** That was the brief and it is the outcome. ⚠ The
tree could not have been built in any case: `third_party/slang` is destroyed.

**Shipped:** `research/demosaic-xtrans.md`, a seven-piece table in `ROADMAP.md`,
`UNSOURCED.md` §29, decision **#114**, and corrections to `demosaic.md`,
`research/README.md`, `FEATURES.md`, `RESEARCH.md` and this file.

### ⚠ The licensing question was the story, and the answer is in two halves

**Half one, and it is the honest "no":** there is **no published description of
Markesteijn's algorithm anywhere.** Checked dcraw (code with an attribution and
no derivation), darktable's `xtrans.c` and RawTherapee's `xtransdemosaic.cc`
(**GPL-3**), both manuals (they say which *variant* to pick, not how either
works), Fujifilm's patents (they cover the 6×6 **array**, not the interpolation,
and Markesteijn is not Fujifilm), and `xtransdemosaicking.blogspot.com` (the
Markesteijn+FDC work in darktable 2.4.0, which calls Markesteijn as a black box).
The only description is the source, and the accessible copies are GPL. **Re-typing
one in Slang is copying, not implementing from a description. That route is
closed.**

**Half two, and it makes half one stop mattering:** the same code ships under
**LGPL-2.1 / CDDL-1.0**, inside LibRaw. `xtrans_interpolate(int)` at
`libraw.h:451` in 0.22.2 — read out of this machine's installed headers, not
remembered — dual-licensed by the distributed `COPYRIGHT`, credited to *"Frank
Markesteijn's algorithm"* in its own source header. ⚠ **The GPL interpolators are
in a different repository**, which is why "LibRaw's demosaics are GPL" gets said:
AMaZE, LMMSE and AFD are in the separate, abandoned
`LibRaw-demosaic-pack-GPL2/GPL3` that Homebrew does not build. The X-Trans one is
in the LGPL/CDDL core. **Orion already links `libraw_r` dynamically, so nothing
about the licence model changes**, and `dcraw_process()` is public with
`user_qual > 2` selecting 3-pass.

### ⚠ What actually makes it expensive is decision #29

`DevelopPipeline.cpp:1269`, in a comment written for Bayer: *"White balance
rewrites the linearize block, which sits at the head of the graph — so moving
temperature legitimately recomputes everything, including the demosaic. That is
inherent."* It is inherent because #29 clips all three channels after white
balance and before demosaic. **So a demosaic that leaves the GPU takes the
temperature slider with it**, and all four ways out cost something — clipping
post-demosaic reopens #29 and loses, a cheap GPU preview demosaic is circular
(that is the thing which does not exist licence-clean), and rescaling gains by a
ratio is unsound because the direction decisions are not linear (§29).

### Two findings nobody was looking for

| | |
|---|---|
| **The GPU graph gets smaller** | Removing `linearize` and the four `rcd:*` and uploading a demosaiced `RGBA16Float` source is **−5 nodes and −445.6 MiB** at 26 MP. **168 nodes on an X-Trans frame against 173 on a Bayer one** — load-immune, and assertable by name the way `dehaze drag` is |
| **40 MP is a problem already** | Every full-res node scales with pixel count, so the *existing* 173-node graph is **~7,700 MiB at 26 MP and ~11,800 MiB at 40 MP** (X-H2 / X-T5) before one X-Trans node exists |

### The published alternative is real, and is not a substitute

Rafinazari & Dubois, ICIP 2014 pp. 660–663, and Rafi Nazari's **open-access**
uOttawa Ph.D. thesis (2017, ch. 3) — **downloaded and read**, per the rule that
came out of the Tang/Tappen correction. Frequency-domain luma–chroma
demultiplexing over the 6×6: 18 components, 13 non-zero, three modulated
Gaussians at σ = 2.32, luma `(2R+5G+2B)/9`. **36.50 dB against Bayer LSLCD's
39.8** — ⚠ on **simulated, gamma-corrected, white-balanced** Kodak mosaics, which
is not Orion's linear domain (the same class of error #111 found one milestone
earlier). ⚠ **And it has never been compared to Markesteijn by anyone** — the
thesis opens the chapter by naming that literature gap itself.

### The test problem, and it is mostly solved by a licence

All three sample frames are Sony Bayer. **`raw.pixls.us` publishes community raw
samples under CC0** — *"I hereby release it under the cc0 license into the public
domain"* — and solicits Fujifilm RAF in both uncompressed and compressed forms.
Piece 0 is five files, including ⚠ **one Bayer Fujifilm as the control**: without
it, a branch that takes the X-Trans path on every Fuji file passes every X-Trans
test. What still cannot be tested here is whether it *looks* right; a PSNR
against a synthetic mosaic is the honest ceiling and §29 says why.

### Gates

⚠ **None run, and this says so plainly.** No build, no `orion-tests`, no
`orion-viewport-tests`, no bench. Nothing under `engine/`, `app/` or `apps/` was
touched — the diff is `research/` and four planning files. Two other agents were
editing the engine in the same hour, and the shader compiler is missing.

---

## Session 2026-08-01r — §3.3, and the estimate that was wrong in one word

**Story:** piece 4 of `research/highlight-reconstruction.md` — Rouf, Lau &
Heidrich §3.3, cross-channel detail transfer. Decision **#109**; `UNSOURCED.md`
§28.

**173 nodes, 7186 MiB — both unchanged.** ROADMAP costed this at **+23 nodes and
~30 MiB**, which is what made it the cheaper of the two remaining pieces.

### ⚠ The re-cost, and it is a word rather than a number

The estimate read *"the pyramid can be reused"*. Two things kill it, and both are
readable in the shipped code rather than measurable only after building:

- **Piece 3's `ρ` is `f` everywhere §3.3 operates.** `hl_mask.slang` writes `Ω^∩`
  as the hole, so every pixel outside it and above the shoulder is *known* with
  `rgb = f`. Over `Ω^∪ \ Ω^∩` — §3.3's entire domain — `ρ ≡ f`, and
  `f*_k = (ρ_k/ρ_j)·f_j` is the identity. `hl_apply.slang`'s own comment said so
  already.
- **Pull-push solves Laplace; §3.3 is Poisson.** There is no residual and no
  relaxation in a pull-push interpolant, so there is nowhere to put `∇·g*_k`.
  Substituting `f* = r·f_j + u` gives `∇²u = f_j∇²r + ∇r·∇f_j`, whose neglected
  term is the size of the detail being transferred.

### ⚠ The census came first, and it changed which thing got built

`apps/bench` block **3e** counts the clip sets on `highlights`' own two sides,
against the ceiling that node was given. On real frames:

| | `_PIC8220` | `_PIC8095` | `_PIC8148` |
|---|---|---|---|
| `Ω^∩` | 112,618 | 54,704 | 17 |
| `Ω^∪ \ Ω^∩` | 86,894 | 89,415 | 77 |
| ...untouched by the window fit | 69% | 78% | all |
| ...and beyond its 12 px reach | 5,608 | 16,578 | 0 |

**0.023%–0.068% of a frame does not buy 23 nodes.** What does is the next row:
the ring that supplies `ρ` for **every** blown core is 11,901 and 20,563 px, of
which **58% and 69%** the window fit hands back untouched. Piece 2 was feeding
still-clipped pixels to the solver as Dirichlet data, so every core in the frame
was solved from a rim that was itself wrong by a mean of 0.14–0.22 of the clip.

So piece 4 is a **correction to piece 3** before it is a feature, and it needed
none of the nodes: `hl_mask`'s hole becomes the part of `Ω^∪` the window fit did
not recover — read off the node as `rec > raw`, not off a threshold — and
`hl_apply` writes the ratio where some channel never clipped. `nRgb_` already
existed, so the second binding costs nothing.

### The two sets piece 2 conflated

Which pixels the fill may **write** and which pixels are **evidence** are
different questions. Piece 2's rule about the first is right and untouched —
Masood et al.'s measurement wins wherever it exists. Nothing in that argument
makes a still-clipped pixel evidence.

### The #29 argument, and it is not #106's

#106 leans on the maximum principle. That does not cover this branch:
`(ρ_k/ρ_j)·f_j` multiplies a rim ratio by the pixel's own measured channel and
can exceed the rim's range in level, which is the point. What is bounded is the
**chromaticity** — `ρ` is a convex combination of known colors, so the ratio is
one the rim exhibited. A neutral rim gives `ρ_k = ρ_j`, hence `f*_k = f_j`. And
under #29's clip the domain is *empty* at a neutral white balance, since equal
gains put `Ω_R = Ω_G = Ω_B`.

### The mutations — seven, and three of them are about the tests

| Mutation | Effect |
|---|---|
| `hl_mask`: hole reverts to `Ω^∩` | **3 red** — §3.3 becomes the identity, 0.0836 → 0.0836 |
| `hl_apply`: write `ρ` instead of the ratio | **2 red** — green 0.610 → 0.505, the measured detail discarded |
| `hl_mask`: drop the shoulder rule | **1 red** |
| `hl_apply`: predicate is a level test, not `rec > raw` | ⚠ **750 engine checks green, bench red at 13,135 px, exit 1.** The fixture's recovered ring is uniform, so `ρ` equals the picture over it and the ratio is the identity whatever predicate is used. Moved to `apps/bench` 3e |
| `hl_apply`: recovered channels count as reference | ⚠ **Equivalent mutation.** `highlights.slang` declines *per pixel*, so either every clipped channel is lifted (and §3.3 never runs) or none is. Nothing can distinguish the two spellings |
| Drop the "may only raise it" floor | ⚠ Nothing red; the census says it binds on **150** and **645** channels on real frames |
| Remove the `kMaxGain` ceiling | ⚠ Nothing red, and it caps **0** channels on either frame. Kept rather than deleted — unlike `hl_pull`'s weight cap it is not *provably* unreachable. `UNSOURCED.md` §28 |

### ⚠ Two fixture defects found and fixed, and the older one was worse

- **The test's target was itself clipped.** Every ring in the wiring fixture is
  neutral at the sensor, so the truth about the lamp is 2 : 1 : 1.5 — and the
  partial ring reads 1.00 : 0.61 : 0.92 because its red hit the ceiling. The test
  asked whether the core took *that* ring's color, which is the error piece 4
  removes. A bright wholly-unclipped ring is now the target.
- **The fixture recovered nothing at all**, so two mutations that overwrite the
  window fit's answer were invisible. The partial ring is 32 px wide now and
  straddles the 12 px reach; the radial profile is printed.
- ⚠ And a `kMaxGain` assertion was **written and deleted** rather than shipped: it
  was green for every value the constant could take.

### Gates

`orion-tests` **750 checks, 0 failures**. `orion-viewport-tests` **3620, 0**. All
**39** `repro/*.txt` exit 0. `orion-bench` exit 0 on `_PIC8220`, **173 nodes,
7186 MiB**, M0 gate PASS at p95 10.06 ms — advisory, and the load-bearing numbers
are the node count and the new exact pixel invariant, both unmoved.

---

## Session 2026-08-01q — three gaps in the Swift layer that a test could not see

**Story:** the gap table's three `app/` rows. Decision **#110** (.1, .2, .3).

### Gap 1 — the memberwise initializer, closed by making the build refuse it

`DevelopState`'s stored properties each carried an inline default. A default on
the property is a default on the **memberwise initializer**, and `Engine.state`
builds one with the memberwise initializer — so a field left out of that call
compiled without a word. It shipped twice: film grain with Amount stuck at 0,
and Grading Balance hours later.

The defaults moved into an `init()` written **in an extension**, which is the
load-bearing part: an initializer in the struct's own body suppresses the
memberwise one, and then there is no argument list left for the compiler to
check. Delegating rather than assigning is the other half — assigning field by
field here would compile with a field missing.

**The proof, with `var proofOfTheTrap: Float` added to the struct:**

    app/EditHistory.swift:399:54: error: missing argument for parameter
        'proofOfTheTrap' in call
    app/Engine.swift:1681:71: error: missing argument for parameter
        'proofOfTheTrap' in call

and the historical defect, `gradeBalance:` deleted from `Engine.state`:

    app/Engine.swift:1669:43: error: missing argument for parameter
        'gradeBalance' in call

⚠ **The hole this does not close, named rather than glossed.** A field added
*with* an inline default gets a default in the memberwise initializer too, and
`Engine.state` can go on omitting it. `testDevelopStateRoster` reflects over the
struct and is red for that case, naming the field.

⚠ **And its second half found a check that could not fail.** `busyState()` — the
fixture behind "every field of a fully-set state survives the sidecar" — had
never moved `perspectiveVertical`, `perspectiveHorizontal`, `perspectiveAspect`,
`gradeBalance`, `grainAmount`, `grainSize`, `vignetteAmount` or
`vignetteFieldAngle`. A field the fixture leaves alone round-trips its own
default through the encoder and back and passes for free, so that test had been
green for eight fields it had never carried. Fixed in the same commit.

### Gap 3 — the wheel can be driven now, and here is the number

`wheel <name> <x> <y> [luma]` and `dragwheel <name> <x,y> <x,y> <n>`. Both puck
components move inside a **single** `edit`, because `ColorWheel.onChanged` does;
two `set`s would be two ticks and two history entries. The path is clamped to
the disc, not the square that bounds it. ⚠ **Added under #89, not renamed** —
`gradeShadowX` / `gradeShadowY` still work and the new repro block uses them.

**12 ticks on `_PIC8220`: 9.6 ms per tick unarmed, 1.2 armed — 8.0×**, and the
settled picture is identical either way at luma 0.2268 / sat 0.5136.

### Gap 2 — the honest half, with the attempt written down

`repro/gesture-preview-agrees.txt` compared an armed run against an unarmed one
and demanded they agree. **That is green when arming does nothing at all**: make
`beginInteraction` a no-op and all six checks pass, because both halves are then
the same unarmed run. A file of six checks that a gutted feature passes.

The preview graph renders only while armed — `if interacting { renderPreview() }
else { render() }` — so the preview *surface* is where arming is observable
without a `View`. Eight ticks of a 2.5-stop drag now leave it at 0.2323/0.2918
unarmed and put it at 0.4814/0.2037 armed, and the no-op mutation is red.

⚠ **The first link is still unverified, and it was attempted rather than
assumed.** `Screenshot.render` already hosts SwiftUI in an off-screen window, so
a real `ColorWheel` was hosted, laid out and hit-tested — `hitTest` returns the
hosting view — and driven with `NSEvent.mouseEvent` through
`NSApplication.sendEvent` at every height down the control. **The puck never
moved.** SwiftUI's macOS gestures want CGEvent-backed events, which want a real
on-screen window and the real cursor; a suite that takes over the machine is not
one that runs 39 scenarios in a loop. The probe was deleted rather than shipped
half-working. **Deleting `ColorWheel`'s `beginInteraction` is green across 744
engine, 3624 viewport and all 39 scenarios** — that is the size of the gap,
measured.

### The mutations

| Mutation | What goes red |
|---|---|
| Drop `gradeBalance:` from `Engine.state` — the 2026-08-01 defect | **the build**, `Engine.swift:1669` |
| Add `var proofOfTheTrap: Float` to `DevelopState` | **the build**, twice — `EditHistory.swift:399` and `Engine.swift:1681` |
| Add `var mutationTrap: Float = 0` — *with* a default, so it compiles | `testDevelopStateRoster`, **2 checks**, both naming `mutationTrap` |
| Revert `busyState()`'s eight new lines | `testDevelopStateRoster`, 1 check, naming all eight |
| `dragwheel` writes x and leaves y | `gesture-preview-agrees`, `gradeShadowY == -0.55` holds 0 |
| Drop the disc clamp from `clampToDisc` | `gesture-preview-agrees`, 2 checks — `gradeMidtoneX` holds 3, not 0.6 |
| `beginInteraction` sets nothing | `gesture-preview-agrees`, `preview_armed != preview_unarmed`. ⚠ **The six agreement checks stay green**, which is exactly why the new block exists |
| Delete `beginInteraction` from `ColorWheel.onChanged` | **nothing** — 744 / 3624 / 39 all green. The gap, measured |

### Gates

`orion-tests` **744 checks, 0 failures**. `orion-viewport-tests` **3624, 0** (was
3620; 4 new). All **39** `repro/*.txt` exit 0. `orion-bench` on `_PIC8220` exit
0, **173 nodes, 7186 MiB** unchanged, M0 gate PASS at 8.76 ms — advisory, and
the node count is the number that means anything.

---

## Session 2026-08-01p — Core ML denoise, researched and deliberately not built

**Story:** M5's "ML denoise (NAFNet-class via Core ML)", on the roadmap since
the first milestone list and never once investigated. **Research and
decomposition only. Nothing was built, no build was run, and neither test suite
was run — there was nothing to test.** That was the brief and it is the outcome.

**Shipped:** `research/denoise-learned.md`, `ROADMAP.md`'s six-piece table,
`UNSOURCED.md` §27, decision #111, and corrections to `FEATURES.md` and this
file.

### ⚠ The premise was wrong, and the wrong half decides everything

Every statement of this line — the roadmap, `FEATURES.md`, and the brief for the
session — assumed Orion's noise handling is pre-demosaic. Read out of the source
rather than assumed:

| Piece | Where | Domain |
|---|---|---|
| The **fit**, `estimateNoise(const BayerImage&)` | `raw/NoiseProfile.cpp`, CPU, before any node | Bayer, sensor counts |
| The **filter**, `denoise:blur 0..3` / `denoise:shrink 3..0` | GPU, **after `rcd:red/blue`, before `camera->working`** | **Linear camera RGB**, demosaiced, `rgba16f` |

The node's own comment says why: `var = a·x + b` only holds in linear camera RGB
and the matrix would mix the variances along with the channels. So Orion has a
**third domain**. Published denoisers are trained on sRGB — gamma-encoded,
tone-mapped, 8-bit, which is what SIDD's 40.30 dB measures — or on the Bayer
mosaic (Brooks et al., CVPR 2019, the paper that names this problem). **Neither
is Orion's.**

⚠ **An sRGB checkpoint dropped in at the current insertion point would look
plausible and be wrong for a reason invisible to inspection.** That is the purple
cast's exact shape, and no test in either suite would catch it.

### The two pieces of arithmetic that decide the rest

**It is not a node.** At 6024×4024 one `RGBA16Float` node is 185 MiB. One fp16
**32-channel** activation is **1,480 MiB** — and so is the entire existing 8-node
denoise chain, because 32 × 2 and 8 × 8 are both 64 B/px. Not a coincidence, an
identity, and it makes the point in one line: **one feature map of a width-32
network costs the whole denoiser Orion ships.** A four-level U-net is ~2,868 MiB
for one activation per level and a guessed 4–8 GiB in practice, on top of 7,186.

**It is not a slider.** NAFNet's Table 6 gives 65 GMAC at 256×256 = 0.992
MMAC/px → **48.1 TFLOP** a frame, 65.5 tiled. No sustained fp16 throughput was
measured here, so it stops at a function: 13.1 s at 5 TFLOP/s, **1.6 s at 40**.
M0's budget is 16 ms. The cheapest row is **100×** it.

### ⚠ The blocker was never the facade

Verified in this machine's SDK headers rather than remembered: `MLModel`,
`+[MLFeatureValue featureValueWithPixelBuffer:]` (`MLFeatureValue.h:60`) and
`-[MLMultiArray initWithPixelBuffer:shape:]` (`MLMultiArray.h:178`) are all
**Objective-C**. The engine already compiles three `.mm` units, so Core ML is
callable from inside the engine with no Swift and no facade crossing. It fails
by `NSError`, so the wrapper returns a status code — an exception crossing would
terminate the process.

`MPSGraphTensorData initWithMTLBuffer:shape:dataType:` (`MPSGraphTensorData.h:57`)
is the fallback if the pixel-buffer path turns out to copy: it takes an
`MTLBuffer` directly. ⚠ Its price is hand-building the network in ObjC, which is
a thousand-line file by construction and against the maintainability constraint.
**Core ML first; MPSGraph only if piece 2 forces it.**

### Licences, all checked rather than remembered

| Artefact | Licence | Usable |
|---|---|---|
| NAFNet code (`megvii-research/NAFNet`) | MIT + Apache-2.0 (BasicSR) | ✅ |
| Restormer code (`swz30/Restormer`) | MIT | ✅ |
| **DnCNN**, **FFDNet** (`cszn/*`) | ⚠ **No licence file at all** — 404 from GitHub's licence API | ❌ No grant. `cszn/KAIR` is MIT and re-implements both |
| DND (Darmstadt) | "**non-commercial purposes**" | ❌ Benchmark only |
| SIDD | Project page claims MIT | ⚠ Single unverified source. Read the archive before shipping |
| `coremltools` | BSD-3-Clause | ✅ |

Decision #78's rule transfers unchanged: **weights inherit their training data's
terms however permissive the architecture's code is.** NAFNet being MIT does not
make NAFNet's checkpoints MIT.

### The decomposition, and the order is the decision

Six pieces in `ROADMAP.md`. **Pieces 1 and 2 are measurements, piece 3 is a
written argument that is allowed to conclude "stop here", and nothing is
converted or trained before them.** `research/highlight-reconstruction.md`'s
estimate was 16× out because the measurement came after the cost; this is the
same trap one milestone later, and the piece table has a guess column so it is
at least visible.

⚠ **Do not start at piece 5.** Pieces 1 and 4 are worth doing regardless: piece
1 gives Orion the paired fixture and full-reference metric **it does not have
today** — there is no clean reference in this repo and therefore **no dB figure
for the shipped denoiser, and none should be quoted** — and piece 4's tiler is
what a full-resolution export path wants anyway.

### Gates

⚠ **None run, and this says so plainly rather than claiming otherwise.** No
build, no `orion-tests`, no `orion-viewport-tests`. Nothing under `engine/`,
`app/` or `apps/` was touched; the diff is `research/` and four planning files.
Three other agents were editing the engine in the same hour.

---

## Session 2026-08-01o — a mask under a keystone is an ellipse, and had been staying round

**Story:** the open edge from decision #100 — `UNSOURCED.md` §24's second item.
Decision **#107**; `research/perspective.md`.

### The maths, and why it is not Smith's

A radial mask's semi-axes went through **√|det J|** at the mask's centre: the
geometric mean of the two axis scales, one isotropic number standing in for a
general 2×2. It is exact where the homography is conformal and nowhere else.

The image of an ellipse under a linear map is an ellipse. Semi-axes
(aₓ, a_y) at φ make the unit disc under **A = R(φ)·diag(aₓ, a_y)**; the image is
the unit disc under **B = J·A**; B's semi-axis lengths are its singular values
along its left singular vectors — **Golub & Van Loan, *Matrix Computations*, 4th
ed., JHU Press 2013, §2.4.1** — and both are read off the symmetric **S = B·Bᵀ**,
whose 2×2 eigenproblem is closed form, their **§8.5.2**. Thirty lines.

⚠ **The instruction was to reuse Smith's closed form (CACM 1961), and it was
not followed.** The only Smith in this repository is
`SkyDetector.Stats.largestVariance()`: Swift, the 3×3 case, returning the
largest eigenvalue and no eigenvector. This needs both roots and an axis, in
C++, on the engine side of the facade — and Smith's trigonometric construction
*reduces* to the quadratic formula at n = 2, so using it would have meant
padding a 2×2 to a 3×3 to reach a worse form of the same answer. Written down
here rather than quietly done, because the reason is the interesting part.

### ⚠ The keystone was the wrong place to look, and the recorded table was wrong

The premise handed over was that a 0.34-of-the-frame mask leaks 2 of 60 cells at
0.0105 luma under vertical 0.45, and would stop after the fix. Neither half held.

- **The configuration does not leak.** `0.34 × 0.22`, run through the scenario
  file itself: **64 clear cells, no leak** — on the build before the fix and the
  build after. The recorded cell count (60) does not match either build.
- **The axis was wrong.** A vertical keystone stretches *y*, so the error grows
  with the mask's extent along **y**; the old sweep varied **x** and held y at
  0.22. `0.30 × 0.34` leaks where `0.34 × 0.22` does not.
- **And most of what does leak is not fixable this way.** Over a mask a third of
  the frame across, the map's *curvature* dominates its anisotropy. The ellipse
  takes vertical 1.0 from **0.1001 → 0.0862** luma: about a fifth, and the shape
  that leaked still leaks.

### Where the fix is unambiguous: the aspect squeeze

Aspect is diag(1/g, g) — **exactly linear, so no curvature, and
area-preserving, so √|det J| is exactly 1.** The old code therefore left a mask
**round while the picture under it was squeezed two to one.** Not an
approximation softening at the rim; the shape dropped.

| Aspect | Mask | Isotropic | Ellipse |
|---|---|---|---|
| +1.0 | 0.20 round, centred | 4 of 84, worst **0.1461** | **none** |
| −1.0 | 0.20 round, centred | 4 of 84, worst 0.1207 | **none** |
| +0.5 | 0.20 round, centred | 4 of 84, worst 0.0567 | **none** |
| +1.0 | 0.24 × 0.14 at 0.6 rad | 2 of 80, worst 0.0067 | **none** |

0.1461 luma is an order more than any keystone reading ever recorded in
`perspective.md`, and it sat in a control that has shipped since #100.

### What kept `radiusToFrame` safe

Its derivation is decision #83's and is pinned by `repro/mask-alignment.txt`,
which caught a semi-axis swap leaking into 14 of 207 cells. Two things kept it:

- **The angle comes back as a delta on `Placement::angle`**, so the quarter
  turns stay outside this function exactly as #83 requires.
- **Neutral is short-circuited rather than solved**, so an uncorrected
  photograph is bit-identical and not merely close. `orion-tests` asserts `==`,
  not a tolerance.

`mask-alignment.txt` is green, and so are the other 37 scenarios.

### The tests, and the mutation

`testPerspectiveMaskExtent`, 12 checks. The one that matters is not a tolerance
argument: under a full aspect squeeze an axis-aligned mask must come out
stretched by **g and not by √g**. Also pinned — every point of the source rim,
mapped, lands on the rim of the ellipse that comes back (over a squeeze, a shear
each way, and a general 2×2, formed exactly as `mask_component.slang` forms it);
a mask at an angle comes out **turned**; the area is |det J|; and neutral is
bit-identical.

**Mutation, run:** return `{ax·√|det J|, ay·√|det J|, 0}` from `radiusToFrame`.
`orion-tests` **5 failures of the 6 new blocks**, and
`repro/perspective-carries-the-mask.txt` **3 failures, exit 1**. The two blocks
that survive it — area, and bit-identity at neutral — say so in their own
comments; they are there for a *future* rewrite, not for this one.

### Gates

`orion-tests` **702 checks, 0 failures**. `orion-viewport-tests` **3,620, 0**.
**All 38 `repro/*.txt` exit 0.** `orion-bench` on `_PIC8220` exit 0, M0 gate
**PASS** at p95 8.87 ms, **149 nodes** unchanged, and the `perspective 0.6`
probe still **1 node**.

⚠ **The M0 gate is noise on this machine, and the number to trust is the node
count.** Eight runs of the *same unchanged binary* gave p95 **8.87, 8.92, 8.96,
8.97, 9.25, 9.95, 20.74, 21.76, 31.45 ms** — three of them failing a 16 ms gate
and the rest passing it by half. An intended A/B against the pre-fix binary was
attempted and its `git checkout` silently no-opped on a quoting bug, so all six
of its runs were the same build; that accident is where the spread above comes
from, and it is a better measurement than the A/B would have been. The change
is host-side float arithmetic in mask parameter assembly — a few dozen flops per
mask component per `apply`, none when no mask exists — and adds no node.

### What is still open, and it is named rather than implied

- A **linear gradient's ramp length** is still isotropic √|det J|. The
  anisotropy could go the same way; the non-uniformity could not, since a
  projective map preserves cross-ratios along a line and not ratios. Unmeasured.
- The map's **curvature** over a large mask. Now the whole remaining error at
  the rim, and not costed.

## Session 2026-08-01n — the highlight fill wired, and the estimate that stopped it

**Story:** pieces 2 and 3 of `research/highlight-reconstruction.md` — the
clipping mask and the node chain. Decisions **#105** and **#106**.

**149 → 173 nodes, 6971 → 7186 MiB.** Off at `highlightRecovery = 0`, the
default. Piece 1's solver is now reachable from the product.

### ⚠ The memory number was the decision, so it was measured before it was spent

ROADMAP costed piece 3 at **+25 nodes and ~516 MB** at full resolution, and that
number was why this was a three-session item. `ρ` solves `∇²ρ = 0` — it is
harmonic, it has no detail to lose, and the only place it moves quickly is the
rim, which the apply pass reads at full resolution anyway. So the factor was
swept against the same Gauss-Seidel reference the solver is judged by, **before a
node was written**:

| Solved at | 1/1 | 1/2 | **1/4** | 1/8 | 1/16 |
|---|---|---|---|---|---|
| Deviation, % of rim span | 6.1 | 6.3 | **6.9** | 8.7 | 12.6 |

A quarter costs 0.8 points on top of an approximation already worth 6.1, for a
sixteenth of the memory. Both ends are pinned by a check, so the factor is a
measured choice and not a free one.

⚠ **Subsampling does not save nodes**, and the estimate's node number was right
for the wrong reason. The level count is logarithmic in the frame: a quarter
removes two levels and nothing else. 24 nodes where the estimate said 25.

⚠ **And the pyramid was never the cost.** Of the +215 MiB, the pyramid is
**30 MiB**; the other **185 MiB is the apply node** — one full-resolution
`RGBA16Float` pass that no factor subsamples away. Decision #96 measured the same
194 MB for the creative vignette and *fused it into the grade* rather than pay
it. There is nothing to fuse into here: the fill must land after `highlights` and
before the denoise.

### What it does, and what it deliberately does not

The mask is **`Ω^∩`**, not the `Ω^∪` §3.2 nominally solves over. The union's
partial case already has a node — `highlights.slang` is Masood et al.'s
cross-channel fit, recovering a clipped channel per pixel from real evidence —
and replacing that with a smooth interpolant is strictly worse. `Ω^∩` is the set
nothing addresses, and under #29 it is where `count == 3` is a literal identity.

Reading `highlights`' output rather than the demosaic is what makes the two one
feature: the window fit recovers the annulus, the fill carries *that* annulus
across the core.

It leaves a **plateau** — no Mach band, since `ρ|∂Ω = f|∂Ω` makes the join
continuous, but a blown lamp comes back with its rim's color and none of its
falloff. That is §3.4 and it is piece 5, now the visible gap.

### The mutations, including the two that found a check that could not fail

⚠ **`filling = true` — the chain never switching off — passed the bench.** The
check was written on the exposure drag, and the fill is *upstream* of exposure:
once it has run it stays cached, and a tick never touches it either way. A chain
running on every photograph opened, forever, for nothing, printed `3 of 173
nodes` and `ok`. Moved to the full render after the control goes to zero.

⚠ **Dropping the mask's shoulder rule passed every check**, at 0.0687 against a
bound of 0.25. A rule with a citation and no failing check is decoration; the
wiring test gained the tighter one it fails (0.0528 clean, bound 0.06).

| Mutation | Effect |
|---|---|
| Re-push the pyramid's params per tick | 23 fill nodes on an exposure tick — #92's shape exactly |
| Apply the fill outside `Ω^∩` | "an unclipped pixel is returned untouched" fails, delta 0.082 |
| Hand the apply the picture's size as the fill grid's | **No effect** — premultiplication makes even the out-of-bounds read harmless, since `rgb` and `a` attenuate together. Recorded, not patched |

### The #29 argument, made on the day the node landed

Decision **#103**. #29's magenta was the white-balance gains: the same magenta on
every blown pixel of every frame, supported by nothing in the picture. `ρ` is the
harmonic interpolant of the region's own rim and by the maximum principle cannot
leave that rim's range, so **a neutral rim still gives a neutral core** — #29's
outcome, reached by evidence rather than by decree. #29 itself is untouched: the
clip still happens, still pre-demosaic, still before RCD interpolates across it.
The headroom stays gone and is not recoverable, which is the trade #29's own row
already named.

### Gates

`orion-tests` **700 checks, 0 failures** (was 692). `orion-viewport-tests`
**3620, 0**. All **38** `repro/*.txt` exit 0. `orion-bench` exit 0, M0 gate PASS.

⚠ **The M0 number is advisory and the spread says why**: five runs of the *same*
binary, minutes apart on a quiet machine, gave p95 **8.88 / 8.97 / 9.13 / 9.16 /
10.94 ms** — and the unchanged binary before this session's first line gave
10.69. The chain is invisible in that noise either way, which is exactly why all
four new invariants count **named nodes** and none of them counts milliseconds.

### ⚠ Found in passing: there are two decisions numbered 96
### ⚠ Found in passing, and fixed at the merge: four decisions were numbered 96

This agent found two — the highlight-reconstruction method and the creative
vignette — and deliberately left them, on the grounds that a renumber missing one
reference is worse than a duplicate everybody can see. Fair, and the count was
worse than it looked: **four** sessions had claimed #96. Perspective was
renumbered to #100 when it merged; the vignette is now **#103** across fourteen
references, six of them code comments and two repro headers, and two snapshot
repro files were citing #96 for a decision that is #99. `grep -rn "#96"` over the
tree returns exactly one hit now, and it is the highlight method that keeps the
number. This session's own two decisions moved to **#105** and **#106** for the
same reason.

*Sessions `2026-08-01m` and earlier are in `HISTORY.md`.*

# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-08-04 (**piece 5's scaffolding is written, #168** — every
constraint and the target number, and **no discretisation**, which the paper
owes. ⚠ **The 8 GB trade, #162, needs your call**)

**Phase:** M0 done. **M1 complete.** M2, **M3 and M4's geometry complete**.
**`research/masking.md` is finished** — primitives, groups, guided refinement, a
raster component, Vision filling it, and a band on brightness. Six mask kinds. A
mask is a *list* of components folded per §6 (add/subtract/intersect), optionally
feathered onto the photograph's own edges, through the graph, the POD facade, the
panel rows, the sidecar, undo and the bench.

⚠ **Duplicated blocks were removed from this header on 2026-08-01, and they grew
back by 2026-08-02** — which is the useful part of the story. The first clean-up
took out two `Last updated` lines, two overlapping queues numbered 4/5 twice, and
three `Suites:` paragraphs, one of them two sessions stale. A day later the
header carried **two `Last updated` lines and two `Phase:` blocks** again, and the
queue had **three** copies rather than two. Nobody added a duplicate on purpose:
each session appends to the top of this file without reading what is already
there, and appending is invisible in a diff that is already long.

⚠ **So check the top of this file for a second copy of what you are about to
write, before you write it.** The header is one `Last updated` line, one `Phase:`
block, and one queue. That is the whole rule, and it has now failed twice.

⚠️ **M3 is done — do not rebuild it.** Dehaze, creative LUTs, exposure fusion
and auto-enhance all shipped with research files, GPU tests and bench probes
(sessions `2026-07-28e` through `2026-07-29d`, now in `HISTORY.md`; the cost
table is immediately below). A stale kickoff prompt naming those four has now
arrived **36 times**; the answer each time is that they exist, and each of the
four now also has something that fails when its *wiring* breaks.

**Next story:** the queue, in order, each with a cost.

⚠ **This list had grown to three overlapping copies**, numbered 1-6, 1-5 and
1-6, each spliced into the next **mid-sentence** — so the seams were invisible
unless you read the whole thing end to end, and the second copy began in the
middle of the first copy's item 6. They disagreed about the one thing a fresh
session would act on: **incremental brush accumulation** was the top open story
in two of the three, costed at "~1-2 sessions", and it **shipped on 2026-08-01**
(#102 and #108; `ROADMAP.md` heads its section *"Incremental brush accumulation
— ✅ shipped"*). Merged 2026-08-02. Closed items are now one line each with the
decision that closed them; **every measurement removed from here was checked to
exist in `DECISIONS.md`, `HISTORY.md` or `ROADMAP.md` first**, so this is a
shortening and not a deletion.

**Open, in order:**

1. ~~**A linear gradient's level sets under a non-conformal correction.**~~ ✅
   **done 2026-08-02, decision #137.** `maskcheck 20 -2.0` under
   `perspectiveAspect 1.0` went from **3 of 27 clear cells leaked, worst 0.1300
   luma** to clean; §4c pins it both signs and against a squeeze composed with a
   keystone. Both prerequisite pieces landed with it — `mask::displayMatrix`
   folds crop, straighten, turns and the correction into one 3 × 3, and
   `mask::ramp` builds the two rows the kernel evaluates.
2. ~~**The perspective map's curvature across a large mask.**~~ ✅ **done
   2026-08-02, decision #138.** Not reduced — removed. A radial mask is no
   longer transported into frame coordinates at all; the kernel carries each
   pixel back through `mask::displayMatrix` and evaluates the ellipse as drawn,
   so there is no derivative left to be first order about. §4d of
   `repro/perspective-carries-the-mask.txt` is the two-sided proof and fails in
   **four places at once** on a revert, worst 0.1219 luma. ⚠ **The performance
   question that came with it is answered by measurement**: 1.02 ms with the
   per-pixel 3 × 3 and divide, 1.07 ms with it removed, on 24 MP — and the
   instrument had to be built first, because all four of `orion-bench`'s
   `mask:0` timings were brushes.
3. ~~**Americanising the persisted keys** (#89) — needs sign-off, it rewrites
   sidecars already on disk.~~ ✅ **already shipped, 2026-08-02, decision
   #112** — a day before this queue offered it. Dual reads on all seven mask
   keys plus `denoiseColour` and `PresetGroup`'s `"colour"`; American keys
   written and British ones never written back; both-keys-present rules asserted
   at two levels; `testAmericanKeyMigration` closes with `CanvasLayout.maskAlpha`
   on a grid, so a renamed key cannot pass by quietly landing on the default.
   **Confirmed by mutation** (#139): deleting one `?? float(.legacyColourR)`
   fallback reddens two viewport checks. ⚠ **Nobody knew because #112 had no row
   in `DECISIONS.md`** — cited nine times from the code, absent from the ledger.

⚠ **THE QUEUE IS EMPTY.** All three items are shipped. This is the **third**
time it has offered already-shipped work as the next story: #135 found two
(snapshots #99 and Balance #104), and #139 found this one. The first two were
duplicate copies of the queue disagreeing with each other; this one was quieter
and worse — the work was done, and the *decision that closed it* had never been
written down, so the only record was a comment inside `EditHistory.swift`. A
session that trusted this file would have re-run a schema migration over the
photographer's sidecars.

⚠ **`tools/check-decisions.py` is what stops the fourth time**, and it is in
`CLAUDE.md`'s test list beside the two suites. It fails on a duplicate decision
number, on a `#N` the tree cites that has no row, and on a gap nobody declared.
It does not check this queue — nothing can, since only the tree knows what is
built — but it does guarantee that a shipped thing has somewhere to be recorded.

**So there is no next story queued, and picking one is your call.** Uncosted
options, from `ROADMAP.md`: X-Trans (pieces 1–6), Core ML denoise (research
landed under #111, explicitly not built), Windows port, DCP profiles. The three
items below need you and cannot move from this side.

**Blocked on the developer, not on work** — these cannot move from this side:

- ⚠ **The flat frame.** A photograph came back as one flat brown rectangle on
  the developer's screen, twice, and nothing in this repository could reproduce
  it: the engine renders that file correctly with the developer's own sidecar
  restored, and so do the export path, `--screenshot`, both suites and ten real
  window opens. Instrumentation shipped instead of a guess (#b3ee5a1) — the
  footer now reads **"Not a photograph — the render is one flat colour,
  rgb(…)"** when it happens. What is needed is one reproduction and the file
  `~/Library/Logs/Orion/session.txt`. ⚠ **Copy that log before running any
  Orion command**, including the CLI ones — every launch rewrites it.
- **Does the brush feel fast?** The numbers say yes (#108); nobody has said so
  with a stylus in hand.
- **Approval to download CC0 X-Trans frames** for piece 0. Without them the
  X-Trans path has no real sensor to test against.

⚠ **The performance audit (`ROADMAP.md`) has started, and its first finding was
about itself, #148.** The table it opens with — four gestures that do not arm
the preview graph — was **wrong in all four rows**; every one had been fixed and
nobody updated it. `tools/check-gestures.py` is now a gate so it cannot rot
again, and it is a grep rather than a test on purpose: a `DragGesture` closure
cannot be driven from either suite (#110.3), so deleting one of these calls is
green everywhere. **The tick is now measured too (#149)**: clarity drags at **4.2 ms** and releases
at **56.4** — the instrument had recorded only the unarmed path since it was
written, so it reported 17 fps for a gesture that runs at 235, and its own text
called the preview path unbuilt long after it was built.
✅ **The measuring protocol is settled and measured (#150)** — five runs of one
binary spread **0.14 ms**; a saturated CPU changes nothing (**8.98, spread
0.06**); a second Orion on the GPU gives **10.09 / 21.23 / 11.23, spread 11.14**.
The rule is *CPU load is harmless, anything else on the GPU is fatal*, and the
bench now warns above a 2 ms spread instead of captioning it as noise. **Every
other number in this audit was taken on a quiet machine and is comparable.**
✅ **Cold open is measured (#151): 210.9 ms**, and for all of it the canvas holds
the *previous* photograph — `isLoaded` is set once and never cleared, and there
is **no busy state anywhere in the application**. ⚠ **A design question is open
because of it**, deliberately unsettled: holding the last frame avoids a flash
of black, but for a fifth of a second the window asserts a picture that is not
the one being opened — worst in the flat-frame case, where a correct photograph
lingers over a wrong one. A busy state is its own story, with a screenshot check.
⚠⚠ **Memory is measured and it is the audit's one real defect (#152): a 24 MP
frame wants 7,186 MiB of intermediates and an 8 GB Mac has about 6,144.** It
cannot open there, the `CLAUDE.md` floor of macOS 14 admits those machines, and
**the alpha is publicly downloadable**. Nothing in the engine checks —
`recommendedMaxWorkingSetSize` is read, carried and exposed, and never compared
against what the graph allocates. The bench prints the ratio and warns now.
✅ **Costed 2026-08-03 (#153), and the answer eliminates two of the three.**
`Pipeline::peakLiveBytes` reports **1,202 MiB** of peak live memory against the
**7,186 MiB** actually allocated — **83% held for nothing**. Tiling and lower
precision both shrink the *working set*, and the working set already fits an
8 GB Mac with room; what does not fit is the **lifetime**, one texture per node
held from construction to destruction. **The fix is a pooled allocator**, and
neither expensive rewrite is warranted. ⚠ The figure is deliberately optimistic
(it lets differing sizes share), so the truth is between 1,202 and 7,186 — but
even a poor pool lands far under 6,144, which is what settles it.
✅ **The pool is built and tested (#155)** — `gpu/TexturePool`, a free list keyed
by exact shape, with `liveBytes`/`peakLiveBytes`/`hits`/`misses` so the wiring
can be checked against #153's predicted **1,202 MiB** rather than assumed. Two
mutations, two caught.
⚠ **It is wired into nothing, and that is the decision.** A texture handed back
while a later node still reads it renders a *plausible* picture made of another
node's pixels — caught before only by a byte-for-byte test.
**Next story: `Pipeline` adopts it — and it is two commits, not one (#158).**

⚠ **`outputs_` is a `vector<unique_ptr<Texture>>`, one owner per node**, and
pooling means two non-overlapping nodes **share** a texture, which a vector of
unique owners cannot express. So a default-off flag is not available: the *type*
has to change first.

1. ~~**Ownership.**~~ ✅ **done, #160.** `outputs_` is a vector of raw pointers
   into `ownedOutputs_`, which still holds one texture per node — no allocation
   changed, and the A/B reports **bit-identical, 0 of 96,962,304**. ⚠ A pointer
   in `outputs_` dies with its owner, and `reallocateOutput` is the only place
   that happens; it replaces the owner *before* re-pointing.
2. ⚠⚠ **Reuse — BLOCKED, and the plan was wrong (#161).** `Pipeline::render`
   skips nodes that are not dirty, and a skipped node contributes **the pixels
   still in its output texture from last render**. A pool recycles exactly
   those, so every render becomes a full one: an exposure drag is **9.33 ms with
   3 of 173 nodes** against **67.25 ms** for all — about **7×**, against an M0
   gate of **<16 ms at p95**. It would turn a passing gate into a failing one.

   ⚠ **The way out is that they only conflict *while editing*.** Opening a
   photograph is a cold render — every node runs, nothing is cached, and the
   7,186 MiB is held for a reuse that has not happened yet. That is also the
   only moment #152's ceiling bites: the frame **fails to open**, not to be
   edited. **So cost these two:** a pool used for the cold render and released
   before the interactive one begins, or a pool restricted to nodes the dirty
   walk always recomputes.

   ⚠ **#153 still stands** — the *lifetime* is the problem, not tiling or
   precision. What changed is that the lifetime worth shortening is the one
   inside a single cold render, which the liveness walk already models.

⚠⚠ **BOTH SHAPES COSTED (#162) — AND THIS NOW NEEDS YOUR DECISION.**
Measured across every ordinary control: a drag recomputes **2–11 of 173 nodes**
(exposure 3, temp 11, contrast 2 …). **So ~162 node textures exist purely as
cache, and that is where the 7,186 MiB is.**
- **Shape 2 (pool the always-dirty nodes) is dead** — those are the 2–11; the
  memory is in the ~162 that are never recomputed while editing.
- **Shape 1 (pool the cold render) does not survive either.** It makes the
  *open* fit — 1,202 MiB instead of 7,186 — but the interactive path then wants
  its cache back, the same 7,186 against ~6,144. **The frame would open and then
  fail**, which is worse than not opening, because it has already been seen.

**So it is a product trade, not an implementation choice**, and only on machines
that cannot hold the cache: **run pooled and re-render fully — 67.25 ms a tick
against 9.33, about 15 fps — or refuse to open the photograph.** The engine
already reads `recommendedMaxWorkingSetSize` (#152) and can tell which machine
it is on. ⚠ The pooled mode **fails M0 by design** (<16 ms p95 is unreachable at
67); a waiver on 8 GB is the honest form of *"this is the degraded mode"*.
**Slow-but-opens vs fast-but-refuses is a question about what Orion is.**
Everything to implement either is built and correct (#155–#160).

✅ **The A/B oracle is built (#159)**, in `orion-bench` — the one place that
already constructs the shipping graph against a real photograph. Today it
asserts **determinism**: same adjustments twice, **96,962,304 samples, zero
differ**. Mutation-tested — a 0.05 EV nudge prints *⚠ DIFFERENT (54,740,215)*.
⚠ **Non-fatal deliberately**, until the gate it guards exists. **When pooling
lands, the second render becomes the pooled one and the same line stops being a
tautology** — make it fatal in that same commit.

✅ **The pins are wired into the accounting (#157).** `Pipeline::setPinned` plus
a structural pin on the final output and the source; `DevelopPipeline` declares
the fusion proxy and the dehaze peak after `compile`. ⚠ **The figure did not
move — still 1,202 MiB — so it was probed**: the pinned nodes cost **2.9 and
11.6 MiB**, 14.5 against 1,202, which rounds away. Genuinely negligible, not
silently broken; had the ids been unassigned the same unchanged number would
have meant the opposite. **Still nothing pooled** — bookkeeping only.

⚠⚠ **WHY THEY MUST BE PINNED (#156).** In-graph
liveness is not the whole rule: `Pipeline::nodeOutput(int)` hands out any node's
texture *after* the render, so a pooled texture reissued to a later node would
hand a consumer another node's pixels — right size, right format, wrong picture.
**The list, checked by grep and complete:** `nFuseProxy_` and `nPeak_`
(`DevelopLocal.cpp:626` and `:664`), the final `output()`, and
`sourceTexture()`. `nodeOutput` has no other caller in the tree.
⚠ So **1,202 MiB is optimistic twice**: it lets differing sizes share an
allocation, and now also assumes those four recycle, which they cannot. The true
figure is higher and still far under the 6,144 MiB an 8 GB Mac allows — which is
what keeps #153's conclusion standing.
✅ **Scroll, zoom, pan and the filmstrip are answered (#154) — by reading, and
recorded as a reading rather than dressed up as a measurement.** Zoom and pan
transform the *already-rendered* texture in the canvas blit; neither re-runs the
graph. The filmstrip is a `LazyHStack`. ⚠ No number is quoted because driving a
scroll or a magnify needs a gesture, and #110.3 established this harness cannot
drive one. Closing it properly wants a verb driving `Viewport.zoomBy` and the
filmstrip offset directly — its own small story.

✅ **THE PERFORMANCE AUDIT ASKED FOR ON 2026-08-01 IS COMPLETE.** Six areas.
**One real defect** (#152, the 8 GB ceiling), **one design question settled by
measurement rather than argument** (#153, pooling beats tiling and precision),
and **five stale figures corrected** (#144, #148, #149, #150, #151).

**Asked for, not yet built** — raised 2026-08-03, uncosted:

- ~~⚠ **A lens cannot be chosen by hand.**~~ ✅ **done 2026-08-03, #147** —
  engine (#145), facade (#146) and interface all landed. Type two characters in
  Optics and pick from the 1,452 the bundled database carries; the choice
  persists per photograph as a **name**, never an index, because the data will
  be refreshed and an index would silently mean a different lens afterwards.
  ⚠ **Why it had to exist:** the developer's file names its lens correctly and
  the bundled database has **no `Art 023` entry at all** (#144) — absent from
  the data, not misspelled, so no matching rule could ever reach it. `lookup`
  still refuses near-misses and must keep doing so; `tests_io.cpp` pins that a
  DG DN lens never matches a DG HSM entry.
  ⚠ **Not done:** the bundled lensfun data is still whatever was copied on the
  day, so a lens newer than it stays absent and a photographer has to pick a
  near equivalent knowingly. ✅ **The licence question is settled (#163): the
  database is CC BY-SA 3.0, not the library's LGPL** — Orion parses the XML and
  links nothing — **so refreshing it is clear to do.** Share-alike binds the
  data, not the application; a corrected calibration goes back under the same
  licence, which means fixing a lens entry means contributing it upstream.
  ⚠ That check also found `NOTICE` carried **no attribution for the database at
  all**, only for the poly3 *model* — fixed in the same commit, since the build
  is publicly downloadable. **Nothing was downloaded**; refreshing is its own
  story with its own diff.

**Closed, each with the decision that closed it** — the full write-ups are in
`DECISIONS.md` and the session log below:

- ~~**Dehaze's drag cost**~~ ✅ **#92, 2026-08-01.** The chain's parameter blocks
  were re-pushed every tick and `setParams` dirties the whole downstream
  subgraph whether or not the bytes changed, so nine nodes — six of them
  full-resolution over 24 MP — were redone for a value none of them read.
  **147.3/146.4 → 102.7/100.6 ms**, paired and interleaved. Pinned by the
  bench's `dehaze drag` invariant, which counts *named nodes* rather than
  milliseconds; reverting the guard prints `DEHAZE REDOES THE DARK CHANNEL`.
- ~~**`reopen` grows 25-49 KB a cycle**~~ ✅ **#133, 2026-08-02, closed by
  measurement** — it does not, any more. 240 reopens: one 556 MB step on the
  first cycle, then **+0.9 KB a cycle** over the remaining ~230, which is the
  `open` control's own slope. Decision #90's fix held. ⚠ The methodology trap is
  the more useful half and is in the session entry below.
- ~~**Incremental brush accumulation**~~ ✅ **#102 and #108, 2026-08-01, both
  sessions.** A pointer event's cost is now flat in what is already painted —
  `mask:0` **5.20 ms appending 49 dabs to 294 against 36.46 ms re-laying them**,
  same dab count and same host work, interleaved. One R32Float accumulator for
  the live component, **98.25 MiB** rather than the 393 a per-component one
  would have cost, decided from the arithmetic before the code. ⚠ The *first*
  evaluation of a component is still linear — once after a reload, not once an
  event. ⚠ The bench's old "the mask kernel is flat in dabs" was a fixture
  artifact and is withdrawn.
- ~~**A mask's extent under a perspective correction is first order**~~ ✅ the
  radial half #102 (2026-08-01), the ramp length #134 (2026-08-02). What is left
  is open item 1 above.
- ~~**M1's library gap**~~ ✅ **#91, 2026-08-01.** SQLite index and a persistent
  thumbnail cache: 300 frames with the page cache warm, **454-688 ms cold
  against 28-54 ms warm, 12.9-17.2×**. The leftovers are named and costed in
  `ROADMAP.md` under *Library index — what is not done*.
- ~~**The creative vignette**~~ ✅ **#103, 2026-08-01** — an exposure change in
  scene-linear light, shaped by the cos⁴ law and centred on the crop. ⚠ Its
  sibling, **split toning, was refused** (#97): the grading wheels already do
  it, and the one thing it has that they do not is Balance, which is open item 3
  above.
- ~~**Export panel**: bit depth, metadata policy, output sharpening~~ ✅
  **#93-#95, 2026-08-01.** ⚠ The premise was wrong in two ways: metadata policy
  had been built and wired for some time, and 16-bit was not "not offered" — it
  was the *only* mode, so every file Orion had ever written was 16-bit. The work
  was the 8-bit path, output sharpening, and a location strip that also removes
  the IPTC place names.
- ~~**Eleven files over the 1,000-line ceiling**~~ ✅ **#113, #117, #118, #120,
  #121, #122, #127, #129, #131.** `DevelopPipeline.cpp` 2,896→452,
  `Engine.swift` 2,331→795, `bench/main.cpp` 2,289→85, `tests_effects.cpp`
  1,716→555, `Scenario.swift` 1,615→301, `OrionApp.swift` 1,557→299,
  `DevelopPanels.swift` 1,366→56, `Screenshot.swift` 1,196→315, and #129's three
  test files. **Nothing in the tree is over 1,000**; the gap table below carries
  the sweep that says so and the warning about believing it. ⚠ Three of these
  splits found checks that **cannot fail** and recorded them rather than quietly
  fixing them: #122's five panel sections below the fold (since closed by #125),
  #120's runner exiting 0 on a scenario that asserts nothing, and #117's two
  mutations that were green everywhere.
- ~~**Snapshots / versions**~~ ✅ **#99, 2026-08-01.** This photograph's whole
  state under a name, in a sibling `PHOTO.orion-snapshots.json`. ⚠ **Two of the
  three merged queue copies still called this "the last unbuilt line of M4",
  unestimated**, while `app/Snapshots.swift` and `ROADMAP.md`'s ✅ had both been
  in the tree since the day it shipped.
- ~~**The grading panel's Balance**~~ ✅ **#104, 2026-08-01** — a rigid shift of
  all three zone centres, ±1.25 EV at full travel; `color_grade.slang` carries
  `float balance` and the reference behaviour (Adobe's Balance, Camera Raw 4,
  2007). ⚠ **Also still listed as open at "~half a session"** in the copy it
  came from, citing #97's remainder — which is exactly what #104 built.

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

### Where the counts stand, and the one gate that flakes

⚠ **Nothing is reported and nothing carried forward loses work.** Every gap
below is either cosmetic, named-and-costed, or needs the developer.

**Suites:** re-measured 2026-08-02 at #138, all four gates run end to end.
`orion-tests` **844 checks** · `orion-viewport-tests` **3708 checks** ·
**41 `repro/` scenarios** · all 0 failures. Bench exits 0 on all three frames.
⚠ Earlier copies of this block said **806/3708/40**, and before that **800** and
**3702** — the counts before #125 and #129 added checks; the suites only ever
grow, so a stale number here reads as a regression. The bench prints **54 named
checks** on `_PIC8220`
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

## Sessions `2026-08-03b` – `2026-08-04` — the performance audit, and a memory ceiling nobody could see

⚠ **One entry for eighteen decisions (#148–#165), written 2026-08-04 because it
had not been written at all.** The ledger carried every one; this file — the
recovery point — carried none of them, which is the same failure as a stale
queue wearing different clothes.

### The performance audit, asked for 2026-08-01, complete

Six areas. **One real defect, one design question settled by measurement, and
five figures found stale against the tree.**

| Area | Outcome |
|---|---|
| Gestures (#148) | Premise table wrong in **all four rows** — all six already armed. `tools/check-gestures.py` added: a **grep, not a test**, because #110.3 proved a `DragGesture` cannot be driven from either suite |
| The tick (#149) | *"Sliders slow"* answered. Clarity **4.2 ms armed / 56.4 unarmed**, 13.4×. The instrument had only ever driven the unarmed path, reporting 17 fps for a gesture running at 235 |
| Protocol (#150) | Spread **0.14 ms** over five runs. **CPU load is harmless; anything else on the GPU is fatal** (a second Orion → spread 11.14). Bench now warns above 2 ms — it had printed *PASS at 10.09* captioned "machine noise" |
| Cold open (#151) | **210.9 ms**, and the canvas shows the **previous photograph** throughout — `isLoaded` set once, never cleared, and there is **no busy state anywhere in the app** |
| Memory (#152) | ⚠ **The defect.** 7,186 MiB of intermediates against ~6,144 on an 8 GB Mac. **A 24 MP frame cannot open there, and nothing checks** |
| Scroll/zoom/pan (#154) | Cheap by construction — the canvas transforms an **already-rendered** texture. Recorded as a *reading*, with no number quoted, because the harness cannot drive the gesture |

### ⚠ The 8 GB ceiling, and why it is still open

#153 costed the fix by measurement rather than argument: peak live memory is
**1,202 MiB** against 7,186 allocated — **83% held for nothing** — which ruled
out tiling and lower precision, because the problem is **lifetime**, not size.

Everything to fix it was then built and is correct: the pool and its tests
(#155), the four textures that outlive the graph and must be pinned (#156), the
pins in the accounting (#157), the A/B byte oracle (#159, **0 of 96,962,304**),
and `outputs_` made non-owning with the render **bit-identical** (#160).

⚠ **Then #161 found by reading `render()` that pooling breaks the interactive
path**: a skipped node contributes the pixels still in its output texture, and a
pool recycles exactly those — every render becomes full, **9.33 ms → 67.25**.

⚠ **#162 costed both ways out and neither is free, because the memory *is* the
cache**: a drag recomputes only **2–11 of 173 nodes**, so ~162 textures exist
purely as cache. **This is a product trade, and it is the developer's call** —
see the top of this file.

### Also

**The lens picker shipped** (#145–#147) after #144 found the developer's own lens
is *absent from the data*, not misspelled — `…DG DN | Art 023`, and the bundled
database has no `Art 023` entry at all. **#163** settled the licence: the data is
**CC BY-SA 3.0**, not the library's LGPL, so refreshing is clear — and the check
found `NOTICE` had **no attribution for the database** in a publicly
downloadable build. **#164** corrected a sixth stale row. **#165** measured the
highlight plateau: nine radial samples flat at the clip value, colour already
correct.

### ⚠ What this stretch should teach the next session

**Six rows were stale** (#144, #148, #149, #150, #151, #164). **Check the tree,
never the record.** And four things were found by *reading* rather than shipping
— #156's pin list, #158's ownership problem, #161's cache conflict, #157's
silently-ineffective pin — each of which would otherwise have surfaced as a red
gate or a **believable wrong picture**.

## Session `2026-08-03a` — the ⓘ drew perfectly and explained nothing

**Reported against a public release.** #140's icon did nothing on hover. It was
a bare `Image` carrying `.help(…)`; SwiftUI needs a hit-testable view for that,
and every other `.help` in this app — all ten — happens to sit on a `Button`, so
the pattern had never been tried anywhere else and looked obviously right.

⚠ **The defect is not the missing modifier, it is how it was signed off.** The
session that shipped it looked at a screenshot, saw the icon, and called it
done — in a repository whose stated rule is that a picture is not a test. **A
capture of a working ⓘ and a dead one are the same pixels.**

### Two gates were attempted for it, and both were deleted rather than shipped

⚠ **Walking AppKit for `NSView.toolTip`** reports **0 tooltips in a panel with
seven icons** — and **0 on the fixed build too**, so it proves nothing. SwiftUI
collapses that panel into **134 views containing 2 `NSButton`s**, and `.help`
never reaches `NSView.toolTip` at all. Written, run, shown to be worthless,
deleted. A false gate is worse than an admitted gap.

⚠ **Drawing the bubble as a `.popover` so a forced-open capture could photograph
it** fails differently: a popover is its own window and does not appear in a
capture of the hosting view. No verification bought, one more failure mode
added. Also reverted.

### What the developer's own observation settled

With hit testing fixed the glyph **highlights** on hover and **still shows no
text**. That splits it cleanly: events reach the icon, and `.help` produces
nothing here. So the text is now **drawn** in a popover rather than delegated —
a popover and not an overlay, because the nameplate is inside a scroll view and
an overlay wide enough to read is clipped exactly where the sentence ends.
`.help` is kept beside it at a cost of one line; nothing depends on it.

✅ **Confirmed working by hand, 2026-08-03**, on the third attempt — the drawn
popover appears on hover where `.help` never did. **v0.4.0-alpha.5 carries it**;
alpha.4 remains public with the broken icon and its notes describe the feature
working, which is why a release followed the fix rather than waiting.

⚠ **It stays unverifiable by the suite**, and is recorded as such rather than
quietly closed. `Engraved.Info`'s header says in as many words that the
verification for this control is a person putting a pointer on it — a control
whose correctness is invisible to the suite should say so where it lives instead
of implying coverage it does not have.

**844 / 3708 / 41 of 41 / bench exit 0 on three frames / check-decisions exit 0.**
The site's interface shot was also regenerated: it was captured 30 July under a
caption reading *"the interface as it runs today"*.

## Session `2026-08-02j` — the panel stopped explaining itself in prose

**Asked for directly:** *"add an i info icon and move all the text into the info
hover — I feel like it's kind of bad to stuff text there."*

It is. Thirty-odd helper paragraphs sat under the sliders they explained, three
lines of 10-point gray each. The panel paid for them on every draw; the
photographer read each one once.

**Fourteen sections** now carry the text on an `Engraved.Info` ⓘ at the far end
of the nameplate — **after** the hairline, so every icon lands in one column at
the panel's right edge. Before it, each would sit at the end of its own name and
the column would ripple down the panel.

⚠ **`.help` and not a popover**, and that is a narrowing rather than a shortcut:
already the idiom in ten places here, no `@State` per row, cannot get stuck open,
and **VoiceOver reads it for free**. The cost is a system hover delay and plain
text — so nothing needed *while dragging* belongs there.

### ⚠ The transform was mechanical, and two attempts were wrong

Matching the enclosing `section("…") {` by counting braces forward picks the
wrong section on nested content, and produced files that would not parse. The
correct walk is backwards from the paragraph: depth 0 on `}`, and the first `{`
at depth 0 is the enclosing brace.

What saved it was that the script **refuses and exits rather than dropping a
paragraph it cannot re-home** — the first run hit a block with no enclosing
section and stopped with the tree untouched, instead of quietly deleting prose.

Six blocks are deliberately left: interpolated status strings — `"Exporting
\(p.done) of \(p.total)…"`, a lens-profile name, a refusal — not explanations.
An ⓘ that explains nothing teaches the photographer the icon is not worth
hovering.

### ⚠ And a gate that nearly started failing for the wrong reason

`detail-tail` asserts the Detail panel **overflows** its column, and exits
nonzero when nothing does. It ran under a **1,500-point window override** chosen
when the panel's content was 1,701 points.

This change took the content to **1,147**, and the fold with it to **16 points**
— one row of margin from going red and blaming whatever touched the panel next.
The override is **deleted rather than retuned**: at the default window the fold
is **466 points**, the two frames overlap by **215**, and nothing sits unseen
between them. Better than the override ever bought.

The number that matters there is the **overlap**, not the overflow, and the
comment now says so for whoever moves panel content next.

**844 / 3708 / 41 of 41 / bench exit 0 on three frames / check-decisions exit 0**,
and all three check-scenes render.

## Session `2026-08-02i` — the queue's last item was done, and the ledger never heard

**The queue offered "Americanising the persisted keys (#89) — needs sign-off, it
rewrites sidecars already on disk."** It had shipped the day before as **#112**.

⚠ **The reason nobody knew is the finding.** #112 had **no row in
`DECISIONS.md`** — the file `CLAUDE.md` calls the record of every settled choice.
It was cited nine times, from `EditHistory.swift`, `Presets.swift`,
`ViewportTests+Sidecar.swift` and `ViewportTests+Preset.swift`, and the only
prose anywhere describing it was a comment inside the decoder. A session that
trusted the queue would have re-run a schema migration over the photographer's
sidecars — the one item on that queue with a blast radius.

**Closed by mutation, not by reading.** Deleting one `?? float(.legacyColourR)`
fallback reddens `colourR still lands on colorR — got 0.18000, want 0.44000` and
*and to exactly the same component the British one gave*. The migration is
genuinely complete, and its test is better than it needed to be: it ends by
asking `CanvasLayout.maskAlpha` on a grid of points, so a renamed key cannot pass
by landing on the default — which is exactly how this failure would have looked.

### ⚠ Five more holes, and a warning aimed at the wrong number

`DECISIONS.md` was missing **#110, #112 and #115** — and, further back, had no
row for **22, 23 or 24** either. It also carried
**two rows numbered 71** underneath a prominent header warning that two rows were
numbered **96**.

That warning was false in all three of its claims. There is **one** row 96; there
is **one** citation of #96 in the tree (`hl_mask.slang:110`); and it means the row
that exists. Its *"twelve files cite decision #96"* was **one file counted twelve
times**, because the count had swept `.claude/worktrees/` — checkouts of this
same repository. The real duplicate sat unmentioned underneath it for two days.

`git log -S` over the whole history of the file finds **no version that ever
contained** #110, #112 or #115. They were never written, not lost. All three are
now reconstructed from the code that cites them, `HISTORY.md` and the tests — and
**each row says on its face that it is a reconstruction**, because a rebuilt row
presented as contemporaneous is the same class of error as the invented constant
this repository has a sourcing rule for.

**22, 23 and 24 are declared gaps**, and are written without the `#` on purpose:
the sigil would claim a decision exists. No row was ever written and **nothing in
the tree cites them**, in code or prose — numbers skipped on 2026-07-27, not
decisions lost. Backfilling them would be invention. The uncited `71` became
**`71b`**.

### The gate, which is the actual deliverable

`tools/check-decisions.py`, in `CLAUDE.md`'s test list beside the two suites.
Fails on a duplicate number, on a `#N` the tree cites that has no row, and on an
undeclared gap. It excludes `.claude/worktrees/` — load-bearing, for the reason
above.

**Three mutations, three caught**, each exiting 1: re-colliding `71b`, deleting
row 112 (it names all nine citations back), and undeclaring gap 23.

⚠ **Nothing in `engine/` or `app/` changed this session.** The tree was already
right; the record was not. That is why the gates below are identical to `h`'s —
and running them anyway is the point, since a documentation session that quietly
breaks a build is the failure this file exists to make survivable.

**844 / 3708 / 41 of 41 / bench exit 0 on three frames / check-decisions exit 0.**

## Session `2026-08-02h` — the mask stopped travelling, and the pixel started

**The queue's last geometric item, and it turned into a deletion.** A radial mask
reached the kernel as an ellipse pushed *forward* into frame coordinates: centre
through `mask::toFrame`, semi-axes and angle through the map's derivative at that
centre — √|det J| first (#100), then the exact singular values of J·R·diag
(#128). The second removed the whole first-order error, which is all it claimed.
What was left is second order, and #136 had measured it the day before at a
**full 1.0000 of coverage** at the rim of a 0.34 mask under a vertical keystone
of 1.00, over 5.8% of the frame.

⚠ **The fix was already written, and it was being used as the ruler.** Every
number in that table came from comparing the shipping answer against the exact
one — carry each frame point out with `mask::fromFrame`, evaluate the mask as
drawn. The reference implementation *was* the answer. It had sat in the
measurement harness for a session grading an approximation it could have
replaced, and nobody noticed because it was filed under "how do I measure this"
rather than under "what should this be".

The kernel now multiplies each pixel by `mask::displayMatrix` — crop, straighten,
quarter turns and the correction as one 3 × 3, the same matrix #137's ramp
already used — divides, and evaluates the superellipse. **The centre, the
semi-axes and the angle reach the shader untransformed.** They are the numbers in
the sidecar.

### It is a net deletion, and that is the strongest thing about it

Gone from the render path: `toFrame` and `radiusToFrame` for the radial; the
angle-as-a-*delta* bookkeeping of #83, which existed only because `toFrame` had
already applied the quarter turns and `radiusToFrame` must not apply them again;
the straighten added to the ellipse's angle while the turns are kept out of it —
#83 again; and `MaskComponent::rampDen`, which was field for field the bottom row
of the matrix now being sent anyway. `testRampDenominatorIsTheMatrix` is a check
on that last deletion: the two must be bit-identical, because the kernel now
reads one where the host derives the other.

`radiusToFrame` and `lengthAlong` stay in the tree, still tested, marked in their
own headers as **not on the render path**. They are the first-order answer, kept
so the exact one can be measured against something.

### ⚠ The performance question, and the instrument that could not answer it

The item carried the one open performance question in the area: the exact answer
is a per-pixel 3 × 3 and a divide, in a kernel that already runs full-resolution
once per component, and nobody had measured it.

The first attempt to measure it came back as **pure noise** — the A/B differed by
less than the spread within either arm. The reason is worth more than the answer:
**`mask:0` appeared in `orion-bench` four times and was a *brush* every time.**
The parametric kinds that every local adjustment starts as had no timing at all,
so the A/B was measuring a kernel the change could not reach.

With a radial node profile added — permanently, so this cannot happen again —
the answer is **1.02 ms with the arithmetic and 1.07 ms with it removed**, on
24 MP. The same number twice. The pass is bound by writing R16Float over the
frame, not by anything computed per pixel. A keystone on top costs nothing
further, because the matrix is applied whether or not it is the identity —
deliberately, so there is no second code path and no branch that only the
uncommon case exercises.

### ⚠ Almost the whole new test passes on the code it replaced

`mask::displayMatrix` and `mask::fromFrame` were **both always correct**; what was
wrong was using a derivative instead of either. So a test that checks the kernel's
arithmetic against `fromFrame` — nine configurations, up to crop + two turns +
straighten + keystone + squeeze at once — is a real check of the plumbing and
says nothing at all about the defect.

What says something is the block after it: build the first-order ellipse the old
host built, and **bound the gap**. 0.0000 under an aspect squeeze — exactly
linear, so there is nothing to buy, and that row is the measurement checking
itself — and between 0.50 and 1.00 under a keystone at 1.00. The GPU case carries
the same pairing: the render matches `fromFrame`, *and* disagrees with the
first-order ellipse by more than 0.5, so the case cannot go slack without saying
so.

### Gates and mutations

**Five mutations tried, five caught**: skipping the pull-back (GPU, worst
1.0000), dropping the projective divide (0.9693), transposing the matrix
(1.0000), kind 1 reading the wrong matrix row (two checks), and the full host
revert to the push-forward construction (four scenario checks, worst 0.1219
luma). `repro/perspective-carries-the-mask.txt` went 32 → 40 checks.

**844 / 3708 / 41 of 41 / bench exit 0 on `_PIC8220`, `_PIC8095`, `_PIC8148`.**

## Session `2026-08-02g` — a gradient is a covector, and the endpoints were not

The defect #136 turned up, fixed. A linear gradient's **level sets** were wrong
under any correction that is not conformal — not its direction, which a
homography carries exactly, and not its length, which #134 made exact. Both
endpoints were right, and that is what hid it: everything anybody had thought to
check about the ramp was correct.

`mask_component.slang` kind 1 projected onto the segment between them, so its
level sets were the lines perpendicular to that segment **in frame
coordinates**. The drawn mask's are perpendicular in *display* coordinates. t is
a linear **functional** — it transforms by **J⁻ᵀ** while a pair of endpoints
transforms by J, and the two agree only where J is conformal: a rotation, a
uniform scale, a crop. Every case anybody checks by hand.

**The exact answer is cheaper than the approximation.** Pulling an affine ramp
back through a projectivity gives a ratio of two linear forms:

    t(q) = ⟨n, (q,1)⟩ / ⟨M₃, (q,1)⟩,   n = (uₓ·M₁ + u_y·M₂ − ⟨z,u⟩·M₃) / |u|²

Six floats, two dots and one divide, against four floats and one dot. M is the
whole frame → display map, so this is exact for the homography, the crop, the
straighten and the quarter turns **at once** — the mask's own numbers now go
through no transform at all, and the transform is applied to the *point*.

| | Before | After |
|---|---|---|
| `maskcheck 20 -2.0`, ramp under `perspectiveAspect 1.0` | 3 of 27 clear cells leaked, worst **0.1300** luma | clean |
| `maskcheck 12 -2.0`, same | 1 of 100 covered cells did nothing | clean |

⚠ **The squeeze, not the keystone — the opposite of §4b's instinct.** A
keystone's derivative is anisotropic but mildly so over a short ramp;
diag(1/g, g) at g = √2 is as far from conformal as this control goes. §4b spent
a session concluding the squeeze was where nothing happened.

⚠ **One mutation was green on everything.** Replacing the exact ratio with its
affine part — dropping the projective divide — passed all 32 scenario checks and
all 821 engine checks. `maskcheck` asserts that cells drawn *clear* are
bit-identical and cells drawn *covered* moved, and says nothing about the falloff
band between them, which is exactly where that error lives.
`testRampIsTheExactPullBack` closes it by checking the algebra directly against
`fromFrame` over seven configurations, and reddens 5 checks on that mutation.
**Six mutations tried, six caught** — the other five were on `displayMatrix`
(straighten aspect weighting, turn direction, composition order, straighten sign,
crop translation), all red.

Two pieces landed with it, both of which `ROADMAP.md` had costed separately:
`mask::displayMatrix` and `mask::ramp`, in `MaskGeometry.h` beside the map they
belong to. The GPU tests call `mask::ramp` rather than keeping a second copy of
the algebra, which is the rule that file already states.

⚠ The params struct grew 8 bytes and every offset from `center` on moved; the
`static_assert`s were updated with it, and six scalars were used rather than two
`float3`s because a `float3` aligns to sixteen and offset 56 does not.

Decision #137. Gates: **829** engine checks (+8), 3708 viewport, 41 of 41 repro,
bench exit 0 on three frames.

## Session `2026-08-02f` — the curvature is costed, and measuring it found a live defect

The queue's next item was the map's **curvature** across a large mask, the only
one carried as *uncosted*. Costing it meant measuring it rather than inferring it
from cell counts: carry each frame point out to the displayed picture and
evaluate the mask the photographer drew, which is exact by construction.

**Three findings, in the order they arrived. Nothing was built.**

**1. The curvature is bigger than the record said.** 600 × 600 grid, feather
0.06, roundness 2, shipping ellipse against exact:

| Correction | Mask | max ΔCoverage | mean Δ | frame >10⁻³ |
|---|---|---|---|---|
| aspect +1.0 | 0.20 round | **0.0000** | 0.00000 | 0.00% |
| vertical 0.45 | 0.34 × 0.34 | 1.0000 | 0.02832 | 5.59% |
| vertical 1.00 | 0.34 × 0.34 | **1.0000** | 0.03913 | 5.81% |
| vertical 1.00 | 0.10 × 0.10 | 0.9947 | 0.00095 | 0.25% |

The first row is the harness checking itself: an aspect squeeze is exactly
linear, so an exact derivative must be exactly right, and it returns 0.0000 and
not 10⁻⁶. "About a fifth off" was an artefact of counting *cells* — most of the
disagreement is inside cells the overlay already calls covered. Quadratic in
mask size, as second order requires.

**2. A claim shipped with #134 was false, and it had reached five documents.**
It said `perspectiveAspect` alone leaves `Placement::jac` the identity, so a
ramp changes nothing at all under a squeeze and the frames are byte-identical.
⚠ Printed directly, that Jacobian is **diag(0.500050, 1.000100)**; a 6 × 6 patch
grid moves in **7 of 36** places under the squeeze (worst 0.0003 luma) and 10 of
36 under a keystone (worst 0.0018, corroborating #134's 0.0019 by a second
route). The *observation* is real — a ramp fixture built around the squeeze does
pass with the fix reverted — and #134's conclusion survives. Only its reason was
invented, and it was written into `research/perspective.md` **by the session
immediately before this one**, which is how five copies happen.

⚠ **A wrong explanation attached to a right observation is the hard case.** The
observation keeps confirming it. It came out only because somebody printed the
matrix instead of re-reading the sentence.

**3. And there is a first-order defect in the shipping build that is not
curvature.** A linear gradient's **level sets**. The kernel projects onto the
segment between two endpoints, so its level sets are perpendicular to it in
*frame* coordinates while the drawn mask's are perpendicular in *display*
coordinates. t is a linear functional: it goes through **J⁻ᵀ**, the endpoints go
through **J**, and the two agree exactly when J is conformal — which is every
case anybody checks by hand.

    maskcheck 20 -2.0, shipping build, ramp under perspectiveAspect 1.0
      3 of 27 clear cells leaked, worst 0.1300 luma
      2 of 287 covered cells did nothing

Worst coverage difference **1.0000** under a squeeze. J⁻ᵀ alone removes about
three quarters; the residual 0.27 is the perspective divide, which makes t a
*ratio* of linear forms. The exact form is closed and **cheaper than what it
replaces** — six floats, two dots, one divide against four floats and one dot —
verified to **2.2 × 10⁻⁶** on a 400 × 400 grid across four corrections.

⚠ **Not added to `repro/` as a red section.** A repository whose gate is red
teaches everyone to ignore the gate. The four-line recipe is a comment in
`perspective-carries-the-mask.txt`, marked to become §4c when the fix lands.

Both remaining terms share a first piece — folding crop, straighten, turns and
the homography into one 3 × 3 — so `ROADMAP.md` costs them together at ~2
sessions. Decision #136.

⚠ **The tree is unchanged.** The only edits are documents; the mutation used to
measure #134 was reverted and `git status` is clean. Gates run anyway: 810 / 3708
/ 41 of 41 / bench 0 on three frames.

## The session log

⚠ **Pruned 2026-08-04**: `2026-08-02d` and `2026-08-02e` moved to
`HISTORY.md`, verified absent here and present there; **1,092 → 992 lines**
before the consolidated entry above was added. The **six most recent sessions
are above** — `2026-08-02i` (#139), `h` (#138),
`g` (#137), `f` (#136), `e` (#135) and `d` (#134). **Everything older lives in
[`HISTORY.md`](HISTORY.md)**, which is the archive and is deliberately *not* part
of the read order in `CLAUDE.md`.

⚠ **Pruned again 2026-08-02 at #139**, same rule, same proof: adding `i` made
seven, so `2026-08-02c` moved to `HISTORY.md` and its four headings were checked
by exact match to be absent here and present there before the write. **880 → 823
lines.**

⚠ **Pruned again 2026-08-02 at #138, and a prune moves rather than copies.**
Adding `h` took the count to twelve blocks, so seven moved: the six wave
write-ups `#131`, `#130`, `#129`, `#128`, `#127` and `#125`, plus session
`2026-08-02b`. **1,061 → 702 lines**, and all seven headings were checked by
exact match to be *absent here and present there* before the write was made —
7 of 7. The rule this enforces is #132's: 2026-08-01 left `2026-07-31j` in both
files byte for byte, and a duplicate is worse than a long file, because the two
copies drift and each looks complete from either end.

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

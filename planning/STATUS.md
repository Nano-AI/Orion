# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-09-05 (**The graph wanted 13.9 GiB and took the machine
down - #219.** `Pipeline::compile()` gave all 205 nodes a texture eagerly and
held them for the graph's life: **13861 MiB** on a 42 MP frame, allocated on
every open. Pooled and lazy now, **1560 MiB high-water, 8.9x**, and that
allocation was also the two-second photo swap. #158's A/B oracle stopped being a
tautology and passed - 0 of 169,559,040 samples differ. Lazy allocation exposed
a latent read of uninitialized GPU memory: `highlightStages()` handed back the
`highlights` node's never-written texture whenever `highlightRecovery` was zero,
which is the default. Also landed: the `MaskList` index-out-of-range crash, from
two `.ips` reports, and the lens-choice render leak. Seven gates green -
1012 / 4113 / decisions / gestures / screens / modes / wiring.

⚠ **Two things found and deliberately not fixed - both want the developer.**
**(1)** The night sky renders washed-out grey-green where macOS renders it
black; cancelling `kBaselineExposureEv`'s silent +1.2 EV matches macOS. But #46
fitted that value and #214 fitted the 8-stop black latitude, both against
references, and a look is not changed unilaterally - see the gap table.
**(2)** All three `samples/*.ARW` symlinks point at moon shots in
`~/Pictures/moon/`, relinked 2026-09-03 to stop them dangling. The frames #46
and #214 were fitted on - a daylight frame, a forecourt, a lamp/face/grass
frame - **are gone**, so neither fit can be reproduced or re-checked here.)

**Previously:** 2026-09-03 (**The bundle was never self-contained - #218.**
The packaged app only ran where Homebrew's OpenCV was installed, and the
script's own check said otherwise because it read `otool -L` and not
`LC_RPATH`. Rpaths swept, `@rpath` dependencies followed, walk de-quadratic-ed
from eleven minutes to 33 seconds. 98 dylibs, 32 MB dmg, zero Homebrew images
at runtime. Nothing published was affected.)

**Previously:** 2026-09-03 (**`fix/display-path` merged, and the version is
0.5.0.** Six decisions came in renumbered #211-#216 - main had already spent
#198-#210 while the branch sat unmerged. The branch's own matte-size fix was
dropped rather than recorded twice: main's #201 is the same fix, arrived at
independently, and main's code is what the merge kept. #217 is the merge's own
find - a hex colour in `HISTORY.md` prose had been readable as a decision
citation all along, and growing the ledger past 212 is what made
`check-decisions.py` finally say so. All seven gates green - check-screens and
check-modes had been reporting missing samples for a month because three
symlinks in `samples/` dangled, which is not the same thing as the frames being
absent and had been read as such three sessions running.)

**Previously:** 2026-08-30 (**The masking UX revamp - #207/#208/#209/#210.**
Masks have names (sidecar field, layer answers to its starting shape's name,
nested roster guard closes the write-only-field shape). The list is cards -
one per mask, shapes as rows with their op written on them, rename by
double-click, merge is one context-menu act with a direction. The cap is
**eight** components, bench-verified: 173 → 205 nodes, +1,375 MiB at 42 MP,
drag cost unchanged. Color-meaning sliders wear label gradients - mixer H/S/L
pinned to the shader's own ±30°, WB, presence, fringe, wheel luma - and the
wheel's luminance track arms degrade-then-refine at last.)

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

**Scope, settled by the developer 2026-08-07 (#176)** — three long-standing
entries are closed, and none of them by building anything:

- ~~**X-Trans**~~ ❌ **out of scope.** *"Not needed, stick to what camera I
  have."* The developer shoots Bayer. **ROADMAP's X-Trans section stays as a
  costed plan and is not queued**, and the CC0-frames approval it was blocked on
  is moot. ⚠ This is a *scope* decision, not a discovery: the plan is still
  correct, nobody is going to execute it.
- ~~**The flat frame**~~ ✅ **accepted as-is.** Unreproduced after
  instrumentation, both suites, the export path, `--screenshot` and ten real
  window opens. The footer says **"Not a photograph — the render is one flat
  colour, rgb(…)"** when it happens (#b3ee5a1). ⚠ **The instrument stays** — if
  it recurs, `~/Library/Logs/Orion/session.txt` is still the evidence, and it
  must be copied **before** any Orion command, CLI included, because every
  launch rewrites it.
- ~~**Patent exposure**~~ — understood, no action wanted. #174's rule stands as
  written: a citation is not clearance, and it gates the *next* filter rather
  than anything shipped.

**Still blocked on the developer:**

- **Does the brush feel fast?** The numbers say yes (#108); nobody has said so
  with a stylus in hand.
- ⚠⚠ **The licence.** `Nano-AI/Orion` is a **public repo with no LICENSE file**,
  so default copyright applies: source-available, **not** open source — nobody
  may legally fork, modify or contribute. Options put to the developer:
  **Apache-2.0** (MIT plus a patent grant — the recommendation, given #174) or
  **GPL-3.0** (nobody can ship a closed paid fork). Both are compatible with
  LibRaw's LGPL and the CC BY-SA lens data.
- ⚠⚠ **#162**, below — still the only shipped defect.

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
✅ **Cold open is measured (#151): 210.9 ms**, and for all of it the canvas held
the *previous* photograph — `isLoaded` is set once and never cleared.
✅ **Fixed 2026-08-07 (#181), and it was never the design question this file
called it.** The decision had been made and the wiring was missing:
`Engine.showPlaceholder`/`clearPlaceholder` existed, `OrionApp+Canvas` drew
`engine.placeholder` over the Metal view under a comment saying *"held while a
new photo decodes"*, and a second comment promised a runloop turn *"so the
placeholder actually paints"* — while **`showPlaceholder`'s only caller was the
screenshot harness and `clearPlaceholder` had none**. The canvas now shows the
arriving photograph's own thumbnail, taken down by a `defer` so a file that
fails to open does not leave its thumbnail over the one still loaded.
⚠ **No test can reach it** — `orion-viewport-tests` compiles zero `OrionApp*`
files (#121) — so `tools/check-wiring.py` is a grep, and its rule is the useful
half: **a harness caller does not count.**
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

**Suites:** re-measured 2026-08-30 at #210. `orion-tests` **1008 checks** ·
`orion-viewport-tests` **4103 checks** · all 0 failures. The growth since
#206: the eight-slot GPU fixture (+3 engine), and MaskLayers grouping/naming,
the nested component roster, and the track-tint endpoint pins (+44 viewport).
**Ten** `repro/` scenarios run on this machine's dog-bracket samples now (all
exit 0) - the eight from #206 plus `mask-merge` and `eight-masks`; the rest
still want `_PIC8220.ARW` / `_PIC8148.ARW`. ⚠ The bench *does* run on the
dog bracket (42.4 MP) - #209's before/after was measured there.
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
| ~~The **nib's constants are uncited** — dab spacing, hardness clamp~~ ✅ **spacing derived and measured 2026-08-07, #180** — `research/brush-nib.md`. **There is nothing to cite**: two dabs `k` radii apart dip the stroke's edge inward by `1 − sqrt(1 − k²/4)`, the hardness clamp makes the falloff band `0.02 r`, and a dip inside that band is swallowed — bounding spacing at **k = 0.398**. ⚠⚠ **The two constants are one decision** and cannot move apart. ⚠ **The margin is 9%, not 37%** — only a smootherstep's steep middle reads as an edge. Measured: **1.12 px ripple against a 2.26 px feather**. ⚠ The **hardness clamp's own value is still chosen**, but it is no longer free | `UNSOURCED.md` §17 |
| **363 commits carry `Co-Authored-By` / `Claude-Session` trailers.** ⚠ **Recounted at the 2026-08-02 prune — the row said 101, and it is 363**, because every agent in every wave since has added more. `git log --format=%B \| grep -c 'Co-Authored-By: Claude'`. Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history, and the longer it waits the larger the rewrite | whole history |
| ~~**A check names the mutation it exists to catch and does not catch it.**~~ ✅ **stale — closed by re-measurement 2026-08-07, #178, the ninth stale row.** The diagnosis was right: check 6 drives a **pure aspect squeeze**, whose Jacobian is diagonal, so `b = c = 0` and the conjugation multiplies two zeros. But **check 6b was added afterwards and does catch it** — a two-way keystone at four off-axis spots, graded against a central difference of `toFrame`'s own centres. Deleting `W⁻¹JW` from `mask::unperspective` now **fails 2 checks, worst axis 1.48 rad**, measured. ⚠ Check 6's squeeze block stays and asserts its own blindness (`b == 0 && c == 0`), so a future fixture cannot quietly go back to being diagonal | `MaskGeometry.h` |
| **The 1000-line rule is not broken anywhere.** ⚠⚠ **It was, and this row said otherwise for five days — recounted 2026-08-07 at `5038f07` (#185).** `tests_mask_geom.cpp` stood at **1,173**. The previous copy of this row read *"Over 1,000: none"* as at `191b451` on 2026-08-02, and nothing recounted it while three sessions added code — which is the failure the row's own last sentence warns about, happening to the row that warns about it. Re-swept as prescribed: `git ls-files` over all **242** tracked `.swift/.cpp/.h/.hpp/.mm/.c/.m/.slang` files, counted with `grep -c ''`, not a directory list. **Over 1,000: none**, after the split. Largest anywhere: `tests_mask_geom.cpp` **809**, `ShaderParams.h` **954**, `tests_io.cpp` **926**, `tests_highlights.cpp` **897**, `tests_mask.cpp` **881**, `tests_tone.cpp` **858**, `Engine.swift` **844**, `tests_perspective.cpp` **837**, `tests_brush.cpp` **824**, `ViewportTests+Index.swift` **809**. ⚠ **The previous copy of this row went stale within hours of being written, which is exactly what it warns about**: it recorded `tests_highlights.cpp` at **865** as at `6767716`, and `1a3083d` — *"bound the fill's weight, and say what the constant rim actually reaches"* — took that file to **897** on the same day. Every other number in it re-derived unchanged. ⚠ **`app/Screenshot.swift` was the last one over the line**, at **1,196** — 809 lines on the morning of 2026-08-02, taken over the line the same day by #125's three interface checks, and split five ways by #131 at the seam between a scene that *asserts* and a scene that *poses*. ⚠ A sweep is of **one worktree at one commit** and cannot see whatever is in flight elsewhere — it is a floor on the violation, not a ceiling. Eleven splits are done: `DevelopPipeline.cpp` 2,896→452 (#113), `Engine.swift` 2,331→795 (#117), `bench/main.cpp` 2,289→85 (#118), `tests_effects.cpp` 1,716→555 (#127), `Scenario.swift` 1,615→301 (#120), `OrionApp.swift` 1,557→299 (#121), `DevelopPanels.swift` 1,366→56 (#122), `Screenshot.swift` 1,196→315 (#131), and #129's three: `tests_brush.cpp` 1,142→824, `tests_perspective.cpp` 1,110→837, `tests_grade.cpp` 1,029→653. ⚠ **Recount by sweep before editing this row; never adjust the numbers in place** — it has carried up to four contradictory copies of itself at once, and three were collapsed into one on 2026-08-02 | whole tree |
| ~~⚠ **The whole Photo menu is unreachable from every check.**~~ ✅ **closed 2026-08-02, decision #125.** `--screenshot --scene menu` hands the process back to `OrionApp.main()` and reads `NSApp.mainMenu` — the shipping `Scene` building the shipping `PhotoCommands` — and asserts **26 commands by title**, exiting 1 and printing the whole 75-item bar when one is missing. Deleting Reset Adjustments now prints `MISSING from the menu bar — "Reset Adjustments"` and exits 1, with every frame and all 40 scenarios still green. ⚠ It asserts **presence, not firing**: the items are disabled at launch and firing one needs a photograph, a key window and focus (#110.3's shape). ⚠ It is not driven through `CullActions`, deliberately — that would be green on the mutation, which deletes the button and leaves the action | `Screenshot.swift` |
| ~~⚠ **The Compare Original menu item ships without its key.**~~ ✅ **closed — and this row was stale for five days, the eighth plan row found so (#177).** The bug was real: a `Button`'s string is a `LocalizedStringKey` whose escape character is the backslash, so `"Compare Original  (\\)"` shipped as **`Compare Original  ()`** — the one item spelling its key only in its title lost it. It was fixed with `Text(verbatim:)` in **`676d24e`**, #125's own merge, *before this row was written as open*. The menu check has been pinning the fixed spelling ever since. ⚠ **Reverting the `Text(verbatim:)` prints `Compare Original  ()` and exits 1**, measured 2026-08-07 — so the bug is reproducible on demand and the check is not decorative | `OrionApp+Commands.swift` |
| ~~⚠ **Three of the four command-line modes are checked by nothing.**~~ ✅ **all four covered as of 2026-08-07** — `--scenario` by the repro sweep, `--screenshot` by `check-screens.py` (#177), and `--library-open` and `--batch-export` by `tools/check-modes.py` (#179). ⚠ **Neither of the last two needed an oracle written** — both already asserted and were simply never invoked: `--library-open` prints **13 checks** over a cold/warm/indexless open, `--batch-export` exits 1 on a photograph that fails. ⚠⚠ **A deleted dispatch does not make Orion exit, it makes Orion open a window**, so both gates catch it by **timeout**, not exit code | `OrionApp.swift` |
| ~~**`Engine.lastFailure` is pinned, the line that displays it is not.**~~ ✅ **closed 2026-08-02, decision #125.** `--scene render-failed` plants the failure **and suspends the engine** — laying the interface out renders, and a successful render clears the value, which wiped the first attempt and photographed the ordinary hint — so the amber "Render failed — …" line is in a byte-compared frame. Deleting the branch changes the frame; `nofailure` stays green on the same mutation, which is exactly the distinction: it pins the state, this pins the line | `Screenshot.swift` |
| ~~⚠ **The three interface checks are run by hand.**~~ ✅ **closed 2026-08-07, #177.** `tools/check-screens.py` runs all three and is in `CLAUDE.md` beside the other four. ⚠ **`render-failed` had to be given an oracle first** — it exited 0 whatever the footer did, because its check was two PNGs and a person. It now renders its own control in-process, so no reference image is on disk. ⚠⚠ **And the first version of that comparison did not catch its own mutation:** deleting the footer's warning line left it green, because the readout beside the dimensions also reads `lastFailure` and still switched to `failed`. It now compares two frames that both carry a failure and differ only in its **text**, which the readout renders identically. All three mutation-tested through the gate. ⚠ **Still nobody's gate: the other ~35 scenes**, which pose rather than assert | `Screenshot.swift` |
| ~~**One screenshot scene is not byte-stable, so it cannot be an oracle.**~~ ✅ **closed 2026-08-07, #178.** A fixed instant in the harness — `Screenshot.epoch` — not in the product, since the panel is right to print when a version was taken. ⚠⚠ **The obvious check for it went green on the mutation:** rendering twice and demanding agreement catches this about **one run in twenty**, because `.short` time style has *minute* resolution and two renders seconds apart share a minute. The deterministic catch is `assertVersionsDoNotShowTheClock` — the rows must be years old, not seconds old. The two-render check is kept for what it alone sees (a random id, an unsettled layout, a late thumbnail). ⚠ **Stable across runs, not across machines** — the string still goes through the machine's locale and time zone | `Screenshot+Scenes.swift` |
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

## Session `2026-09-03b` - the bundle was never self-contained, #218

**Asked for directly, and the answer was no:** *"how do i send it to people?"*
You could not. The packaged app only ran on a machine with Homebrew's OpenCV
installed, and `package-app.sh` printed **"verified no paths outside the
bundle"** while producing it.

**Four faults, each hiding the next.** The binary's rpath list carries
`/opt/homebrew/opt/opencv/lib` ahead of `@executable_path/../Frameworks`, so
the shipped app loaded OpenCV from the Cellar, which pulled a second `libomp`,
and it died on the first raw with `OMP: Error #15`. The script deleted one
rpath by name — LibRaw's, from when LibRaw was the only dependency — and OpenCV
arrived in `49bac4b` without telling it. The dependency walk followed only
absolute Homebrew paths, so `@rpath/libopencv_dnn` was invisible and 92 of the
98 needed libraries were never copied; it ran anyway because the stale rpath
caught every miss. And the verification read `otool -L` only, which shows what
is referenced and nothing about where `@rpath` looks — half the mechanism,
reporting success for the exact failure it exists to catch.

**The size argument was had with the wrong number first.** `du` on
`/opt/homebrew/opt/openvino/lib` reads 106 MB and pointed at a 300-400 MB app,
which is why dropping `opencv_video` looked necessary. The real dependency
closure is **98 dylibs, 82 MB** — OpenVINO contributes 25 MB and the rest of
that directory is plugins nothing references. ⚠ **Measured before decided.** The
bundle ships whole at **32 MB compressed**; `Align.cpp` keeps its ECC
refinement, which it already treats as best-effort inside a try/catch.

⚠ **The fix contained a fifth fault of the same family.** Marking a library seen
when it came *off* the queue rather than when it went *on* made the walk
quadratic: at 98 libraries it held 100% CPU with no child processes for eleven
minutes, which reads as a hang and is arithmetic. Bounded at enqueue, the same
walk runs in **33 seconds**.

**Verified by running it, not by reading it:** zero `/opt/homebrew` images under
`DYLD_PRINT_LIBRARIES`, 13 of 13 library checks, a real 7968x5320 render, and
`--hdr-merge` — the OpenCV path itself — writing a 243 MB DNG.

**1012 / 4113 / check-decisions 0 / check-gestures 0 / check-screens 3 scenes /
check-modes 13 checks + 2 exports + a merge / check-wiring 0.**
**Published**, by developer instruction, as a **pre-release**:
`https://github.com/Nano-AI/Orion/releases/tag/v0.5.0-alpha.1`, 33.7 MB.
⚠ Verified the way the fix was: the asset was **downloaded back**, checksummed
byte-identical to what was built, mounted, and run from the read-only image -
13 of 13 library checks, signature valid. The tag was force-moved from the
version-bump commit to the packaging fix, since checking out the old one gives
a script that builds a bundle nobody can run; safe because no release object
had consumed it yet.

⚠ **`web/index.html` still points at `v0.4.0-alpha.6`** and is deliberately
left there. `v0.4.0-alpha.6` also remains flagged Latest. The new build is a
pre-release, so the site's only call to action should not lead to it until the
developer says so.

---

## Session `2026-09-03` - `fix/display-path` merged, six decisions renumbered, #217

**Asked for directly:** pull main, merge what was ahead, rebuild, bump the
version and push - a build to record video against.

**What was actually ahead was one commit and a month of drift.**
`fix/display-path` carried `0f4bee7`, two sessions of work committed on
2026-08-25 but *done* on 2026-08-10 and 2026-08-14. Main moved twenty commits
past it in the meantime. Six files conflicted and the interesting ones were not
the code.

**Two conflicts were the same bug fixed twice, and main's fix won.**
`Engine+Render.swift` and `SubjectMatte.swift` both carried the 42 Mpx subject
selection fix - the matte's size having two derivations, one truncating and one
rounding, disagreeing by a row at 7968 x 5320. The branch filed it as #200 on
2026-08-14; main reached the same conclusion independently as #201 on
2026-08-27 and went further, moving the arithmetic into
`MatteGeometry.analysisSize` and returning the turn count from
`renderForAnalysis` rather than having the caller name it. ⚠ **The branch's
`MatteGeometry.previewSize` no longer exists**, so taking either side was not a
style question - the branch's code would not have compiled. Main's side taken
whole, the branch's #200 row dropped rather than written twice.

**The other four merged as additions, because they were.**
`app/CMakeLists.txt` wanted both new sources (`CanvasReduce.swift` from the
branch, `FrameDisplayMap.swift` from main) in the viewport-tests target, not
one. `OrionApp+Commands.swift` was two independent growths of the same struct -
the branch's `exportAll`/`batchCount` and main's gallery and trash commands -
and `CullActions` had already auto-merged to hold all of both, so the
initialiser just needed its arguments back in declaration order.

**The ledger numbers collided and the branch renumbered, #211-#216.** Main
spent #198-#210 between 2026-08-25 and 2026-08-30; the branch had spent
#198-#204 on dates a fortnight earlier. Main is pushed, so the incoming rows
moved: #198→#211 (the `CanvasTool` enum and `ToolButton` template), #199→#212
(M6 committed scope), #201→#213 (the canvas decimating instead of averaging),
#202→#214 (AgX's black latitude), #203→#215 (bulk export on the File menu),
#204→#216 (a reject is never batch-exported unasked). Citations moved with them
across `FEATURES.md` (13), `ROADMAP.md` (2) and the three session records, which
went to `HISTORY.md` where sessions that old belong. ⚠ **The branch's #200
citations point at main's #201 now**, not at a renumbered row - the decision
they refer to is main's.

**#217, which the merge found rather than caused.** Growing the ledger to 216
turned `check-decisions.py` red on a sentence nobody had touched: `HISTORY.md`
records a marker stroke's unlit text as holding the colour `#262c30`, and the
citation regex has always read that as a reference to decision 262. It stayed
green only because of the guard `if n > max(numbers) + 50: continue` - at 210
rows the ceiling was 260 and 262 sat outside it. At 216 rows the ceiling is 266.
⚠ **Fixed in the regex, not in the archive**: `HISTORY.md` opens by saying
nothing in it is edited, and quietly rewriting a recorded colour to appease a
checker is the damage that promise exists to prevent. Hex-shaped tokens are now
struck from a line before citations are read, and **only those carrying a hex
letter** - a pure-digit `#204` is still a citation. The blanket rule ("never
followed by a word character") was rejected because it would also silence
`#71b`, the letter-suffix spelling this same script recommends for a duplicate
row. Swept: exactly one token in the tree is affected either way.

**Version 0.5.0**, from 0.4.0, by developer instruction - `project(Orion
VERSION)` in `CMakeLists.txt`, which the plist template reads. Ninety-two
commits since `v0.4.0-alpha.6` on 2026-08-02, among them gallery mode, the
deletion path, HDR merge, snapshots and the eight-slot masking revamp. Tagged
`v0.5.0-alpha.1`; the prerelease suffix stays in the tag, per
`tools/package-app.sh`.

**Also swept up:** `.claude/` is in `.gitignore` at last. `scheduled_tasks.lock`
had reached two commits, and `.claude/worktrees/` holds whole checkouts of this
repository - which is why `check-decisions.py` and `check-wiring.py` already
skip that path by name.

**All seven gates ran and all seven are green** - the first time in a month, and
the reason is worth writing down because three sessions in a row recorded the
wrong cause.

**check-screens and check-modes were never short of samples. The symlinks were
dangling.** `samples/` holds three of them, made on 2026-08-02 and pointing into
`~/Pictures/July 25`, `~/Pictures/Rejects` and `~/Pictures/Cars july 25th` -
folders since renamed or emptied. ⚠ **A dangling symlink is not a missing file
and `ls` will not tell you which you have**: the entry is listed, `ls -la` shows
it, and only `Path.is_file()` or `ls -L` says it resolves to nothing. Both gates
check with `is_file()`, correctly reported "no sample photograph", and every
session since 2026-08-14 read that as "the frames are gone" and moved on. The
frames *were* gone; the fix was never to find them, only to point three
symlinks at any raw that decodes. Repointed at `~/Pictures/moon/DSC0950{2,4,6}`.

⚠ **`samples/` is gitignored, so this repair is local and the next machine
starts dangling again.** Also: `check-modes.py` keeps `_PIC8095.ARW` out of
`EXPORTS` because that frame has people in it, and on this machine the name now
resolves to a moon. The comment records intent about a *frame*, so leave it
standing - but the names no longer describe what they point at here, and the
replacements were picked as moon shots so the privacy rule cannot be broken by
the substitution either way.

**1012 / 4113 / check-decisions 0 / check-gestures 0 / check-wiring 0 /
check-screens 3 asserting scenes green, versions byte-stable / check-modes
--library-open 13 checks, --batch-export 2 files at 2635 KB, --hdr-merge a
243 MB DNG.** **Still owed by the developer:** a stylus verdict on the brush,
and the trailer-stripping history rewrite.

---

## Session `2026-08-30` - the masking UX revamp, and gradient tracks

**Asked for directly:** commit the gallery session conventionally, then a much
more polished masking experience - named masks, easy merging (subtract), a
list that says how many masks exist and what each does - and gradient tracks
on the hue sliders and wherever else they make sense. Cap raise to 8 and the
gradient scope settled by Q&A. Decisions #207-#210, four stories, each
committed green.

**Names (#207).** `MaskComponentState.name` through all three halves of the
decoder trap (struct, `Key`, `init(from:)`); layer's display name = its
starting shape's, defaults like "Radial 2" / "Sky 1". `MaskLayers` (pure,
SwiftUI-free) is now the one grouping definition - `Engine.selectedLayer`,
`layerCount`, the cards and `masklayer` all read it. ⚠ The real win is the
**nested roster guard**: `testMaskComponentRoster` applies #110's Mirror rule
to the nested struct whose write-only-field failure (rangeLo/Hi/Soft, five
sessions) motivated it.

**Cards and merge (#208).** `DevelopPanels+MaskList.swift`: header (rename on
double-click - draft state, submit commits, escape abandons), layer-wide eye,
shape rows with op glyphs, "N masks · M of 8 shapes" always visible.
"Subtract from mask above" is `mergeIntoLayerAbove` - `setLayerBreak(false)`
plus compose in **one** undoable act, closing #197's gap where a fresh
two-mask stack showed no compose control at all. Add menu gained "Into the
selected mask". Verbs `maskname`/`maskshape`/`maskmergeup`;
`repro/mask-merge.txt` pins rename-moves-nothing, subtract-cancels-exactly,
split-restores.

**Cap 4 → 8 (#209).** Three constants, the shader's hand-branched
`mask4..mask7` bindings, `LinearAdjust` 320 → 480 both sides, `toTuple8` for
the nine `local_*` fills, and the `{-1, -1, -1, -1}` initializers that would
have **zero-filled** slots 4-7 (0 is a valid node index). ⚠ Nothing in the
tree exercised past slot 3 - `testMaskEightSlotsGpu` and
`repro/eight-masks.txt` now do, GPU and app path respectively. **Measured:
173 → 205 nodes, 12,567 → 13,942 MiB at 42.4 MP, exposure drag 4.54 ms and
still 3 nodes, A/B bit-identical.** #152's ceiling was already breached
pre-change; the lazy-allocation shrink option is recorded in the row.

**Gradient tracks (#210).** `TrackTint` - labels, not renderings; mixer Hue
ends are the shader's own centers ∓30° and pinned; moderated below the
swatches per #63; the throw reveals the gradient at full strength in place of
the accent bar. Catalogue rows tint through `TrackTint.forAdjustment` so the
spec stays SwiftUI-free. Judged from a rendered frame, not reasoned - 0.28
base opacity was too faint, shipped at 0.45. Drive-bys: `ColorWheel`'s
luminance track never armed degrade-then-refine (now does, plus its tint);
`Palette.rail`'s stale doc fixed in `tokens.json` and regenerated.

**Verified:** suites 1008 / 4103, 0 failures; ten sample-runnable repros exit
0; check-decisions/-gestures/-wiring exit 0; check-screens/-modes exit 2 for
want of `_PIC` samples (pre-existing). ⚠ The ~35 posed screenshot scenes
shift wherever a tinted slider or the mask panel is posed - the three
asserting scenes are untouched (detail-tail photographs the Detail panel,
which carries no gradients; geometry unchanged everywhere). Sessions
`2026-08-24c` and `2026-08-24b` moved to `HISTORY.md` at this prune.

## Session `2026-08-28` - the gallery, and the first deletion path

**Asked for directly:** one view of all the photos to rate/reject them, delete
one and delete all rejected (both confirmed), optimized - embedded previews,
never the raw - but big enough to judge framing, and **not** the default view.
Decisions #204 (the mode), #205 (thumbnails), #206 (trash).

**The gallery (#204).** `EditorMode { develop, cull }` on `Editor`; bare `G`
(the local monitor), a toolbar chip and "Gallery  (G)" in the View menu toggle
it; the grid replaces canvas+tools+filmstrip below the toolbar. The focus is
its own `@State`, never `current` - browsing moves a ring for free and only
Return/double-click pays the ~210 ms decode. Arrows are 2D through pure
`GalleryLayout.move` (clamped, ragged-last-row aware; the live column count is
written back from layout); 1-5/`/R act through `cullScope(focus)`, the same
scope as the menu and strip. Cells: 3:2 letterboxed (framing is the question,
so never cropped), 200-400 pt slider (`@AppStorage`), real stars, the strip's
two-ring selection language, its context menu plus Open in Editor and Move to
Trash. Develop-only commands grey in gallery; `0 9 \ [ ]` are swallowed.
`PhotoSelection` reused verbatim - shift/cmd click ranges work day one.

**Thumbnails 512 → 1024 (#205).** Schema 2 → 3 (pixels changed, stamps did
not - one cold rebuild), budget 512 MB → 1.5 GB,
`testGallerySliderConstantsHoldTheirOrder` pins `maxCell * 2 <=
thumbnailLongEdge`. ⚠ Decoded-side memory (~2.8 MB/frame in `Library.photos`)
is **unmeasured on a big folder** - two sample frames here; 768 is the
recorded fallback if a real shoot objects.

**Deletion (#206).** `TrashPlan.plan` (pure, listing-driven) decides what
travels: `BASE.xmp`, `BASE.orion-snapshots.json`, `BASE.orion-matte-*.png` -
the `IMG_1`/`IMG_10` prefix trap is pinned. `Library.trash` moves the raw
first, per photograph; sibling failure never stops the batch; one complaint
sentence, never a dialog per frame. `runTrash` stops autosave *before* files
move (a coalesced write would resurrect a sidecar beside a trashed raw),
re-arms it if the raw refuses to move, computes canvas/focus survivors before
mutation, and lands an emptied folder on the empty gallery. Index untouched
by design - `plan` prunes on next open, stale rows miss on their stamps.
Entry points: ⌘⌫ "Move to Trash…" over `cullScope`, "Delete Rejected
Photos…" folder-wide with the count in the alert title, the gallery header
button and context menu - one `pendingTrash` alert in the sync-confirm shape.

**Verified:** suites 1005 / 4059, 0 failures; all eight sample-runnable repros
exit 0; check-decisions/-gestures/-wiring exit 0; the menu scene reads **29 of
29** commands (three new titles asserted); `--library-open samples` rebuilds
cold at schema 3 and passes all 13 checks warm; `--screenshot --scene gallery`
(new posing scene, via the `startMode:` seam) renders the grid with letterboxed
cells, greyed edit chips and a disabled Delete Rejected. check-screens/-modes
still exit 2 for want of `_PIC` samples (pre-existing). ⚠ **Owed:** a real
hand-driven trash of a photograph - the `FileManager.trashItem` calls have no
automated coverage (a test that trashes real files would be its own hazard),
so the first delete on a real shoot should be watched: raw+xmp+json together
in the Trash, and put-back restoring an editable photo. ⚠ The ~35 posed
screenshot scenes are untouched (the gallery is a new scene, not a change to
any existing one). Sessions `2026-08-24` (HDR merge) and `2026-08-07b` moved
to `HISTORY.md` at this prune.

## Session `2026-08-27` - masks anchored to the image, and detection un-broken

**Both reported directly by the user, both root-caused before any fix.**

**Subject/sky detection never worked on a full-frame body (#201).** The
analysis render sized itself with round-to-nearest while the engine sizes its
matte texture with floor division: 1024x684 against a 1024x683 allocation on
a 7968x5320 a7R III, and `setMaskMatte` rejects over-allocation by design.
Exact 3:2 sensors trip the same half-pixel, and the one test in the area
carried a +-1 tolerance that specifically absorbed it. The size now comes
from `orion_engine_max_matte_size` - one source of truth - and
`renderForAnalysis` returns the EXIF-only turn it rendered under, closing a
latent un-turn-by-the-wrong-turn bug on rotated photographs. Verified on the
reporter's own files (subject 21.5%, sky 25.3% covered) and pinned by
`repro/subject-selection-42mp.txt` / `sky-selection-42mp.txt` on the
dog-bracket samples, which are the exact camera that always failed.

**A mask warped with the crop (#202, #203).** Parametric masks were stored in
display space - normalized against the *live* crop - and the kernel folded
the current geometry in per apply, so every later crop, straighten, turn or
keystone re-aimed every mask at new image pixels. `research/masking.md`'s
"Same result" departure note was the claim that hid it (corrected in place),
and the overlay moved with the render, so the maskcheck oracle was blind by
construction. Masks now live in frame coordinates like spots and mattes:
the kernel applies no geometry at all, the gesture layer converts once at
the boundary through the engine's own `orion_engine_display_map`, and a
display-space-era sidecar converts once at load under its own persisted
geometry, gated by the `maskSpace` marker (#112's rule; absent means legacy
wherever masks exist). Free wins, both pinned: geometry ticks no longer
re-stamp four full-resolution mask nodes, and a straighten no longer
re-uploads every brush stroke. E2E: `repro/mask-follows-the-frame.txt` holds
exact-pixel patches across a turn and a crop placed *after* the mask - the
crop check read 0.3130 -> 0.5248 before the fix and is bit-identical now -
and `repro/mask-survives-the-fix.txt` watches the sidecar migration round-trip
a real file, mutation-tested by skipping the migration call.

⚠ **Not runnable here:** `mask-alignment.txt` and
`perspective-carries-the-mask.txt` are reinterpreted by the convention change
(their headers say how) and want `_PIC` samples this machine lacks - their
first run elsewhere should re-read the cell counts. The ~35 posed screenshot
scenes shift wherever a mask is posed under geometry. Out of scope, noted:
`renderForAnalysis` still does a full-resolution RGBA16F readback plus a
per-pixel Swift loop on the main actor, so Subject on 42 MP blocks the UI
for a beat even now that it works.

**1005 / 4027 / eight sample-runnable repro scenarios exit 0 /
check-decisions, -gestures, -wiring exit 0 / check-screens, -modes exit 2
for want of `_PIC` samples (pre-existing). Largest file in the tree:
`tests_mask.cpp` at 955.** Still owed by the developer: a stylus verdict on
the brush, and the trailer-stripping history rewrite.

## Session `2026-08-25` - shift-locked crop, and the sideways portrait bug

**Asked for directly, both:** Shift while dragging a crop corner should hold
the ratio the rectangle had when the drag began (like every shape tool); and
portrait photos rendered sideways in the filmstrip, then painted the canvas
landscape before snapping upright on open.

**The crop lock (#198).** The drag arithmetic left `CropOverlay` for a pure
`CanvasLayout.cropDrag(handle:start:dx:dy:lockAspect:)` per the `maskDrag`
pattern - #110.3 means the pure function is the only coverage a drag can
have. The locked solve anchors the opposite corner, lets the dominant axis
win, then clamps aspect-aware (minimum and frame room in an order that
cannot conflict), so the result is a **fixed point of `clampedCrop`** - the
engine's per-axis clamp never gets to break the ratio. Shift is
`NSEvent.modifierFlags` polled per tick (the `AnalogTrack` precedent), so
pressing or releasing it mid-drag snaps on the next movement. Seven checks
in `ViewportTests+Crop.swift`, mutation-tested (a 2% ratio skew reddens the
suite). ⚠ No repro scenario, deliberately: scenarios reach `setCrop`, never
the drag layer, and `controlValue` getters nobody asserts are forbidden by
that function's own comment.

**The portrait bug (#199), one root, two symptoms.** An ARW's embedded
preview JPEG begins `FF D8 FF DB` - **no EXIF segment at all** - so
`shrink`'s `kCGImageSourceCreateThumbnailWithTransform` (whose comment
claimed the tag "is stored" with the preview) was a no-op, and
`extractThumbnail` discarded the actual source, LibRaw's `sizes.flip`. Now
the flip rides `extractThumbnail`, crosses the facade as a nullable
`int32_t* out_turns` on `orion_read_thumbnail` (quarter turns, not LibRaw's
private vocabulary; an `OrionRawInfo` field was rejected - info and
thumbnail are read at different times, and routing through `readInfo` would
cost a second LibRaw open per thumbnail), and `PhotoIndex.shrink` bakes it
into the stored pixels. ⚠ **The tag wins where one exists**, or formats that
tag their previews *and* report a flip would turn twice. `quarterTurnsFor`
is one declared symbol in `raw/RawImage.h` now, used by the geometry node
and the thumbnail path alike; `testOrientation`'s "mirror kept in step"
checks the real function. **`schemaVersion` 1 → 2** evicts every cached
sideways thumbnail (the SQLite cache, never a sidecar; one cold rebuild).
The placeholder flash fixed itself: `showPlaceholder` (#181) draws the same
thumbnail, now upright. Verified end to end on this machine's two portrait
sample ARWs: the facade returns turns=3 (flip 5), and a real `--open
samples` launch rebuilt the index at version 2 holding a 288x512 thumbnail
where version 1 held 512x288.

**The strip's cells follow (#200).** Upright thumbnails exposed the fixed
`cellHeight * 1.5` gate: `.fill` chopped a portrait to a landscape band of
its middle. Cells now take their picture's aspect at the strip height,
clamped 0.5...2.0 (letterbox rejected - bars inside a film gate read as part
of the photograph). Checked by eye on a rendered `--screenshot` frame: two
tall narrow cells, whole frames visible, sprockets and gates aligned. ⚠ The
~35 posed scenes that show the strip all shift; the three asserting scenes
do not assert strip layout.

**1009 / 3940 / four sample-runnable repro scenarios exit 0 /
check-decisions, -gestures, -wiring exit 0 / check-screens, -modes exit 2
for want of `_PIC` samples (pre-existing).** Still owed by the developer: a
stylus verdict on the brush, and the trailer-stripping history rewrite.

# Orion — Status

**Update this at the end of every session.** It is the recovery point when
context is cleared.

⚠ **Before you write: check the top of this file for a second copy of what you
are about to add.** The header is one `Last updated` line, one `Phase:` block
and one queue. That rule has failed three times — duplicated blocks were removed
on 2026-08-01 and had grown back by 2026-08-02, because each session appends to
the top without reading what is there, and appending is invisible in a diff that
is already long.

⚠️ **M3 is done — do not rebuild it.** Dehaze, creative LUTs, exposure fusion
and auto-enhance all shipped with research files, GPU tests and bench probes.
A stale kickoff prompt naming those four has arrived **36 times**; the answer
each time is that they exist, and each now has something that fails when its
*wiring* breaks. The cost table is below.

---

**Phase:** M0 done. **M1 complete.** M2, **M3 and M4's geometry complete.**
`research/masking.md` is finished — six mask kinds, a mask is a *list* of
components folded per §6, optionally feathered onto the photograph's own edges,
through the graph, the POD facade, the panel rows, the sidecar, undo and the
bench.

**Last updated:** 2026-09-09b — a design pass over the landing page, on
branch `web/design-pass`. Two decisions (#241, #242), no engine change and no
app change: the page's honesty block stopped being its ugliest block, two
scroll-theatre devices were retired, and the footer became an exit instead of
two dead strings. The session before it was the develop panels (#234-#240).

**Recent sessions** — full write-ups below, older ones in `HISTORY.md`:

| Date | What landed |
|---|---|
| 2026-09-09b | **The landing page, reworked against four taste skills** — audit first, targeted evolution, no rewrite. Nineteen hairlined register rows became six groups in a grid that fills at every breakpoint, all 22 feature strings byte-identical; the footer gained the four routes out of the page; the frame counter and the hero's scroll cue retired, taking an `IntersectionObserver` and 49 lines with them. Zero em-dashes, `rel=canonical`, 44-point targets, `prefers-reduced-transparency` — #241, #242 |
| 2026-09-09 | **The develop panels, quieted.** ~200 tick marks and 40 knurled thumbs cut back to what a camera scale actually needs; the mixer swatches and the three grading wheels brought under #63's chroma rule, which they had never been inside; one typographic register (engraved names, sentence-case buttons); section rhythm 26/11/9; PRESETS stopped clipping. Two a11y fixes no check can see: five icon-only toolbar buttons had no spoken name, and the swatches had 16-point targets — #234-#240 |
| 2026-09-08b | The orange/yellow saturation deficit (and red/magenta excess) fitted into the table's own 90 hue bins rather than another region — mean band error **13.6% → 6.3%** over 12 damped rounds; red and magenta, coupled to the bands being pushed up, land at 20%/13% and were still improving when the fit stopped. `tools/huesatfit.py` is the new instrument — #232 |
| 2026-09-08 | Two photographs on screen at once, fixed by blanking to neutral instead of a thumbnail; the open photograph's filename now drawn in the footer — #233 |
| 2026-09-06 | The colour complaint was white-balance/matrix, not AgX. A second `HueSatMap` region for warm/earth tones: mean hue error **9.1° → 2.2°**, 5 frames, 14 patches — #225 |
| 2026-09-05 | The graph wanted **13.9 GiB** and took the machine down. Pooled and lazy: **1560 MiB high-water, 8.9×**; also the two-second photo swap, a `MaskList` crash and the lens render leak — #219 |
| 2026-09-04 | Whites reached the data — its band sat at +5.5 EV over middle gray against a pipeline maximum of **+3.674** — #221. The washed-out night render did not reproduce; the premise was a UI screenshot compared against a `sips` render — #220 |
| 2026-09-03b | The bundle was never self-contained: the check read `otool -L` and not `LC_RPATH`. 98 dylibs, 32 MB dmg, zero Homebrew images at runtime — #218 |
| 2026-09-03 | `fix/display-path` merged, six decisions renumbered #211–#216, version **0.5.0** — #217 |

---

## Open

**The queue is empty.** Every item it carried is shipped. This is the **third**
time it offered already-shipped work as the next story (#135 found two, #139
found the third), and the third time the *decision that closed it* had never
been written down, so a session that trusted this file would have re-run a
schema migration over the photographer's sidecars. `tools/check-decisions.py`
is what stops the fourth.

**So there is no next story queued, and picking one is your call.** Uncosted,
from `ROADMAP.md`: Core ML denoise (research landed under #111, explicitly not
built), Windows port, DCP profiles. X-Trans is out of scope (#176).

| # | Open | State |
|---|---|---|
| 1 | ~~**Chroma is at 0.83** of the camera JPEG~~ | ✅ **closed 2026-09-07, #229 — it was never a defect.** Measured on eight sidecar-free frames against two references: **Orion/camera 0.778, Apple RAW/camera 0.796, Orion/Apple 0.978.** Apple sits as far below the camera JPEG as Orion does, because a camera JPEG carries Sony's Creative Style and `Contrast: High` and a neutral RAW render carries neither. Five sessions hunted a bug inside a look difference |
| 2 | **High-key desaturation, 14-18% below Apple** | ⚠ #230, the one saturation finding that survives. Six of eight frames put Orion at 0.98-1.04 of Apple; `DSC09747` reads **0.861** and `DSC09749` **0.817**, and those two are the brightest (luma 0.69/0.75 against 0.33-0.66). Cause undiagnosed. **Do not widen it into #225's old shape** — two frames, one reference, bright content only |
| 3 | **`Engine.contrast = 1.45`** | Unchanged and still yours. #46 co-fitted it with the baseline exposure, so lowering it moves every photograph. ⚠ Exposure is **not** the problem: Orion is within **±0.08 EV of Apple on all eight frames** (#229) |
| 4 | **The default look, if you want the camera's** | ⚠ **Partially built, 2026-09-08, #232.** A fitted 90-bin saturation curve closes most of the per-hue gap (mean band error 13.6% → 6.3%) but not all of it — red/magenta land at 19.6%/12.8%, still converging when the fit stopped, coupled to orange/yellow through gamut-boundary clipping. Still a camera-matching decision, not a correctness fix — #229 |

⚠ **(3) no longer gates anything, and the frames were never the blocker.**
**Every RAW carries the camera's own JPEG inside it** (`extractThumbnail`,
`LIBRAW_THUMBNAIL_JPEG`), so any photograph is its own reference and #229 was
measured without adding a single sample. The `samples/*.ARW` symlinks are still
three moon shots, and `bench_controls.cpp`'s floors are still fitted at
contrast 1.0 — but a measurement no longer waits on either.

⚠ **Closed 2026-09-08, #233 — the placeholder swap no longer shows a picture at
all.** It used to paint the camera's embedded JPEG and clear it ~210.9 ms later
(#151), so a photograph shifted from Sony's punchy rendering to a neutral one
at ~0.78 its saturation on every open — reported as the picture "washing out".
`Engine.isOpening` now blanks the canvas to `Palette.surround` instead of
drawing anything over it, so there is no second look to see. ⚠ **Saved
sidecars made the old symptom look far worse**: `DSC09734` carries
`exposureEv −1.96`, `DSC09742` −2.73, `DSC09752` −3.22 with blacks and whites
pinned at the slider limits. Orion applied them correctly, and they were
mistaken for a renderer defect twice in one session before this fix — an
argument that still stands for showing the edited state on open sooner than
~210 ms in.

**Also still on you, carried forward:** *does the brush feel fast?* The numbers
say yes (#108); nobody has said so with a stylus in hand.

---

## Where the counts stand, and the one gate that flakes

**All seven green, measured 2026-09-08b (after #232 landed):**
`orion-tests` **1034 checks** (was 1029 — +5 from `testHueSatMapGpu` §6, the
new `satCurve()` section) · `orion-viewport-tests` **4113 checks**, unchanged
· both 0 failures · decisions (231 rows, 3 declared gaps, 192 cited — #232's
row resolves the citation #233's own session flagged as red) · gestures (6)
· screens (3 asserting + 1 byte-stable) · modes (`--library-open` 13 checks,
`--batch-export`, `--hdr-merge`) · wiring (1 declared, 414 swept, 9
harness-only — unchanged by this session; `showPlaceholder`/
`clearPlaceholder` moved there by #233's session, not this one).
⚠ **`testCreativeVignetteGpu`'s corner-spread threshold moved 4.0 → 5.0,
decision #232** — the fitted curve reaches a demosaic-edge color cast in
that fixture's corners (hue ~330°, sat ~0.29) that both narrow regions
before it missed; measured spread 4.333, still 1.7% of the 8-bit range.
Confirmed by reverting only `HueSatMap.h`/`DevelopCapture.cpp` and
re-running: the failure disappears, so it is this session's curve, not
#233's concurrent Swift changes (which touch no engine code) or a
pre-existing flake.

⚠ **Re-measure; never adjust these in place.** This block has carried up to
*four* copies of itself at once with four different numbers, and the three most
recent copies read 806/3708, 1008/4103 and 1012/4113. The suites only ever grow,
so a stale number here reads as a regression.

⚠ **Both suites fail inside a sandboxed shell** — `no Metal device available`,
and the viewport suite dies on blocked file writes. That is the sandbox, not the
code. Run them with it off.

⚠ **The M0 bench gate is a wall-clock threshold and it therefore flakes**, in
both directions, and it cost four sessions before it was named (#116). Eleven
runs of one binary once spread 8.83 → **30.42 ms** with `CoreSpotlight` at 99%
CPU indexing the sample folder. A p95 is only meaningful next to one taken
minutes away from it: **compare paired runs or do not compare**, and do not
chase this number on a busy machine.

---

### Known gaps, carried forward

Small, named, and none of them blocking the next story:

| Gap | Where |
|---|---|
| ⚠ **The check floor says a file measured something, never that it measured the right thing.** #124 takes M4 (one verb family claiming every verb) from 39/40 exiting 0 to 3/40. Two survivors are the declared instruments. The third, `snapshot-keeps-its-matte.txt`, keeps 3 real checks under the mutation because `snapshot missing` is implemented by the very family that claims everything — a floor cannot see that. The oracles that *can* are the byte comparison of rendered frames and the full-output diff, and neither is what the gate runs. Raising the floor would not help; the residual is a coverage shape, not a threshold | `repro/` |
| ~~**The session log replay now exits 1.**~~ ✅ **already closed in the tree, found stale at the 2026-08-02 prune.** `InteractionLog.start()` writes `minchecks 0` into the header it emits, with five comment lines above it saying why (`InteractionLog.swift:65-78`) — so a replayed session log asserts nothing and exits 0, which is the one workflow it exists for. The row said the header "owes" that line and that it was left for whoever owns the file; whoever owns it had already written it | `InteractionLog.swift` |
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero. ⚠ It was **not** bounded until 2026-08-01 — on a photograph with no sidecar the sweep could never run at all, and 26 orphans had piled up beside one sample frame. Decision #87. ⚠⚠ **Recounted 2026-09-07: there are now 134**, 126 of them on `_PIC8095` alone, against **one** remaining sidecar in `samples/`. The sweep is keyed on open, and these frames are not being opened — so "bounded" is bounded by a sweep that does not run, not by the sweep working. 2.7 MB, so still cheap; the *number* is what was wrong | `MatteStore` |
| ~~The **nib's constants are uncited** — dab spacing, hardness clamp~~ ✅ **spacing derived and measured 2026-08-07, #180** — `research/brush-nib.md`. **There is nothing to cite**: two dabs `k` radii apart dip the stroke's edge inward by `1 − sqrt(1 − k²/4)`, the hardness clamp makes the falloff band `0.02 r`, and a dip inside that band is swallowed — bounding spacing at **k = 0.398**. ⚠⚠ **The two constants are one decision** and cannot move apart. ⚠ **The margin is 9%, not 37%** — only a smootherstep's steep middle reads as an edge. Measured: **1.12 px ripple against a 2.26 px feather**. ⚠ The **hardness clamp's own value is still chosen**, but it is no longer free | `UNSOURCED.md` §17 |
| **446 commits carry `Co-Authored-By` / `Claude-Session` trailers**, of 535 total. ⚠ **Recounted at the 2026-09-07 prune — the row said 363, and it is 446**; before that it said 101 and was recounted to 363 on 2026-08-02, because every agent in every wave since has added more. `git log --format=%B \| grep -c 'Co-Authored-By: Claude'`. Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history, and the longer it waits the larger the rewrite | whole history |
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

## Session `2026-09-09b` - the landing page, reworked against four taste skills, #241/#242

**Asked for directly:** apply `design-taste-frontend`, `redesign-existing-projects`,
`interface-kit` and `high-end-visual-design` to the website, on a new branch,
and update the docs.

**The mode was the whole decision.** `design-taste-frontend`'s own redesign
protocol (§11.E) says: if the information architecture, the content and the SEO
are sound, take the targeted evolution and not the rewrite — about 70% of the
value at 40% of the risk. All three are sound here. The page is already
art-directed, already dependency-free (#58), already correct without JavaScript
(#59), and its scenes are the brand. So this was an audit and a set of targeted
fixes, and **nothing structural was rewritten**. What follows is only what
genuinely failed.

**What the skills were right about.** Two of the page's devices are on the same
ban list and agreeing cost less code than defending them: the hero's `scroll to
look through` cue, and the fixed frame counter ticking `01 · Speed` through
`End of roll`. The counter's case does not need the skill — it told the reader
what the page already showed, cost a second `IntersectionObserver` and a class
toggle every animation frame, and was the only reason the page carried two
accent inks. Both gone, with `.dev__cue`, `.fr`, `@keyframes tick`,
`@keyframes frtick` and 27 lines of `main.js`. **#241 records that this reverses
half of #60**, which introduced rebate amber for exactly that counter.

**The register was the real defect and no checklist was needed to see it.**
Nineteen shipped features as nineteen hairlined rows beside a three-row column
left a quarter of the section empty, so the block whose entire job is *we do not
overclaim* was the worst-looking thing on the page. Now six groups (Library and
Export split, which is the truer division) in a grid that divides evenly by
three, two and one, so it is full at every breakpoint; "Not yet" moved
underneath as a full-width closer. ⚠ **All 22 feature strings were diffed
byte-for-byte against `HEAD` before and after** — `ea94b0e` and `901cfcb` exist
because this list has twice been wrong about what ships, so the regroup was
allowed to move strings and not to edit them.

**The footer was a dead end** and is the only exit the page offers, so it now
carries source, releases, research and the licence at a 44-point target.

**Mechanical, all verified in a browser against the live page:** zero em-dashes
anywhere visible (`−` in the mask readout is U+2212 and stays), middle dots
rationed to one per line, `rel=canonical` and `og:image:alt`, `text-wrap:
pretty`, the citation hover moved off `padding-left` onto `transform`, an
invisible 44-point reach around the download chip, and a
`prefers-reduced-transparency` fallback for the two blurred surfaces.

⚠ **What was deliberately NOT done, and why.**

| Skill said | Not done because |
|---|---|
| React / Next / Tailwind / Motion as the default stack | #58 settled this: dependency-free static files, and the page has no interactivity to justify a runtime |
| Install Phosphor or Tabler, never hand-roll an SVG icon | Four icons on a page with no `package.json`. A CDN request is exactly what #58 and the self-hosted fonts exist to avoid |
| Use `picsum.photos` placeholders | The photographs are real, taken by the developer, and screened (see the `_PIC8095` note above) |
| Serif display, premium palettes, glass, bento | Contextual advice for a brief this is not. The page is dark by decision, and teal-for-live-numbers-only is #60's rule |
| Add a nav | One page, one narrative, one CTA. A nav bar would fight the hero and add a component to maintain |

⚠ **`DESIGN.md` at the repository root is a trap and was left alone.** It is
untracked, and it is a design system for **Anthropic's Claude product** —
cream canvas, coral CTAs, Copernicus serif. `interface-kit` opens by saying a
root `DESIGN.md` overrides all of its defaults, so any future session that loads
that skill will try to paint Orion in another company's brand. It was not
applied here and it is not deleted, because it is not this session's file.
Rename it or move it under `research/` before it is believed.

⚠ **Nine image files in `web/img/` are referenced by nothing** —
`orion-hero-canvas`, `orion-mask-radial` (both sizes), and the
`photo-lambo-showroom` / `photo-m5-bluehour` / `photo-revuelto` pairs. Left in
place; flagged rather than deleted, since spare frames may be intentional.

⚠ **`web/index.html` still points at `v0.4.0-alpha.6`** and this pass did not
touch that, the schema block, any anchor `id`, or any URL. Same reasoning as the
note above: the site's only call to action must not lead at a pre-release until
the developer says so.

**Gates:** `check-decisions.py` green (240 rows, 1-242, all cited numbers
resolve). The other six are engine and app gates and this branch changes neither
`app/` nor `engine/`; the page was verified in a browser instead — markup
balanced, all 25 element `id`s preserved, 22 register items, no console errors,
no horizontal overflow, footer targets measured at 44 points.

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

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

**Last updated:** 2026-09-15 — engine performance and desktop interaction audit (#271). Fusion strength runs 4 nodes instead of 35; paired daylight p95 36.548 → 25.903 ms and 35.587 → 25.463. Compare/autosave, cold preview, rotation undo, proposal review and export feedback fixes are uncommitted. All nine recorded gates pass; 12 targeted scenarios pass. Full 42 MP exposure still misses 16 ms; session retention reaches 9.51 GiB. See `feedback/2026-09-15-engine-ui-audit.md` for evidence and limits. Website #270 is published; downloads remain withdrawn (#268).

**Recent sessions** — audit reports in `feedback/`; older rows and write-ups in `HISTORY.md`:

**Publication:** the developer requested committing and pushing the verified
audit on 2026-09-15. Target branch: `fix/engine-ui-audit`. The uncommitted labels
below describe the audit's original handoff, before this publication step.

| Date | What landed |
|---|---|
| 2026-09-15b | **Engine performance and desktop interaction audit (#271).** Fusion invalidation optimized with paired timings and GPU equivalence checks; Compare/autosave, cold preview, rotation undo, proposal locking/committed baseline, readability and export feedback fixed. Nine gates and 12 targeted scenarios pass; exposure budget, retained memory and broader UI coverage remain open. Uncommitted. `feedback/2026-09-15-engine-ui-audit.md`. |
| 2026-09-15 | **Website audit and polish (#270).** Mobile fallback/menu dismissal, accurate static exposure demo, keyboard compare steps, 44 px exposure target, Firefox focus style, accessible paper-grid escape from the animated stack, hero tour action and development status. `feedback/2026-09-15-website-audit.md`; all nine gates pass. Chrome tested at 320–1440 px over localhost, including reduced motion and script failures. Committed as `ba5ebda`, fast-forwarded to `origin/main` at the developer's request; Pages deployment `35002361269` succeeded. |
| 2026-09-14q | **Compare shows white balance (#269).** `captureOriginal` starts from `defaults` (the camera's as-shot reading) instead of copying the current temperature and tint; `repro/compare-shows-white-balance.txt` red before, green after; compare scenarios and the nine checks green. ⚠ 24 unrelated repro files fail or stop in this checkout (none uses compare). Not committed. |
| 2026-09-14p | **Download and Contact (#268).** Pages workflow serves the newest release's `.dmg` as `download/Orion.dmg` (fails the deploy if none); nav Download, hero and close Download for macOS plus glass Contact, footer Research/Download/Contact; `check-site.py` (l). `v0.5.0-alpha.5` pre-release packaged clean from `a2eb517` and published. |
| 2026-09-14o | **Published; MCP-driven workflow; leaked binaries gone (#267).** Heading and one line over a window up to 1680 px wide (16:9 photo, Claude pane 1:2.2), nav "MCP", Saturn behind Keep. Seven release DMGs verified leaking and deleted, `dist/` cleared. MX verified on Cloudflare; site committed and pushed to `main` for Pages. |
| 2026-09-14n | **No PII (#266).** Site audited clean (text, images, inlined maps, deployed `main`). App binary had `/Users/<name>/` ×50: `strip -S` in packaging, engine resource paths now relative to the build dir found at runtime, SwiftTerm's dead dump path moved to the temp dir, packaging fails on any `/Users/<name>/`, `$HOME` or git email, `check-site.py` (i) image metadata and (j) home paths. Old release DMGs still carry it. Nothing committed. |

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

Gaussian splatting, Marigold and Qwen are discussion only (#271). Queueing or
building any of them requires explicit scope.

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

**All nine green, recorded 2026-09-15 (`nine-gates-20260915/`):** engine **1104**, viewport **4210**,
both zero failures; gestures **6**; screens **3 asserting + 1 byte-stable**;
modes (library **13**, batch **2 files**, HDR merge); wiring **472 product
functions / 8 harness-only**; agent **14/14**; site **14 assertions**; decisions
**268 rows, 1–270**, all references resolve (before #271). Previous count snapshot
archived in `HISTORY.md`. These gates do not include the full repro sweep or
the wall-clock benchmark: full 42 MP exposure still fails at **17.38 ms p95**.
After the documentation update, `check-decisions.py` passes with **269 rows,
1–271**, three declared gaps and all references resolved; `git diff --check` passes.
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
| **Full-resolution exposure exceeds 16 ms at 42 MP.** Before 17.67–19.30 ms p95; after 17.38. No 24 MP corpus was available for revalidation | #271, `feedback/2026-09-15-engine-ui-audit.md` |
| **Session memory stays high after filters turn off.** Footprint 2.78 → 9.51 GiB, retained across reopen; cache retention needs a separately scoped policy | #271, engine audit |
| **Broader desktop verification remains open.** Baseline 54 scenarios: 30 pass, 10 missing fixtures, 3 cold-preview failures fixed, 11 scene-assertion failures unresolved. All 12 targeted after-runs pass, including the new 27-check workflow regression. Physical gestures and VoiceOver were not exhaustively tested | #271, desktop audit |
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

Session details: `feedback/2026-09-15-engine-ui-audit.md` (#271) and
`feedback/2026-09-15-website-audit.md` (#270). The older website write-ups and
15 recent-session rows moved verbatim to `HISTORY.md` on 2026-09-15.

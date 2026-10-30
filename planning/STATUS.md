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
**Phase:** M0-M4 complete. The Avalonia app is Orion on macOS (#380). Windows
and Linux build, test and package in CI on Dawn (#376) and are not released.
On `main` since the last phase line: JPEG XL export (#363), foreign-XMP
preservation (#370), region-of-interest rendering (#371), lens correction from
DNG opcodes (#372, #373), the Dawn backend (#375) and one palette for the site
and the app (#381-#383).

**Last updated:** 2026-10-30 - **The final review's fixes (branch `fix/final-review`).** NOTICE per system for Dawn, the shim guarded, this file and `ARCHITECTURE.md` made true to `main`. Next: the numbered list under *Open*.

**Recent sessions** — audit reports in `feedback/`; older rows and write-ups in `HISTORY.md`:

| Date | What landed |
|---|---|
| 2026-10-30d | **The final whole-branch review's fixes.** Branch `fix/final-review`. **NOTICE** lists Dawn's third-party code per system, read from CI's audit of each static library (the step stays in `build.yml`). **The shim is guarded:** every `shim_*` entry point that allocates runs in `shim_guard` (`shim.h`); its header no longer calls it spike glue; CLAUDE.md names it in the boundary rule. `shim_render_region` falls back to the whole graph only on BAD_ARG (documented in `orion.h` as the region's "render the whole graph"); OOM or a lost device is `Graph.Failed`, "render failed", not the 3.3 GB path. `ORION_ERR_NO_GPU`'s text is backend-neutral and marked never returned. The lens-name calls catch. **`CApi.cpp` split**: export and snapshot calls in `CApiExport.cpp`, the firewall in `CApiGuard.h` (996 → 846 lines). **New check:** a region under a DNG-opcode warp with an off-centre centre (fails at max 197 steps with the lens dropped from the edge sweep). check-decisions now fails on a declared gap that has a row (376 had one). Docs: this file pruned, `ARCHITECTURE.md` (Avalonia, the two C surfaces, the licence, Dawn), CLAUDE.md's issue list and CI, README, FEATURES/ROADMAP on JPEG XL; issues 5 and 6 spelt "issue N". `spikes/avalonia-shell/` ignored. GATES_PLACEHOLDER |
| 2026-10-30c | **Windows and Linux build, test and package in GitHub Actions (issue 24 items 3-4, #376).** Merged to `main` at `1e2f4dd`. Dawn is the only backend off macOS (D3D12, Vulkan), Clang everywhere (clang-cl on Windows), vcpkg on both (never the distribution's libraries: Ubuntu's OpenCV reached poppler, GPL); `.github/workflows/build.yml` runs engine, tests, the shell's `--selftest` on a generated linear DNG, and packages on all three. Export off macOS goes through OpenCV and libjxl (sRGB only, no camera metadata); the shim reads pixels back through a new facade call, `orion_engine_read_pixels`. **CI:** macOS green (Metal and Dawn); run 41059710829: Windows green on WARP (orion-tests 1308/0, selftest 427 passed with seven stated skips, zip + installer); Linux green with every GPU check a stated skip, because no CPU Vulkan driver gives Dawn `TextureFormatsTier1`. Both packages pass `tools/package-gates.py` (every file licensed, no GPL, no PDB, nobody named); actions pinned to commits, downloads checksummed. Real bugs found: `Motion.Ask` recursed under Reduce Motion; the DNG writer could not replace a file on Windows; batch names differing only in case or normalisation shared a file; a non-ASCII path did not open on Windows (the engine now opens every file wide there). ⚠ Not built: zero-copy present off macOS (CPU copy instead). ⚠ Open: no GPU test has run on Linux; undiagnosed, the brush accumulator's first append is optimisation-dependent on WARP (Windows stays RelWithDebInfo) and the double-click spring runs 0 frames on Windows CI. Local: engine 1378/0, selftest 437 passed, 5 stated skips. |
| 2026-10-30b | **One palette for the site and the app, and Node Garden (#382, #383).** Merged to `main` at `91acd7a`. Both design branches merged; the site's bubblegum accent reverted, cyan stays the accent (#382 is the site's token pass). `tokens.json` gains `palettes` (`hubble`: cyan, gold #EEBB58, star-white #E2EEFF, deep #166069; `belt`: the same with the mark in star-white) and `activePalette`; roles (`accent` incl. the current selection, `status`, `mark`, `onAccent`, `accentInk`, `gradient`) feed `Palette.g.cs` and a generated `web/css/tokens.css` linked before `base.css`. `build-tokens.py` refuses a palette under its WCAG bars, with status within 0.08 OKLab of text or dim, or with a red-to-pink slot (hue ≥ 320 or ≤ 45) beside cyan. The edited dot is `status`. Swapping to `belt`: the app byte-identical, the site changes only the sky's belt. Node Garden (Book of Shapes, NOTICE) behind the empty window (`NodeGarden.cs`) and on `web/img/og-orion.jpg` (`tools/make-og-card.py`). ⚠ The app's stars and warnings moved #F0C674 → #EEBB58 (ΔE 3.3). **Gates at 1df4f17**: orion-tests 1381/0, selftest 467/0, check-screens 7 tabs, check-modes, check-wiring 726 swept, check-agent 27/27 (needs `npm ci` in `mcp/` in a fresh worktree), check-gestures 6, check-decisions, check-site. |
| 2026-10-30 | **Design pass: the shell reads only tokens.json (#381).** ~200 values routed (audit in the branch report): space 2·4·6·10·14·18·26, type 11·12·13·17·28, radius 3·5·7, one shadow, `warn` apart from `star`, `faint` off words. Icons keep their 24 grid (no more stretch-to-bounds), one stroke, `sm` 12 / `md` 16; the mask glyphs' Viewbox path is gone. Fluent's accent shades come from `Palette.Accent` (were seven hex literals). Accent stays cyan: magenta #EF6DC9, aqua #3CE5C7, rose #F771A0 measured and shot for the developer. Gates: build, selftest 439/1 (the known glide), screens, wiring, decisions |
| 2026-10-29 | **Orion.app is the Avalonia app; the Swift app is gone (#380).** `spikes/avalonia-shell/` -> `shell/`, assembly and bundle `Orion` / `com.nanoai.orion`, DMG `Orion-<label>.dmg`; `SettingsImport` carries Swift's presets and assistant settings over once. `Orion --agent` ported (the MCP server's backend, checked against the Swift binary), plus `--batch-export` and `--hdr-merge`. Every bundle file maps to a licence or the package fails. Gates ported headless: check-screens (7 tabs), check-modes (batch, HDR; `--library-open` gone with the index, `--save-safety` is the selftest's), check-agent, check-gestures, check-wiring (C# sweep); `orion-viewport-tests` and the repro sweep retired with their view models. Deleted: `app/`, `third_party/SwiftTerm`, pixstat, ~76k lines. check-decisions had read nothing inside a worktree; fixed. After the merge with main and fix round 1: engine 1378/0, selftest 439/1 (the known 9-track glide; +47 from the agent CLI port, the drawn footer, Trash refusal, frozen commands), agent 27/27 on `_PIC8095` (high_iso_recipe now measures the flattest patch), mcp 47/47, screens, modes, wiring 724 swept / 6 harness-only, gestures 6, decisions 374 rows (1-380), site green, package green. Repro scenarios mapped in `planning/repro-retired.md`. `tools/huefit.py` needs a port |
| 2026-10-29b | **The engine renders through Dawn as well as Metal (issue 24 item 2, #375).** One seam, no new interface: `gpu::Device/Texture/Library/Kernel/CommandBuffer` each own an abstract `Impl` (`gpu/Backend.h`), Metal and Dawn subclasses; `ORION_GPU=dawn` picks Dawn, Metal stays the default. Every kernel also builds to WGSL (`wgsl_prep.py` maps bindings, repacks the parameter struct, fills storage formats); RGBA8 lives in an IOSurface so `displayHandle()` is a Metal texture over it. **Parity:** each node's kernel replayed alone on Dawn on Metal's inputs, 56 kernels, 54 bit-identical, 2 within one fp16 step; all 57 compile; the whole graph's output bit-identical on six edits; the whole suite passes on Dawn (1460/0); `present_test` and the shell `--selftest` (344/1, the old glide) pass on Dawn. **Bench** (24 MP ARW, median ms, Metal → Dawn): fit drag 0.85 → 0.83, fit settled 9.16 → 9.54, 100% region 2.41 → 2.40, pan at 100% 82 → 85, exposure drag 8.79 → 9.32. ⚠ **First render after open is slower on Dawn** (49 → 96-507 ms, varies by run; Dawn zero-fills every new texture) — why Metal stays default on macOS. Slang 2026.14.1 segfaults or emits bad WGSL on parameter-block arrays reaching a branch or function; two shaders copy them to locals. |

---

## Open

**Work is tracked as GitHub issues.** Open: issue 24 Windows and Linux, issue 5
lens correction (the Adobe half), issue 25 visibility, issue 4 features (camera
profiles; its JPEG XL half shipped). Closed: issue 22 (#371), issue 23 (#380),
issue 6 (#363). This file keeps the order and the recovery point.

**Next**, in order:

1. **Zero-copy present off macOS.** Windows (a D3D11 keyed mutex into ANGLE) and
   Linux (the one Vulkan allocator, #355). Both copy every render through the
   CPU today (`shim_read`).
2. **A Linux runner with a GPU.** No CPU Vulkan driver gives Dawn
   `TextureFormatsTier1`, so every GPU check on Linux is a stated skip: no GPU
   test has ever run there.
3. **Windows, two undiagnosed.** In Release the brush accumulator's first
   append takes the full path twice on WARP (correct pixels, fast path refused),
   so Windows CI builds RelWithDebInfo; and the double-click spring runs 0
   frames on Windows CI (a stated skip in `tools/ci-selftest.py`). Both want a
   real Windows machine.
4. **Export off macOS** writes sRGB only and carries no camera metadata (OpenCV
   and libjxl; ImageIO does both on macOS).
5. **Adobe `.lcp` lens profiles wait on the developer.** The reader is on branch
   `lcp-adobe-profiles` (`0a54b9c`); US 8,368,762 B1 is the only source for four
   of its field meanings, and a patent-only source is the developer's call
   (CLAUDE.md). Precedence on `main` is DNG opcodes, then lensfun.
6. **Owed: one real DJI `FC8482` DNG.** The opcode path was tested on synthetic
   lists only; confirm LibRaw names the body and the result looks right.
7. **The developer's checkout.** `/Users/grootbeat/Documents/Orion` is on
   `avalonia-port`, 129 commits behind `main`, with uncommitted `bench_stages`
   edits to files `main` also changed; its `CLAUDE.md` is the stale one sessions
   load. Commit that work to a branch, then switch the checkout to `main`.

**Parked minors** from the open-issues plan's reviews (none blocks; each is one
line in its review):

| Area | Minor |
|---|---|
| Dawn build | The staged-install path is untested until the next Dawn build; universal arm64+x86_64 untested; never run on a real macOS 14 |
| Export | The 16-bit export snapshot renders 89-326 ms on the UI thread (every format); Homebrew's libjxl, OpenCV and LibRaw dylibs are built for macOS 26 against the 14 floor (the linker warns) |
| Sidecars | The engine-level canLeave selftest covers only Open/Load; nested unknown keys in mask rows are dropped (as Swift did); window-only autosave wiring wants one manual check at a window; no direct 4 MB foreign-XMP test |
| Library | `ReadMarks` has no generation token (a folder reopened mid-read can apply an older batch); headless runs lose marks on reload; `LibraryChecks` leaves the engine's photo list on a deleted temp folder |
| Masks | Removing a rejected matte may leave an undo entry; `UsesToneBands` has an unchecked cast; no orphan-matte sweep yet (versions now record matte ids, so it can be added safely) |
| Assistant | `ProposalWatcher`'s Vanished/RestoreSidecar decode outside a try; the NuGet lock file is not enforced in packaging; `Broken` is not reset in `Attach`; approve-after-partial-restore untested |
| Region | A pan that reloads the window costs ~31 ms; `displayedToFrame` straightens about the crop centre where `geometry.slang` uses the frame centre, so masks and spots sit off on a straightened off-centre crop (wants an issue); many filters still fall back to the whole graph |
| CI and facade | A stale `createError` for null-handle callers; the GPL regex is case-sensitive (no-rule-fails is the real guard); the PII scan misses UTF-16; a conditional second Undo in `MaskUx`; include order |
| Tools | check-modes skips `_PIC8095` and reads only this tree's `samples/`; check-agent needs `npm ci` in `mcp/` in a fresh worktree; `tools/huefit.py` needs a port |

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

**Also still on you, carried forward:** *does the brush feel fast?* The numbers
say yes (#108); nobody has said so with a stylus in hand.

---

## Where the counts stand

COUNTS_PLACEHOLDER

Older count snapshots are in `HISTORY.md` (moved at the 2026-10-30 prune).

⚠ **Re-measure; never adjust these in place.** This block has carried up to
*four* copies of itself at once with four different numbers, and the three most
recent copies read 806/3708, 1008/4103 and 1012/4113. The suites only ever grow,
so a stale number here reads as a regression.

⚠ **orion-tests fails inside a sandboxed shell** — `no Metal device available`.
That is the sandbox, not the
code. Run them with it off.

⚠ **The M0 bench gate is a wall-clock threshold and it therefore flakes**, in
both directions, and it cost four sessions before it was named (#116). Eleven
runs of one binary once spread 8.83 → **30.42 ms** with `CoreSpotlight` at 99%
CPU indexing the sample folder. A p95 is only meaningful next to one taken
minutes away from it: **compare paired runs or do not compare**, and do not
chase this number on a busy machine.
---

### Known gaps, carried forward

Named gaps; the new data-safety findings take priority over the former next story:

| Gap | Where |
|---|---|
| **Full-resolution exposure exceeds 16 ms at 42 MP.** Before 17.67–19.30 ms p95; after 17.38. No 24 MP corpus was available for revalidation | #271, `feedback/2026-09-15-engine-ui-audit.md` |
| **Reduced-fixture inactive texture retention improved; full-size RAM still unmeasured.** Controlled no-shrink texture payload 903.1 → final 303.5 MiB after filters off at 1992×1330 plus preview. The old 2.78 → 9.51 GiB full-session footprint from #271 was not re-run under memory restraint; active-filter peak and 42 MP capacity remain open | #286, `feedback/2026-09-28-memory-retention.md` |
| **Histogram still performs full native readback on the main actor.** The redundant float copy is removed (#288); reduced timing improved, but full-size transient process memory and end-to-end UI latency remain unmeasured | `feedback/2026-09-28-histogram-memory.md` |
| **Data safety and desktop behavior need fixes.** Nine desktop findings include batch autosave cross-photo overwrite, dropped failed saves, erased foreign XMP, sync defaults/invalid JSON, main-thread batch, and library lifecycle. Five pure Swift probes ran at the original base; merged upstream was source-revalidated. Failed-save departure/Trash paths, including alias identity and Shift fallback, pass a mounted 50-check probe and all nine gates (#289). Erased foreign XMP is fixed in both apps (#370). Quit/close and the other findings remain open, as audited against the Swift app; re-check them against the Avalonia app before working them | `feedback/2026-09-28-desktop-io-audit.md` |
| **Mask correctness and engine I/O need targeted reproductions.** Eight mask findings are source-traced; the engine audit names HDR/output collision and failed-open/export hazards. The nine gates do not cover these cases | `feedback/2026-09-28-masking-audit.md`, `feedback/2026-09-28-engine-io-audit.md` |
| **Broader desktop verification remains open.** Earlier baseline 54 scenarios: 30 pass, 10 missing fixtures, 3 cold-preview failures fixed, 11 scene-assertion failures unresolved. Physical gestures and VoiceOver were not exhaustively tested. Current sample symlinks allowed the screen and mode gates to pass | #271, desktop audit |
| ⚠ **The check floor says a file measured something, never that it measured the right thing.** #124 takes M4 (one verb family claiming every verb) from 39/40 exiting 0 to 3/40. Two survivors are the declared instruments. The third, `snapshot-keeps-its-matte.txt`, keeps 3 real checks under the mutation because `snapshot missing` is implemented by the very family that claims everything — a floor cannot see that. The oracles that *can* are the byte comparison of rendered frames and the full-output diff, and neither is what the gate runs. Raising the floor would not help; the residual is a coverage shape, not a threshold | `repro/` |
| **A matte is not regenerated when the edit changes.** Exposure and white balance change what Vision would see; they do not move the subject. Regenerating costs two renders and an inference, so it is on demand — and #79 now adds a second reason it must stay on demand: a model that has changed between OS releases would give a *different* selection, silently, on a finished edit | `SubjectMatte` |
| **A regenerated matte leaves the old file until the next open.** Files are immutable by design, so pressing Subject five times writes five PNGs; the sweep runs on open. Bounded and cheap, but it is not zero. ⚠ It was **not** bounded until 2026-08-01 — on a photograph with no sidecar the sweep could never run at all, and 26 orphans had piled up beside one sample frame. Decision #87. ⚠⚠ **Recounted 2026-09-07: there are now 134**, 126 of them on `_PIC8095` alone, against **one** remaining sidecar in `samples/`. The sweep is keyed on open, and these frames are not being opened — so "bounded" is bounded by a sweep that does not run, not by the sweep working. 2.7 MB, so still cheap; the *number* is what was wrong | `MatteStore` |
| **446 commits carry `Co-Authored-By` / `Claude-Session` trailers**, of 535 total. ⚠ **Recounted at the 2026-09-07 prune — the row said 363, and it is 446**; before that it said 101 and was recounted to 363 on 2026-08-02, because every agent in every wave since has added more. `git log --format=%B \| grep -c 'Co-Authored-By: Claude'`. Developer approved stripping them; needs a history rewrite and a force-push to a public repo. ⚠ Not done unasked — it rewrites published history, and the longer it waits the larger the rewrite | whole history |
| **The 1000-line rule is not broken anywhere.** ⚠⚠ **It was, and this row said otherwise for five days — recounted 2026-08-07 at `5038f07` (#185).** `tests_mask_geom.cpp` stood at **1,173**. The previous copy of this row read *"Over 1,000: none"* as at `191b451` on 2026-08-02, and nothing recounted it while three sessions added code — which is the failure the row's own last sentence warns about, happening to the row that warns about it. Re-swept as prescribed: `git ls-files` over all **242** tracked `.swift/.cpp/.h/.hpp/.mm/.c/.m/.slang` files, counted with `grep -c ''`, not a directory list. **Over 1,000: none**, after the split. Largest anywhere: `tests_mask_geom.cpp` **809**, `ShaderParams.h` **954**, `tests_io.cpp` **926**, `tests_highlights.cpp` **897**, `tests_mask.cpp` **881**, `tests_tone.cpp` **858**, `Engine.swift` **844**, `tests_perspective.cpp` **837**, `tests_brush.cpp` **824**, `ViewportTests+Index.swift` **809**. ⚠ **The previous copy of this row went stale within hours of being written, which is exactly what it warns about**: it recorded `tests_highlights.cpp` at **865** as at `6767716`, and `1a3083d` — *"bound the fill's weight, and say what the constant rim actually reaches"* — took that file to **897** on the same day. Every other number in it re-derived unchanged. ⚠ **`app/Screenshot.swift` was the last one over the line**, at **1,196** — 809 lines on the morning of 2026-08-02, taken over the line the same day by #125's three interface checks, and split five ways by #131 at the seam between a scene that *asserts* and a scene that *poses*. ⚠ A sweep is of **one worktree at one commit** and cannot see whatever is in flight elsewhere — it is a floor on the violation, not a ceiling. Eleven splits are done: `DevelopPipeline.cpp` 2,896→452 (#113), `Engine.swift` 2,331→795 (#117), `bench/main.cpp` 2,289→85 (#118), `tests_effects.cpp` 1,716→555 (#127), `Scenario.swift` 1,615→301 (#120), `OrionApp.swift` 1,557→299 (#121), `DevelopPanels.swift` 1,366→56 (#122), `Screenshot.swift` 1,196→315 (#131), and #129's three: `tests_brush.cpp` 1,142→824, `tests_perspective.cpp` 1,110→837, `tests_grade.cpp` 1,029→653. ⚠ **Recount by sweep before editing this row; never adjust the numbers in place** — it has carried up to four contradictory copies of itself at once, and three were collapsed into one on 2026-08-02 | whole tree |
| **Nothing asserts that a gesture *arms*** — narrowed 2026-08-01, decision #110.3, and it is now the *first* link only. `repro/gesture-preview-agrees.txt` used to compare an armed run against an unarmed one and demand they agree, which is green when arming does nothing; it now also asserts arming has an effect (the preview surface goes 0.2323/0.2918 → 0.4814/0.2037 over the same eight ticks), so a no-op `beginInteraction` fails. What is still unreachable is a `DragGesture` closure calling it: **attempted** — `NSHostingView` off-screen lays the wheel out and hit-tests it, but `NSEvent.mouseEvent` through `NSApplication.sendEvent` never reaches the recognizer, and CGEvent-backed events need a real on-screen window and the real cursor. Deleting `ColorWheel`'s call is green across 744 / 3624 / 39, measured | `Scenario.swift` |

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
15 recent-session rows moved verbatim to `HISTORY.md` on 2026-09-15. The socket branch's six write-ups (#272-#279) moved
verbatim to `HISTORY.md` at the 2026-09-20 merge.

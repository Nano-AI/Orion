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

**Last updated:** 2026-10-31 - **Linux hardening, round two (branch `darora1/linux/daily-driver`, #399-#406).** On the developer's own 336-photo, 42 MP folder: the folder opens at once, 42 MP renders fit the card, panning moves rendered pixels, side by side compare, `tools/bench.py` to measure all of it, and the developer's four Linux calls built (#403-#406). Rebased onto `main` after stacking (#391) and Velopack (#389). Next: the numbered list under *Open*.

**Recent sessions** — audit reports in `feedback/`; older rows and write-ups in `HISTORY.md`:

| Date | What landed |
|---|---|
| 2026-10-31g | **GPU memory has one rule, and the round's merge (#404, #406).** Branch `lh/decide-gpu`, merged with `lh/decide-shell` and `lh/decide-package` into `darora1/linux/daily-driver`. **#406:** each render plans its peak against min(2.5 GB, the driver's device-wide free count plus what the graph holds; NVML or amdgpu, else the cap and a refusal's ceiling): a plan that fits caches as before, one past it frees every output at its last reader but a keep set and hands idle memory back before allocating; the starlet chain sums fine to coarse. 42 MP, all three of clarity, denoise and texture: out of memory -> 1,871 MiB; a white-balance move then a Luminance tick stay frugal under budget, no lean retry; 24 MP clarity tick ~15 ms as before (cached). Side by side, two 42 MP ARWs at fit then 1:1: 3,044 -> 2,265 MiB, and beside 896 MiB held elsewhere 3/3 failed -> 3/3 render; a pane out of memory has the other pane give back and retries once. Lean retry is one render, not sticky (supersedes #392's). **Fixes found in review:** the static C++ runtime stays private to each library (`--exclude-libs`; vcpkg's ICU exported 4,736 `std::` symbols into a process where .NET loads the system's libstdc++), and the floor scan fails a library that exports it; vcpkg's host tools carry their runtime (`tools/vcpkg-triplets/x64-linux.cmake`), so ICU builds on NixOS; the workflow passes shellcheck; the compare selftest times its second toggle from a settled heap (a gen-1 collection fell due inside it on 2 runs in 3); the spring selftest steps its springs itself. **Gates at `c590def`:** orion-tests 1378/0, ci-selftest linux 462 passed / 4 stated skips / 0 unexpected (twice), check-screens, check-modes, check-gestures, check-wiring (780 swept), check-decisions (1-406). Installed for the developer at `~/.local/share/orion`. Untested: macOS, Windows, AMD/Intel, the 22.04 container build and the floor job (CI only, not pushed). |
| 2026-10-31f | **Two of the developer's open decisions, built (#403, #405).** Branch `lh/decide-shell` off `darora1/linux/daily-driver`. **#403:** the tool column's widest is measured from the tab names' face (`MainWindow.ToolColumn.cs`, a real `TextLayout`, at startup and on a face change): 560 on macOS and Windows, 592 on Linux with DejaVu Sans (widest tab 64 pt, cells 80), cap 600, room shrinking toward 4 pt past it; the tab-fit selftest now passes on Linux and asserts at the computed widest. **#405:** sidecar naming is Adobe's in full, one rule in `Sidecar.Locate`: `photo.xmp` for a proprietary raw, `photo.<ext>.xmp` for DNG/JPEG/TIFF/HEIF/PSD with the embedded XMP as a read-only baseline (`orion_read_xmp`, LibRaw `xmpdata`), `orion:Source` in every sidecar, `crs:RawFileName` honoured, a raw beside another's `photo.xmp` escapes to `photo.ARW.xmp` with a status-line notice; the Trash's siblings ask `Sidecar.OwnPath`. End to end through the agent CLI: photo.ARW, photo.dng, photo.DNG (embedded rating 3) in one folder keep three sidecars with their own rating, label and edit, photo.DNG byte-identical, photo.NEF escapes to photo.NEF.xmp and leaves photo.xmp untouched. Gates: orion-tests 1338/0, ci-selftest linux 462 passed, 4 stated skips, 0 unexpected (486/3 skips before the review fixes on three copies of the CI DNG, which runs the layout checks), check-screens 7 tabs, check-modes, check-wiring 779 swept / 7 harness-only, check-decisions, check-gestures. A DNG's pre-#405 `photo.xmp` migrates on the first write when it names the DNG, or names nothing and no other photo shares the stem. ⚠ Open: snapshots and mattes still share the stem. Untested: macOS, Windows. |
| 2026-10-31e | **The AppImage's glibc floor is 2.35, ICU bundled (#404).** Branch `lh/decide-package` off `darora1/linux/daily-driver`. CI's Linux job now builds in an Ubuntu 22.04 container (Clang 18 from apt.llvm.org, vcpkg and Dawn from source, `-static-libstdc++ -static-libgcc`); the package carries vcpkg's ICU 78 with filtered data (overlay port `tools/vcpkg-ports/icu`) through `AppLocalIcu`; `tools/check-glibc-floor.py` gates every ELF (GLIBC <= 2.35, libstdc++ only for Microsoft's runtime, by name, at most 22.04's); AppRun refuses in one line without a Vulkan loader, fontconfig, a display or its own files; a `linux-floor` job runs the AppImage in bare `ubuntu:22.04` and `24.04`. Local (NixOS): scanner pass and failure cases, AppImage 62.5 -> 71.6 MB with ICU, bare 24.04 rootfs batch export of the case and accent names 4/4 on the GPU with no system ICU, AppRun refusals on bare 22.04 and 24.04. **Not yet run: the 22.04 container build and the floor job (CI only, not pushed).** |
| 2026-10-31d | **The developer's folder, end to end (#399, #401).** Branch `darora1/linux/daily-driver`, the developer's 336 Sony A7R III ARWs (42 MP, 86 MB, exFAT on a spinning disk), RTX 3070 with ~2.9 GB free. **Opening it took 32 s cold with nothing drawn:** the filmstrip read every embedded JPEG on the UI thread (~40 ms a file cold) and the window built the HDR sheet only to discard it, after it read all 336 files' capture data. Now thumbnails read on one background thread nearest the open photo, in two sizes (256 px for every photo, 800 px only for gallery cells on screen, 64 kept off screen), sheets are built when opened, and the decoding line has a moving bar: window and first photo 1.4 s (bench, warm) / 4.3 s (cold), thumbnail memory -350 MiB against all at 800 px. **A 42 MP full render ran out of GPU memory, even lean:** the geometry texture was a 1.45x square of the long side (509 MB for a 162 MB picture) and one batch kept every dropped intermediate until its submit ended. Now the texture is the frame it writes, and a lean render submits and gives memory back as it hands textures back (+28 ms): lean peak 2.5 -> 1.53 GB, interactive 1.68 GB, both succeed. **Panning** never rendered per frame on a plain drag (0.1 ms UI); the stutter was the rest timer firing under a held hand, a whole-graph edit copying 42 MP a frame, and an export estimate that failed out of memory leaving 2.2 GB behind. Now the canvas tells the engine the view only at rest, renders an eighth's margin round it, and nothing renders inside what is rendered: drags 0 renders, 0 stalls; the rest render 115 -> ~170-190 ms with the margin (#401). Reviewed by gemini-3.8-flash-high (agy) per stream, findings fixed or rejected with evidence. **Gates at `c9f5cf1` and after:** orion-tests 1338/0, ci-selftest linux 452 passed / 4 stated skips / 1 unexpected (tab fit), check-screens, check-modes, check-gestures, check-wiring, check-decisions (1-402). Installed for the developer at `~/.local/share/orion` with a rofi entry. |
| 2026-10-31c | **`tools/bench.py`: one command for "did this make Orion slower or hungrier?" (#402).** Branch `lh/bench` off `darora1/linux/daily-driver`. Eight scenarios run headless as a person meets them (`--list`): a cold folder open (`posix_fadvise` eviction, `fincore`-verified) and a warm one (time to a responsive window, to the first picture, to settled, the worst UI-thread stall), a swap, an exposure drag, the pan's own `--bench --pan` (#401), read from its `bench pan` lines, the first full render with the card's per-process peak (nvidia-smi), an export through `ExportRun`, and orion-bench (now built off macOS). Five processes a scenario, interleaved; median and MAD; Hodges-Lehmann shift with its exact interval against a per-machine baseline outside the repo, or `--ab TREE` interleaved. Guarded: GPU lock per sample, niced, refuses under 30% memory or load 8. **Baseline numbers, RTX 3070, base `63e7cac`:** developer's 336 ARWs on exFAT 27.6 s cold / 9.5 s warm to a responsive window, all of it in window construction (synchronous filmstrip thumbnails); same count of links on an SSD 8.7 s; neighbour swap 443 ms, jump 436 ms; preview drag 7 ms UI / 3.6 ms engine; full drag 195 ms a tick (out of GPU memory and retried every tick); pan 0.2 ms UI, sharp 190-205 ms after the hand stops (measured by a driver of this branch's, since dropped for #401's); full render and export fail out of GPU memory under ~3.4 GB free; engine full render 24 ms, exposure p95 5.0 ms, then orion-bench itself runs out of memory at 42 MP. Every scenario run twice: nothing slower. Gates: check-wiring, check-decisions, check-gestures; ci-selftest linux 443 passed, 4 stated skips, 1 unexpected (tab fit, as on the base). ⚠ A headless frame raster-draws on the UI thread (~45 ms at 1:1, ~110 ms in a drag): `*_ui_ms` excludes it, `*_frame_ms` does not. |
| 2026-10-31b | **Side by side (#400).** Branch `lh/compare` off `darora1/linux/daily-driver`. C compares two photos (the selection, or the photo and the next): select left, candidate right, zoom and pan linked (L or the bar unlinks, Shift for one gesture), Left/Right step the candidate, Down swaps, Up makes it the select, ratings mark the active side. Two 42 MP ARWs at rest, zoomed 3x: 837 MiB of the GPU against 532 for one photo; the peak is the select's own full pass. Selftest: one raw linked twice, the two renders must agree pixel for pixel. Untested on macOS and Windows; panes use the CPU copy, not the zero-copy surface |
| 2026-10-30j | **Photo stacking: noise, star and landscape stacks to a linear DNG (#391).** Branch `feat/stacking`; plan `planning/plans/stacking.md`, sources `research/stacking.md`, constants `research/UNSOURCED.md` §32 (was a second §22). Engine `engine/src/stack/` (CPU, half-float frames, rows threaded; `orion_engine_stack`, `--stack --mode noise|stars|landscape`): per-pixel rejection by frame count (≤3 none, 4-20 winsorized, >20 linear fit, percentile and sigma by hand), star registration (SExtractor-style detection + sharpness gate, triangle matching, RANSAC homography H = K·R·K⁻¹), a stack over the memory available now warns in the sheet with Proceed anyway (engine: ORION_ERR_MEMORY unless forced, `--stack --force`), landscape = sky on the stars + ground unaligned + feathered seam, darks and flats on the mosaic. Shell: Stack sheet (`Stack.cs`, `StackSheet.cs`, `StackSplit.cs`, `StackCalibration.cs`: sky painter, Darks... and Flats...), and the Library suggests a stack for 5+ steady-cadence frames (`StackGroups.cs`, `StackSuggestion.cs`; an astro group opens the sheet in Stars). **Measured (synthetic unless said):** star registration max corner error 0.0279 px (59 inliers), 0.0244 px with 40 hot pixels; a 14 mm, 200 s sky turn (3000x2000, full-frame field of view) 0.0028 px median, 0.0098 px max, where the old similarity refused the frame; a folding or mirroring homography is refused; through the facade median star error 0.029 px, 59/60 matched; noise vs √N 3.860 vs 4 (16 frames, integer shifts) and 2.930 vs 2.828 (facade, 8 frames; RMS to the true scene 0.00351, identity registration 0.01206 fails); landscape ground edge 1.000, star peak 0.994 of one frame; satellite streak 0 of 480 px outside 4σ; rejection leaves a fixed hot pixel 0 of 72 landings outside; darks take a tripod hot pixel 0.6807 → 0.0679, flats a vignette corner/centre 0.846 → 0.995; 24 MP star mode on 10 cores, low priority: 10 frames 15.6 s, 20 frames 29.8 s, peak RSS 3.9 GB, so no GPU node; real raw: three copies of a 24 MP ARW, `--mode noise`, 2.44 s, 145.7 MB DNG (`check-modes` runs it). Review rulings: star registration is a homography (at 14 mm the best similarity misses by ~15 px median in an earlier full-resolution simulation); Auto is winsorized from 4 frames, never sigma (the median-centred 3σ cannot reject a lone outlier at n ≤ 7, works from 8) nor percentile (keeps a dark-sky satellite on 89-98 % of pixels at 4-6 frames); normalization scale k from 7x7 boxes at star cores; normalized flat gain < 0.1 means no signal and stays 1; unknown shutter/ISO never forms a suggested group; painter tint is Reject red. orion-tests 1567 checks, 0 failures. Shell `--selftest` 481 passed, 1 failed ("after an arrow key every one of 9 tracks shows its photo's value at once..."), confirmed on origin/main 76c5295's own shell (built against this branch's shim), two runs. ⚠ Patents: nothing assessed; the sky/ground split is the risky part (see #391). |
| 2026-10-30i | **Installers and updates through Velopack (#389).** Branch `build/velopack`. The shell (Velopack 1.2.161) checks GitHub Releases, pre-releases included, five seconds after an interactive launch, downloads in the background and shows "Update ready, restart to apply." with Restart in the footer; "Check for updates" is in the Open menu (`check-for-updates`, `restart-to-update`). Not installed by Velopack: nothing happens; offline: a status line (two selftest checks, both fail with the `IsInstalled` test reversed). vpk 1.2.161: Windows `Orion-win-Setup.exe` + `Orion-win-Portable.zip` replace Inno Setup and the zip; Linux vpk builds the AppImage from the same AppDir (appimagetool gone); macOS vpk's re-signed copy of Orion.app is what the gates check and the DMG carries (no .pkg). Each OS also emits its feed in `dist/releases/`. CI's macOS job packages for the first time (check-agent a stated skip there, no sample RAW). ⚠ An updated Mac bundle fails `codesign --verify` (the .nupkg drops the .dlls' xattr signatures; it runs): fix before notarizing. ⚠ A release must be tagged and newer than the last for anyone to be offered it. Local: orion-tests 1393/0, selftest 447/1 (the known glide), check-decisions/gestures/screens/modes/wiring/agent/site green, `package-app.sh` + vpk green and the packaged app renders headless. CI run 41149726467 green on all three, artifacts `orion-macos` (DMG + feed), `orion-windows` (Setup.exe, Portable.zip, feed), `orion-linux` (`Orion.AppImage`, feed); the packaged Mac app renders headless there too. |

---

## Open

**Work is tracked as GitHub issues.** Open: issue 24 Windows and Linux, issue 5
lens correction (the Adobe half), issue 25 visibility, issue 4 features (camera
profiles; its JPEG XL half shipped). Closed: issue 22 (#371), issue 23 (#380),
issue 6 (#363). This file keeps the order and the recovery point.

**Next**, in order:

1. **Zero-copy present off macOS.** Windows (a D3D11 keyed mutex into ANGLE, opt-in
   since #386) and Linux (the one Vulkan allocator, #355: confirmed not built, nothing
   calls Vulkan). Linux copies every render through the CPU, measured correct (#393).
2. **A Linux runner with a GPU.** CI's Linux GPU checks are still stated skips (no
   CPU Vulkan driver gives Dawn `TextureFormatsTier1`); the first real GPU runs were
   local, on NVIDIA (#392-#398). AMD and Intel, native Wayland and a non-NixOS
   desktop are untested.
3. **Owed from #403-#406:** Windows CI wants one run to confirm the dropped spring skip and that Release passes the brush (#398), and the first run of the 22.04 container build and the `linux-floor` job (#404); the developer confirms #405's DNG metadata is read-only, #406's 2.5 GB cap and #403's 600 cap; snapshots (`photo.orion-snapshots.json`) and mattes still share a stem across `photo.ARW` and `photo.dng`; frugal ticks at 42 MP with all three effects are slow (Luminance ~300 ms) and unprofiled; Windows has no driver free count (DXGI could give it).
4. **The rest render after a pan still waits on the GPU** on the UI thread (~170 ms at
   42 MP, 95% of it `Pipeline::render`'s `commitAndWait`): submit without waiting, as
   #386's readback does (#401). Pan pacing in a real window on NVIDIA is unmeasured.
5. **Export off macOS** writes sRGB only and carries no camera metadata (OpenCV
   and libjxl; ImageIO does both on macOS).
6. **Adobe `.lcp` lens profiles wait on the developer.** The reader is on branch
   `lcp-adobe-profiles` (`0a54b9c`); US 8,368,762 B1 is the only source for four
   of its field meanings, and a patent-only source is the developer's call
   (CLAUDE.md). Precedence on `main` is DNG opcodes, then lensfun.
7. **Owed: one real DJI `FC8482` DNG.** The opcode path was tested on synthetic
   lists only; confirm LibRaw names the body and the result looks right.
8. **Stacking follow-ups (#391).** Tune the sharpness 0.5, flat 0.1 and dark 0.15 thresholds on real star, flat and dark frames (all measured on synthetic data only); band streaming (frames spilled to disk, integrated in row bands, as Siril and DeepSkyStacker do) so a stack over the memory available needs no warning (half-float frames in memory peak at 3.9 GB for 10-20); measure a real wide lens's distortion residual after homography registration; find what grows ~6 B/px per frame on the raw path (measured 12 B/px a light against the half planes' 6).
   - Stop is not checked inside `integrate` (about half the run time; progress sits at 0.8).
   - `HdrMergePanel.Merge` has the same unguarded `async void` the Stack sheet's Run had.
   - The sheet's Auto label uses the candidate count; the engine picks by registered count (3/4 and 20/21 can differ).
9. **The developer's checkout.** `/Users/grootbeat/Documents/Orion` is on
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
| Display (#386) | The Windows D3D11 share's review minors stay with it until a Windows window has run it (opt-in, `ORION_D3D_SHARE=1`); `orion.h` (~920 lines) and `CApi.cpp` (812) are past the ~800-line guide and want splitting; Metal's synchronous display copy copies twice (slower at level 0 than `read_pixels`, headless only) |
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

**At `darora1/linux/daily-driver`, 2026-10-31, local Linux (RTX 3070):** `orion-tests` **1335/0** · ci-selftest **443 passed, 4 stated skips, 1 unexpected** (tab fit) · ARW selftest **446/2** · check-screens **7 tabs** · check-modes · check-agent **26/27** · mcp **48/48** · wiring **743 swept, 7 harness-only** · gestures **6** · decisions **388 rows, 1-398, 11 gaps**. Not run here: macOS, Windows, present_test (macOS only). Rebased onto `origin/perf/display-copy` at `7788dc6` before these were taken.

**At `fix/final-review`, 2026-10-30, local macOS:** `orion-tests` **1380/0** · selftest **441/1** on `_PIC8095` (the known glide), **437 passed, 5 stated skips** on the generated CI photo (`tools/ci-selftest.py`) · check-screens **7 tabs**, Light byte-stable · check-modes (batch **2 files**, HDR DNG) · check-agent **27/27** · mcp **47/47** · present_test passed · wiring **726 swept, 6 harness-only** · gestures **6** · decisions **380 rows, 1-385, 6 gaps** (after merging `main` at `aea1401`) · site green. `orion-viewport-tests` no longer exists (retired with the Swift app, #380). CI on the branch (run 41106615962, `547822a`, green on all three): macOS orion-tests **1439/0** on Metal and on Dawn, selftest 437 passed / 5 stated skips; Windows on WARP **1310/0**, selftest 427 / 7 stated skips; Linux **599/0** with every GPU check a stated skip, selftest 50 then the stated "needs a photo" stop.

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
chase this number on a busy machine. `tools/bench.py` (#402) is that comparison:
repeated processes, an interval, and `--ab` for two builds interleaved.
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

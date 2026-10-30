# Open-issues run: controller ledger (2026-10-29 → 30)

The working log of the plan that closed issues #23, #24, #22, #5 and #6: every dispatch, review, fix round, merge and ruling, in order. Decisions themselves live in DECISIONS.md; this is the trail behind them. Scratch paths are shortened to `scratchpad/`.

Spec: GitHub issues #23 #24 #22 #5 #6 (no separate spec doc).

## Pre-flight scan
| Pair / task | Shared | Finding |
|---|---|---|
| T1 JXL vs T2 lens | engine/ (export vs lens stage), NOTICE, DECISIONS.md | disjoint code; NOTICE/DECISIONS append-only, renumber at merge |
| T1 vs T3 sidecars | shell export panel vs shell document/save | disjoint files expected |
| T2 vs T5 ROI | lens geometry | T5 after T2 (sequenced) |
| T4 Dawn vs T9 port | Dawn build | T9 after T4 |
| T6/T7/T8 | shell EngineModel/Session | one owner each, assigned in dispatch |
| T11 vs T6-8 | app/ deletion | T11 last, only once stub count is 0 |
| each task | self-consistent | yes |

Ruling: run wave tasks in parallel, each in its own worktree on disjoint files, against the skill's one-at-a-time rule — the developer asked for a swarm and builds are serialized by heavy.sh anyway — cost if wrong: merge conflicts in shared files, resolved at merge.
Ruling: implementers on opus (judgment-heavy, multi-file), reviewers on sonnet — cost if wrong: weaker review on subtle engine code.

## Dispatch (base 80cebfa, 2026-10-29)
Task 1 (JXL, #6): dispatched, opus, branch issue-6-jxl
Task 2 (lens opcodes+LCP, #5): dispatched, opus, branch issue-5-lens
Task 3 (XMP sidecars, #23.1): dispatched, opus, branch issue-23-sidecars
Task 4 (Dawn source build, #24.1): dispatched, opus, branch issue-24-dawn-build
Reports: scratchpad/sdd-task-N-report.md
Task 4: implementer DONE_WITH_CONCERNS (80cebfa..0315c04, worktree agent-a49cc2ed1ca67512b); review dispatched (sonnet)
Task 4: review — spec ✅, changes needed (2 Important: Apache §4(a) full text; BSD copyright lines). Fix round 1 sent to implementer with 2 minors (staging install, preflight). Deferred minor: guard-only enforcement in build-dawn.sh (optional).
Task 4: fix round 1/5 (2 addressed, 0 open; commits 0315c04..e4cc3c5) — verified by controller (small diff)
Task 4: complete (commits 80cebfa..e4cc3c5, review clean), merged to main
Task 4: minor (deferred): staged-install path untested until next Dawn build; universal arm64+x86_64 untested; not run on real macOS 14
Task 1: implementer DONE_WITH_CONCERNS (80cebfa..2cc0ffd, worktree agent-ac8e186d4f1e6509b); review dispatched (sonnet)
Ruling: Task 1 concern 1 (size estimate encodes on UI thread, 2.7-7 s freeze for JXL) is a real defect, goes in the fix round: estimate off the UI thread, latest-wins — cost if wrong: a little extra code
Ruling: Task 1 concern 7 (no Swift UI picker entry) not done — the Swift app is being retired (#23) — cost if wrong: Swift users miss JXL until the Avalonia app ships
Ruling: Task 1 selftest must run before merge with samples at <repo>/samples/_PIC8095.ARW — cost if wrong: none
Task 1: review — spec ✅ (lossy ICC as enumerated encoding, accepted per #361), changes needed: 1 Important (UI-thread encode) + 3 minors; fix round 1 sent incl. CApi catch check and real selftest run
Task 3: implementer DONE_WITH_CONCERNS (80cebfa..d0f3357, worktree agent-a31de210eb292805e); review dispatched (opus — data-loss surface)
Ruling: Task 3 concern 6 (failed save + later reopen can overwrite unsaved edit) is load-bearing — goes into the fix round regardless of review — cost if wrong: none
Ruling: Task 3 concern 5 (ExportRun.cs Session.Open -> OpenEdited) goes into Task 3's fix round; Task 1 does not touch ExportRun.cs per its diff stat check at merge — cost if wrong: trivial merge conflict
Ruling: concerns 2-4 (spots, matte images, ratings) belong to Tasks 6-8; carried there
Task 3: review — spec ✅ w/ gap (canLeavePhoto not ported), changes needed: 2 Important (pending-job replacement loses edit; non-ENOENT read failure overwrites sidecar) + minors; fix round 1 sent with ExportRun OpenEdited and new failing-first selftests
Task 2: implementer DONE_WITH_CONCERNS (80cebfa..086c3cf, worktree agent-a4f1d569f23c32701); review dispatched (opus — untrusted-file parsing)
Ruling: Task 2 cites a lens-profile patent text for field meanings; the model itself is also documented in lensfun's manual, so not patent-only — proceed and surface to the developer at the end — cost if wrong: developer may ask to drop LCP support
Task 2: review — spec ✅, changes needed: 4 Important (Canon/Nikon body match; empty-lens mismatch; synchronous LCP scan; hand-rolled XML) + minors; fix round 1 sent incl. a fresh search for a non-patent source of LCP field semantics
Ruling: hold merge of the LCP part until a non-patent source is found or the developer decides (CLAUDE.md: patent-only sources are the developer's call) — cost if wrong: delay of the Adobe-profile path only
Task 3: fix round 1 committed d0f3357..a1638b7 (7 fixes; fix 4 has no failing-first check — no input reaches it); scoped re-review dispatched (sonnet); real-ARW selftest requested (samples/ symlinks dangle; real RAWs in agent-afc33b0f239ec8270/samples)
Task 3: fix round 1/5 (7 addressed, 0 open; commits d0f3357..a1638b7)
Task 3: minor (deferred): engine-level canLeave selftest only really covers Open/Load (one-photo list); nested unknown keys in mask rows dropped (as Swift); window-only autosave wiring needs one manual check at a window
Task 3: complete (commits 80cebfa..145d1fc, review clean), merged to main (decision renumbered 362)
Tasks 6/7/8 dispatched (opus, base 498ce73): issue-23-library, issue-23-masks, issue-23-assistant; briefs scratchpad/sdd-task-{6,7,8}-brief.md
Ruling: Tasks 6-8 run in parallel on the shell; shared EngineModel/Session edits kept additive, conflicts resolved at merge — cost if wrong: merge work
Cleanup: removed merged worktrees for Tasks 3, 4; Dawn build kept at scratchpad/dawn-build for Task 9
Task 2: fix round 1 committed 086c3cf..0a54b9c; scoped re-review dispatched. Citation: Adobe LPC User Guide 2010 found for folders/body-lens matching; ImageXCenter/YCenter (fractions of Dmax, 0.5 default) and FocalLengthX/Y semantics still patent-only (US 8,368,762) — awaiting developer
Note: check-agent high_iso_recipe fails at base 80cebfa too (pre-existing)
Task 2: fix round 1/5 (5 addressed, 0 open; commits 086c3cf..0a54b9c). Review clean. Merge HELD pending developer's LCP patent decision.
Task 2: minor (deferred): xlocale.h/strtof_l macOS-only (Task 10 must port); lens name count/at lack try/catch (pre-existing); RICOH/Pentax rebrand unmatched
Ruling: Task 5 (ROI) branches from issue-5-lens (0a54b9c) so it doesn't wait on the LCP decision; if LCP is dropped only LensLcp files change — cost if wrong: a rebase
Task 5 (ROI, #22): dispatched (opus), branch issue-22-roi from 0a54b9c + main
Task 1: fix round 1 committed 2cc0ffd..3144952 (snapshot handle split: render on UI thread, encode off thread); selftest 235/2 (same 2 fail at base); scoped re-review dispatched
Task 1: minor (deferred): check-modes excludes _PIC8095 and reads only worktree samples; check-agent needs mcp/node_modules (npm install never run in worktrees); Swift app still measures on main thread
Task 1: fix round 1/5 (6 addressed, 0 open; 3 new minor breakages in Measure failure/close paths) — fix round 2 sent with UI-half timing request
Task 1: fix round 2/5 (3 addressed, 0 open; 3144952..7751c90) — verified by controller
Task 1: complete (commits 80cebfa..7751c90), merged to main (decision renumbered 363); merged-tree orion-tests 1339/0, selftest 280/2
Task 1: minor (deferred): 16-bit export snapshot render 89-326 ms on UI thread (pre-existing render cost, all formats); Homebrew libjxl/opencv dylibs built for macOS 26 vs our 14 floor (packaging)
Task 7: implementer DONE_WITH_CONCERNS (498ce73..2c042c2, worktree agent-ad7bebad3bbd05df4); selftest 307/1 on 8095; review dispatched (opus)
Task 6: implementer DONE_WITH_CONCERNS (498ce73..4835fb6, worktree agent-aef5a30312ddd7fa6); selftest 306/2; review dispatched (sonnet). Mask-id-in-undo request sent to Task 6 agent (may not have been picked up)
Ruling: orphan-matte sweep deferred until versions record matte ids — cost if wrong: ~30 KB per selection accumulates beside photos
Task 6: review — spec ❌ (masks not in undo history; N-selected readout + 3 stubs missing), 4 Important (sync sidecar reads on UI thread; stale mark cache; spot undo desync; version-restore undo drops masks); fix round 1 sent. Coordination note to Task 8: SpotModel.Replace single entry point; Task 6 owns ExportSelected + strip menu entries
Task 7: review — spec ✅, 2 Important (corrupt matte locks photo Unreadable; 'process' key not synced) + 4 minors; fix round 1 sent
Ruling: NEW Task 12 — sidecar writes must preserve foreign XMP (Lightroom/Capture One write IMG.xmp too; both apps rewrite it with only Rating/Label/Develop, dropping their data). Pre-existing since the Swift app. Scheduled after Tasks 6-8 merge (Sidecar.cs shared); covers shell and Swift app — cost if wrong: none; it is data loss today
Task 6: fix round 1 committed 4835fb6..c387efd (selftest 311/2, folder 340/0, mutation-checked); scoped re-review dispatched. Versions record matteIds — orphan sweep can be added safely later
Task 6: fix round 1/5 (7 addressed, 0 open; 4835fb6..c387efd)
Task 6: complete (commits 498ce73..c387efd), merged to main (decision 364); merged-tree selftest 312/2
Task 6: minor (deferred): ReadMarks has no generation token (same folder reopened mid-read can apply an older batch); headless runs lose marks on reload
Task 7: fix round 1 committed 2c042c2..9fda19d. Ruling: accept implementer's process-key rule (sync adds process only when target states none and has no tone values, as AgentSync.committed) over my 'add to Light group' — it preserves legacy edits' own process; Swift tests assert no group owns it — cost if wrong: none found
(correction) Task 6 merge commit actually landed as 811e688 after staging fix; merged-tree selftest 312/2
Task 7: scoped re-review dispatched
Task 8: implementer DONE_WITH_CONCERNS (498ce73..3b739b2, worktree agent-a04ac61e1f23afe6e); selftest 322/2; review dispatched (opus). Decisions #363-#366 in branch (renumber at merge: main now at 364)
Task 7: fix round 1/5 (6 addressed, 0 open; 2c042c2..9fda19d)
Task 7: complete (commits 498ce73..9fda19d), merged to main (decision 365); merged-tree selftest 343/1 after reordering MatteChecks before LibraryChecks
Task 6: minor (deferred): LibraryChecks leaves engine photo list on its deleted temp folder (test isolation) — anything after it in Run sees dead paths
Task 7: minor (deferred): group.Remove of a rejected matte may leave an undo entry; UsesToneBands unchecked cast
Task 8: review — spec ❌ (SpotModel.Replace has no callers), 4 Important (spot edits not undoable; proposal approve/reject misassigns matte ids by position = sidecar corruption; unreadable proposal crashes app; terminal package unpinned, bus factor 1) + minors; fix round 1 sent incl. merge main + renumber to 366-369
Task 12 (foreign XMP preservation, both apps): brief written
Task 5: implementer DONE (partial per brief) 8aa5b97..7eff0b8 on issue-22-roi (built on issue-5-lens + main); orion-tests 1351/0; bench fit 0.84->0.87ms, 100% drag sharp 2.4ms, settled 9.2->2.5ms, GPU 3.3GB->0.9GB; review dispatched (opus)
Note: ROI branch contains the lens branch (incl. LCP), so ROI merge also waits on the developer's LCP decision
Task 5: minor (deferred): pre-existing displayedToFrame straightens about crop centre vs geometry.slang frame centre — masks/spots off on straightened off-centre crops (file as issue); Swift app ROI wiring not done; many filters fall back to whole graph
Task 5: review — spec partly met; 1 Critical (edge strip: last whole%16 rows/cols never loaded), 5 Important (stale eyedropper; deny-list w/o static_assert; test coverage; memory not actually lower; non-sharpen radii unscaled) + minors; fix round 1 sent incl. merge main + renumber (ROI→371, lens 361/362→372/373)
Task 5: fix round 1 committed 7eff0b8..32e0509 (fixes + merge main; ROI #371, lens rows #372-#374; branch declares gaps 366-370 temporarily — remove at merge). orion-tests 1395/0, selftest 344/1, GPU 3551->1240 MiB.
Ruling: denoise-forces-step-1 drag regression at 50% (0.8 -> 4.7-6.5 ms) blocks per CLAUDE.md; fix round 2: quarter preview while dragging when a step-1-only filter is on and step>1, region on settle — cost if wrong: blurrier drag with denoise at <100%, same as before the task
Task 8: fix round 1 committed 3b739b2..b65c819 (merge main + fixes); selftest 358/1; scoped re-review (opus) on 58054c2..b65c819
Ruling: ROI drag past fit without denoise 0.8 -> 1.6-2.4 ms accepted — it is the sharp region the issue asks for (blurry quarter preview replaced), fit zoom unchanged, well under frame budget — cost if wrong: developer may prefer the old blurry-fast drag; one flag restores it
Task 8: fix round 1/5 (5 addressed, 0 open; 3b739b2..b65c819)
Task 8: complete (498ce73..b65c819), fast-forwarded to main (decisions 366-369); selftest 358/1
Task 8: minor (deferred): ProposalWatcher Vanished/RestoreSidecar decode outside try; NuGet lock file not enforced (no locked-mode restore in packaging); Broken not reset in Attach; approve-after-partial-restore untested
Task 5: fix round 2 committed 32e0509..a807689 (interacting flag); scoped re-review of rounds 1+2 dispatched (opus)
Parity: after Task 8, remaining stub(#315) markers are only a template comment (CommandTable.cs:109) and Linux trash (Trash.cs:62)
Task 5: rounds 1-2 re-review: 7/7 + minors addressed; 2 new minor breakages (measured() hands out released graph mid-drag; export past fit rebuilds 3.3 GB and keeps it) — fix round 3 sent (memory matters: laptop crash risk)
Task 5: minor (deferred): 31 ms pan reload; Swift app ROI wiring; displayedToFrame vs geometry.slang pivot (file issue)
Task 12: implementer DONE (58054c2..31787da on fix-foreign-xmp, decision #370); viewport 4473/0 (8 new fail on old path), selftest 356/1 (pre-Task-8 main); review dispatched (opus)
Task 5: fix round 3/5 (2 addressed, 0 open; a807689..be28be6) — verified by controller
Task 5: complete (8aa5b97..be28be6, review clean). Merge HELD with Task 2 (branch contains lens incl. LCP) pending developer's LCP decision. Branch declares gaps 366-370 temporarily: drop at merge (366-370 now real on main/fix-foreign-xmp)
Task 9 (Metal->Dawn): brief written; base be28be6 (ROI head, includes lens + main)
Task 9 (Metal->Dawn): dispatched (opus), branch issue-24-dawn-engine from be28be6
Task 12: review — spec ❌ parity gaps; 3 Important (shell mis-decodes encodings; Swift deletes non-base64 Develop on rating; missing tests) + unverified ancestor-prefix rebind + label-loss race on early rating; fix round 1 sent incl. merge main
Task 12: fix round 1 committed 31787da..79e1169 (+ merge main dc7dc21); viewport 4486/0, selftest 384/1; scoped re-review dispatched. Ruling: accept fresh-prefix (orion1…) over refusal for collisions — never rebinds foreign prefixes and keeps the file writable — cost if wrong: unusual prefix in output
Task 12: fix round 1/5 (5+4 addressed, 0 open; 31787da..79e1169)
Task 12: complete (58054c2..dc7dc21), fast-forwarded to main (decision 370); viewport 4486/0, selftest 384/1
Task 12: minor (deferred): no direct 4 MB test; no Swift Library-level early-rating test
Task 11 (ship Avalonia, retire Swift app): brief written; base main dc7dc21
Task 9: implementer DONE w/ gap (be28be6..9775e06, worktree agent-a8b0abc65648745f1); 57 kernels on Dawn behind ORION_GPU=dawn, Metal default (first render after open 96-507 ms vs 49); orion-tests 1460/0 both backends; graph bit-identical on 6 edits; review dispatched (opus)
Task 9: review — spec ✅, quality Approved w/ 3 Important (wgsl_prep zip truncation; short struct zero-fill; robustness hard-coded) + threading minors; fix round 1 sent incl. check-modes/check-agent runs
Task 9: fix round 1 committed 9775e06..a0d855c (12/12 claimed); check-modes passes, check-agent 26/1 (high_iso_recipe pre-existing at be28be6); scoped re-review dispatched
Ruling: unblock lens+ROI+Dawn chain without shipping the patent-only part: preserve the Adobe LCP reader on branch lcp-adobe-profiles (=0a54b9c), and strip LCP from the top of the merged chain before merging to main; LCP returns only if the developer says yes — cost if wrong: the developer may want LCP; it is one merge away
Task 9: fix round 1/5 (12 addressed, 0 open; 9775e06..a0d855c); sizeof risk settled by Dawn suite passing 1460/0
Task 9: round 2 sent: strip LCP (preserved on lcp-adobe-profiles), fix stale #375 text, merge main, renumber 371-375 contiguous, full test run
Task 9: fix round 2 (LCP strip + merge main) a0d855c..0d4cd47; verified by controller (no live LCP refs)
Task 9: complete (be28be6..0d4cd47); Tasks 2 (DNG opcodes only) and 5 (ROI) complete via the same chain; fast-forwarded to main 0d4cd47. Tests: orion-tests 1440/0 Metal+Dawn, viewport 4486/0, present_test 13/13, selftest 385/1, check-modes pass
Branch lcp-adobe-profiles pushed (Adobe .lcp reader, awaiting developer decision)
Task 10 (Windows/Linux CI): brief written; base main 0d4cd47. Ruling: the agent may push branch ci/windows-linux to origin and run GitHub Actions on it (needed: no Windows/Linux machine); no releases/tags — cost if wrong: Actions minutes on a public repo (free)
Task 11: implementer done dc7dc21..f48e6f7 (branch issue-23-ship, worktree agent-a2e59a16d004e7ac7); review package scratchpad/review-task11.diff (deletions of app/ and third_party/SwiftTerm excluded); opus review dispatched
Task 11 review: spec ❌, quality changes needed (0C/6I/8M) — scratchpad/sdd-task-11-review.md. Fix round 1 sent to implementer. Ruling: retired repro sweep → mapping table, not a rebuilt sweep — cost if wrong: some scenarios uncovered, listed explicitly
Task 11 fix round 1: 62f2eba; re-review all 14 addressed. Fix round 2 sent (footer check silent skip, StatusPanel leak, high_iso no-op passes). Ruling: tighten high_iso to require real reduction — a check a no-op passes is not a check
Task 11: complete — ff-merged to main b46e476, pushed, issue #23 closed
Side work (developer request, reels): web + app design passes done (branches design/web-accent-tokens, design/app-accent-tokens); unifying agent on design/one-accent.
Ruling: accent stays cyan (developer: "main color of Orion, that's cyan"); bubblegum site accent reverted — cost if wrong: one-line token change.
Ruling: brand palette "Hubble" = cyan #4DB6C4 + gold #EEBB58 + star-white #E2EEFF + deep #166069; never cyan+red (H-alpha #ff4356 ≈ TikTok red) — developer asleep, picked as most astro, zero new visuals — cost if wrong: DESIGN.md section only.
Ruling: Node Garden on social card + empty Library (developer chose).
Developer asleep 2026-10-30 ~01:00: finish everything planned autonomously (merge design, Task 10, final review, STATUS).
Developer approved Hubble; asked: swappable palette driving web+app from tokens.json, roles, contrast enforced in build-tokens.py — sent to design/one-accent agent
design/one-accent b46e476..8753696 reviewed (opus): spec mostly met, quality good; 4 Important / 11 Minor. Fix round 1 sent.
Ruling: keep Node Garden coordinates in shell source with a licence/no-reuse header — developer chose it after the licence summary; licence allows use as part of a product — cost if wrong: remove file + history note; FLAG to developer in morning.
Ruling: belt palette = star-white mark, gold status (status is data, not brand) — cost if wrong: one key.
Ruling: accent covers selection / you-are-here states (proposal heading, selected mask, "This computer", nav current) — cost: doc line.
design/one-accent: re-review 12/13 + 2 leftovers fixed by controller (91acd7a); ff-merged to main 91acd7a, pushed. Worktrees/branches removed.
Task 10 (ci/windows-linux, fbe5f93) DONE, CI run 41047748426 green 3 OS. Review package review-task-10.diff vs 91acd7a.
Task 10: review (opus) — spec ✅ with stated gaps; quality changes needed: 1 Critical (AppImage ldd sweep likely bundles GPL poppler/gdal via apt opencv), 5 Important (Linux skip too broad; 2 Windows skips not timing; non-ASCII Windows paths; unpinned supply chain; no PII/licence gates in win/linux packaging), 6 Minor. Fix round 1 sent (all except minor 9 simplified as suggested, 7 folded into 3).
Ruling: GPL-in-AppImage is load-bearing even with no release planned — artifact is downloadable from CI — cost if wrong: an extra OpenCV build step.
Task 10: fix round 1/5 (fbe5f93..11668cb, run 41064898228 green 3 OS); open: brush Release fast-path, Windows spring (undiagnosed, in #376); MicroCom /home/kekekeks excuse -> developer. Scoped re-review dispatched (sonnet).
Task 10: re-review (sonnet) — 12/12 addressed or accepted-open; 6 new Minor. New-1 (createError copy could throw bad_alloc out of the facade) fixed by controller on main. Minors 2-6 parked: stale createError for null-handle callers; GPL regex case-sensitive (no-rule-fails is the real guard); PII scan misses UTF-16; conditional second Undo in MaskUx; include order.
Task 10: complete (955a2bd..11668cb + controller fix 1e2f4dd), ff-merged to main, pushed; orion-tests 1378/0 local, CI 41064898228 green 3 OS.
Final review (opus): 0 Critical, 5 Important (I1 Dawn NOTICE macOS-only; I2 shim.cpp extern "C" unguarded; I3 STATUS stale queue; I4 ARCHITECTURE stale; I5 main checkout on avalonia-port 129 behind w/ uncommitted edits — developer's call), 9 Minor. ONE fix dispatch for I1-I4 + M1-M9 (M7 via ignore rule only).
Ruling: I5 not touched — main checkout holds developer's uncommitted work — FLAG in morning.
Final fix round: PAUSED for developer's interview at ea5a52a (WIP) on fix/final-review, pushed. Done I2,I4,M1-M9; I3 WIP; left: I1 NOTICE (read CI run 41071491173 audit), fill STATUS placeholders, amend WIP, rerun gates, final CI, cleanup. Resume agent a7e1b2f801ede8a6c.

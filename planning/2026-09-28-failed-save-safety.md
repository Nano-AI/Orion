# Failed-save departure safety implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** An owed failed edit must survive photo/folder navigation and Trash; refuse the departure before changing document or selection state, then permit retry after storage recovers.

**Architecture:** Keep the existing one-document Autosave and proposal-preview semantics. A shared save preflight gates document departure. Library stages its existing scan result, then commits the listing and calls the photo loader synchronously after that preflight; no second Library, index or thumbnail loader.

**Tech Stack:** Swift, Foundation/AppKit/SwiftUI, existing C++/Metal engine and test binaries.

**Spec:** User's active file-handling goal; `feedback/2026-09-28-desktop-io-audit.md` finding 2; Trash caller in `codex/batch-export-safety:feedback/2026-09-28-app-complete-audit.md`; behavior contract below.

## Behavior contract

- If saving open A fails, direct load, plain filmstrip selection, gallery-to-develop, Open Photo, Open Folder, command-line browsing and post-HDR-output navigation keep A's current identity, live edit/history, selection, snapshots, watcher and autosave intact. Display the existing save failure. No operation may rebind B's edits to A.
- A confirmed Trash containing current A must leave the entire requested set and owned siblings in place when A's save fails. Trash of only other photographs remains available.
- Edits made while a folder scan runs must be included in the final save decision. A failed decision leaves the old folder/list/selection; no new metadata/thumbnail task starts for the refused listing.
- A retry that saves A may proceed normally; subsequent B edits save to B. Returning A to its saved state removes the pending failure normally.
- Keep proposal preview's intentional `stop`/`begin` behavior. Quit/window-close veto, existing overlapping-scan ordering/cancellation, foreign XMP preservation, sync/proposals and pending batch safety remain separate open work. A saved HDR output remains on disk if the later navigation is refused.

## Global Constraints

- **No Rust.** No Vulkan, new dependencies or filter math. macOS 14+.
- **Maintainability is a hard requirement.** No first-party file at or above 1,000 lines; reuse existing models and fixtures, no generic transition framework.
- View models remain plain `@Observable` objects with zero SwiftUI types inside.
- Preserve original RAW/XMP/mattes/snapshots and user preferences. Probe only unique temporary copies of `tools/fixtures/mask-invert.dng` (64×64); inject a temporary archive mover instead of touching the user's Trash.
- One app/compiler/GPU owner at a time; builds at most `-j2`; stop only the owned process group if kernel memory pressure reaches 2. Protect the complete 23-name dynamic `/tmp` fixture roster before suite runs, not just literal path matches.
- Base is verified main `3dceaa0` on `codex/failed-save-safety`, in the existing checkout. Pending batch branch `059c873` remains separate. A later batch run will need a rebuild after this checkout changes its binary.
- Controller owns plans, decisions, commits, reviews and push. Implementer owns only the listed source/tests. All nine gates before delivery; no Lightroom or full-resolution performance claim from these fixtures.

## Review Focus

- A fresh edit or failed timer write during an async scan must be seen at commit: probe the final preflight and refused callback/list install.
- A refused switch must retain more than the queued tuple: actual Editor state/history/selection and real render callback remain bound to A.
- Refused current-photo Trash must not move any member of a multi-photo set; noncurrent Trash and successful retry remain usable.
- Coalesced changes, returning to saved state, and proposal preview must retain existing semantics; keep old tests and add refusal/retry coverage.
- Opening a named photo must still start decoding immediately, without waiting for its folder scan. Folder/CLI/HDR replacement must use the final guarded commit; probe real action methods, without claiming physical keyboard/VoiceOver coverage.

### Task 1: Refuse unsafe departures and verify actual actions

**Files:** Modify `app/Autosave.swift`, `Library.swift`, `OrionApp+Files.swift`, `OrionApp+Commands.swift`, `OrionApp.swift`, `Filmstrip.swift`, `HdrMergePanel.swift`, `ViewportTests+Sidecar.swift`, `ViewportTests.swift`, `app/CMakeLists.txt`, `tools/check-modes.py`. Create `app/SaveDepartureProbe.swift`. Add to `tools/check-wiring.py` only if its existing explicit mechanism/harness roster needs the new entry. No unrelated changes.

**Interfaces:**
- Add `Autosave.flushBeforeLeaving() -> Bool`: call `flush()`, return `!isDirty`. Do not change `stop`, `begin` or `note` contracts.
- Add `Editor.canLeavePhoto() -> Bool` that uses the preflight and reports the existing `lastFailure` via the current message UI on refusal. Add `Editor.openPhoto(_ url: URL, beforeListingScan: (() async -> Void)? = nil)` for the panel’s chosen-file action: preflight then immediate guarded `load`, with no await before decode is scheduled; background listing accepts only while `current == url` and merely focuses that URL after commit, never reloads it. The default-nil hook permits the probe to wait for initial decode and make a real edit before the delayed listing starts; the product path adds no wait.
- Extend `Library.open` to `@discardableResult func open(folder: URL, beforeReplacing: () -> Bool = { true }, didReplace: () -> Void = {}) async -> Bool`. Scan off-main first; after resuming on main, decide, install the listing/start its existing loader, and call `didReplace` with no intervening await. Refusal returns false with prior folder/photos/selection/failure intact; clear transient `loading`. Preserve the default caller behavior and error reporting.
- Extend Library's existing initializer with one defaulted `(URL) throws -> Void` trash-move closure, storing it outside Observation; default remains `FileManager.trashItem`. Both RAW and sibling moves use it. The probe supplies an owned temporary archive; no new mover type or persistence layer.
- Add only the small Editor construction/ready seam needed to run its real methods with an injected Autosave, isolated Library/preset/watermark stores and a mounted SwiftUI State. Match the pending batch branch's `startPresets`/`startWatermark` parameter spellings if needed. Do not instantiate RootView or a second Engine.
- Register `--save-safety` in OrionApp and the existing mode gate. The probe uses the real Editor actions and the existing tiny fixture, not a duplicate model of departure behavior.

- [x] **RED:** Add the runnable app probe and injection seam first while retaining unsafe departure behavior. With a rejecting Autosave writer, invoke actual `load`, plain-filmstrip action, `openFromGallery` and current-photo `runTrash`; assert A's state/history/identity/selection, original sidecar bytes and requested RAW/siblings remain. Route Trash to a private archive. Demonstrate behavioral failures on the old path, not a compile error or locked-desktop focus failure. The probe requires no key window and must not assert one.
- [x] Add writer-injected viewport coverage: failed preflight is false and retains A, an additional A edit replaces only A's pending value, returning to saved clears it, recovered storage saves A before a permitted begin/edit of B; B lands separately. Keep existing coalescing/proposal tests intact.
- [x] Implement the preflight and put `load`'s guard before watcher detachment, stop, snapshots, current/focus/crop/opening changes. Guard gallery mode changes before mutation. Plain Filmstrip clicks call the guarded onSelect directly; modified clicks retain their existing selection behavior. Extract only the tiny callable gesture-policy method needed for the real-action probe.
- [x] Guard current-photo Trash before clearing its pending list or moving any file; preserve the existing noncurrent-only path. Implement the defaulted I/O closure at Library's filesystem boundary, never by replacing `runTrash` with a test double.
- [x] Stage Library's existing scan result without a second model/index. Apply its final guard before any folder/list/selection replacement or new loader. Route Open Folder, command-line first photo, and HDR post-output navigation through `didReplace`; handle refused Bool in command-line stepping so it does not continue. Open Photo uses `openPhoto` to preserve immediate decode; its background listing drops results if current changed, and may commit while that same chosen photo has a later pending edit because it does not depart that document. Keep `load` guarded for independent callers. For empty folders/missing folders, preserve existing library error semantics.
- [x] Extend the app probe: old listing and callback stay intact/uncalled when final preflight fails (including a new A edit at that decision); recovery permits list commit and load; subsequent B edit writes only B; chosen-file action starts the accepted load before any await/listing completion and does not reload B or discard B’s later pending edit when its listing catches up; noncurrent-only Trash uses the injected archive; current-included set moves nothing on failure. Await only bounded readiness, not arbitrary long sleeps. Assert actual `engine.onEdit` remains connected after refusal. Cover failed-load, list-commit and Trash guards with the same shared-policy negative control or the original RED run; report exactly what each catches.
- [x] Build `cmake --build build -j2`; run `./build/orion-viewport-tests` and `./build/Orion.app/Contents/MacOS/Orion --save-safety` under the pressure/fixture guard. Safe final source must be restored and rebuilt after any negative control. Full nine gates run later once review is clear, not after each edit.
- [x] Self-review the focused diff, append exact RED/GREEN commands/counts and limitations to this plan's report, then release the build/GPU lock. Do not commit or push; controller runs independent scoped and final reviews, records the decision and delivery evidence.

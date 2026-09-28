# Desktop, file handling and library audit

2026-09-28. Read-only audit at `40618db`, source-revalidated after upstream
`bda5847` was merged (the five probes below were not rerun). Performed alongside
the engine and mask audits. No application launches, GPU renders, nine-gate run,
competitor measurement or new latency/RSS measurement in this sub-audit.
Priority: P1 = data loss/wrong document or major workflow failure; P2 = scaling
or responsiveness defect. Findings below distinguish executed probes from
source-traced conclusions. Existing comments are not treated as test evidence.

## Ranked findings

### 1. P1 — Batch export can overwrite the open photograph with the last batch photograph's edits

**Trigger:** open A with an edit different from B; export a batch whose last
successfully opened/restored photograph is B.

`Editor.installAutosave` wires `engine.onEdit` to `autosave.note`
(`app/OrionApp.swift:405`). `runBatchExport` only calls `autosave.flush()`
(`app/OrionApp+Files.swift:75`), leaving A as the autosave target, then passes
the same engine to the batch driver (`:85`). `BatchExportDriver.swift:111-113`
opens/restores each photo; `Engine+Document.swift:132,170` renders, and
`Engine+Render.swift:243` emits those states to autosave. The final reload
(`OrionApp+Files.swift:96`) begins with `autosave.stop()` (`:250`), which flushes
the last batch state to A before A's sidecar is read again. No timer interleaving
is required. The final reload also resets A's undo history.

**Executed:** unchanged `Autosave.swift`, `Sidecar.swift` and `BatchExport.swift`
compiled with a one-float `DevelopState` stub. Reproducing the exact callback
sequence wrote `A.ARW` with B's exposure 2 instead of A's exposure 1.
`/tmp/orion-desktop-audit/main.swift` records the probe; it is not a real-engine
or product-UI end-to-end test.

**Smallest fix/check:** disarm autosave before lending the engine to the batch;
abort if the outgoing write is still owed. Restore the current document and its
history after the batch. A regression must install the real `onEdit` callback,
export A/B with different edits, and byte-check A's sidecar before/after. The
existing CLI batch gate has no editor autosave and cannot catch this.

### 2. P1 — A failed save is dropped when the next photograph is edited

**Trigger:** A's sidecar cannot be written; switch to B and edit it; storage
becomes writable again.

`Autosave` has exactly one `pending` tuple (`app/Autosave.swift:55`). `stop` and
`begin` retry A but proceed when retry fails (`:93-105`). `note` on B overwrites
that tuple (`:123`); A is no longer retained anywhere in autosave. A successful
B save also clears the warning (`:154`). The preservation comment in `flush`
is accurate only until another target is edited.

**Executed:** the same unchanged-product-file probe made A writes fail, began
B, edited B, restored storage, then flushed. Only `B.ARW` was written.

**Smallest fix/check:** either refuse a document switch until the owed save
lands, or retain pending state by URL. A two-photo failing-writer test must
assert A survives B's edit and the final retry. Do not use a one-photo retry
check as proof of this invariant.

### 3. P1 — Rating or editing a Lightroom XMP deletes foreign metadata and edit settings

`Sidecar.merge` says it keeps the rest (`app/Sidecar.swift:89`) but reads only
rating, label and Orion's develop payload (`:28-45`), constructs a fresh XML
document (`:110-131`), and atomically replaces the original (`:137`). Any
`dc:subject`, creator/copyright, Lightroom `crs:*` adjustments, or other XMP
properties disappear on the first rating/autosave/sync/agent flag. Atomic write
protects against partial bytes, not semantic loss. Callers:
`Library.swift:457`, `Autosave.swift:84`, `SyncSettings.swift:134`, and
`AgentCLIDriver.swift:151,171`.

**Executed:** wrote valid XMP with a `dc:subject` keyword, used the actual
`Sidecar.merge` to change rating, read it back: keyword absent.

**Smallest fix/check:** preserve the existing XML document while changing only
owned fields, using Foundation XML facilities or the already chosen XMP
implementation; distinguish absent from unreadable and refuse destructive
replacement on read/parse failure. Test unknown attributes, elements and
namespaces survive rating and develop writes. No new generic persistence layer
is needed.

### 4. P1 — Light-only sync changes untouched photos' white balance to 5500 K / tint 0

`SyncSettings.patched` deliberately leaves unselected fields absent
(`app/SyncSettings.swift:75-102`), and its comment claims open will fill them
from the camera. `DevelopState.init(from:)` instead starts from fixed defaults
(`app/EditHistory.swift:639-648`, defaults at `:512`) and assigns missing
white-balance fields from those defaults. `Engine.restore`
assigns that whole decoded state (`app/Engine+Document.swift:155-170`), replacing
the as-shot values `Engine.open` had just set. Applies to product open, batch
export and agent open of an untouched photo that was synced without WB.

**Executed:** compiled the actual `EditHistory`, `CurveSupport`, `Presets`,
`SyncSettings` and `Sidecar` files. A light-only patch contains no WB keys;
decoding it returns `5500.0, 0.0`. Camera-specific visual effect was not rendered.
Probe: `/tmp/orion-desktop-audit/sync/main.swift`.

**Additional source finding after `bda5847`:** a light-only sync into an
untouched target also omits `process`. Missing `process` plus nonzero tone sliders
decodes as legacy process 1 (`EditHistory.swift:764-768`), while a fresh state
starts at process 2 (`:517`). Existing explicit and legacy versions should stay
intact; a newly created partial sidecar needs the fresh generation. This was
source-traced, not executed.

**Smallest fix/check:** resolve missing WB keys against the current photograph's
as-shot defaults at the shared restore boundary. Assert a light-only sync
preserves a fixture's non-5500/nonzero as-shot WB after reopen and export.

### 5. P1 — Sync treats existing malformed develop JSON as an empty edit

`SyncSettings.patched` initializes an empty dictionary and only replaces it
when JSON parsing succeeds (`app/SyncSettings.swift:83-87`). Invalid existing
bytes therefore become a valid partial patch, and `sync` writes it over the
existing payload (`:139-141`). The normal product open protects unreadable
saved edits (`OrionApp+Files.swift:364-373`); sync bypasses that protection.

**Executed:** actual product patcher accepted a truncated existing JSON object
and returned a replacement light patch. No caller is told it discarded unreadable
state.

**Smallest fix/check:** treat present-but-invalid JSON as a failed target,
retain its bytes and report it in `Outcome.failed`; absent remains patchable.
One malformed-target sync test should assert byte equality and failed count.

### 6. P1/P2 — Batch export blocks the entire UI and its Stop button cannot run

`OrionApp+Files.swift:84` enters a main-actor task, but
`BatchExport.run(jobs:engine:settings:)` (`BatchExportDriver.swift:105-139`) and
its loop (`BatchExport.swift:128-141`) are fully synchronous. There is no await,
yield or runloop turn between photos. The comment promising a yield and a
responsive Stop (`OrionApp+Files.swift:35-40`) is false. The Stop action in
`DevelopPanels+Presets.swift:164` cannot change `batchCancelled` until the batch
returns; progress changes cannot paint while the actor is occupied.

The same main-thread engine execution is present in open (`+Files:296-330`),
single export (`:496`), file-size measurement (`OrionApp.swift:331-339`), full
settle (`Engine+Render.swift:155-165`) and debounced histogram (`:358-364`).
Debouncing limits frequency, not execution duration. This audit did not measure
new hitch times; the 2026-09-15 measured full-render costs remain relevant.

**Smallest fix/check:** first give the batch genuine suspension between photos,
with document actions locked while it borrows the engine. For responsive work
within one photo, serialize engine ownership on a worker and publish completed
frames on main; simply detaching concurrent calls on one handle is unsafe. Test
that a main-queue heartbeat and cancel execute before the second export begins.

### 7. P2 — Library thumbnails have no in-memory budget or visibility policy

Every `Library.Photo` owns an `NSImage` (`app/Library.swift:33`). Every file in
a folder is queued (`:250-275`) and retains a 1024-edge thumbnail (`:292`,
`PhotoIndex.swift:115`, `Library.swift:362-372`), irrespective of visible cells.
The SQLite LRU budget (`PhotoIndex.swift:225,493-542`) controls disk blobs only;
it cannot evict these live `NSImage` references. A lazy grid (`GalleryView.swift:117`)
does not make the model's images lazy. The `thumbnail` comment saying “loaded
lazily” is inaccurate for an opened folder.

For scale only, a decoded 1024×683 4-byte image is 2.67 MiB, or 13.0 GiB for
5,000. This is an arithmetic upper working-set illustration, **not measured
RSS**: NSImage decode retention depends on whether images are drawn and AppKit
cache behavior. Encoded image data is also retained for every photo. Repeated
`visible` filtering and per-thumbnail observation of the whole `photos` array
adds full-list work (`Library.swift:97-108`, `GalleryView.swift:122`,
`Filmstrip.swift:161`).

**Smallest fix/check:** move live image ownership into a cost-limited NSCache,
request visible cells plus a small prefetch window, keep model rows as metadata
and identity. Measure warm browse of a 5,000-file fixture while scrolling all
cells, then leaving the folder; assert an intentional decoded-byte ceiling.
Do not start by replacing SwiftUI: model retention exists independent of cells.

### 8. P2 — Folder opens race and old thumbnail jobs keep running

`Library.open` awaits detached scanning (`app/Library.swift:240-242`) and installs
its result unconditionally (`:244`). If slow A finishes after later B, `folder`
can still say B while `photos` becomes A. `loadTask` is replaced (`:250`) without
cancelling the prior task; its task group keeps scheduling all remaining work
(`:270-272`). The apply URL guard (`:288`) protects individual row assignment,
not the listing or CPU/I/O consumed for abandoned folders. Six workers is a
per-open bound, not a session bound. Product `openFile`/`openFolder` permit this
through separate tasks (`OrionApp+Files.swift:222,234`).

A cold metadata load can also return old marks after the user already rated the
photo: `apply` overwrites marks (`Library.swift:291`) without checking the
sidecar identity or a per-row edit generation.

**Smallest fix/check:** one scan generation, cancel the previous load task, and
check cancellation before spawning more jobs and before applying a result.
Retain the index/URL guard as well. Inject ordered scans to complete A after B;
assert B remains installed and abandoned tasks stop scheduling. Separately
hold a marks result across a rating change and assert it cannot revert the UI.

### 9. P2 — Batch export ignores restore failure and performs a redundant full render

`BatchExportDriver.swift:111` uses default `restoring: false`, rendering as-shot
before restoring an edited photo. It then discards restore's Bool (`:113`) and
exports as-shot even when a saved develop payload cannot decode. The normal
product loader handles both cases (`OrionApp+Files.swift:329-347`), as does the
agent's rejection of an unreadable payload (`AgentCLIDriver.swift:211-213`).
Batch therefore reports success on an unedited deliverable and pays an avoidable
full render for every edited file. Source trace only; no GPU run here.

**Smallest fix/check:** read the saved payload first, pass `restoring: saved !=
nil`, throw/report when restoration fails. Run a batch with one malformed
sidecar and verify that photo is failed, never exported as a successful default.
Raster restoration is separately referred to the mask audit: batch does not
call `restoreMattes`. `AgentCLIDriver.openEngine` now restores mattes and refuses
missing files (`AgentCLIDriver.swift:250-256`, upstream `f8598cd`).

## Other inspected behavior and limits

- Snapshot files refuse writes when unreadable and write atomically; the count
  is bounded at 100. History is bounded at 50. These bound entry counts, not
  arbitrary external file byte sizes. Automatic pre-restore snapshot failure
  is logged only (`Snapshots.swift:286-287`); undo remains available in-session.
- Sidecars, snapshot files and proposals use atomic writes. Export writes its
  destination directly (`engine/src/util/ImageWriter.mm:434-445`), so an
  interrupted replacement can leave a partial output. The batch collision plan
  checks existing names before work; it is not an exclusive reservation.
- `ProposalWatcher` separates proposed and committed state and suppresses
  autosave during preview. Approval removes the proposal even if its flush
  failed (`Proposal.swift:222-223`, `ProposalWatcher.swift:199-203`); pending
  autosave retains the approved state only until finding 2 is triggered or
  the process quits. This deserves a failed-approval persistence test.
- MCP `get_stats` creates an engine and renders a full frame via
  `AgentCLIDriver.runStats/openEngine`, and every call spawns a process
  (`mcp/server.ts:29-40`). “Cheap” there is token cost, not compute or RAM.
  Concurrent tool requests have no renderer concurrency bound. This is a
  memory-pressure multiplier with the GUI open; no concurrent-process RSS
  experiment was run. Proposed-state and regional stats/proxies and
  `detect_faces` exist after upstream; regional inspection still materializes
  the full developed frame (`AgentInspect.swift:29-31`). `list_folder` has only eight RAW extensions
  (`server.ts:13`) against the desktop's longer shared list.
- Creative LUT identity/data is not in `DevelopState`, only `lutStrength`
  (`Engine.swift:276,529,554`; `Engine+Document.swift:15-39`). Persistence and
  cross-photo LUT lifecycle need a separate product test; this report does not
  assume the intended lifecycle from the UI label.
- Trash moves RAW first then owned siblings, reports failures, and removes only
  successful RAWs from the library. It rescans a folder for each trashed photo
  (`Library.swift:496-499`), a straightforward avoidable scaling cost.

## Read coverage

**Full source/logic review:** `Library.swift`, `PhotoIndex.swift`,
`Engine+Document.swift`, `Engine+Render.swift`, `Autosave.swift`, `Sidecar.swift`,
`BatchExport.swift`, `BatchExportDriver.swift`, `PhotoSelection.swift`,
`Proposal.swift`, `ProposalWatcher.swift`, `ExportPanel.swift`,
`ExportSettings.swift`, `ExportProbe.swift`, `Snapshots.swift`,
`SyncSettings.swift`, `AgentCLIDriver.swift`, `mcp/server.ts`, `TrashPlan.swift`,
`HdrMergeFlow.swift`, `HdrMergeDriver.swift`. Some files were read with full
line-numbered code and comments omitted to reduce output; comments directly
relevant to findings were read separately.

**Targeted call-site/data-model review:** `OrionApp.swift`,
`OrionApp+Files.swift`, `OrionApp+Commands.swift`, `OrionApp+Chrome.swift`,
`DevelopPanels+Presets.swift`, `Engine.swift`, `Engine+Compare.swift`,
`EditHistory.swift`, `Presets.swift`, `CurveSupport.swift`, `Filmstrip.swift`,
`GalleryView.swift`, `Scenario+Workflow.swift`, viewport sidecar/agent test
call sites, `ImageWriter.mm:425-450`, app build source list. Not a full visual
or accessibility audit of all UI controls. No C++ Pipeline or mask-internals
review, which belong to sibling audits.

**Context:** read `planning/STATUS.md` first, root instructions, vision,
architecture/UI decision and preceding engine/UI audit; skimmed feature,
roadmap/research and relevant decision-ledger material. The decision ledger is
extremely large (127k tokens); this report does not claim every historical
prose entry was reread. Applied SwiftUI Expert performance/list/image guidance
and Superpowers systematic debugging. No product code was edited.

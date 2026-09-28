# GUI batch export safety — 2026-09-28

**Implementation and verification in progress.** This report records the scope
and evidence required for the batch story; no gate result or defect closure is
claimed until the final tree is reviewed and exercised.

The initial focused probe on a 64×64 fixture reported no failures, a main-queue
heartbeat before job two at about 4.736 ms, RSS 216.9 MB and process footprint
228.6 MB with zero swaps. These are focused fixture observations, not a
full-resolution latency or RAM result. A controlled old-path mutation was
underway afterward; its owned build was stopped when kernel memory pressure
reached level 2. The owned process exited 143 and the safe source was restored;
the final A/B lens and proposal checks remain uncompiled. Memory pressure
still blocks executable checks. No nine-gate result follows from this
interrupted mutation run.

The first independent scoped **source-only** review of checkpoint `efae4f1`
found three important gaps under repair: present but ill-typed saved edit fields
could decode to defaults; valid single-quoted or spaced XML attributes could
evade the Develop payload extractor; and the batch keyboard guard could block
Tab/Space activation of visible Stop. None is closed by the provisional probe.
Checkpoint `dc02241` addresses these in source with opt-in strict typed reads,
native namespace-aware XML extraction and a real Editor Escape event probe.
The independent source re-review found no remaining Important or Critical
finding in those changes. **This is source review only:** the revised build,
probe, actual event delivery and nine gates have not run.

The initial desktop audit reproduced a cross-photo autosave overwrite and a
dropped failed save using pure Swift probes, then traced the actual GUI batch
through `Editor`, `Engine.onEdit`, `Autosave`, and the shared batch driver. It
also found a synchronous loop that prevented Stop/progress between photographs
and a shared loader that could export as-shot after malformed saved edits. See
`2026-09-28-desktop-io-audit.md` findings 1, 2, 6 and 9. Those probes did not
exercise the real GUI and GPU path, so the new product regression must do so.

## Closure evidence pending

| Behavior | Required evidence | Result |
|---|---|---|
| A's sidecar and live session survive shared-engine batch | Real `onEdit`/autosave callback, distinct A/B edits, sidecar byte comparison, state and undo/redo assertions after success, cancel and failure | Pending |
| An outgoing failed save prevents borrowing the engine | Inject a failing save and check that the batch does not start or discard owed edits | Pending |
| Stop/progress can run before job two | Main-queue heartbeat and cancellation before the second export begins | Pending |
| Malformed saved edits and missing mattes fail safely | Shared driver opens a small fixture, rejects invalid state and missing required matte without a successful as-shot export | Pending |
| A failed restore cannot label B's pixels as A | Inject restore failure and inspect the visible/document state and reported error | Pending |
| Nine repository gates on final tree | Serial runs with exit/status logs and memory observation | Pending |

Per-photo decode, render and export remain synchronous in this story, so a
single photo can still block the main thread. An active creative LUT is refused
by the GUI batch until LUT identity and persistence can be restored safely.
These are disclosed limits, not closed findings.

The full audit goal remains open: foreign Lightroom XMP preservation, malformed
sync input, HDR/output collision and atomic standard export, mask correctness,
library thumbnail bounds and folder cancellation, 42 MP RAM and latency, UX
interaction, whole-source coverage, and measured comparison with **Adobe
Lightroom desktop Local**. No Lightroom run or paired competitor result exists.
Normal photo switching can also still drop an owed failed autosave when B is
edited: this batch-specific outgoing-save refusal does not change `Autosave`'s
single pending tuple. The desktop audit's finding 2 remains open.

## Orchestration choices and checkpoint checks

| Ruling | Cost retained |
|---|---|
| Reuse one checkout on a local branch, with one code implementer | Avoids duplicate build/device memory; provides no worktree isolation, so file ownership must remain explicit. |
| Refuse GUI batch with a live creative LUT until its identity and pixels can be restored | Prevents losing A's LUT or leaking it into B; this workflow remains unavailable. |
| Allow scoped source review while memory pressure holds executable validation | Finds source defects now, but cannot establish compilation, keyboard event delivery or runtime safety; final build, focused checks and all nine gates are still required. |

The controller ran only the lightweight decision-ledger gate and whitespace
check after documentation froze: `check-decisions.py` passed (285 rows, range
1–287, three declared gaps, all cited numbers resolve); `git diff --check`
passed. This does not validate the product or replace the pending nine-gate run.

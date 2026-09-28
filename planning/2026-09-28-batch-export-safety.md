# Safe, interruptible GUI batch export

Continuation of the full performance/RAM/UX/masking/file-handling goal, after
cleanup delivery 4479927. The previous turn made progress: reviewed cache
fix, artifacts removed, nine gates passed and main pushed. This story closes
the batch-related audit findings; it does not establish Lightroom parity.

## Global constraints

- Preserve originals, sidecars, mattes, snapshots, live edits and undo history.
- Keep one shared engine; do not retain a second full-resolution pipeline.
- No Rust, Vulkan, GPL code, new dependencies or filter mathematics.
- Reuse existing document, batch, history and test mechanisms. No generic job
  framework or new persistence layer. Keep files below the project's limit.
- GPU/build work is serial; build with at most -j2 and use small fixtures.
- Preserve decisions 34, 71b, 79, 99, 215, 216, 248 and 285.
- One roadmap story: safe batch workflow. Other audited defects stay queued.

## Task 1 — Make the real batch workflow safe and interruptible

Ownership: batch driver/model, Editor batch orchestration and guards, the
minimal history/engine helpers and app test harness needed for this workflow.
Trace every caller before changing a shared boundary.

Before borrowing the engine, reject an in-flight open or live proposal,
capture A's live document and undo/redo position, and suspend autosave/proposal
observation. If the outgoing save fails, refuse to start and preserve its
pending state. Batch renders must never become autosave edits for A.

Restore A's captured live state, required mattes and exact undo/redo history
after success, cancellation or export failure. Rearm saving only after A is
valid again. A restore failure must report an error and prevent B's image or
edits from being displayed/saved as A. Avoid the normal document reload path
that resets undo or replaces live state from disk.

Give the GUI loop a real suspension between jobs, so progress and cancellation
can run before the next photo starts. Prevent competing document/engine actions
while the batch borrows it. Preserve the CLI behavior through the shared driver.
One photo remains synchronous; record that responsiveness ceiling honestly.

At the shared batch-open boundary, read saved edits before decoding, suppress
the redundant default render, reject malformed saved state, restore mattes and
refuse missing required mattes. Never report an unedited fallback as success.

Add a minimal runnable regression through the actual GUI-used orchestration
and real onEdit/autosave callback, using small generated/copied fixtures. It
must protect A's sidecar bytes, live state, undo/redo, failed-save refusal,
cancel/heartbeat before job two and restore failure. Shared-driver checks
cover malformed edits and required mattes. Prove the relevant old behavior or
mutation fails. Do not rely solely on the existing fresh-engine CLI gate.

## Task 2 — Verify, review, document and integrate

Run the nine repository gates on the final product tree, serially with memory
monitoring. Scope-specific tests run once per changed implementation; reviewers
inspect evidence instead of repeating the suite. Update STATUS, prune recent
sessions, record new decisions with reasons, and mark exactly which audit
findings are closed. Include the parallel source coverage inventory without
claiming exhaustive review. Independent scoped and final branch reviews precede
integration. Keep the full goal active and use ordinary, non-forced Git updates.

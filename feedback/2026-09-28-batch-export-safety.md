# GUI batch export safety — 2026-09-28

**Implementation and verification in progress.** The batch story remains open
until the keyboard check and final integration review are complete. The results
below distinguish passing checks from the remaining verification gap.

The current combined-tree evidence is in the follow-up section below.

At the earlier `3094212` checkpoint, the full `-j2` build and eight of nine repository gates passed.
`check-modes.py` exits 1 only on the `--batch-safety` Escape monitor check:
49/50 focused checks pass, but the locked desktop gives the probe no key/active
window (`lifecycle=true`, `key=false`, `active=false` after 2 s). The event
reaches Stop; the local monitor's role is unproved. A key-swallow mutation
survived 50/50 under the same condition, so this is a real oracle gap, not a
keyboard closure. The 64×64 post-job timer read 0.037166 ms in that failing
gate, not a full-resolution responsiveness result. All 15 sample inventory
entries hashed identically before/after the serial run; 23 pre-existing
`/tmp` test fixtures were restored. Kernel pressure stayed at level 1. Logs:
`/tmp/orion-final-gates-sr6d6f7w/`.

Earlier, the first compiled strict-XML probe failed 12 checks because saved
Develop payloads appeared absent. A source fix made those checks pass; CLI
success/missing/malformed smoke passed 3/3. The old-path mutation then reddened
preservation, between-job Stop and failed-save refusal, with safe source
restored. A separate 30/30 run on earlier source reported ~4.735916 ms from
**session start to a timer before job two**, including preflight, job one and
assertions. `4a18848` corrected that origin to first-progress scheduling.
None of these small-fixture timings measures 42 MP work. The earlier mutation
build interrupted by pressure level 2 produced no result.

The first independent scoped **source-only** review of checkpoint `efae4f1`
found three important gaps: present but ill-typed saved edit fields
could decode to defaults; valid single-quoted or spaced XML attributes could
evade the Develop payload extractor; and the batch keyboard guard could block
Tab/Space activation of visible Stop. None is closed by the provisional probe.
Checkpoint `dc02241` addresses these in source with opt-in strict typed reads,
native namespace-aware XML extraction and a real Editor Escape event probe.
The independent source re-review found no remaining Important or Critical
finding in those changes. Later source and probe corrections reached
`3094212`. **Source review did not establish runtime correctness:** the first
compiled probe found the XML failures above, and the key-swallow mutation
showed that an Escape event reaching Stop does not prove the local monitor ran.
A key-window positive control that reddens on that mutation is required for
keyboard closure.

The initial desktop audit reproduced a cross-photo autosave overwrite and a
dropped failed save using pure Swift probes, then traced the actual GUI batch
through `Editor`, `Engine.onEdit`, `Autosave`, and the shared batch driver. It
also found a synchronous loop that prevented Stop/progress between photographs
and a shared loader that could export as-shot after malformed saved edits. See
`2026-09-28-desktop-io-audit.md` findings 1, 2, 6 and 9. Those probes did not
exercise the real GUI and GPU path, so the new product regression must do so.

## Combined-tree follow-up at `203d116`

Verified histogram work from main `3dceaa0` merged without product-source
conflicts. In an unlocked session, safe-before passed **50/0** with lifecycle,
key window, monitor window and app active all true; Stop activated. The
key-swallow mutation then failed **49/1**, exactly the Escape assertion, with
the same true prerequisites and Stop inactive. This is the previously missing
positive/mutation contrast. Safe Commands bytes were restored exactly and the
safe build passed, but the desktop relocked before its final run: **49/1** only
because the key/monitor/active prerequisites were false. Final restored-safe
confirmation is therefore still pending; do not repeat the valid mutation or
rebuild merely because the desktop is locked. Evidence:
`/var/folders/n2/fp41fkxn2nz96bbnn693mlj00000gn/T/orion-batch-key-unlocked-r154zqx5/`.

A fresh combined-tree `-j2` build and **eight independent gates pass**: engine
1192/0, viewport 4269/0, decisions 286 rows, gestures 6, screens 3 asserting +
1 byte-stable, wiring 497 swept/8 harness-only, agent 21/21, site green.
**Modes was explicitly deferred and never launched**, pending the final safe
keyboard check. All observed pressure readings stayed level 1. The 15-entry
sample inventory matches the last histogram run and this run's before/after;
all 23 protected temporary outputs were restored by hash/type/mtime. Logs:
`/tmp/orion-batch-combined-gates-89x1xy48/run-ag1rpzms/`. Independent integration
review found no new source blocker, but retains the final keyboard/gate condition.

## Closure evidence pending

| Behavior | Required evidence | Result |
|---|---|---|
| A's sidecar and live session survive shared-engine batch | Real `onEdit`/autosave callback, distinct A/B edits, sidecar byte comparison, state and undo/redo assertions after success, cancel and failure | Passed focused 64×64 checks; old-path mutation reddened preservation |
| An outgoing failed save prevents borrowing the engine | Inject a failing save and check that the batch does not start or discard owed edits | Passed focused check and old-path mutation |
| Stop/progress can run before job two | Main-queue heartbeat and cancellation before the second export begins | Passed focused check; 0.037166 ms after first-progress scheduling on 64×64; old-path mutation red |
| Malformed saved edits and missing mattes fail safely | Shared driver opens a small fixture, rejects invalid state and missing required matte without a successful as-shot export | Passed focused checks after the XML fix; first compiled probe's 12 failures are retained above |
| A failed restore cannot label B's pixels as A | Inject restore failure and inspect the visible/document state and reported error | Passed focused 64×64 checks |
| Nine repository gates on final tree | Serial runs with exit/status logs and memory observation | **Eight pass at combined `203d116`; modes explicitly deferred.** Engine 1,192/0; viewport 4,269/0; decisions, gestures, screens, wiring, agent and site green |
| Escape routes through the Editor's local key monitor | Key/active-window positive control and key-swallow mutation causing Stop failure | **Contrast proved:** safe-before 50/0 and key-swallow 49/1 with the actual key window. **Final confirmation open:** restored-safe build passes, but macOS relocked before its last probe |

Per-photo decode, render and export remain synchronous in this story, so a
single photo can still block the main thread. An active creative LUT is refused
by the GUI batch until LUT identity and persistence can be restored safely.
These are disclosed limits, not closed findings.

The full audit goal remains open: foreign Lightroom XMP preservation, malformed
sync input, HDR/output collision and atomic standard export, mask correctness,
library thumbnail bounds and folder cancellation, 42 MP RAM and latency, UX
interaction, behavioral proof across the source inventory, and measured
comparison with **Adobe Lightroom desktop Local**. No Lightroom run or paired
competitor result exists.
Normal photo switching can also still drop an owed failed autosave when B is
edited: this batch-specific outgoing-save refusal does not change `Autosave`'s
single pending tuple. The desktop audit's finding 2 remains open.

## Orchestration choices and checkpoint checks

| Ruling | Cost retained |
|---|---|
| Reuse one checkout on a local branch, with one code implementer | Avoids duplicate build/device memory; provides no worktree isolation, so file ownership must remain explicit. |
| Refuse GUI batch with a live creative LUT until its identity and pixels can be restored | Prevents losing A's LUT or leaking it into B; this workflow remains unavailable. |
| Allow scoped source review while memory pressure holds executable validation | It found source defects before compilation; later executable checks exposed the XML defect. Source review alone supplies no runtime proof. |
| **Superseded:** count direct Escape dispatch to Stop without foreground focus | Direct dispatch can reach Stop while the local monitor is bypassed; the key-swallow mutation stayed green 50/50. This oracle cannot cover product keyboard handling. |
| Require an actual key window for Escape coverage | The host GUI session reported `CGSSessionScreenIsLocked=True`; the harness could not establish `keyWindow` or app activation. The combined-tree safe/mutation contrast now proves that route; only restored-safe confirmation and modes remain. Programmatic event delivery still does not establish physical keyboard focus/navigation. |

The earlier `3094212` serial run passed `check-decisions.py` (285 rows, range 1–287,
three declared gaps) and seven other gates; `check-modes.py` is the one failure.
`git diff --check` passed. The existing `/tmp` fixture outputs and all sample
contents were preserved as described above.

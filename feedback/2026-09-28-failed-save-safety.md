# Failed-save departure safety — 2026-09-28

Decision #289; base `3dceaa0`, product source `61b7e29`.

## Failure and correction

Autosave keeps one pending photograph/state pair. A rejected write did not stop
`Editor.load` from leaving that photograph, so the next photograph's edit could
replace the pending pair. Current-photo Trash could also move the photograph
and its companions while the write was still owed.

`flushBeforeLeaving` now gives the editor a shared refusal point before it
changes document state. Plain filmstrip selection and gallery opening obey the
same refusal. Trash containing the current photograph refuses the whole set
before moving any member; other-photo Trash remains usable.

Folder scans stage the existing result, then flush immediately before installing
the listing and loading its photograph in the same main-actor turn. This sees
an edit made during the scan. Named Open Photo still schedules decode
immediately: its background listing may commit only while that URL remains
current, and never reloads it or drops its later pending edit.

## Behavioral evidence

| Check | Result |
|---|---|
| Original unsafe actions, successfully built | `--save-safety`: 18 checks, 7 failures across direct load, plain filmstrip, gallery and current-photo Trash |
| Corrected mounted Editor actions | `--save-safety`: 38 checks, zero failures after the review fix |
| Viewport suite | 4,275 checks, zero failures; includes failed preflight, further pending edit, return-to-saved, retry and separate B save |
| Deliberate later-listing reload | 38 checks, exactly 3 failures: pending edit, sidecar baseline and extra decode; safe restoration passes all 38 |
| Initial source identity | SHA-256 of the sorted 12 source/test paths plus NUL plus bytes: `af598d17cf20e296f29db5083fccc09edbcaa25c993989f588ca010a233c668f` |

The app probe calls the actual Editor actions and verifies current identity,
live state/history/selection, unchanged sidecar bytes, continued edit callbacks,
late scan preflight, retry and later B saves. It mounts SwiftUI State without
requiring a key window. Its only photographs are unique temporary copies of
the existing 64×64 DNG. Library's injected filesystem move archives those copies
privately; the user's Trash is never used.

Final review-fix source hash (two paths plus NUL plus bytes):
`5c0f3e1cb4440a2c4a066e25c7d26249b8bea8b3e42b689b38aa848a7d33aced`.
The strengthened probe waits for the first decode, makes a real edit, then
allows a delayed listing. A new private D fixture proves the listing committed;
the existing session log proves only one decode occurred after the engine settles.

Focused evidence: `/tmp/orion-save-red-probe.log`,
`/tmp/orion-save-final2-build.log`, `/tmp/orion-save-final-probe.log`, and
`/var/folders/n2/fp41fkxn2nz96bbnn693mlj00000gn/T/orion-save-focused-4g8au4m2/viewport.log`.
The focused suite restored all 23 protected `/tmp` outputs and preserved the
15-entry sample inventory. Final mutation/safe logs are respectively
`/var/folders/n2/fp41fkxn2nz96bbnn693mlj00000gn/T/orion-save-round1-j8mjvz3b/probe.log`
and `orion-save-round1-nqlqkfje/probe.log` under that same temporary parent;
final safe build: `/tmp/orion-save-round1-safe-final-build.log`. Peak observed kernel pressure was 1; builds used
`-j2`, with no concurrent app/compiler/GPU work.

An intermediate build caught a probe source edit during compilation. It is
invalid build evidence, not a behavioral failure; the frozen source was then
rebuilt successfully. The earlier histogram story's temporary-fixture protection
miss remains documented in its own report; these checks do not undo that miss.

## Delivery verification

Task review and one scoped fix re-review are clear. Review required the
pending-edit/no-second-load Open Photo check; the strengthened probe passes and
catches the deliberately reintroduced reload. Its initial `/var` versus
`/private/var` assertion was corrected to use the unique fixture filename.
A bounded engine-settling wait makes the decode count judge completed work.

At frozen product source `61b7e29`, the fresh `-j2` build and **six of nine gates**
pass. Full verification is incomplete; the branch is not ready for integration.

| Gate | Result |
|---|---|
| Engine | 1,192 checks, zero failures |
| Viewport | 4,275 checks, zero failures |
| Decisions | 287 rows, 1–289, three declared gaps; all 246 cited numbers resolve |
| Gestures | 6 arm/disarm checks |
| Wiring | 501 product functions swept, 8 harness-only accounted for |
| Site | All checks pass |
| Screens | Stopped by the owned-process-group guard at kernel pressure 2 after 6.1 seconds; one retry after normal recovery also stopped at pressure 2 after 6.0 seconds |
| Modes, agent | Not launched in this final gate run after the repeated pressure warning |

The screenshot stops are resource-limit results, not passing assertions or
identified product regressions. No third full-size attempt was made. Pressure
returned to 1 after each stop, and no Orion process remained after the first.
Lightweight wiring/site gates then passed at pressure 1. Each verification
phase restored all 23 temporary fixtures with identical type/hash/mtime;
all 15 sample entries matched both the previous histogram baseline and the
phase's before/after inventory.

Logs and machine-readable results are under
`/var/folders/n2/fp41fkxn2nz96bbnn693mlj00000gn/T/orion-failed-save-gates-7urmxgav/`:
`run-7bklaagu` (initial), `run-7it9nvvv` (screens retry), and `run-psixue9n`
(lightweight checks). Keep the SDD workspace and branch for continuation.
Final whole-branch source review is pending. Pending batch source is separate;
its gate evidence does not cover this branch.

## Rulings and limits

| Ruling | Cost or limit |
|---|---|
| Reuse the existing checkout and build on a branch from verified main | Replaces the batch binary; rebuild that branch before its remaining safe keyboard check |
| Stage the existing Library scan tuple, then gate commit and load together | The old listing remains visible during a scan; existing overlapping-scan ordering and cancellation are still open |
| Scope this story to photo/folder departure and Trash; preserve proposal stop/begin semantics | Quit/window-close veto is still absent; quitting despite a failed-save warning can lose the owed edit |
| Keep immediate named-photo decode and accept its later listing only for the same current URL | URL identity is the guard; a mistake here could install stale selection/listing state, which the probe checks for its covered actions |
| Permit one default-nil async hook to sequence the real Open Photo probe before its listing scan | A probe-only seam must not delay the default production path; the separate late scan-decision check still covers edits arriving at commit |

The focused checks cover the corrected failed-save departure paths. Full
delivery remains gated; other file-handling findings also remain open. Foreign XMP preservation, malformed sync/proposal input, pending batch
safety and output destination hazards remain separate work. No physical input,
VoiceOver, full-resolution RAM/latency or Lightroom comparison is proved by the
tiny fixtures.

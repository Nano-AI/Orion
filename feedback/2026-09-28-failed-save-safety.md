# Failed-save departure safety — 2026-09-28

Decision #289; base `3dceaa0`, final source `0622da0`, verified at `5495328`.
Verified and locally installed on 2026-09-29; main fast-forwards to this source.
Earlier checkpoint evidence below remains as a record of the initial holds.

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

## Initial checkpoint verification (2026-09-28)

Task review and the first scoped fix re-review are clear for `61b7e29`. Review required the
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
Final whole-branch source review found two Important gaps: a canonical library
URL could bypass the Trash guard for an alias-opened current photo, and Shift
with no visible anchor lost its fallback navigation. Source checkpoint
`0622da0` addresses these locally: Trash snapshots resolved path keys before
moves for refusal, survivor choice and completion; document/save URLs stay as
opened. A Library callback overload evaluates the existing selection policy
once on a copy, routes navigation through the save guard, and commits only
selection-only results. Added probe cases cover alias refusal/retry and missing
or filtered Shift anchors; the alias retry explicitly restores develop mode.

**These final edge-case changes are unbuilt and unexecuted.** The attempted
pre-fix RED build was terminated at kernel pressure 2 (build exit -15, wrapper
241; `/tmp/orion-save-final-review-red-build.log`). No behavioral RED probe ran.
No further compiler/app/GPU/viewport runs were attempted. This also means the
build artifact must be rebuilt before further app verification. Earlier 38/0
and 4,275/0 results do not validate the final changes.

The final four-file hash is
`10e498fa78ec30624c3e17e7a7cbda333ce4b751c63624524d05f930ec105a6d`
(path + NUL + bytes, ordered Filmstrip, Library, OrionApp+Files, SaveDepartureProbe).
The post-stop 23-file/15-sample inventory is at
`/tmp/orion-save-final-review-held-inventory.json`; it is a current inventory,
not before/after proof for this source-only wave. No app or fixture runner ran
in that wave. The one scoped static re-review found both issues addressed in source and no
new breakage. It explicitly refused merge readiness because the final source
is unbuilt and its checks are unrun. Keep this as a branch checkpoint until
verification is complete. The final source/doc checkpoint also passes
`check-decisions.py` (287 rows, 246 citations), `check-wiring.py` (501 swept,
8 harness-only) and `git diff --check`; these are source checks, not substitutes
for compilation or runtime verification. Pending batch source is separate;
its gate evidence does not cover this branch.

## Delivery verification (2026-09-29)

The final source now builds and passes all nine gates. `cmake --build build -j1`
serializes the independent app and viewport Swift commands. It passed in 117.3
seconds at peak kernel pressure 1. The first screenshot attempt still reached
pressure 2 and its owned process group was stopped. After approved desktop apps
and additional user-managed terminal sessions closed, pressure stayed normal;
only the remaining gates were resumed, with no source changes.

| Check | Final result |
|---|---|
| Engine / viewport | 1,192 / 4,275 checks, zero failures |
| Decisions / gestures | 287 rows and 246 citations resolve / 6 checks |
| Screens | 3 asserting scenes and 1 byte-stable scene pass |
| Modes | Save safety 50; library 13; batch 2 files; HDR DNG succeeds |
| Wiring / agent / site | 501 swept, 8 harness-only / 21 of 21 / all pass |
| Packaged app probe | 50 save-safety checks, zero failures, pressure 1 |
| Installation | Strict deep signature verifies; installed binary matches package; normal Orion window observed |

The final alias refusal/retry and missing/filtered Shift-anchor cases pass.
Their pre-fix behavioral RED remains unobserved because that earlier build was
stopped; no RED claim is retroactively made. The original shared departure RED
and the three-failure reload mutation remain separate valid evidence.

Logs under `/var/folders/n2/fp41fkxn2nz96bbnn693mlj00000gn/T/orion-main-install-f0hniusp/`:
`run-2rz53qrl` (build/first gates/guarded screenshot stop), `run-fg63qgvy`
(remaining gates pass), `package-log`, and `packaged-save-probe`.
Both gate phases restored all 23 temporary fixtures unchanged and kept all 15
sample entries equal to both the previous baseline and before/after manifests.

`tools/package-app.sh` used a fresh, task-owned output directory. Its bundled
resources/dependencies, removed Homebrew search paths, privacy checks and ad-hoc
signature all pass. The installed binary at `/Applications/Orion.app` has SHA-256
`f2cd784b86c86d288c9c0e29d25b0d85bd45b72ef45b796c59193a2f93e34215`.
The previous app is preserved as `previous-Orion.app` under the same log root;
`installation.json` records the replacement and rollback paths. The app opened
normally with no photograph loaded and pressure 1. No release was published.

## Rulings and limits

| Ruling | Cost or limit |
|---|---|
| Reuse the existing checkout and build on a branch from verified main | Replaces the batch binary; rebuild that branch before its remaining safe keyboard check |
| Stage the existing Library scan tuple, then gate commit and load together | The old listing remains visible during a scan; existing overlapping-scan ordering and cancellation are still open |
| Scope this story to photo/folder departure and Trash; preserve proposal stop/begin semantics | Quit/window-close veto is still absent; quitting despite a failed-save warning can lose the owed edit |
| Keep immediate named-photo decode and accept its later listing only for the same current URL | URL identity is the guard; a mistake here could install stale selection/listing state, which the probe checks for its covered actions |
| Permit one default-nil async hook to sequence the real Open Photo probe before its listing scan | A probe-only seam must not delay the default production path; the separate late scan-decision check still covers edits arriving at commit |
| Normalize file identity locally in Trash, before any file moves, while retaining document/save URLs | A missed alias could evade refusal or leave a deleted photo active; global normalization could change existing sidecar locations, so actual alias refusal/retry checks cover the narrower change |
| Stop heavy work at warning pressure and initially push only a source checkpoint | Alias/Shift changes stayed unbuilt until adequate headroom became available; the build, focused checks and all nine gates passed on 2026-09-29 |

The focused checks and full gates cover this failed-save departure story.
Other file-handling findings remain open. Foreign XMP preservation, malformed sync/proposal input, pending batch
safety and output destination hazards remain separate work. No physical input,
VoiceOver, full-resolution RAM/latency or Lightroom comparison is proved by the
tiny fixtures.

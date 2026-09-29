# Batch branch review — 2026-09-28

Independent whole-branch source review of `4479927` through `a28f450`, using the full diff and relevant unchanged callers. The reviewer ran no build, app, GPU work or tests.

**Not ready to merge:** required keyboard validation remains incomplete. No additional Critical or Important source defect was established in the batch implementation; this is not integration approval.

## Original review at `a28f450`

| Severity | Evidence and required action |
|---|---|
| Important — validation blocker | `BatchExportProbe.swift:343-374` correctly requires its actual key window. The final mode log records lifecycle installed, no key/active window, and Stop activation despite the bypassed monitor. The prior swallowed-key mutation stayed green. Required closure: safe GREEN with the actual key window, swallowed-key RED for Stop, safe source restored, and `check-modes.py` GREEN. The host console is locked; no skipped/pass fallback is accepted. |
| Critical source defect | None newly established. |
| Minor | No additional change required. |

The review verified capture/save/disarm ordering, restoration from live state and exact history, required mattes before rearming, and safe restore-failure identity handling. Between-job suspension and one-engine ownership match the plan. Strict decoding reuses the existing field roster, preserves absent legacy defaults and refuses present invalid types; XML supports namespace aliases, attributes and elements. Actual autosave callbacks, byte/state/history/rendered comparisons, failure injection and the old-path mutation provide useful evidence. Audit reports distinguish source reads from executed behavior.

## Review limits and controller disposition

| Behavior set aside by the reviewer | Controller disposition |
|---|---|
| Physical keyboard traversal, mouse input and VoiceOver | Remain open. The pending programmatic Escape proof does not establish these broader interactions. |
| 24/42 MP responsiveness and memory | Remain open; tiny-fixture results do not establish full-size latency or capacity. |
| Exhaustive hostile XML/JSON, numeric ranges and future versions | Not certified. This story adds typed saved-state refusal while preserving existing legacy defaults; further trust-boundary checks remain part of the wider audit. |
| Existing normal-switch/Trash failed saves, foreign XMP loss, sync/proposals, output finalization, LUT and mask defects | Remain queued in STATUS and the dated audits; this branch does not clear them. |
| The 228-file source inventory | Cumulative documented reads, not this reviewer's independent reread of every file or behavioral certification. |
| Lightroom comparison, vendored code, patent clearance and supported-OS packaging | Unverified; no parity, clearance or deployment-floor claim. |

The controller accepts these as explicit limits of this batch review, not waivers of the wider goal. Review proceeded while the locked desktop held keyboard validation; its cost is that source approval cannot substitute for the remaining runtime evidence. No integration approval follows from this review; the pending branch was later pushed as a checkpoint.

Verification at product `3094212`: successful full build; eight passing gates; `check-modes.py` exit 1 for the actual key-window check; 15 sample inventory entries unchanged and all 23 pre-existing temporary test outputs restored. Logs: `/tmp/orion-final-gates-sr6d6f7w/`. The previous cleanup delivery `4479927` is already on `origin/main`; the pending batch checkpoint was subsequently pushed at `d1be013`.

## Combined-tree review at `203d116`

Reused the prior whole-branch review and checked the integration delta after
merging verified histogram work. Batch product files are unchanged; native
histogram formats, bins, stride, API and scheduling remain compatible. No new
Critical/Important source blocker was found.

The reviewer inspected the new logs directly: safe-before **50/0**, all key
monitor prerequisites true and Stop true; key-swallow **49/1**, prerequisites
true and Stop false; restored-safe **49/1**, because macOS relocked and the
key/monitor/active prerequisites were false. Safe source hashes match and all
three builds passed. The valid mutation contrast is established. **Still not
ready to merge** until the restored-safe probe passes with a key window and
`check-modes.py` passes on the combined tree. Its eight independent gates have
now passed; modes was explicitly deferred. Prior declined-behavior limits stand.

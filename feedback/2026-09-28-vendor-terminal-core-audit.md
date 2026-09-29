# Vendored terminal core audit — 2026-09-28

Complete source/logic read of all **8,102 lines** of
`third_party/SwiftTerm/Sources/SwiftTerm/Terminal.swift` at the `203d116`
checkpoint. No build, test, app launch, memory measurement or vendor edit.
The narrow integration trace also read relevant excerpts of
`app/AssistantPanel.swift`, `EscapeSequenceParser.swift` and `CharData.swift`;
these are not additional complete vendor-file reads.

Orion embeds `LocalProcessTerminalView` and feeds it `/bin/zsh` plus the chosen
assistant CLI (`app/AssistantPanel.swift:86`). Its default scrollback is 500
lines. Process output therefore reaches these parser paths. Findings below are
source-supported risks, **not reproduced Orion failures or measured RAM**.

| Priority | Trigger and evidence | Existing limit / smallest useful check |
|---|---|---|
| P1 | **Negative palette reset index can trap.** `Terminal.swift:2567` rejects indices above 255 but accepts negative values; OSC 104 parses `-1` and indexes `ansiColors[-1]`. | No lower-bound guard on this path. Feed `printf '\033]104;-1\007'` to an isolated terminal process, never a live photo-editing session; assert the process survives and the palette remains valid. |
| P1 | **Inline image input has no byte cap before copying/decoding.** `Terminal.swift:2847` copies OSC 1337 base64 into `Data`, decodes it, then calls the image delegate. The parser accumulates the sequence without a byte cap. | The 4,096-pixel check limits requested display dimensions, not payload bytes. Use bounded synthetic input in an isolated process, measure peak allocation and verify refusal before copying/decoding; do not stress the user's host with an unbounded image. |
| P2 | **Evicted hyperlink payloads remain retained.** `Terminal.swift:2656` registers payload atoms. `garbageCollectPayload()` at `:6307` can release unused atoms but has no caller in Orion or the vendored tree. | Terminal destruction releases them. Print more than 500 distinct OSC 8 linked lines and check retention after scrollback eviction. Separately, the process-wide TinyAtom code counter (`CharData.swift:211`) never reuses released IDs; garbage collection alone does not settle eventual ID exhaustion. |
| P2 | **Unique complex characters remain indexed for the terminal's lifetime.** `Terminal.swift:1578` adds new non-BMP/combined `Character` values to two dictionaries; reset and bounded scrollback do not prune them. | Repeated text reuses entries. Use a bounded stream of distinct grapheme clusters, evict the lines and inspect retained entries/bytes. User-visible size and impact remain unmeasured. |
| P3 | **Saved title stacks are unbounded.** CSI `22;2t` pushes at `Terminal.swift:4026` use array concatenation and retain entries until popped or destruction. | Probe bounded repeated pushes, including a long title, then pops; measure retained data and work. No Orion slowdown was measured. |

These findings do not justify splitting or replacing a mature dependency based
on file length. Reproduce at the terminal boundary, then prefer a small upstream
fix or supported lifecycle limit. The other vendor sources remain outside this
read; no whole-dependency behavior, security or license certification follows.

## Bounded reproduction preflight

A standalone OSC 104 parser probe was considered, then stopped before compilation:
there is no reusable SwiftTerm module/library/object in this checkout. CMake
compiles its sources directly into Orion. Creating a new dependency build was
outside the bounded check. Neither index 0 nor index −1 was fed; there is no
runtime crash result. No app or child probe was launched and no source changed.
The preflight reported 61% system-wide free memory and released the heavy-work
lock. Exact commands are in `/tmp/orion-osc104-probe-check.md`.

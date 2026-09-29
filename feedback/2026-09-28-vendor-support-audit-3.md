# Vendored terminal support audit, roster 3 — 2026-09-28

Complete source/logic read of the following **21 files, 5,675 lines**. Each file was read from first to last line; the two larger files were read in bounded, adjoining ranges. This is source review only: no vendor edits, build, test, app launch, GPU use, network request, or memory/latency measurement. `MacTerminalView.swift`, `AppleTerminalView.swift`, and `Terminal.swift` were audited separately. Only narrow caller excerpts outside this roster were used to establish reachability.

| File under `third_party/SwiftTerm/` | Lines read |
|---|---:|
| `Sources/SwiftTerm/KittyKeyboardEncoder.swift` | 1–1,112 |
| `Sources/SwiftTerm/EscapeSequenceParser.swift` | 1–959 |
| `Sources/SwiftTerm/LocalProcess.swift` | 1–600 |
| `Sources/SwiftTerm/KittyPlaceholder.swift` | 1–500 |
| `Sources/SwiftTerm/CircularList.swift` | 1–446 |
| `Sources/SwiftTerm/SixelDcsHandler.swift` | 1–408 |
| `Sources/SwiftTerm/UnicodeWidthData.swift` | 1–316 |
| `Sources/SwiftTerm/Mac/MacLocalTerminalView.swift` | 1–223 |
| `Sources/SwiftTerm/HangulInput.swift` | 1–220 |
| `Sources/SwiftTerm/Mac/MacCaretView.swift` | 1–151 |
| `Sources/SwiftTerm/Pty.swift` | 1–136 |
| `Sources/SwiftTerm/Apple/PowerlineRenderer.swift` | 1–125 |
| `Sources/SwiftTerm/Apple/BlockElementRenderer.swift` | 1–105 |
| `Sources/SwiftTerm/HeadlessTerminal.swift` | 1–93 |
| `Sources/SwiftTerm/Bidi.swift` | 1–78 |
| `Sources/SwiftTerm/ExtensionsTerminal.swift` | 1–76 |
| `VERSION` | 1–48 |
| `Sources/SwiftTerm/Apple/Extensions.swift` | 1–31 |
| `Sources/SwiftTerm/SearchOptions.swift` | 1–25 |
| `Sources/SwiftTerm/Mac/MacAccessibilityService.swift` | 1–15 |
| `Sources/SwiftTerm/File.swift` | 1–8 |

Orion's `AssistantTerminalView` constructs `LocalProcessTerminalView` (`app/AssistantPanel.swift:86–92`) and starts `/bin/zsh` with a chosen working directory (`:107–121`). Its default `LocalProcess` delivers output on the main queue (`Mac/MacLocalTerminalView.swift:90–94`, `LocalProcess.swift:124–129`); `dataReceived` feeds the terminal (`Mac/MacLocalTerminalView.swift:204–209`). Thus macOS PTY, parser, sixel, and AppKit paths below are reachable through assistant CLI output. `HangulInput.swift` is an iOS keyboard helper and is not an Orion input path; `HeadlessTerminal.swift` is a different, unused host. Metal renderer code is compiled conditionally but its shader resource is deliberately omitted and the view uses CoreGraphics (`VERSION:7–16`); the conditional graphics helpers themselves are available on macOS.

| Priority | Source-supported finding and existing limit | Smallest bounded check |
|---|---|---|
| **P1** | **A short sixel sequence can trap on integer overflow.** The parser dispatches `DCS q` to `SixelDcsHandler` (`EscapeSequenceParser.swift:609–618`). Its `nextInt` multiplies an output-controlled decimal with ordinary checked `Int` arithmetic, without a length or value bound (`SixelDcsHandler.swift:33–49`). `!` passes that value to the repeat count (`:220–242`). A 20-digit number followed by a sixel byte reaches the multiplication before any large allocation is needed. The CSI/DCS *parameter header* has a 24-item/UInt16 saturation limit (`EscapeSequenceParser.swift:354–379,795–807`), but that limit does not apply to the sixel body. | Feed `ESC P q #0 !<20 decimal digits>~ ESC \\` to an **isolated terminal subprocess**, choosing digits above `Int.max`; assert it refuses or survives. Do not run an enormous valid repeat value in Orion. Crash reachability is source-derived; no process was launched. |
| **P1** | **Sixel body and expansion have no effective size cap.** `put` appends all DCS bytes (`SixelDcsHandler.swift:22–31`); `unhook` allocates `maxX * maxY * 4` (`:82–123`), while repeat counts drive `for _ in 0..<reps` in both passes (`:220–242,291–316`). Even a compact decimal repeat can request far more decoded pixels/work than input bytes. PTY pending output is capped at 4 MiB with a 1 MiB resume threshold (`LocalProcess.swift:96–105,132–152`), but that controls queued **input**, not one decoder expansion. | In an isolated process, send a small sixel with a modest repeat count, measure peak bytes/time, then use a bounded refusal threshold and assert no bitmap allocation beyond it. Do not use an unbounded stress stream. No RSS or latency is claimed. |
| **P2** | **A disappeared working directory silently becomes the inherited directory.** The forked child ignores `chdir`'s return value before `execve` (`Pty.swift:95–109`). Orion checks a saved directory only at model initialization (`app/AssistantProcess.swift:71–85`) and accepts a later selected directory without a directory check (`:93–97`), so a removed/renamed directory can reach this path. The shell then runs somewhere other than the folder shown to the user; this can change which `.mcp.json` its agent sees. | In an isolated `PseudoTerminalHelpers.fork` call, request a nonexistent `currentDirectory`, execute `/bin/pwd`, and assert launch fails or does not report the parent's directory. No live agent launch was attempted. |

The parser's OSC/APC arrays also append without a byte cap (`EscapeSequenceParser.swift:863–878,925–931`), including the APC graphics dispatch at `:596–606`. This supports the previously filed inline-image input finding in `feedback/2026-09-28-vendor-terminal-core-audit.md`; it is not counted as a newly reproduced defect. `CircularBufferLineList.trimStart` subtracts its unbounded argument after computing a clamp (`CircularList.swift:394–399`), but both current callers pass no more than the list count (`Buffer.swift:839–850,918–939`), so this review does not treat it as an Orion failure.

All findings remain **source traces**. There was no terminal output fixture, crash reproduction, peak RSS result, stopwatch result, or whole-dependency licensing/security certification. File length alone gives no reason to split or replace SwiftTerm.

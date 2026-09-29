# Vendored terminal views audit — 2026-09-28

Complete source reads at `203d116`: `Mac/MacTerminalView.swift` (3,640 lines)
and `Apple/AppleTerminalView.swift` (3,222), both under
`third_party/SwiftTerm/Sources/SwiftTerm/`. Truncated output ranges were re-read.
No build, app launch, test, memory measurement or vendor change.

Orion embeds `LocalProcessTerminalView` (`app/AssistantPanel.swift:86`).
The macOS/CoreGraphics path is active; Metal is disabled by default and Orion
does not enable it. UIKit, iOS/visionOS and preview branches do not run in Orion.
The following are source findings, not reproduced Orion failures.

| Priority | Finding and source trace | Existing bound / smallest useful check |
|---|---|---|
| P2 | For 150 ms after input, each feed chunk delivered on main calls `updateDisplay()` synchronously, bypassing `pendingDisplay` coalescing (`AppleTerminalView.swift:2754–2761,2790–2805`). Each call scans visible cells for blinking and posts accessibility changes (`:673–717,2450–2458,2509`). | `LocalProcess.swift:124–129,198–205` defaults to main delivery, with a 4 ms drain slice and 4 MiB backlog limit. Those bound queue pressure but do not merge redraws. Feed 100 small chunks to an isolated view immediately after `recordUserInput()` and count redraws/main-thread time. No latency measured. |
| P2 | AppKit `selectedRange()` computes selection location using `row * displayBuffer.rows + col`, although terminal stride is columns (`MacTerminalView.swift:2433–2449`). Its cursor calculation uses columns. | In an 80-column, 24-row view, select row 1, column 0; selection location should be 80, while this path gives 24. IME impact was not physically tested. |
| P2 | With mouse reporting active, `mouseDown` sends a press (`MacTerminalView.swift:2887–2890`), but `mouseUp` can open a visible link and return before sending release (`:2934–2944`). | Default link highlighting requires Command (`:1193`). In an isolated PTY, Command-click an OSC 8 link with mouse reporting enabled and inspect both events. No live TUI behavior observed. |
| P3 | `urlAndParamsFrom` splits `params;URL` on every semicolon but returns only `split[1]` (`AppleTerminalView.swift:1324–1340`). Preview and activation use it (`MacTerminalView.swift:3048–3055`, `AppleTerminalView.swift:1427–1432`). | Check `id=1;https://example.test/a;b`; source indicates the URL loses `;b`. No browser link was opened. |

True-color, attribute, CGColor, fallback-font and short CTLine caches have
explicit caps (`AppleTerminalView.swift:122–195,538–543,930–940`). Inline image
insertion retains a bitmap stripe for each displayed row (`:3115–3153`),
compounding the core audit's input-size risk; no separate RSS claim follows.

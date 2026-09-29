# Vendored terminal support audit 2 — 2026-09-28

Complete source/logic read of the **19 assigned files, 5,670 lines** listed below. Every numbered line was read; the larger files were read in bounded, contiguous ranges. No build, test, app launch, GPU measurement, vendor edit, or network lookup was performed. Only targeted caller excerpts outside the roster were inspected. This report covers this roster, not the whole SwiftTerm dependency.

| File (under `third_party/SwiftTerm/`) | Lines read |
|---|---:|
| `Sources/SwiftTerm/Buffer.swift` | 1–1,724 |
| `Sources/SwiftTerm/SelectionService.swift` | 1–729 |
| `Sources/SwiftTerm/BufferLine.swift` | 1–555 |
| `Sources/SwiftTerm/Colors.swift` | 1–490 |
| `Sources/SwiftTerm/Apple/BidiMirroringData.swift` | 1–442 |
| `Sources/SwiftTerm/SearchEngine.swift` | 1–380 |
| `Sources/SwiftTerm/Apple/ArabicShapingData.swift` | 1–250 |
| `Sources/SwiftTerm/SemanticPrompt.swift` | 1–175 |
| `Sources/SwiftTerm/Apple/TerminalProgressBarView.swift` | 1–161 |
| `Sources/SwiftTerm/SearchService.swift` | 1–138 |
| `Sources/SwiftTerm/Mac/MacDebugView.swift` | 1–127 |
| `Sources/SwiftTerm/Mac/MacExtensions.swift` | 1–115 |
| `Sources/SwiftTerm/SearchLineCache.swift` | 1–105 |
| `Sources/SwiftTerm/Apple/Metal/CoreTextGlyphRasterizer.swift` | 1–90 |
| `Sources/SwiftTerm/KittyKeyboardProtocol.swift` | 1–53 |
| `Sources/SwiftTerm/Apple/Metal/MetalError.swift` | 1–49 |
| `Sources/SwiftTerm/Apple/ColorBridge.swift` | 1–46 |
| `LICENSE` | 1–23 |
| `Sources/SwiftTerm/SyncDebug.swift` | 1–18 |

Orion constructs a macOS `LocalProcessTerminalView` in `app/AssistantPanel.swift:86–90`. Its native find UI calls `findNext`/`findPrevious` (`MacTerminalView.swift:2595–2606`), and resizing reaches `Buffer.resize` (`Terminal.swift:931–953, 6740–6742`). The search and resize findings below are therefore reachable in the embedded assistant. Orion does not call `setUseMetal(true)`; SwiftTerm's Metal renderer is disabled by default (`MacTerminalView.swift:235–237, 416–429`). The Metal rasterizer and Metal error types in this roster describe conditional behavior only, and no terminal GPU cost should be attributed to Orion's photo engine.

| Rank | Finding and source | Existing bounds / smallest useful check |
|---|---|---|
| **P2 — search correctness** | In whole-word mode, `SearchEngine.findInLine` gets the first substring match and returns `nil` if that occurrence is not a whole word (`SearchEngine.swift:269–295`). Its callers then advance to the next row (`:44–59, 80–105, 148–175`); they do not try later occurrences on the same line. A line such as `catapult cat` searched for `cat` misses the valid second occurrence. The same branch serves forward, reverse, and highlight search (`SearchService.swift:88–110`). | Search terms are user supplied; `findAll` caps highlights at 1,000 (`SearchService.swift:11–12, 99–104`). Small check: put `catapult cat` on one row, search `cat` with `wholeWord=true`, and require the second match at column 9 in forward and highlight results. For reverse, use `cat catapult` and require the match at column 0. |
| **P2 — resize/tab UX** | Shrinking a buffer removes tab stops with `newCols..<tabStops.count-1` (`Buffer.swift:866–872`), leaving the old final stop as one extra array entry at the new right edge. `Buffer.resize` is called on width changes (`:775–902`). A later one-column widen can inherit that stale stop at a different column instead of creating a fresh default slot. | Terminal clamps width to at least two columns (`Terminal.swift:322, 6740–6742`), so this is not the separate one-column reflow warning. Small check: set a tab stop at the old last column, shrink the terminal, widen by one, and assert the new column has no inherited stop. The likely correction is the removal range ending at `tabStops.count`. |

The buffer has a finite scrollback capacity (`Buffer.swift:625–649`; Orion receives SwiftTerm's default 500 lines from `TerminalOptions.swift:112–125`). Search caches are invalidated by the service and have a 15-second inactivity rule (`SearchService.swift:30–46`; `SearchLineCache.swift:12–45`). Generated Unicode mapping tables are static data, not a per-frame allocation. `SyncDebug.enabled` is false (`SyncDebug.swift:5–16`), and `TerminalDebugView` is a separately instantiated diagnostic view; neither is evidence of shipping redraw cost. `CoreTextGlyphRasterizer` allocates per-glyph bitmaps only if the opt-in terminal Metal path is active (`CoreTextGlyphRasterizer.swift:8–27`). None of the two findings has a measured latency, RAM, or crash result; the bounded checks above are proposals, not tests already run.

# SwiftTerm support-source audit 1 — 2026-09-28

**Scope.** Complete source read of the exact 20-file roster supplied in `/var/folders/n2/fp41fkxn2nz96bbnn693mlj00000gn/T/orion-vendor-remaining-lu3javdz/roster-1.txt`: **5,668 lines**. `Terminal.swift` was covered separately in `2026-09-28-vendor-terminal-core-audit.md` and was not reread here. I read `KittyGraphics.swift` in contiguous ranges 1–350, 351–700, 701–1050, 1051–1400, 1401–1750 and 1751–1940; `BoxDrawingRenderer.swift` 1–330 and 331–635; `MetalRendererRecovery.swift` 1–270 and 271–511; and every smaller file whole. No output was truncated in these reads. The 0-line `BufferSet.swift` was inspected as empty. This is source evidence, not a runtime, memory, security, or licensing verdict. No app, compilation, test, GPU, network call, or vendor edit was made.

| File under `third_party/SwiftTerm/Sources/SwiftTerm/` | Lines |
|---|---:|
| `KittyGraphics.swift` | 1,940 |
| `Apple/BoxDrawingRenderer.swift` | 635 |
| `Apple/Metal/MetalRendererRecovery.swift` | 511 |
| `Utilities.swift` | 458 |
| `CharData.swift` | 435 |
| `Apple/Metal/GlyphAtlas.swift` | 340 |
| `CharSets.swift` | 262 |
| `Mac/MacFindBarView.swift` | 167 |
| `EscapeSequences.swift` | 155 |
| `TerminalOptions.swift` | 150 |
| `TerminalViewSearch.swift` | 127 |
| `Apple/TerminalViewDelegate.swift` | 107 |
| `Apple/CaretView.swift` | 98 |
| `Line.swift` | 93 |
| `Apple/BellStyle.swift` | 55 |
| `Position.swift` | 52 |
| `SearchState.swift` | 34 |
| `SwiftTermBuildInfo.swift` | 30 |
| `Apple/Metal/MetalBufferingMode.swift` | 19 |
| `BufferSet.swift` | 0 |
| **Total** | **5,668** |

**Orion reachability.** `app/AssistantPanel.swift:86–117` creates a `LocalProcessTerminalView` and launches `/bin/zsh` for the selected `claude`/`codex` CLI. The terminal processes that child's output. `EscapeSequenceParser.swift:597–605` routes APC `G` to `Terminal.handleKittyGraphics`; macOS is included in the graphics and CoreGraphics compilation branches. `TerminalOptions.swift:111–125` defaults to 500 scrollback lines, a 320 MiB stored Kitty image cache, and 500 BiDi paragraph rows. Orion does not configure these options or call `setUseMetal`; `MacTerminalView.swift:237` starts with the optional Metal renderer disabled. Thus the Kitty and CoreGraphics caret paths are relevant to the embedded assistant, while the Metal support files are compiled but their render path is inactive by default.

## Ranked source findings

1. **P1 — Chunked Kitty image input has no aggregate pending-byte bound.** `KittyGraphics.swift:153–179` copies each APC `G` `m=1` payload into `kittyGraphicsState.pending.base64Payload` and appends every later chunk. The 400 MiB decoded-image check is reached only after a final `m=0` chunk (`:458–468`), and does not constrain an unfinished transfer. A child CLI can emit repeated small `ESC _ G ... m=1 ; ... ESC \\` chunks; each individual chunk may be modest while the terminal retains their sum. A bounded future check should feed a few deliberately small chunks to a scratch `Terminal`, verify the pending byte count is capped or the transfer refused, and avoid stressing RAM. The parser and Orion boundary make this path reachable; no memory growth was measured here. Closing the assistant destroys its terminal state; the default stored-image cache does not mitigate pending data.

2. **P1 — zlib expansion is checked only after full decompression.** `KittyGraphics.swift:699–709` checks `inflated.count <= 400 MiB` after `decompressZlib` returns. The decode loop at `:1880–1934` appends every 64 KiB output block to `Data` without checking the limit. Small compressed input can therefore demand arbitrarily large intermediate memory or CPU before rejection. macOS has the `Compression` module, and APC `G` is dispatched from child output. A future bounded check can use a tiny zlib payload whose decoded size crosses a deliberately lowered local test limit, asserting refusal before crossing it; this audit did not feed a payload. The 400 MiB encoded/decoded checks and 320 MiB image-cache default apply after this work.

3. **P1 — Shared-memory slice arithmetic can trap on a small object.** `KittyGraphics.swift:955–1007` accepts nonnegative `O` and `S` as `Int`, opens a named shared-memory object, then computes `offset + size` at `:990` before proving either fits the object. `O=Int.max,S=1` overflows in Swift even when the object has only a few bytes. This requires a child process to create a named shared-memory object and send APC `G` with transmission `s`; it is a reachable macOS path but not a typical assistant response. The object-size check (`<=400 MiB`) does not prevent the arithmetic trap. The smallest future check is a private child with a one-byte shared-memory object and those two fields, plus a normal `O=0,S=1` control; no such object was created here.

4. **P2 — The CoreGraphics caret leaves saved graphics states on common return paths.** `Apple/CaretView.swift:19` calls `context.saveGState()`. The unfocused caret returns at `:25–29`, and every bar or underline style returns at `:47–49`, before the restore at `:96`. `Mac/MacCaretView.swift:139–140` calls this method during layer drawing, and Orion uses the default CoreGraphics terminal renderer. A bounded future check can draw an unfocused caret and a bar caret into separate scratch contexts, then check balanced save/restore or inspect a repeated-draw image. The source proves the imbalance; it does **not** establish a persistent RAM leak or visible defect because the layer may discard its context after drawing.

The `CharData.swift:191–239` monotonic `TinyAtom` identifier and terminal payload lifetime corroborate the separate terminal-core report; they are not counted as a new finding here. The reviewed Metal atlas has explicit maximum sizes and a frozen overflow path (`GlyphAtlas.swift:76–91, 130–175`); its default renderer is inactive in Orion. No claim about real terminal latency, RAM peak, pixel fidelity, or crash behavior follows from these reads alone.

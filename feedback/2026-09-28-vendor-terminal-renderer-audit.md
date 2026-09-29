# Vendored terminal rendering audit — 2026-09-28

Complete source reads at `203d116`: `Apple/Metal/MetalTerminalRenderer.swift`
(3,101 lines) and `Apple/TerminalBidi.swift` (974), under
`third_party/SwiftTerm/Sources/SwiftTerm/`. No edits, builds or runtime measurements.

Orion constructs `LocalProcessTerminalView` without calling `setUseMetal(true)`
(`app/AssistantPanel.swift:86–90`); SwiftTerm defaults Metal off
(`MacTerminalView.swift:235–237,416–429`). CoreGraphics/BiDi findings are reachable
in the current assistant. Optional Metal findings do not explain current Orion
RAM or photo-engine latency. All impact estimates remain unmeasured.

| Priority / reachability | Source finding | Smallest bounded check |
|---|---|---|
| P2 / active | Process-wide BiDi caches each retain up to 256 paragraph results, including copied cells and visual layouts, after a terminal closes. Eviction occurs at the entry threshold, with no teardown purge (`TerminalBidi.swift:464–489,807–825`). | Print a bounded set of distinct RTL paragraphs in an isolated terminal, close it, then compare retained cache bytes. A count cap is not a measured byte bound. |
| P2 / active | A visible row's BiDi lookup scans its soft-wrapped paragraph for bounds/revision before a cache hit (`TerminalBidi.swift:545–579,735–750`). A changed row rebuilds the whole paragraph (`:632–726,767–815`), capped at 500 rows by default (`TerminalOptions.swift:125`). CoreGraphics drawing reaches it (`AppleTerminalView.swift:1058–1063`). | Compare repeated updates to one long wrapped paragraph against separate short paragraphs with the same visible area. No frame-time measurement made. |
| P3 / optional Metal | RGBA Kitty texture lookup copies the entire byte array to `Data` to hash its first 64 bytes, even on cache hits (`MetalTerminalRenderer.swift:2563–2600`). Multiple placeholders can repeat it (`:1507–1523`). | In an isolated opt-in Metal harness, display one bounded virtual image and count copied bytes per redraw. |
| P3 / optional Metal | Texture signature uses payload length and only the first 64 bytes (`MetalTerminalRenderer.swift:127–133,2592–2613`). Replacing an image ID with an equal-size/equal-prefix payload may reuse its old texture (`:2563–2570`). | Replace one bounded image with pixels changed after byte 64, force redraw and compare the rendered result. |

The Metal atlas, buffer pool, visible-row cache and Kitty pruning already have
limits (`MetalTerminalRenderer.swift:630–657,727–730,1883–1967,2959–2981`).
The full-cap atlas arithmetic is 256 MiB across CPU/GPU storage; it is a ceiling
calculation for an inactive renderer, not observed Orion consumption.

# Orion

Fast, subscription-free desktop RAW photo editor for macOS. Modern minimal UI, capable of dramatic edits. Built by a solo developer.

## Read these first
Everything lives in `planning/`. On a fresh session, read in this order:

1. **`planning/STATUS.md`** — where we are right now, what's in flight, what's blocked. **Always start here.**
2. `planning/VISION.md` — what Orion is and isn't
3. `planning/DECISIONS.md` — every settled choice + why. Don't relitigate these.
4. `planning/ARCHITECTURE.md` — stack, engine design, open questions
5. `planning/ROADMAP.md` — milestones, epics, stories
6. `planning/FEATURES.md` — feature map with target milestones
7. `planning/RESEARCH.md` — algorithm and library findings with sources
8. `planning/UI-DECISION.md` — UI shell evaluation (open decision)

## Hard constraints — do not violate

- **No Rust.** The developer cannot debug it. Propose C++ alternatives even when Rust is the popular answer. This rules out Tauri, wgpu, egui, iced.
- **No Vulkan.** Too much boilerplate for a solo maintainer. Metal on macOS; shaders authored in Slang for later portability.
- **Maintainability is a hard requirement.** One node = one small shader (50–150 lines). Adding a feature should be a repeatable 3-file change. No 1000-line anything. The developer must be able to hand-edit any file later.
- **Prefer mature libraries** over hand-rolled code, even at some performance cost.
- **Avoid GPL libraries** (notably exiv2) until the license model is settled.

## Stack
C++20 engine · Metal GPU compute · Slang shaders · **SwiftUI/AppKit UI** · LibRaw decode · OpenColorIO + lcms2 color · SQLite index · XMP sidecars as source of truth.

**UI rules:** view models are plain `@Observable` objects with zero SwiftUI types inside, so any panel can be re-hosted in AppKit. Canvas is `MTKView` in `NSViewRepresentable`, zero-copy. The C++↔Swift boundary is a narrow hand-written POD facade — no templates, no move-only types, and **no C++ exception may escape it (it would terminate the process)**. Dear ImGui is for internal debug tooling only, never product UI. macOS 14+ floor.

## Working agreement
- One roadmap story per coding session. Update `STATUS.md` at the end of every session — this is what makes context loss survivable.
- Log any new decision in `DECISIONS.md` with its reason.
- Benchmark every milestone. Latency regressions block new features.
- Planning docs stay concise. Dense tables over prose.

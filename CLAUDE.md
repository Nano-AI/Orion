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
8. `planning/UI-DECISION.md` — UI shell evaluation (settled)
9. `research/` — **algorithm sources.** Read before touching any filter.
10. `feedback/` — **outside review and self-assessment.** Every critique of this
    repository, and the investigations that came out of them, kept together so
    the criticism is as findable as the plan. Read `feedback/README.md` first.

## Algorithm sourcing — non-negotiable

**Every technically non-trivial filter must cite a published reference in
`research/`.** Anything not cited belongs in `research/UNSOURCED.md` until it is
replaced or defended. A citation must be published, dated, and established in
practice.

This rule exists because plausible-looking constants shipped a purple cast on
every image. Cite the source so the numbers are checkable, and test the
invariant so they stay right.

Do not copy GPL code (darktable, RawTherapee). Implementing a *published
algorithm* from its description is fine — mathematics is not copyrightable — but
say so in the entry.

## Hard constraints — do not violate

- **No Rust.** The developer cannot debug it. Propose C++ alternatives even when Rust is the popular answer. This rules out Tauri, wgpu, egui, iced.
- **No Vulkan.** Too much boilerplate for a solo maintainer. Metal on macOS; shaders authored in Slang for later portability.
- **Maintainability is a hard requirement.** One node = one small shader (50–150 lines). Adding a feature should be a repeatable 3-file change. No 1000-line anything. The developer must be able to hand-edit any file later.
- **Prefer mature libraries** over hand-rolled code, even at some performance cost.
- **Avoid GPL libraries** (notably exiv2) until the license model is settled.

## Stack
C++20 engine · Metal GPU compute · Slang shaders · **SwiftUI/AppKit UI** · LibRaw decode · OpenColorIO + lcms2 color · SQLite index · XMP sidecars as source of truth.

**UI rules:** view models are plain `@Observable` objects with zero SwiftUI types inside, so any panel can be re-hosted in AppKit. Canvas is `MTKView` in `NSViewRepresentable`, zero-copy. The C++↔Swift boundary is a narrow hand-written POD facade — no templates, no move-only types, and **no C++ exception may escape it (it would terminate the process)**. Dear ImGui is for internal debug tooling only, never product UI. macOS 14+ floor.

## Tests

```
./build/apps/tests/orion-tests     engine maths + real GPU renders
./build/orion-viewport-tests       canvas geometry
```

Run both before claiming anything works. The GPU tests matter most: pure maths
tests pass happily on code that renders garbage, because they never touch a
texture. Two shipped bugs — a torn frame and a purple cast — were invisible to
inspection and obvious to a five-line assertion.

## Working agreement
- One roadmap story per coding session. Update `STATUS.md` at the end of every session — this is what makes context loss survivable.
- Log any new decision in `DECISIONS.md` with its reason.
- Benchmark every milestone. Latency regressions block new features.
- Planning docs stay concise. Dense tables over prose.

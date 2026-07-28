# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-07-27
**Phase:** **M0 COMPLETE — gate passed.** Next: M2.
**Next story:** M2 — tone curve, HSL, sharpening, denoise, lens corrections

---

## Where we are

**M0 is done and the gate passed with room to spare.** A 24 MP Sony ARW goes
through a seven-node GPU pipeline and an exposure change re-renders in
**8.15 ms at p95 — at full resolution**, against a 16 ms budget. The preview-ROI
optimisation the architecture assumes we would need is not needed yet.

```
Source          Sony ILCE-7M3, 6024 x 4024 (24.2 MP, RGGB)
  decode        47 ms   (511 MP/s, LibRaw)
Pipeline        7 nodes, 971 MiB of intermediates
  full render   43.9 ms  (every node)
  exposure drag  8.2 ms  (2 of 7 nodes recomputed)  <- the number that matters
M0 gate         PASS
```

Per-node caching works: moving exposure dirties only exposure + AgX, so
linearize, all three RCD passes and the colour matrix are served from cache.

### Dev machine (measured, not assumed)
Apple **M4**, macOS 26.4.1, arm64 · Xcode 26.6 / clang 21 · 17.8 GiB recommended working set · 13.3 GiB max buffer · **unified memory** · Apple7 GPU family supported.

Unified memory is a real advantage: CPU↔GPU transfers are free, so LibRaw can decode straight into a shared buffer with no staging copy.

### Toolchain installed
`cmake` · `ninja` · `libraw 0.22.2` · `little-cms2` (was already present)
Still needed: **Slang** (S0.3, not in Homebrew — grab a GitHub release).

### Build
```
cmake -S . -B build -G Ninja
cmake --build build
./build/apps/probe/orion-probe
```

### What exists
```
engine/include/orion/orion.h        C facade — POD only, no exceptions cross it
engine/src/CApi.cpp                 exception firewall; guard() turns throws into status
engine/src/Engine.{h,cpp}           engine proper, RAII
engine/src/gpu/MetalDevice.{h,mm}   device + queue
engine/src/gpu/Resources.{h,mm}     Texture, Library, Kernel, CommandBuffer
engine/src/raw/RawImage.{h,cpp}     LibRaw decode -> untouched CFA mosaic
engine/src/pipe/Pipeline.{h,cpp}    the DAG: Kahn topo sort, per-node dirty caching
engine/src/pipe/DevelopPipeline.*   the standard 7-node graph + adjustments
engine/src/pipe/ShaderParams.h      host mirrors of shader structs, static_assert'd
engine/src/util/ImageWriter.mm      PNG out via ImageIO
engine/shaders/*.slang              7 kernels, one file each
app/*.swift                         SwiftUI shell, MTKView canvas, zero-copy
apps/probe, apps/bench              C-API smoke test, and the M0 gate
design/                             tokens.json -> CSS + Swift; darkroom mockup
```

### Two bugs worth remembering
1. **Slang binding indices are cumulative across a module.** Compiling all
   kernels into one metallib gave kernel 2 textures at index 2/3 and kernel 3 at
   4/5, while the host binds from 0 every dispatch — so every kernel after the
   first read unbound slots and produced black. Fix: **one metallib per kernel**.
   Do not "optimise" that back into a single module.
2. **The camera matrix must be row-normalised.** Without it, white balance and
   the colour matrix fight: the data is already neutral after WB, and an
   unnormalised matrix re-tints it (we had a magenta cast). dcraw normalises
   rgb_cam for the same reason.

## Settled

See `DECISIONS.md` for the full list with reasoning. Headlines:

- C++20 engine, Metal GPU, Slang shaders. **No Rust, no Vulkan.**
- Compute DAG, one shader per node, `rgba16f` linear Rec.2020, scene-referred, pixels stay on GPU.
- XMP sidecars = truth, SQLite = disposable rebuildable index. Folder-based, no catalog.
- macOS first. Sony ARW only for v1.
- RCD demosaic, AgX-family sigmoid tone mapper, profiled wavelet denoise.
- Maintainability is a hard constraint (solo dev): small shaders, 3-file feature changes.

## In flight

**Nothing in flight.** UI shell decision is closed — see `UI-DECISION.md`. Planning is complete enough to start coding.

⚠️ Session limit and the 200-call web-search budget were both exhausted on 2026-07-27. **Do research inline and sparingly** — the developer asked for fewer subagents, and they proved fragile at this scale.

## Blocked / needs a decision from the developer

1. ~~UI shell~~ ✅ **Resolved: SwiftUI/AppKit + C++ engine** (decision #25). Qt was picked then reversed — see `UI-DECISION.md` for why.
2. **License / business model** — undecided by choice. Building to keep both doors open: avoid GPL libraries, dynamically link LGPL ones. Revisit before v1 ships.

## Scope — locked 2026-07-27

Every feature now has a milestone. Notable calls:
- **Cut from v1:** card import (point at a folder instead), brush masking, keywords/search.
- **Local edits land in M4**, gradient + luminance/color-range masks + AI subject/sky. Spot removal kept.
- **Bilateral grid + BGU pulled forward to M1** — built before needed, as the escape hatch for the latency budget.
- No tethered shooting.

## Next actions — M2

M0 stories S0.1–S0.8 are all complete. M1 (browse, cull, crop, export, sidecars)
is deliberately skipped for now per the goal; M2 is next:

1. Tone curve node — the mockup's editor is the UX spec; monotone cubic Hermite
   on the GPU, matching the JS in `design/mockup-darkroom.html`
2. HSL / colour mixer, 8 hue bands
3. Sharpening (amount / radius / masking)
4. Profiled wavelet denoise + a per-camera noise profile
5. Lens corrections via lensfun
6. Before/after split — the mockup's Compare interaction

### Known gaps to close in M2
- Demosaic is **RCD-family, not a faithful RCD port** — directional +
  gradient-corrected + clamped, which is genuinely good but not the reference
  algorithm. Revisit against https://github.com/LuisSR/RCD-Demosaicing
- Highlight reconstruction is not implemented at all (clip only)
- The pipeline runs at full resolution; the preview-ROI path in ARCHITECTURE.md
  is designed but unbuilt. Not needed yet on an M4 — will be on lesser GPUs
- Black level ignores LibRaw's 2D cblack pattern (averaged instead)
- AgX output is sRGB-encoded; the EDR/P3 path is not wired up

## Notes for whoever picks this up

- The developer wants **evidence, not agreement**. When they express skepticism about a technology, research it honestly — they explicitly asked to have their assumptions tested.
- Keep planning docs concise. Dense tables, not essays.
- The most important research finding is **Bilateral Guided Upsampling** (`RESEARCH.md` §4) — it is the general solution to "this algorithm is too slow to be interactive" and should be a DAG node type built in M1, before it's needed.

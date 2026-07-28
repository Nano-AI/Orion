# Orion — Roadmap

AGILE structure: **Milestones** (releasable increments) → **Epics** → **Stories** (one Opus session each, ideally).
Rule: every milestone ends with something you can actually shoot with. No milestone is pure infrastructure.

---

## M0 — Prove the budget (risk spike)

**This milestone is a benchmark, not a feature.** Everything after it depends on <16 ms being real. Find out now, not in month four.

**Definition of done:** a 24 MP Sony ARW decodes, demosaics, and renders to screen through a ≥5-node GPU pipeline, with an exposure slider that responds in **under 16 ms at preview resolution**, measured and logged.

| Story | Notes |
|---|---|
| S0.1 Skeleton: C++20 project, CMake, Metal device init | |
| S0.2 Decode one ARW with LibRaw → GPU texture | Bayer data, no processing |
| S0.3 Slang→MSL shader build step in CMake | Proves the portability plan works |
| S0.4 RCD demosaic as a compute kernel | Port the MIT reference |
| S0.5 Minimal DAG: node graph, topological sort, `rgba16f` buffers | The engine's spine |
| S0.6 Exposure + AgX display transform nodes | Simplest end-to-end pipeline |
| S0.7 Display the output texture on screen, zero copies | `MTKView` in `NSViewRepresentable`; POD facade at the C++↔Swift boundary |
| S0.8 **Benchmark harness + latency numbers** | Gate: pass or rethink the stack |

**Kill criteria:** if <16 ms is unreachable on your Mac after optimization, stop and revisit the GPU/UI architecture before building features on it.

---

## M1 — Usable editor

**Definition of done:** you edit a real shoot in Orion start to finish and export JPEGs you're happy with. No other tool involved.

- **Epic: Browse** — folder open, grid + filmstrip, async thumbnails, LRU cache
- **Epic: Cull** — star ratings, reject flags, color labels; filter and sort; SQLite index
- **Epic: Core develop** — exposure, contrast, highlights/shadows, whites/blacks, WB (temp/tint + eyedropper), vibrance/saturation
- **Epic: Geometry** — crop, rotate, straighten
- **Epic: Edit model** — op stack → XMP sidecar, undo/redo, history panel
- **Epic: Export** — full-res tiled render path, JPEG/TIFF, resize, color space
- **Epic: Interaction** — per-node caching, degrade-then-refine during drags
- **Epic: Look** — neutral-gray dark theme, panel layout

---

## M2 — Depth

**Definition of done:** Orion handles a difficult image (harsh light, high ISO, bad lens) as well as Lightroom would.

- Tone curve (parametric + point, per-channel)
- HSL / color mixer, 8 hue bands
- Sharpening (amount / radius / masking)
- Profiled wavelet denoise + per-camera noise profile
- Lens corrections via lensfun
- Broader camera support via LibRaw + DNG
- Before/after and split view
- Keyboard-first workflow

---

## M3 — The "180°" milestone

**Definition of done:** one click makes a flat RAW look striking.

- Local Laplacian clarity/texture
- Single-image exposure fusion (shadow lift with local contrast preserved)
- Dehaze (dark channel prior)
- **Auto-enhance** combining the above with percentile auto-levels
- Color grading wheels (ASC CDL), split toning, vignette
- Creative LUTs (.cube, tetrahedral)
- Segmentation-based highlight reconstruction

---

## M4 — Local edits & workflow

**Definition of done:** you can dodge, burn, and fix a specific area — then apply it across a shoot.

- Gradient masks (linear + radial) — cheapest, pure math
- Luminance and color range masks — cheap given M1's bilateral grid
- Mask combine operators (add/subtract/intersect)
- AI subject/sky selection (Core ML) — needs the mask system first
- Spot removal (sensor dust and blemishes, not Photoshop-grade healing)
- **No brush masking in v1** — deliberately cut
- Presets (user + built-in looks)
- Copy/paste/sync settings across a selection
- Batch export
- Snapshots/versions, perspective correction, film grain

---

## M5 — Advanced

- ML denoise (NAFNet-class via Core ML) as a **background pass**, not a live slider
- Brush masking (deferred from M4)
- User-loadable DCP profiles
- X-Trans support (Markesteijn)
- Customizable tool panels / saved workspaces
- Windows port (engine already portable; UI is the work)

---

## Working agreement

- **One story per Opus session**, with `planning/` as the brief. Update `DECISIONS.md` whenever a choice is made mid-build.
- **Benchmark every milestone.** Latency regressions get fixed before new features land, not after.
- **Dogfood M1 onward.** If you avoid using Orion for a real shoot, that's the bug report.
- Open questions live at the bottom of `ARCHITECTURE.md` until resolved.

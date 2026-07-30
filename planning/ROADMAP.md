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
- **Epic: Interaction** — per-node caching ✅, **degrade-then-refine during
  drags — not built, and now the largest open reported bug.** Measured cost of
  one tick on a 24 MP frame: exposure 9.4 ms, clarity 65.7, dehaze 116.4, each
  blocking the main thread on `commitAndWait` (`repro/slider-drag-cost.txt`).
  The graph compiles at a single resolution, so the shape is a second
  `DevelopPipeline` at a quarter-linear proxy — about 380 MiB on top of 6092 —
  fed by the same `apply`, with the full render scheduled on settle, and export,
  the histogram and the eyedropper guaranteed never to read the proxy
- **Epic: Look** — neutral-gray dark theme, panel layout

### Export panel — modeled on macOS Preview's export sheet

Basic export works (JPEG/PNG/TIFF, quality, longest-edge resize) but has no UI
beyond a save dialog. Preview's sheet is the right reference because it is the
one every Mac user already understands.

| Control | Behavior |
|---|---|
| **Format** | JPEG · PNG · TIFF · HEIF. Changing it swaps the options below and updates the extension in the filename. |
| **Quality** | JPEG and HEIF only. Slider with a live **estimated file size** beside it — the number is why the slider is legible. |
| **Resolution** | Preset menu (Full · 4096 · 2048 · 1024 px long edge · Custom) plus explicit width/height fields that respect the aspect ratio. Shows the resulting pixel dimensions. |
| **Color space** | sRGB · Display P3 · Adobe RGB. sRGB default, since it is what survives the web. |
| **Bit depth** | 8-bit · 16-bit, TIFF and PNG only. ⚠️ Needs the pipeline to end in `rgba16f` rather than `rgba8`. |
| **Metadata** | Keep all · Strip location · Strip everything. |
| **Output sharpening** | None · Screen · Print. Resampling softens; this is the standard correction. |

**Live estimate** matters more than it sounds: quality sliders are meaningless
without one, which is exactly why Preview shows it.

**Blocked on:** 16-bit export needs the final node's format changed and the
orientation node widened to match. Not hard, but it touches the pipeline tail.

### Extending format support

| Format | Notes |
|---|---|
| HEIF | Free via ImageIO — smaller than JPEG at equal quality, and native on Apple platforms |
| JPEG XL | Better still, but ImageIO support needs verifying before promising it |
| AVIF | Same caveat |
| DNG | *Export* as DNG is a different problem — writing a raw container, not an image. Later. |

---

## M2 — Depth

**Definition of done:** Orion handles a difficult image (harsh light, high ISO, bad lens) as well as Lightroom would.

- ✅ Tone curve (monotone cubic Hermite, per-channel LUT)
- ✅ HSL / color mixer, 8 hue bands, with targeted adjustment
- ✅ Sharpening (amount / radius / masking)
- ✅ Guided filter → local highlights and shadows *(He, Sun & Tang)*
- ✅ Profiled wavelet denoise + per-camera noise profile *(Starck et al.)*
- ✅ Lens corrections, with lensfun's database vendored and read directly
- ✅ Broader camera support via LibRaw — and unsupported sensors refused by name
- ✅ Before/after split — the mockup's Compare interaction
- ✅ Keyboard-first workflow and control accessibility
- ✅ Camera profile beyond the 3×3: BaselineExposure and HueSatMap *(DNG spec)*

---

## M3 — The "180°" milestone

**Definition of done:** one click makes a flat RAW look striking.

- ✅ Local Laplacian clarity *(Paris/Hasinoff/Kautz; Aubry et al.)* — Texture, the fine-level variant, is not built
- ✅ Single-image exposure fusion *(Hessel & Morel; Mertens et al.)*
- ✅ Dehaze (dark channel prior) *(He, Sun & Tang; guided-filter refinement)*
- ✅ **Auto-enhance** — percentile auto-levels driving the above *(Simplest Color Balance; CIPA DC-004)*
- Color grading wheels (ASC CDL), split toning, vignette
- ✅ Creative LUTs (.cube, tetrahedral) *(Adobe Cube LUT Specification 1.0; Sakamoto & Itooka 1981)*
- Segmentation-based highlight reconstruction

---

## M4 — Local edits & workflow

**Definition of done:** you can dodge, burn, and fix a specific area — then apply it across a shoot.

- ✅ Gradient masks (linear + radial) — parametric, applied to the parameter
- ✅ **Luminance** range mask — 2026-07-30, `research/masking.md` §4b. ⚠ The
  old note here said this was cheap given M1's bilateral grid; M1 never built
  one, and a range mask is pointwise so it would not have helped
- Colour range mask — needs a colour distance and so a colour space
- ✅ Mask combine operators (add/subtract/intersect) — 2026-07-29, decision #62
- ✅ Guided feathering of the mask group — 2026-07-29, `research/masking.md`
  §4 *(He, Sun & Tang)*. The mask's edge is pulled onto the photograph's,
  and left alone where there is none to find
- ✅ AI **subject and person** selection — 2026-07-30, Apple Vision, no bundled
  model and no new dependency. **Sky is not done and is not cheap**: Apple has
  no API for it (its matte is capture-time metadata only), so it needs a
  bundled BiRefNet or U²-Net — both MIT/Apache. ⚠ RMBG is the trap every
  tutorial reaches for and its licence forbids commercial use
- ✅ Spot removal — 2026-07-30, clone and heal. `research/spot-removal.md`;
  the zeroth-order term of Farbman et al.'+chr(39)+'s interpolant, with the bounded
  failure across a hard edge recorded in `UNSOURCED.md` §21 and stated in the
  panel
- Brush masks — **reinstated 2026-07-29**, see DECISIONS #54 (was cut from v1)
- ✅ Presets — 2026-07-30. A **patch**, not a state: only the groups it carries
  are applied, and the crop, the dust and the masks are never among them
- ✅ Copy/paste/sync — 2026-07-30. Sync edits sidecars **without opening the
  photographs**, at the level of the JSON keys, so a photo that has never been
  edited keeps its as-shot white balance. ⚠ "Across a selection" is across
  every photo in view; the filmstrip has no multi-selection yet
- ✅ Batch export — 2026-07-30. One engine reused across the list (466 ms a
  photo, peak RSS flat); each photo'+chr(39)+'s own sidecar restored before it is
  exported; nothing overwritten and no two sources colliding
- Snapshots/versions, perspective correction, film grain

---

## M5 — Advanced

- ML denoise (NAFNet-class via Core ML) as a **background pass**, not a live slider
- Edge-aware brush refinement beyond M4's guided-filter pass
- User-loadable DCP profiles
- X-Trans support (Markesteijn)
- Customizable tool panels / saved workspaces
- Windows port (engine already portable; UI is the work)

---

## Where things actually stand

**`planning/STATUS.md`.** There is no second copy here on purpose: this file
had one, it disagreed with STATUS on the milestone percentages, on whether
16-bit export existed and on which story was next, and two copies of "where we
are" guarantee that outcome.

This file says what the milestones *are*. STATUS says where they stand.

## Working agreement

- **One story per Opus session**, with `planning/` as the brief. Update `DECISIONS.md` whenever a choice is made mid-build.
- **Benchmark every milestone.** Latency regressions get fixed before new features land, not after.
- **Dogfood M1 onward.** If you avoid using Orion for a real shoot, that's the bug report.
- Open questions live at the bottom of `ARCHITECTURE.md` until resolved.

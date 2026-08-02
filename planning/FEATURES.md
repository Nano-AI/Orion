# Orion — Feature Map

Status: **M0–M5** = target milestone · **cut** = explicitly out. **Scope locked 2026-07-27** — no `?` remain.
Algorithm picks come from `RESEARCH.md`; stack from `ARCHITECTURE.md`.

## 1. Library & Browse
| Feature | Status | Notes |
|---|---|---|
| Folder-based browse (no catalog) | M1 | Matches how you already work |
| Grid + filmstrip, fast thumbnails | M1 | Async workers, LRU cache, bilinear demosaic |
| Star ratings, reject flags, color labels | M1 | Written to XMP sidecar |
| Filter/sort by rating, flag, label, date | M1 | SQLite index makes this instant |
| XMP sidecars as source of truth | M1 | Portable, no lock-in |
| Import from card (copy + rename) | **cut** | Point Orion at a folder; card offload stays in Finder. Zero import code, no data-safety failure modes |
| Keywords / search | cut (v1) | Catalog territory |

## 2. RAW Decode
| Feature | Status | Notes |
|---|---|---|
| Sony ARW | M0 | Only priority format for v1 |
| Other Bayer cameras via LibRaw | M2 | Mostly free once ARW works |
| DNG | M2 | |
| X-Trans (Fuji) | M5 | Needs separate Markesteijn demosaic path |
| Demosaic: RCD | M0 | MIT impl, ▲ quality, excellent GPU fit |
| Demosaic: bilinear for thumbs/zoomed-out | M0 | |
| Demosaic: dual (RCD+VNG4) quality option | M4 | |
| Highlight recon: clip + inpaint-opposed | M1 | Cheap, stable default |
| Highlight recon: region-scoped (Dirichlet fill) | M3 | Quality option for blown skies. ⚠ Renamed from "segmentation-based" — decision #99: a Dirichlet solve is already region-scoped, so there is no component pass. Solver built 2026-08-01, **not wired**; +25 nodes and ~516 MB costed in ROADMAP |

## 3. Global Adjustments
| Feature | Status | Notes |
|---|---|---|
| Exposure, contrast, highlights/shadows, whites/blacks | M1 | All scene-linear |
| White balance (temp/tint, eyedropper, presets) | M1 | |
| Vibrance / saturation | M1 | |
| Tone curve (parametric + point, per-channel) | M2 | |
| HSL / color mixer (8 hue bands) | M2 | |
| Color grading wheels (ASC CDL slope/offset/power) | M3 | |
| Split toning | **cut** | ❌ **Refused 2026-08-01, decision #97.** It is the grading wheels with fewer controls, and Adobe retired the panel it copies in Oct 2020 in favour of exactly what Orion already ships. Its one real remainder — **Balance**, a signed EV offset on the three zone centres — ✅ **shipped 2026-08-01, decision #104**, on the grading panel under the wheels it moves |

## 4. Color Science
| Feature | Status | Notes |
|---|---|---|
| Scene-referred linear Rec.2020 f16 pipeline | M0 | Foundational — no Lab anywhere |
| AgX-family sigmoid tone mapper (2–3 sliders) | M1 | The "great defaults" engine |
| Camera matrices from Adobe DNG data | M1 | Sony first |
| User-loadable DCP profiles | M4 | RawTherapee's model |
| Creative LUTs (.cube, tetrahedral interp) | M3 | Post-tone-map |

## 5. The "180°" Toolkit
| Feature | Status | Notes |
|---|---|---|
| Local Laplacian clarity / texture | M3 | Halo-free local contrast — big visual payoff |
| Single-image exposure fusion (shadow lift) | M3 | The "phone photo pop" ingredient |
| Dehaze (dark channel prior) | M3 | |
| One-click auto-enhance | M3 | Combines the three above + percentile auto-levels |
| **Bilateral grid + Bilateral Guided Upsampling** | **M1** | Built early, before needed — general escape hatch for the 16 ms budget (`RESEARCH.md` §4) |
| Learned auto-enhance (HDRnet-class) | cut (v1) | v2 experiment |

## 6. Detail
| Feature | Status | Notes |
|---|---|---|
| Sharpening (amount/radius/masking) | M2 | |
| Denoise: profiled wavelet (à-trous) | M2 | Per-camera/ISO noise model |
| Denoise: NLM luma option | M4 | |
| Denoise: ML (NAFNet-class, Core ML) | M5 | **Background pass, not a live slider** (~12s/24MP) |

## 7. Optics & Geometry
| Feature | Status | Notes |
|---|---|---|
| Crop / rotate / straighten | M1 | |
| Lens corrections (distortion, vignette, CA) | M2 | lensfun DB |
| Perspective / keystone | M4 | ✅ 2026-08-01, decision #100. Vertical, horizontal and aspect, folded into the geometry node's one sampling pass. `research/perspective.md` |

## 8. Local Edits
| Feature | Status | Notes |
|---|---|---|
| Linear + radial gradient masks | M4 | Cheapest — pure math, no painting infrastructure |
| Luminance range mask | M4 | ✅ **Built 2026-07-30**. A band in log2 luminance on the *reference* image, so adjusting through it cannot change what it selects. ⚠ The old note here said this was cheap because M1 built a bilateral grid — M1 did not, and a range mask is pointwise so it would not have helped. `research/masking.md` §4b |
| Color range mask | M4 | Needs a colour distance and so a colour space: CIE76 in CIELAB is the cheap answer, CIEDE2000 the accurate one. Its own story |
| Mask combine (add/subtract/intersect) | M4 | ✅ **Built 2026-07-29** (decision #62). One component kernel per node, folded left from zero; up to 4 components with a per-row op, in the panel, the sidecar and undo. Pre-group sidecars migrate to a single component |
| Guided feathering of a mask | M4 | ✅ **Built 2026-07-29**. He, Sun & Tang's own named application of the guided filter — the second input binding `research/masking.md` §4 predicted, and nothing else changed in the filter. Seven nodes on the folded group, off at strength 0. Radius and epsilon are Orion's own: `UNSOURCED.md` §20 |
| AI subject / sky selection (Core ML) | M4 | Requires the mask system first. The guided pass above is what refines and upsamples a coarse matte |
| Brush mask | M4 | ✅ **Built 2026-07-29.** Reinstated from the v1 cut (DECISIONS #54) — the cost estimate was wrong: storage is a list of centres, not a raster, and edge-aware snapping is the guided filter already built. Paint on the canvas, Size/Flow/Hardness, persists in the sidecar. ⚠ A stroke over 256 dabs truncates (warns on stderr); the nib's constants are in UNSOURCED §17 |
| Mask overlay (see the coverage) | M4 | ✅ **Built 2026-07-29.** Red over the picture, drawn in `develop:linear`. A viewing aid, not an edit: never in the sidecar, never in undo, forced off around an export. UNSOURCED §18 |

## 9. Repair
| Feature | Status | Notes |
|---|---|---|
| Spot removal (clone/heal) | M4 | ✅ **Built 2026-07-30**. `research/spot-removal.md`. The zeroth-order term of Farbman et al.'s mean-value interpolant — no solver. ⚠ Bounded failure across a hard edge, recorded in `UNSOURCED.md` §21 and stated in the panel |

## 10. Effects
| Feature | Status | Notes |
|---|---|---|
| Vignette (creative, post-crop) | M3 | ✅ **Built 2026-08-01**, `research/vignette.md`, decision #103. cos⁴ natural falloff *(Reiss 1945; Kingslake 1992)* as an exposure change in scene-linear light, centred on the crop. ⚠ Not the lens vignette **correction** in §6 — that removes a measured falloff, this adds one, and a photograph can carry both |
| Film grain | M4 | |

## 11. Presets & Batch
| Feature | Status | Notes |
|---|---|---|
| User presets | M4 | ✅ **Built 2026-07-30**. A **patch** over DevelopState, not a state: only the groups it carries are applied. Crop, dust spots and masks are excluded from every group — they belong to one photograph. Four built-in looks, not written to the user file so improving one reaches everybody |
| Built-in looks | M4 | Sells the "stunning" promise |
| Copy/paste settings, sync across selection | M4 | ✅ **Built 2026-07-30**. Sync patches sidecars at the JSON-key level without decoding them, so an unedited photo keeps its as-shot white balance. Confirmed, with the group names in the question. ⚠ Applies to every photo in view — no multi-selection yet |
| Batch export | M4 | ✅ **Built 2026-07-30**. One reused engine, ~466 ms a photo with flat memory. ⚠ Each photo's sidecar is restored before export — without that the second frame exports with the first frame's grade, which looks plausible. Numbered suffixes rather than overwriting |

## 12. Edit Model
| Feature | Status | Notes |
|---|---|---|
| Non-destructive op stack → sidecar | M0 | Foundational |
| Undo/redo + history panel | M1 | |
| Snapshots / versions | M4 | ✅ **Built 2026-08-01**, decision #99. The whole `DevelopState` under a name, in a sibling `PHOTO.orion-snapshots.json` — not the sidecar, which autosave rewrites 900 ms after any slider moves. Restoring keeps the working edit first, as one automatic version, because undo dies with the process. ⚠ Mattes a version names are **pinned against the sweep**; one it can no longer find is named on the row before it is pressed |

## 13. Export
| Feature | Status | Notes |
|---|---|---|
| JPEG/TIFF/PNG, quality, resize, color space | M1 | |
| Full-res tiled render path | M1 | Separate from preview pipe |
| Output sharpening, metadata options | M4 | |
| Watermark | M5 | Low priority |

## 14. Performance & UI
| Feature | Status | Notes |
|---|---|---|
| <16 ms slider feedback (preview ROI pipe) | M0 | **Benchmark, not a feature — validate first** |
| Per-node caching, degrade-then-refine on drag | M1 | |
| Before/after, split view | M2 | |
| Keyboard-first workflow | M2 | Capture One's Speed Edit is the model |
| Neutral-gray dark theme | M1 | Neutral surround improves color judgment |
| Customizable tool panels / workspaces | M5 | Capture One's differentiator |

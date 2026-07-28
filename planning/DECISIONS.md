# Orion — Decision Log

One line per decision. Newest last.

| # | Date | Decision | Why |
|---|---|---|---|
| 1 | 2026-07-27 | Planning docs live in `planning/`, kept concise | Single source of truth for Opus build sessions |
| 2 | 2026-07-27 | macOS first; Windows/Linux later | Smaller test matrix, native Metal + Core ML, ship sooner |
| 3 | 2026-07-27 | **No Rust.** Engine in C++20 | User can't debug Rust. C++ also has the stronger imaging ecosystem (LibRaw, OCIO, lensfun, lcms2) |
| 4 | 2026-07-27 | GPU via Metal; shaders authored in Slang | Best Mac tooling + f16 + ANE path; Slang keeps SPIR-V/HLSL portability open |
| 5 | 2026-07-27 | Engine = compute DAG, one shader per node, `rgba16f` linear Rec.2020 | vkdt's proven design; avoids darktable's dual CPU/OpenCL maintenance tax |
| 6 | 2026-07-27 | Scene-referred pipeline, exactly one display transform, no Lab | Avoids artifacts/hue shifts of display-referred; industry direction |
| 7 | 2026-07-27 | Default tone mapper: AgX-family sigmoid, 2–3 sliders | Great defaults without filmic's fiddly multi-tab UI |
| 8 | 2026-07-27 | Demosaic: RCD default (MIT impl), bilinear for thumbs | Near-AMaZE quality at PPG speed, excellent GPU fit |
| 9 | 2026-07-27 | XMP sidecars = source of truth; SQLite = disposable rebuildable index | Folder-first workflow, no catalog lock-in, but instant filtering on large shoots |
| 10 | 2026-07-27 | Avoid exiv2 (GPL-2.0); use Apple ImageIO for metadata | Prevents forced open-sourcing / commercial license fee |
| 11 | 2026-07-27 | Sony ARW is the only priority format for v1 | User's camera; dogfooding beats breadth |
| 12 | 2026-07-27 | Engine is a library with a narrow C API; UI is a client | Makes UI choice reversible and Windows port tractable |
| 13 | 2026-07-27 | **No Vulkan.** Metal only on macOS | Vulkan needs ~1000 lines of boilerplate to draw a triangle; unmaintainable for a solo dev |
| 14 | 2026-07-27 | Maintainability is a hard constraint: one node = one 50–150 line shader; adding a feature = 3-file change | Solo developer must be able to hand-edit any file months later |
| 15 | 2026-07-27 | Build bilateral grid + Bilateral Guided Upsampling as first-class DAG node types in M1 | General escape hatch for the 16 ms budget — decouples algorithm cost from slider responsiveness (MIT/Google, open-source impl) |
| 16 | 2026-07-27 | `CLAUDE.md` at root + `planning/STATUS.md` updated every session | Survives context clears and session wipes |
| 17 | 2026-07-27 | **No bespoke/Skia UI layer** | 1.5–2.5 engineer-years for macOS alone; Zed took 5 yrs + 1M LOC funded. Impossible solo |
| 18 | 2026-07-27 | No Dear ImGui for product UI — but **use it for internal debug tooling** | Its own FAQ says skinning is limited, built for debug tools. Perfect for a pipeline inspector |
| 19 | ~~2026-07-27~~ | ~~UI shell = SwiftUI/AppKit~~ **SUPERSEDED by #21** | Was based on two errors: HDR is not Qt-exclusive-missing, and the platform-integration argument killed bespoke, not Qt |
| 20 | ~~2026-07-27~~ | ~~`@Observable` view models~~ **VOID** — SwiftUI not chosen | |
| 21 | ~~2026-07-27~~ | ~~UI shell = Qt 6 / QML~~ **SUPERSEDED by #25** | Was correct when the criterion was "stay in one language". Priorities shifted to stunning look + minimal bloat, where native wins |
| 22–24 | ~~2026-07-27~~ | ~~Qt LGPL / styling rules~~ **VOID** — Qt not chosen | Note for the record: Qt is **not** paywalled. LGPLv3 is genuinely free for closed-source commercial use with dynamic linking |
| 25 | 2026-07-27 | **UI shell = SwiftUI/AppKit + C++ engine** (final) | Stunning look and minimal bloat are the top two priorities; native wins hardest on both. ~5–15 MB bundle vs Qt's 40–60 MB; Apple materials/vibrancy/typography free vs building a visual language from scratch. Zero licensing obligations |
| 26 | 2026-07-27 | View models = plain `@Observable` objects, zero SwiftUI types inside | Any panel re-hostable in AppKit when SwiftUI hits a density cliff. Cheap on day one, expensive to retrofit. Sets macOS 14+ floor |
| 27 | 2026-07-27 | Narrow hand-authored POD facade at the C++↔Swift boundary | No templates, no move-only types, **no exceptions escaping (they terminate the process)**. Only boundary stable across Swift releases |
| 28 | 2026-07-27 | Rejected: C++ webview shells (`webview/webview`, saucer), WebGPU, WebAssembly, Go | Webview canvas layering is fragile and the canvas *is* the app. WebGPU is an abstraction over Metal (no speedup). Wasm is slower. Go has no imaging libraries, cgo overhead, GC in the pixel path |
| 29 | 2026-07-28 | **Clip the mosaic to a single white level after white balance**, in `linearize`, before demosaic | A blown pixel arrives as (S, S, S) and white balance turns it into the gains — magenta on every clipped light. dcraw's default highlight mode; before demosaic because RCD interpolates across an unclipped neighbour and leaves a fringe rather than a clean edge. Costs the headroom highlight reconstruction could have used, which is the right trade: the cast was on every frame, the reconstruction is optional |
| 30 | 2026-07-28 | **A control's readout is its own reset**, colored by whether it has moved | One affordance says which controls are modified *and* what is clickable. A separate change dot in the margin would say it twice, smaller and harder to hit. The base is per photo, not per type: white balance is the camera's reading |
| 31 | 2026-07-28 | **Export converts through ColorSync, tagged sRGB at source** | A chromatic adaptation typed in here is a cast waiting to happen, and ColorSync's definitions are what every other application reads the file against. Tagging the pixels as the destination instead of converting them is how a file opens oversaturated |
| 32 | 2026-07-28 | **Export metadata comes from ImageIO reading the RAW**, not from LibRaw or exiv2 | exiv2 is GPL (#10). ImageIO reads the container's own EXIF/TIFF/GPS blocks, so the export carries what the camera recorded. Orientation is dropped on purpose — the geometry node already applied it, and copying the tag turns the picture twice |
| 33 | 2026-07-28 | **American spelling throughout**, code and docs | The developer's own English. `denoiseColour` is still read from old sidecars |


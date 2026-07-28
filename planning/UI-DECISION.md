# Orion — UI Shell Decision

Working doc. The one blocking architectural choice. Requirements: modern/beautiful (Capture One north star), fast, C++-friendly, macOS first, must display a Metal texture with zero copies, **maintainable by a solo developer**.

---

## Option A — SwiftUI/AppKit + C++ engine

### C++ interop: better than expected
Swift 5.9+ embeds Clang and imports C++ headers directly as modules — **no C or Objective-C shim required** ([Swift.org](https://www.swift.org/documentation/cxx-interop/)). Apple's [WWDC25 session 311](https://developer.apple.com/videos/play/wwdc2025/311/) demos this with — notably — an *image pipeline* example (`struct ImageBuffer { std::vector<uint8_t> data; }`), almost exactly our boundary.

Works: `std::vector`, `array`, `span`, `string`, `map`, `optional`, `function`; random-access containers auto-conform to `RandomAccessCollection`; virtual methods; template *specializations* via `typedef`.

**Does not work — these shape the design:**
- **C++ exceptions cannot be caught. An uncaught one reaching Swift terminates the process.** The engine must never let one escape.
- No move-only types as parameters (no r-value refs) — awkward for a C++20 engine leaning on move semantics.
- `std::variant`/`std::tuple` not fully supported; class templates need explicit instantiation.
- Real compiler bugs still exist at the `std::vector` boundary ([swiftlang#70253](https://github.com/swiftlang/swift/issues/70253)).

**Conclusion:** even though Swift no longer forces a shim, still hand-author a **narrow POD facade** — plain structs, int/float handles, no templates, no exceptions crossing. Not because Swift demands it, but because it's the only boundary stable across Swift releases *and* it doubles as the Windows port seam.

### Metal canvas: solved, genuinely zero-copy
Wrap `MTKView` or `CAMetalLayer` in `NSViewRepresentable` — Apple's recommended path ([WWDC22 10114](https://developer.apple.com/videos/play/wwdc2022/10114/)). The C++ engine encodes straight into the drawable's texture; Swift never touches pixels. Pass `MTLDevice`/`MTLCommandQueue` as opaque handles.

⚠️ SwiftUI's own `.layerEffect`/`.colorEffect`/`.drawingGroup` **cannot** do this — they rasterize SwiftUI's own content. UI garnish, not a viewer.

### ★ HDR/EDR — the underrated argument
This is arguably the strongest reason to go native, and I'd underweighted it:
- `wantsExtendedDynamicRangeContent = true`
- `pixelFormat = .rgba16Float` — explicitly preferred over 10-bit `bgr10a2Unorm`
- `colorspace = extendedLinearDisplayP3` / `extendedLinearITUR_2020`
- Headroom via `NSScreen.maximumExtendedDynamicRangeColorComponentValue`

Half-float working space + P3/Rec.2020 extended-linear output is a *better* color path than 10-bit integer, matches our `rgba16f` pipeline exactly, and would be a fight on Qt or web. ([Metal by Example](https://metalbyexample.com/hdr-video/))

### SwiftUI for dense pro UI: the weak point
Not "costly" — **unpredictable**, which is worse.

- `LazyVGrid` fine for "a few hundred to a few thousand" items; stutters at tens of thousands. **No cell reuse** like `NSCollectionView` ([DigitalBlake](https://digitalblake.com/2026/04/28/swiftui-vs-appkit-macos-ui-performance/)). A filmstrip of hundreds is fine; a 50k grid is not.
- macOS worse than iOS: reports of jitter past ~2000 items, one case of [30–45s to render 1700 items](https://developer.apple.com/forums/thread/718929).
- `body` runs on the main thread on every state change — structural tax absent in AppKit.
- Counterpoint: [Eclectic Light](https://eclecticlight.co/2026/04/04/explainer-appkit-and-swiftui/) reports recent macOS Lists handling hundreds of thousands of rows fine. **The contradiction is the problem** — it's construction- and OS-version-dependent.
- Loudest 2026 critique: "productive, modern, and often delightful, right up until you try to make a really good Mac app" ([Daring Fireball](https://daringfireball.net/2026/06/swiftui_only_makes_it_easy_to_develop_bad_apps)). Ghostty ripped out SwiftUI lifecycle management in an 802-line change.
- Known gaps that bite a photo editor specifically: **no direct pinch-zoom trackpad events**, no imperative window-title API.

**`@Observable` is mandatory, not optional.** `ObservableObject` re-renders on *any* published change; with a 40-parameter edit model, one slider drag repaints all 40 panels at 60 Hz. Sets a **macOS 14+ floor**.

### Precedents
- **Pixelmator Pro** — Swift + Metal + Core ML + SwiftUI photo editor that ships ([Wikipedia](https://en.wikipedia.org/wiki/Pixelmator_Pro)). Caveat: its engine is Metal/Core Image, not a separate C++ core, and SwiftUI is credited for animations rather than all chrome.
- **Ghostty** — best structural analogue and open source: `libghostty` is a **C-ABI** core; macOS shell is Swift/AppKit/SwiftUI + Metal, Linux is GTK4 + OpenGL ([ghostty.org](https://ghostty.org/docs/about), [Hashimoto](https://mitchellh.com/writing/libghostty-is-coming)).
- **Raycast** — chose **AppKit**, not SwiftUI, in 2025 ([engineering blog](https://www.raycast.com/blog/a-technical-deep-dive-into-the-new-raycast)).
- No public architecture docs found for Capture One, Affinity, Lightroom, or Resolve — treat internet claims about their internals as unsourced.

### Learning curve: favorable
Doug Gregor (Swift compiler lead) wrote [*Swift for C++ Practitioners*](https://www.douggregor.net/posts/swift-for-cxx-practitioners-value-types/), aimed exactly at this profile. His warning: C++ devs "project C++ idioms onto Swift" because the languages feel similar. Four things to unlearn: value semantics, ARC, optionals, protocol generics (not templates). Target SwiftUI over AppKit — declarative model is close to the React mental model already held. Productive in weeks; the last 10% (windowing, focus, text, perf debugging) is where months go.

### Portability cost — the honest version
- **Ports cleanly (~65–80%):** RAW decode, demosaic, color math, tone/curve logic, index DB, file I/O, undo model, threading.
- **Does NOT port despite being "engine":** every Metal shader + the whole command-encoding layer. MSL→HLSL/SPIR-V plus a D3D12 backend is a second project. *Slang mitigates the shader half; the encoding layer is still real work.*
- **Rewritten wholesale:** SwiftUI shell, window/document lifecycle, EDR display integration, file dialogs, drag-drop, preferences.

### Verdict on Option A
Viable, with one non-negotiable mitigation. The three things that could have killed it don't: interop is real and shim-free, the Metal canvas is unremarkable to build, and HDR/P3/half-float output — the things that actually differentiate a pro photo editor — are first-class here and a fight anywhere else.

**Biggest risk: SwiftUI's macOS maturity ceiling is unpredictable, not merely costly.** Nobody can say in advance *which* panel hits a cliff, and when one does the fix is on Apple's annual cycle.

**Mitigation — cheap on day one, expensive to retrofit:** treat every panel as independently replaceable. Keep view models plain `@Observable` objects with zero SwiftUI types inside, so any panel can be re-hosted in `NSViewRepresentable` without touching state or engine code. Explicitly budget the filmstrip/grid to end up as `NSCollectionView`.

---

## Option C — Custom UI layer (Skia / bespoke Metal renderer)

**Researched thoroughly. Verdict: disqualified for a solo developer.** The numbers are not close.

### The headline finding
> *"A retained-mode C++ widget kit that looks modern does not exist off-the-shelf."*

Every credible modern-looking desktop app in this class either (a) uses Qt and fights it into submission, or (b) builds their own. There is no third door.

### Cost of building your own
Estimated **60–110 engineer-weeks ≈ 1.5–2.5 engineer-years, macOS only** — excluding the Skia/Metal integration itself, excluding Windows, and assuming zero rework (rework is guaranteed; Zed, Figma and Rive each rebuilt their renderer at least once).

Reference points:
- **Zed** — [five years, 1M+ lines, funded team](https://zed.dev/blog/zed-1-0), and GPUI is *still pre-1.0* with breaking changes between versions. Their scope cut was brutal: **five primitives only** (rect, shadow, text, icon, image), no arbitrary path rendering at all, CoreText delegated for all text ([writeup](https://zed.dev/blog/videogame)).
- **Figma** — custom renderer, custom DOM, custom text engine. *"We've basically ended up building a browser inside a browser."*
- **Aseprite** — small-team reality check: built `laf` over ~1,025 commits, and is **still pinned to Skia m124 from June 2024**. Pin and never upgrade is what small-team maintenance actually looks like.

### What you'd be rebuilding that SwiftUI gives free
This is the part that settles it. Each of these is yours to write in a bespoke stack:

| Subsystem | Est. |
|---|---|
| Text shaping/layout (HarfBuzz + ICU, bidi, fallback, itemization) | 2–4 wk to *integrate*; ∞ to write |
| Text editing widget (caret, selection, undo, RTL affinity) | 4–8 wk |
| IME / marked text (`NSTextInputClient`, tested with CJK IMEs) | 2–4 wk |
| Focus, keyboard nav, modal focus trapping | 4–6 wk |
| Scroll momentum, rubber-banding, scroll phases | 3–5 wk |
| Virtualized lists/grids (non-negotiable for 50k thumbnails) | 4–6 wk |
| Accessibility (`NSAccessibility`) | 6–12 wk, ongoing |
| Native menus, file dialogs, drag & drop | 5–8 wk |
| DPI / multi-monitor / Retina-to-non-Retina drag | 2–4 wk |
| ~25–40 widgets for a RAW editor | 15–30 wk |

On text specifically, Raph Levien: *"A complete account of text layout would be at least a small book."* Accessibility and IME are the items teams skip in v1 and then find **un-retrofittable**, because focus and text state weren't modeled correctly from day one.

### Skia-specific risks (if you went that route anyway)
- **No API stability, no ABI, no semver.** Concrete churn in exactly the code you'd write: m115 renamed `SkSurface::MakeFromBackendRenderTarget` → `SkSurfaces::WrapBackendRenderTarget`; m124 moved `GrDirectContext::MakeMetal` → `GrDirectContexts::MakeMetal`; m128 moved the Graphite Metal types. Every upgrade is a porting project.
- **The Metal backend is the least battle-tested path.** Per Skia devs on [skia-discuss](https://groups.google.com/g/skia-discuss/c/Pd92csb5o4o): Ganesh is slated for deprecation, Vulkan *"will definitely be maintained and used in production,"* while **Metal "will be maintained and kept working for testing purposes."** Chrome ships Graphite on Apple Silicon through Dawn, not Skia's native Metal path. You'd be the one finding the bugs.
- **Flutter — the largest non-Google Skia consumer — built a replacement** (Impeller) and [removed Skia on iOS entirely](https://docs.flutter.dev/perf/impeller). "Everyone uses Skia for custom UI" is now false.
- Build story: GN + ninja + depot_tools, CMake explicitly unsupported. Prebuilts exist ([JetBrains/skia](https://github.com/JetBrains/skia/releases), m151, macOS arm64, 44 MB, weekly) but you write the CMake glue.

### Other options in this family, dismissed
- **Dear ImGui** — disqualified by its own [FAQ](https://github.com/ocornut/imgui/blob/master/docs/FAQ.md): *"designed and optimized to create debug tools, the amount of skinning you can apply is limited… only so much you can stray from the default look and feel."* Your instinct was right. **Do use it for internal pipeline-debug tooling.**
- **Elements (cycfi)** — README says *"not yet production ready"*; shipping backend reverted to **Cairo (CPU)**, which cannot drive a 120 Hz canvas.
- **NanoVG** — README: *"This project is not actively maintained."* OpenGL-only; Metal port last touched 2023. No text shaping.
- **Blend2D** — genuinely excellent code, zlib licensed, best CMake story here. But **CPU-only** and still 0.x after 11 years with a bus factor of ~1. Wrong for a full-window 120 Hz compositor.
- **vger** — Metal-native and fast, but one person's renderer for one app; no cubic béziers, single font.
- **Rive Renderer** (C++, MIT, very active) and **Yoga** (Meta, MIT, flexbox-only) are the two credible pieces if you ever *do* go bespoke.

---

## Option B — Qt 6 / QML

✅ Researched against primary Qt documentation. **Stronger than I initially assessed — two of my objections were wrong.**

### ✅ HDR is supported — my earlier claim was wrong
[`QRhiSwapChain`](https://doc.qt.io/qt-6/qrhiswapchain.html) exposes three HDR formats, including **`HDRExtendedDisplayP3Linear` — 16-bit float RGBA, extended linear Display P3**. That is *exactly* the format I said only Metal/SwiftUI could give us. Also `HDRExtendedSrgbLinear` (scRGB) and `HDR10` (Rec. 2020), plus `isFormatSupported()` and `hdrInfo()` for querying the display. **HDR is not a differentiator between Qt and SwiftUI.**

### ✅ Metal texture import is documented, not a hack
Qt Quick's [scene graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html) documents three integration paths, and there is an official **"Scene Graph — Metal Texture Import"** example:
1. **Texture-based** — render to an `MTLTexture` externally, wrap it in a custom `QQuickItem`. This is our case.
2. **Underlay/overlay** — issue Metal commands around `beforeRendering()`/`afterRendering()`, bracketed with `beginExternalCommands()`/`endExternalCommands()`.
3. **Inline** — subclass `QSGRenderNode` to inject draw calls into the scene graph stream.

Fiddlier than `MTKView` in `NSViewRepresentable`, but supported and documented rather than workaround territory.

### ✅ Styling can go fully bespoke
[Qt Quick Controls customization](https://doc.qt.io/qt-6/qtquickcontrols-customize.html) supports a complete custom style: QML files named per control, rooted on `QtQuick.Templates`, registered via `qmldir`. A full bespoke design system is achievable, including C++ extensions via `QML_ELEMENT` and attached properties for theme-wide tokens.

⚠️ Important caveat: *"The macOS and Windows styles are not suitable for customizing."* You must base a custom style on a cross-platform base style, not the native one — which means you're building the look from scratch (not the widget primitives, but the entire visual language).

### ❌ LGPLv3 — the real cost, and it's non-trivial for a paid closed-source Mac app
Per [Qt's own obligations page](https://www.qt.io/licensing/open-source-lgpl-obligations):

- **Dynamic linking is fine** — *"it is possible, but not mandatory, to keep application source code proprietary."* Static linking is not: the app *"may no longer be 'work that uses the library' and thus become subject to LGPL."*
- You must **ship Qt's complete corresponding source** with the app, or a written offer for it.
- You must display a **prominent notice** and include the LGPL license text.
- **The user must be able to relink Qt — including by reverse engineering** — and under LGPLv3 must be able to *run* the relinked binary, with *"sufficient installation information"* provided. This *"forbids the creation of closed devices, also known as tivoization."*
- **App stores are called out as a conflict:** *"some means of distribution, such as online application stores, may have rules that are in conflict with LGPL, in which case those cannot be used."*

For a code-signed, notarized Mac app the relinking right creates real friction (a relinked binary breaks the signature), and the Mac App Store is effectively off the table. Commercial Qt licensing exists as the escape hatch, at cost.

## What pro imaging apps actually use

⚠️ **Research incomplete** — agent died on session limit. Partial signal only: no public architecture documentation was found for Capture One, Affinity, Lightroom, or DaVinci Resolve. Treat any online claim about their internals as unsourced.

---

## Recommendation

### Settled by evidence
1. **Bespoke/Skia UI is out.** 1.5–2.5 engineer-years for macOS alone. Violates the solo-maintainability constraint outright.
2. **Dear ImGui is out for product UI** — by its own documentation. **Adopt it for internal debug tooling** (pipeline inspector, node graph, overdraw, frame timings) from week one.

### Two corrections to earlier drafts of this doc
- **HDR is not a differentiator.** Qt's `QRhiSwapChain` supports `HDRExtendedDisplayP3Linear` — the exact 16-bit float extended-linear P3 format. My earlier claim that only Metal/SwiftUI offered this was wrong.
- **The "40 engineer-weeks of platform integration" argument kills bespoke, not Qt.** Qt also provides text shaping, IME, accessibility, dialogs and drag-drop. I conflated those two arguments. Qt's versions are Qt abstractions rather than native controls, so the feel differs — but the work is not yours either way.

### The real Qt vs SwiftUI tradeoff
| | Qt 6 / QML | SwiftUI/AppKit |
|---|---|---|
| Language | **C++ throughout** — no new language | Swift for UI (new, but small surface) |
| Windows port | **UI ports free** | UI rewritten wholesale |
| Predictability | **Mature, no density cliffs** | Unpredictable ceiling on dense panels |
| Native Mac feel | Qt abstractions — competent, not native | **Best possible** |
| Licensing | LGPLv3: ship Qt source, relink rights, **no Mac App Store**; or pay for commercial | **None** |
| Metal canvas | Documented texture-import path | **Trivial (`MTKView`)** |
| Visual design | Full bespoke style possible, **built from scratch** | Native look free; bespoke also possible |
| Binary size | Qt frameworks bundled | Small |

## Option D — C++ webview shell (Tauri-equivalent), WebGPU, Wasm, Go

All rejected. Assessed from knowledge, not verified research (search budget exhausted).

- **`webview/webview` / saucer** — real Tauri equivalents for C++, wrapping the *system* webview, so bundles are ~5–10 MB not Electron's 150 MB+. But a 4K frame is ~33 MB; you cannot push that through IPC at 60 fps. The only viable path is layering a `CAMetalLayer` view under a transparent WKWebView, which means the native layer must track a DOM element's geometry (resize desync) and trackpad gestures over the webview need interception. **Latency is fine; fragility is the problem — and for a photo editor the canvas *is* the app.**
- **WebGPU** — an abstraction *over* Metal on macOS. Cannot be faster than Metal. Buys portability, not speed. Dawn is a legitimate C++ option if cross-platform ever matters more than it does now.
- **WebAssembly** — substantially *slower*. No direct GPU access, constrained SIMD, copy overhead at every boundary. Only makes sense for a browser build.
- **Go** — wrong tool, not close. No mature RAW/OCIO/lensfun libraries, cgo per-call overhead in a GPU-driving hot path, GC pauses against multi-hundred-MB buffers.

---

### ✅ FINAL: SwiftUI/AppKit + C++ engine

Chosen 2026-07-27, revising the earlier Qt pick after the developer's priorities clarified to **stunning look + minimal bloat** above language uniformity.

1. **Looks.** Apple's materials, vibrancy, typography and animation curves come free — the things that make an app read as expensive rather than merely dark-themed. With Qt the entire visual language is built from scratch, which was the main risk to "looks amazing."
2. **Bloat.** ~5–15 MB bundle vs Qt's 40–60 MB of bundled frameworks.
3. **Zero licensing obligations.** (For the record, Qt is **not** paywalled — LGPLv3 is genuinely free for closed-source commercial use with dynamic linking. It was dropped on merit, not licensing.)
4. **Canvas is trivial and truly zero-copy** — `MTKView` in `NSViewRepresentable`.
5. **Learning curve is contained.** The engine — all the hard, debuggable code — stays C++. Swift is confined to UI, the most tutorial-covered and Claude-assisted part of the stack. Doug Gregor's [*Swift for C++ Practitioners*](https://www.douggregor.net/posts/swift-for-cxx-practitioners-value-types/) targets exactly this background.

**Accepted risk:** SwiftUI's density ceiling is unpredictable. Mitigation is mandatory from day one — see decisions #26–27.

**Given up:** free Windows UI portability. Acceptable — "as long as I can use it on a Mac."

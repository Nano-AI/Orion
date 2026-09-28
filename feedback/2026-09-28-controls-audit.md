# Develop controls audit

2026-09-28. Source-only review of the 17 named controls/panel files. No build,
app launch, GPU render, physical pointer test, or VoiceOver session was run.
P1 means likely data loss or wrong photograph; P2 means a material control or
responsiveness failure. Source paths below establish reachability, not observed
UI behavior or a measured latency.

## Findings

### 1. P1 — Disabling a lens profile on A silently disables it on B

`app/DevelopPanels+Optics.swift:27-40` binds the per-photo-looking Lens profile
switch to `engine.lensProfileEnabled`. This Boolean starts `true` but lives only
on the shared `Engine` (`app/Engine.swift:231`). `Engine.open` resets the chosen
lens and all `DevelopState` values for each photo (`app/Engine+Document.swift:
110-129`); neither `open`, `asShotState`, `DevelopState`, nor restore resets or
stores the switch. The actual C block uses that retained Boolean to suppress
the found profile (`app/Engine.swift:751`). Trigger: open profiled A, turn its
switch off, then open profiled B. B displays the same off switch and renders
without its profile, even though the off choice was made on A. It also cannot
be recovered from B's sidecar on a fresh launch. **Evidence:** source trace;
no two-photo render was executed. **Root fix:** make the enabled choice part of
the per-photo state, with a default of true for old sidecars, and let open/restore
seat it before rendering. **Check:** open A and B with found profiles, disable
only A, switch between them and reopen both; assert each C adjustment flag and
render, then inspect each sidecar's persisted choice.

### 2. P2 — Picking or clearing a lens profile has no undo step

The two product actions assign `engine.lensChoice` directly
(`app/DevelopPanels+Optics.swift:136,149-151`). Its observer applies the lookup
and renders (`app/Engine.swift:220-226`), but does not call `Engine.edit` or
`history.record`. `lensChoice` is in `DevelopState` (`app/Engine.swift:553`),
so later recorded edits can snapshot the choice, but the choice itself cannot
be undone immediately. Trigger: choose a lens in Optics and invoke Undo before
any other adjustment. **Evidence:** source trace through both callers and the
only history entry path (`app/Engine.swift:715-721`); no keyboard action was
executed. **Root fix:** record one history state for each accepted selection or
clear at the common choice action. Avoid recording rejected names or internal
open/restore assignments. **Check:** select, undo, redo and clear with a real
matched profile; assert selected name, applied coefficients and history label at
each step.

### 3. P2 — Curve point insertion renders at full resolution before preview arming

When the graph is pressed away from a handle, `CurveEditor.drag` inserts a point
and assigns `engine.curve[channel]` at `app/CurveEditor.swift:266-275`; only
after that assignment does it call `beginInteraction` (`:278-283`). The curve
property's `didSet` calls `pushAndRender` (`app/Engine.swift:202`), and
`beginInteraction` only changes render routing after it succeeds
(`app/Engine+Render.swift:130-153`). Thus the first insertion tick pays for a
full graph render before the preview path starts, exactly the expensive first
tick the preview contract is meant to avoid. Dragging an existing point arms
before its mutation. **Evidence:** source order; no timing measurement.
**Root fix:** determine that insertion is valid, arm, then assign the new curve;
keep rejected clicks inert. **Check:** instrument a new-point press and a
handle press on a real loaded frame; assert the first changed render in each
uses the preview graph, the final render uses the full graph, and the settled
pixels agree.

### 4. P2 — Mixer band swatches cannot be selected from the keyboard

The eight swatches use a `Circle` with `.onTapGesture` and accessibility traits
(`app/DevelopPanels+Color.swift:83-102`). They have no `Button`, `.focusable()`,
key handler or accessibility activation action. The panel's Hue/Saturation/
Luminance bindings index the selected `band` (`app/OrionApp+Tools.swift:150-154`),
so a keyboard user cannot choose which band those sliders edit through this row.
The Target tool requires a photo pick and is a different workflow.
**Evidence:** source-only reachability; VoiceOver behavior was not run.
**Root fix:** make each swatch a native `Button` with its name and selected trait,
retaining the existing 24-point hit area. **Check:** tab to each swatch and
activate it with Space, then assert the selected band and subsequent slider
binding; repeat with VoiceOver on a real window.

## Known overlaps and limits

The mask audit already covers asynchronous selection destination, GPU layer
ownership, brush cursor geometry and refinement cost. The desktop audit covers
autosave, XMP, sync, batch and library lifecycle. This report does not count
those again. `check-gestures.py` statically pins four preview arming calls but
does not drive a SwiftUI gesture. Existing curve geometry and color grading
math tests do not establish the event ordering in finding 3. Source comments
were used to locate intent, never as proof that an interaction ran. No broad
control parity, accessibility, latency or RAM result follows from this audit.

## Read inventory

Fully read (line counts from this tree): `app/AdjustmentSlider.swift` 143,
`AnalogTrack.swift` 289, `ColorLoupe.swift` 80, `ColorWheel.swift` 249,
`CompareOverlay.swift` 134, `CropOverlay.swift` 211, `CurveEditor.swift` 345,
`DevelopPanels+Color.swift` 122, `DevelopPanels+Detail.swift` 151,
`DevelopPanels+Light.swift` 59, `DevelopPanels+Optics.swift` 173,
`DevelopPanels.swift` 57, `Engraved.swift` 229, `RatingBar.swift` 80,
`TargetedAdjust.swift` 140, `ToolButton.swift` 78, `TrackTint.swift` 128;
**2,668 lines total**.

Targeted caller/contract inspection: `Engine.swift` edit/state/lens/curve/C-block
paths; `Engine+Document.swift` open/restore; `Engine+Render.swift` interaction;
`Engine+Geometry.swift` crop; `OrionApp+Tools.swift` bindings;
`OrionApp+Canvas.swift` overlay and tab wiring; `EditHistory.swift` record;
`CurveSupport.swift`, `CurveMath.swift`, `ViewportTests+Curve.swift`, and
`check-gestures.py`. Context: current `planning/STATUS.md`, root `AGENTS.md`,
relevant research/decisions, `feedback/README.md`, desktop and mask audits,
and SwiftUI review guidance for API, state, performance, focus and accessibility.

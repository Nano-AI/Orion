import SwiftUI

/// The gradient mask, drawn on the canvas and dragged there.
///
/// Sliders can express every one of these numbers and that is exactly the
/// problem: nobody places a gradient by deciding its center is 0.42. The
/// controls stay — they are precise, keyboard-reachable and the only way to
/// reach feather and roundness — but the placing happens here.
///
/// **No geometry lives in this file.** Where the handles are, what a press
/// grabs and what a drag means are all `CanvasLayout`, which is the one copy of
/// where the picture is on screen and is tested without a window. This view
/// draws what it is told and forwards presses.
struct MaskOverlay: View {
    @Bindable var engine: Engine

    /// The map from normalized picture coordinates to this view, built from the
    /// viewport's own numbers by the caller.
    let map: CanvasLayout.PictureMap

    @State private var dragging: CanvasLayout.MaskHandle?
    @State private var grab: CGPoint = .zero
    @State private var start: CanvasLayout.MaskPlacement?

    // Painting. `carry` is the distance left over from the previous pointer
    // event, which is what keeps dab spacing continuous across events instead
    // of restarting at each one.
    @State private var painting = false
    @State private var carry: CGFloat = 0
    /// How many dabs this gesture has laid. ⚠ A count, not the dabs: the
    /// stroke itself lives in `Engine`'s unobserved buffers for the length of
    /// the gesture. Two `@State` arrays here meant every pointer event
    /// republished a growing SwiftUI value and re-flattened it on the way to
    /// the engine.
    @State private var dabs = 0
    @State private var last: CGPoint = .zero
    @State private var cursor: CGPoint?

    private var mask: CanvasLayout.MaskPlacement { engine.maskPlacement }
    private var isBrush: Bool { engine.maskKind == 3 }

    // The mask reads as an instruction to the picture rather than as furniture:
    // a thin white line with a dark halo under it, which stays legible on both
    // a blown sky and a black shadow. The same trick the crop overlay uses.
    private let line = Color.white
    private let halo = Color.black.opacity(0.55)

    var body: some View {
        ZStack(alignment: .topLeading) {
            if isBrush {
                brushCursor
            } else if mask.kind != 0 {
                // Clipped to the picture, not to the canvas. A mask exists on
                // the photograph; drawing its lines across the letterbox says
                // the gradient continues into the black bars, and at fit on a
                // tall window that is most of what is on screen. The handles
                // stay unclipped — a handle is a control, and one dragged to
                // the very edge has to remain grabbable.
                shape.clipShape(Rectangle().path(in: map.rect))
                handles
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .contentShape(Rectangle())
        .gesture(isBrush ? AnyGesture(paint.map { _ in () })
                         : AnyGesture(drag.map { _ in () }))
        // Where the brush is, without waiting for a press — a brush whose size
        // you can only discover by painting is a brush you undo a lot.
        .onContinuousHover { phase in
            guard isBrush else { cursor = nil; return }
            switch phase {
            case .active(let p): cursor = p
            case .ended:         cursor = nil
            }
        }
        // The picture underneath still pans and zooms; only the mask's own
        // handles and body take a press, and `maskHit` returning nil is what
        // lets everything else through.
        .allowsHitTesting(mask.kind != 0)
    }

    // MARK: What it looks like

    @ViewBuilder private var shape: some View {
        switch mask.kind {
        case 1: linearLines
        case 2: radialOutlines
        default: EmptyView()
        }
    }

    /// Three lines: where the effect is nothing, where it is half, where it is
    /// all. The middle one is solid because it is the one you aim; the outer
    /// two are dashed because they are the extent of a ramp rather than an
    /// edge, and drawing them solid reads as a hard boundary the mask does not
    /// have.
    private var linearLines: some View {
        ZStack {
            isoLine(0, dashed: true)
            isoLine(1, dashed: true)
            isoLine(0.5, dashed: false)
        }
    }

    private func isoLine(_ t: CGFloat, dashed: Bool) -> some View {
        let (a, b) = CanvasLayout.maskIsoLine(mask, at: t)
        var p = Path()
        p.move(to: map.point(a))
        p.addLine(to: map.point(b))

        let dash = StrokeStyle(lineWidth: 1.5, lineCap: .round,
                               dash: dashed ? [5, 5] : [])
        return ZStack {
            p.stroke(halo, style: StrokeStyle(lineWidth: 3.5, lineCap: .round,
                                              dash: dashed ? [5, 5] : []))
            p.stroke(line, style: dash)
        }
    }

    /// The boundary of the effect, and inside it where the falloff begins.
    ///
    /// Both are sampled in the mask's own space by `CanvasLayout` and mapped out
    /// point by point rather than drawn as an ellipse, because the shader is
    /// isotropic in normalized coordinates and the picture is not square — an
    /// ellipse fitted on screen is a different curve from the one the falloff
    /// follows, and further from it the further the frame is from square.
    private var radialOutlines: some View {
        ZStack {
            outline(at: 1, dashed: false)
            if mask.feather > 0.01 {
                outline(at: 1 - mask.feather, dashed: true)
            }
        }
    }

    private func outline(at level: CGFloat, dashed: Bool) -> some View {
        let pts = CanvasLayout.maskOutline(mask, at: level).map(map.point)
        var p = Path()
        if let first = pts.first {
            p.move(to: first)
            for q in pts.dropFirst() { p.addLine(to: q) }
            p.closeSubpath()
        }
        let dash: [CGFloat] = dashed ? [4, 5] : []
        return ZStack {
            p.stroke(halo, style: StrokeStyle(lineWidth: 3.5, dash: dash))
            p.stroke(line.opacity(dashed ? 0.75 : 1),
                     style: StrokeStyle(lineWidth: 1.5, dash: dash))
        }
    }

    // MARK: The handles

    private var handles: some View {
        ForEach(CanvasLayout.maskHandles(mask), id: \.self) { h in
            knob(h)
                .position(CanvasLayout.maskHandlePoint(h, mask, map))
        }
    }

    @ViewBuilder private func knob(_ h: CanvasLayout.MaskHandle) -> some View {
        switch h {
        case .center:
            // Larger and ringed, because it is the one you reach for to move
            // the whole thing and it has to be findable among the others.
            ZStack {
                Circle().fill(halo).frame(width: 15, height: 15)
                Circle().fill(line).frame(width: 11, height: 11)
                Circle().fill(Color.black.opacity(0.7)).frame(width: 4, height: 4)
            }
        case .rotate:
            ZStack {
                Circle().fill(halo).frame(width: 14, height: 14)
                Circle().fill(line).frame(width: 10, height: 10)
                // `arrow.clockwise`, not `arrow.trianglehead.clockwise`: the
                // latter is SF Symbols 6 and this app's floor is macOS 14, so
                // it draws here on the developer's machine and blank on a
                // user's. Exactly the kind of thing a screenshot on one Mac
                // cannot catch.
                Image(systemName: "arrow.clockwise")
                    .font(.system(size: 7, weight: .bold))
                    .foregroundStyle(Color.black.opacity(0.75))
            }
        default:
            ZStack {
                Circle().fill(halo).frame(width: 12, height: 12)
                Circle().fill(line).frame(width: 8, height: 8)
            }
        }
    }

    // MARK: The brush

    /// The nib, drawn where the pointer is. **Round**, because the paint is.
    ///
    /// ⚠ This said the opposite until 2026-07-31 — "an ellipse on any frame
    /// that is not square, because that is the shape the kernel stamps" — and
    /// cited `mask_brush.slang`, which decision #62 folded away. The fold moved
    /// the dab into frame pixels and the cursor was left behind, so it drew half
    /// again as wide as the paint on every landscape photograph. The shape lives
    /// in `CanvasLayout.brushCursor` and `testBrushCursorIsRound` pins it.
    ///
    /// Built in a plain function, not in the `ViewBuilder` — a `for` loop
    /// cannot live inside one.
    private func cursorPath(_ at: CGPoint) -> Path {
        let pts = CanvasLayout.brushCursor(at: map.unit(at),
                                           radius: CGFloat(engine.brushRadius),
                                           map)
        var p = Path()
        guard let first = pts.first else { return p }
        p.move(to: first)
        for q in pts.dropFirst() { p.addLine(to: q) }
        p.closeSubpath()
        return p
    }

    @ViewBuilder private var brushCursor: some View {
        if let c = cursor {
            let p = cursorPath(c)
            ZStack {
                p.stroke(halo, lineWidth: 3)
                p.stroke(line, lineWidth: 1.25)
            }
            .clipShape(Rectangle().path(in: map.rect))
            .allowsHitTesting(false)
        }
    }

    /// Painting. Dabs are laid along the drag at a fixed spacing rather than
    /// once per pointer event, so a fast stroke and a slow one over the same
    /// path lay the same paint. `CanvasLayout.brushDabs` owns that walk.
    private var paint: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                let here = map.unit(value.location)
                if !painting {
                    painting = true
                    carry = 0
                    dabs = 0
                    // ⚠ **Painting is a drag and had never said so.** Every
                    // slider armed the preview graph through `AnalogTrack`;
                    // this gesture did not, so a stroke re-rendered the *full*
                    // graph at full resolution on every pointer event while a
                    // slider tick paid a sixteenth of that.
                    engine.beginInteraction()
                    // The stroke lives in the engine for the length of the
                    // gesture, in buffers nothing observes — see
                    // `Engine.beginBrushStroke`. It used to live here, in two
                    // `@State` arrays that were re-flattened and re-published
                    // on every pointer event, which invalidated the whole
                    // develop panel sixty times a second.
                    engine.beginBrushStroke()
                    engine.appendBrushDabs([here], erasing: engine.brushErasing)
                    dabs += 1
                    last = here
                    return
                }
                let added = CanvasLayout.brushDabs(from: last, to: here,
                                                   radius: CGFloat(engine.brushRadius),
                                                   carry: &carry)
                last = here
                guard !added.isEmpty else { return }
                engine.appendBrushDabs(added, erasing: engine.brushErasing)
                dabs += added.count
            }
            .onEnded { _ in
                painting = false
                carry = 0
                // One observable write and one history entry for the whole
                // stroke, not one of each per dab.
                let painted = engine.endBrushStroke()
                if painted && dabs > 0 { engine.commitBrushEdit() }
                dabs = 0
                // ⚠ After the commit. `endInteraction` renders the full graph
                // once — every event of the stroke went to the preview, so this
                // is the first time the full graph sees the paint rather than a
                // refinement of it. Unconditional, because a press that laid
                // nothing still has to come off the preview texture.
                engine.endInteraction()
            }
    }

    // MARK: Dragging

    /// One gesture for the whole overlay rather than one per handle.
    ///
    /// The crop overlay gives each corner its own gesture on its own box, which
    /// works because its handles never overlap. A mask's do — pull a radial
    /// mask small and the center, both axis handles and the rotate lollipop are
    /// all within a few points of each other — and stacked SwiftUI gestures
    /// resolve by draw order, so whichever was drawn last would win regardless
    /// of which is nearer the cursor. `CanvasLayout.maskHit` decides instead,
    /// on distance.
    private var drag: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                if dragging == nil {
                    guard let h = CanvasLayout.maskHit(value.startLocation, mask, map)
                    else { return }
                    dragging = h
                    grab = value.startLocation
                    start = mask
                    // ⚠ Here, not at the top of `onChanged`. A press that grabs
                    // no handle falls through to the picture and pans it, and
                    // arming before the hit test would swap the canvas to the
                    // preview texture for a gesture that is not an edit at all.
                    //
                    // Measured with `drag maskCenterX 0.2 0.8 40` on a radial
                    // mask carrying a local exposure: 13.0 ms a tick unarmed,
                    // 1.3 ms armed. Placing a mask by eye is exactly when the
                    // picture has to keep up with the hand.
                    engine.beginInteraction()
                }
                guard let h = dragging, let from = start else { return }
                engine.maskPlacement = CanvasLayout.maskDrag(
                    h, from: grab, to: value.location, start: from, map)
            }
            .onEnded { _ in
                let moved = dragging != nil && start != mask
                dragging = nil
                start = nil
                // Only if it actually changed. A press that grabs a handle and
                // releases without moving is not an edit, and recording it
                // would put an entry in the history that undoes to itself.
                if moved { engine.commitMaskEdit() }
                // Unconditional, unlike the commit. `endInteraction` returns
                // immediately when nothing was armed, so a press that grabbed
                // no handle costs nothing here — and a press that grabbed one
                // and did not move still has to come off the preview texture.
                engine.endInteraction()
            }
    }
}

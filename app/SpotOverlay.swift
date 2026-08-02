import SwiftUI

/// Dust spots, drawn on the canvas and dragged there.
///
/// Before this, a spot was invisible the moment it was placed. The click chose
/// a source for you — one radius and a bit to the right, or downward if that
/// ran off the frame — and there was no way to see where it had gone, no way to
/// move it, and no way to say "no, take it from *there*". The only correction
/// available was Undo spot.
///
/// That is the wrong shape for the tool. Healing a blemish is a judgment about
/// *where the replacement comes from*, and every editor that does this well
/// makes the source a thing you drag. The automatic source stays as the
/// starting position, because a click that immediately does something sensible
/// is worth keeping — it is now a first guess rather than the whole answer.
///
/// **No geometry lives in this file**, the same rule `MaskOverlay` follows:
/// where the handles are and what a press grabs are `CanvasLayout`, which is
/// tested without a window. This draws what it is told and forwards presses.
struct SpotOverlay: View {
    @Bindable var engine: Engine
    let map: CanvasLayout.PictureMap

    @State private var dragging: CanvasLayout.SpotHandle?
    /// Where a fresh spot's source is being dragged out from, before it exists.
    @State private var placing: CGPoint?

    private var spots: [CanvasLayout.SpotPlacement] { engine.spotPlacements }

    // The same ink as the mask overlay: a thin white line over a dark halo, so
    // it stays legible on a blown sky and on a black shadow alike.
    private let line = Color.white
    private let halo = Color.black.opacity(0.55)

    var body: some View {
        Canvas { context, _ in
            for (i, s) in spots.enumerated() {
                draw(s, index: i, in: &context)
            }
        }
        // ⚠ Hit-testing only where a spot actually is. `contentShape` over the
        // whole rectangle would make this view swallow every press on the
        // photograph — including the pans and the eyedropper — for as long as a
        // single spot existed anywhere in the frame.
        .contentShape(spotRegions)
        .gesture(drag)
    }

    /// The grabbable area: both discs of every spot, plus the whole picture
    /// while the tool is armed, since an armed tool is asking for a click
    /// anywhere.
    private var spotRegions: some Shape {
        Path { p in
            if engine.spotPlacing { p.addRect(map.rect) }
            for s in spots {
                for c in [s.destination, s.source] {
                    let v = map.point(c)
                    let r = max(CanvasLayout.spotRadius(s, in: map),
                                CanvasLayout.spotHandleMin)
                    p.addEllipse(in: CGRect(x: v.x - r, y: v.y - r,
                                            width: r * 2, height: r * 2))
                }
            }
        }
    }

    // MARK: Drawing

    private func draw(_ s: CanvasLayout.SpotPlacement, index: Int,
                      in context: inout GraphicsContext) {
        let d = map.point(s.destination)
        let src = map.point(s.source)
        let r = max(CanvasLayout.spotRadius(s, in: map), 4)
        let selected = index == engine.selectedSpot

        // ⚠ The link first, so it passes *under* both discs rather than across
        // them. It is the only thing on screen that says these two circles are
        // one edit, and it is what makes "this came from there" readable at a
        // glance instead of needing a caption.
        var link = Path()
        link.move(to: src)
        link.addLine(to: d)
        context.stroke(link, with: .color(halo), lineWidth: 3)
        context.stroke(link, with: .color(line.opacity(selected ? 0.85 : 0.45)),
                       style: StrokeStyle(lineWidth: 1, dash: [3, 3]))

        // The source: dashed, because it is where the pixels are *taken from*
        // and nothing happens to it. The destination is solid, because that is
        // the part of the photograph being changed. One glance separates them
        // without a legend.
        circle(src, r, in: &context, dashed: true, selected: selected)
        circle(d, r, in: &context, dashed: false, selected: selected)

        // Heal and clone differ only in whether the tone is carried across, and
        // that is invisible until it is wrong. A mark on the destination says
        // which one this spot is.
        if !s.heal {
            var tick = Path()
            tick.move(to: CGPoint(x: d.x - r * 0.35, y: d.y))
            tick.addLine(to: CGPoint(x: d.x + r * 0.35, y: d.y))
            context.stroke(tick, with: .color(halo), lineWidth: 3)
            context.stroke(tick, with: .color(line.opacity(0.9)), lineWidth: 1)
        }
    }

    private func circle(_ c: CGPoint, _ r: CGFloat, in context: inout GraphicsContext,
                        dashed: Bool, selected: Bool) {
        let box = CGRect(x: c.x - r, y: c.y - r, width: r * 2, height: r * 2)
        let path = Path(ellipseIn: box)
        context.stroke(path, with: .color(halo), lineWidth: selected ? 4 : 3)
        context.stroke(path, with: .color(selected ? Palette.accent : line.opacity(0.85)),
                       style: StrokeStyle(lineWidth: selected ? 2 : 1,
                                          dash: dashed ? [4, 3] : []))
    }

    // MARK: Dragging

    private var drag: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { g in
                if dragging == nil && placing == nil {
                    if let hit = CanvasLayout.spotHit(g.startLocation, spots, in: map) {
                        dragging = hit
                        switch hit {
                        case .destination(let i), .source(let i): engine.selectedSpot = i
                        }
                    } else if engine.spotPlacing {
                        // ⚠ Placed on press, then its source dragged out — the
                        // gesture Lightroom uses and the reason the tool reads
                        // as one action. Placing on *release* instead would
                        // mean the photographer drags a source for a spot that
                        // does not exist yet and cannot be shown.
                        let u = CanvasLayout.spotDrag(to: g.startLocation, in: map)
                        guard engine.addSpot(atFrame: u) else { return }
                        placing = u
                    } else {
                        return
                    }
                    // Armed only once a spot is grabbed or placed — the `return`
                    // above is a press that hit nothing and pans the picture
                    // instead. Decision #84.
                    engine.beginInteraction()
                }

                let u = CanvasLayout.spotDrag(to: g.location, in: map)
                if placing != nil {
                    engine.moveSpot(engine.selectedSpot, destination: nil, source: u)
                    return
                }
                switch dragging {
                case .destination(let i): engine.moveSpot(i, destination: u, source: nil)
                case .source(let i):      engine.moveSpot(i, destination: nil, source: u)
                case nil:                 break
                }
            }
            .onEnded { _ in
                // One history entry for the whole drag, the same split `setCrop`
                // and the brush use: the moves rendered without recording, and
                // this is the record.
                //
                // ⚠ Except while placing, where `addSpot` has already recorded
                // one. Placing a spot and dragging its source out is a single
                // act from the photographer's side and gets a single undo.
                if dragging != nil && placing == nil { engine.commitSpotEdit() }
                dragging = nil
                placing = nil
                // Unconditional: it returns immediately when nothing armed.
                engine.endInteraction()
            }
    }
}

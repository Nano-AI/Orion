import SwiftUI

/// Interactive crop, drawn over the canvas.
///
/// The exterior is darkened rather than hidden, so you can see what you are
/// giving up — that context is the whole reason a crop overlay beats numeric
/// fields.
///
/// The rectangle moves and resizes by drag. Corner handles resize freely;
/// dragging the interior pans the crop across the frame, which is what you
/// want after picking an aspect ratio and finding it centred on the wrong part
/// of the picture.
struct CropOverlay: View {
    @Bindable var engine: Engine

    /// The rendered canvas's rectangle inside the view.
    ///
    /// While the crop tool is open this is the preview canvas, which covers
    /// more than the frame so a turned picture has somewhere to go. The crop
    /// rectangle is placed on it through the same numbers the engine renders
    /// from, so the white box lands on the pixels rather than near them.
    let frame: CGRect

    /// The canvas view's own size, which is what the renderer fills.
    ///
    /// Everything that is not the crop is dimmed out to these edges. Dimming
    /// only as far as `frame` left the rotation overhang bright, so a
    /// straightened picture had corners poking out of the mask.
    let bounds: CGSize

    @State private var dragging: Handle?
    @State private var startRect: CGRect?

    private enum Handle: Equatable, Hashable {
        case move
        case topLeft, topRight, bottomLeft, bottomRight
    }

    /// Crop rectangle in view coordinates. The engine stores the crop
    /// normalised to the frame, so this is the one conversion.
    private var rect: CGRect {
        let canvas = engine.previewCanvas
        let crop = CGRect(x: CGFloat(engine.cropX), y: CGFloat(engine.cropY),
                          width: CGFloat(engine.cropW), height: CGFloat(engine.cropH))
        return CanvasLayout.inView(
            CanvasLayout.onCanvas(crop, canvasOrigin: canvas.origin,
                                  canvasSize: canvas.size),
            in: frame)
    }

    private let armLength: CGFloat = 26
    /// Fixed box each corner mark is drawn and hit-tested in.
    private var handleBox: CGFloat { armLength + 18 }

    var body: some View {
        ZStack(alignment: .topLeading) {
            dim
            thirds
            outline

            // Interior drag target first, so the corner handles sit above it in
            // the stack and win wherever the two overlap. A later child is
            // drawn — and hit-tested — on top.
            Color.clear
                .contentShape(Rectangle())
                .frame(width: max(rect.width - handleBox, 1),
                       height: max(rect.height - handleBox, 1))
                .offset(x: rect.minX + handleBox / 2, y: rect.minY + handleBox / 2)
                .gesture(drag(.move))

            corners
        }
    }

    // MARK: Pieces

    private var dim: some View {
        Path { p in
            p.addRect(CGRect(origin: .zero, size: bounds))
            p.addRect(rect)
        }
        .fill(Color.black.opacity(0.55), style: FillStyle(eoFill: true))
        .allowsHitTesting(false)
    }

    /// Always on while the tool is open. Thirds are for judging where a horizon
    /// or subject sits, which is a decision made before the drag starts.
    private var thirds: some View {
        Path { p in
            for i in 1..<3 {
                let x = rect.minX + rect.width * CGFloat(i) / 3
                p.move(to: CGPoint(x: x, y: rect.minY))
                p.addLine(to: CGPoint(x: x, y: rect.maxY))

                let y = rect.minY + rect.height * CGFloat(i) / 3
                p.move(to: CGPoint(x: rect.minX, y: y))
                p.addLine(to: CGPoint(x: rect.maxX, y: y))
            }
        }
        .stroke(Color.white.opacity(dragging != nil ? 0.5 : 0.3), lineWidth: 0.5)
        .allowsHitTesting(false)
    }

    private var outline: some View {
        Rectangle()
            .strokeBorder(Color.white.opacity(0.9), lineWidth: 1)
            .frame(width: rect.width, height: rect.height)
            .offset(x: rect.minX, y: rect.minY)
            .allowsHitTesting(false)
    }

    private var corners: some View {
        ForEach([Handle.topLeft, .topRight, .bottomLeft, .bottomRight], id: \.self) { h in
            ZStack {
                // Drawn inside a fixed box, never an unsized one. A Path view
                // takes the size it is offered, so an unsized path here became
                // as large as the whole overlay and `.position` then centred
                // *that* — which threw the corner marks into the middle of the
                // window while the crop rectangle stayed where it was.
                cornerPath(h)
                    .stroke(Color.white,
                            style: StrokeStyle(lineWidth: 3, lineCap: .round))
                    .shadow(color: .black.opacity(0.5), radius: 1)

                Color.clear.contentShape(Rectangle())
            }
            .frame(width: handleBox, height: handleBox)
            .position(position(of: h))
            .gesture(drag(h))
        }
    }

    /// An L of two arms, the way every crop tool draws a corner: it reads as a
    /// corner rather than a dot, and shows which two edges the handle moves.
    /// Coordinates are local to the handle's box, with the corner at its centre.
    private func cornerPath(_ h: Handle) -> Path {
        let c = CGPoint(x: handleBox / 2, y: handleBox / 2)
        let hx: CGFloat = (h == .topLeft || h == .bottomLeft) ? 1 : -1
        let vy: CGFloat = (h == .topLeft || h == .topRight) ? 1 : -1

        var p = Path()
        p.move(to: CGPoint(x: c.x + armLength * hx, y: c.y))
        p.addLine(to: c)
        p.addLine(to: CGPoint(x: c.x, y: c.y + armLength * vy))
        return p
    }

    private func position(of h: Handle) -> CGPoint {
        switch h {
        case .topLeft:     CGPoint(x: rect.minX, y: rect.minY)
        case .topRight:    CGPoint(x: rect.maxX, y: rect.minY)
        case .bottomLeft:  CGPoint(x: rect.minX, y: rect.maxY)
        case .bottomRight: CGPoint(x: rect.maxX, y: rect.maxY)
        case .move:        CGPoint(x: rect.midX, y: rect.midY)
        }
    }

    // MARK: Dragging

    private func drag(_ handle: Handle) -> some Gesture {
        DragGesture(minimumDistance: 1)
            .onChanged { value in
                if dragging == nil {
                    dragging = handle
                    startRect = CGRect(x: CGFloat(engine.cropX), y: CGFloat(engine.cropY),
                                       width: CGFloat(engine.cropW),
                                       height: CGFloat(engine.cropH))
                }
                guard let start = startRect, frame.width > 0, frame.height > 0 else { return }

                // View points to crop coordinates. The canvas is wider than the
                // frame, so a point of drag is less than a point of crop.
                let canvas = engine.previewCanvas
                let dx = value.translation.width / frame.width * canvas.size
                let dy = value.translation.height / frame.height * canvas.size

                var r = start
                switch handle {
                case .move:
                    r.origin.x += dx
                    r.origin.y += dy
                case .topLeft:
                    r.origin.x += dx; r.size.width -= dx
                    r.origin.y += dy; r.size.height -= dy
                case .topRight:
                    r.size.width += dx
                    r.origin.y += dy; r.size.height -= dy
                case .bottomLeft:
                    r.origin.x += dx; r.size.width -= dx
                    r.size.height += dy
                case .bottomRight:
                    r.size.width += dx
                    r.size.height += dy
                }

                // The engine constrains it to the turned frame; asking for
                // more than fits simply gets less.
                engine.setCrop(x: Float(r.origin.x), y: Float(r.origin.y),
                               w: Float(r.size.width), h: Float(r.size.height))
            }
            .onEnded { _ in
                dragging = nil
                startRect = nil
                engine.commitCropEdit()
            }
    }
}

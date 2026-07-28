import SwiftUI

/// Interactive crop, drawn over the canvas.
///
/// The exterior is darkened rather than hidden, so you can see what you are
/// giving up — that context is the whole reason a crop overlay beats numeric
/// fields. Thirds guides appear only while dragging: they are for placing a
/// subject, and once placed they are clutter.
///
/// The rectangle moves and resizes by drag. Corner handles resize freely;
/// dragging the interior pans the crop across the frame, which is what you
/// want after picking an aspect ratio and finding it centred on the wrong part
/// of the picture.
struct CropOverlay: View {
    @Bindable var engine: Engine
    /// The photo's rectangle inside the canvas view, in view coordinates.
    let frame: CGRect

    @State private var dragging: Handle?
    @State private var startRect: CGRect?

    private enum Handle: Equatable {
        case move
        case topLeft, topRight, bottomLeft, bottomRight
    }

    /// While straightening, the previewed frame is grown to hold the rotated
    /// picture, so the crop's normalised coordinates map into a sub-rectangle
    /// of what is on screen. Without this the rectangle drifts outside the
    /// image as the angle changes.
    private var growth: CGFloat {
        let a = abs(CGFloat(engine.straightenDeg) * .pi / 180)
        return cos(a) + sin(a)
    }

    /// The original frame's rectangle inside the (possibly grown) preview.
    private var imageRect: CGRect {
        let g = growth
        guard g > 1.0001 else { return frame }
        let w = frame.width / g
        let h = frame.height / g
        return CGRect(x: frame.midX - w / 2, y: frame.midY - h / 2, width: w, height: h)
    }

    /// Crop rectangle in view coordinates.
    private var rect: CGRect {
        let base = imageRect
        return CGRect(x: base.minX + CGFloat(engine.cropX) * base.width,
                      y: base.minY + CGFloat(engine.cropY) * base.height,
                      width: CGFloat(engine.cropW) * base.width,
                      height: CGFloat(engine.cropH) * base.height)
    }

    private let handleSize: CGFloat = 22
    private let armLength: CGFloat = 26

    var body: some View {
        ZStack(alignment: .topLeading) {
            // Everything outside the crop, dimmed.
            Path { p in
                p.addRect(frame)
                p.addRect(rect)
            }
            .fill(Color.black.opacity(0.55), style: FillStyle(eoFill: true))
            .allowsHitTesting(false)

            if dragging != nil {
                thirds
            }

            Rectangle()
                .strokeBorder(Color.white.opacity(0.9), lineWidth: 1)
                .frame(width: rect.width, height: rect.height)
                .offset(x: rect.minX, y: rect.minY)
                .allowsHitTesting(false)

            corners

            // Interior drag target, below the corner handles in the stack so
            // the corners win where they overlap.
            Color.clear
                .contentShape(Rectangle())
                .frame(width: max(rect.width - handleSize, 1),
                       height: max(rect.height - handleSize, 1))
                .offset(x: rect.minX + handleSize / 2, y: rect.minY + handleSize / 2)
                .gesture(drag(.move))
        }
    }

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
        .stroke(Color.white.opacity(0.35), lineWidth: 0.5)
        .allowsHitTesting(false)
    }

    private var corners: some View {
        ForEach([Handle.topLeft, .topRight, .bottomLeft, .bottomRight], id: \.self) { h in
            let p = position(of: h)
            ZStack {
                // An L of two arms, the way every crop tool draws a corner —
                // it reads as a corner rather than a dot, and shows which two
                // edges the handle moves.
                Path { path in
                    let hx: CGFloat = (h == .topLeft || h == .bottomLeft) ? 1 : -1
                    let vy: CGFloat = (h == .topLeft || h == .topRight) ? 1 : -1
                    path.move(to: CGPoint(x: armLength * hx, y: 0))
                    path.addLine(to: .zero)
                    path.addLine(to: CGPoint(x: 0, y: armLength * vy))
                }
                .stroke(Color.white, lineWidth: 3)

                Color.clear
                    .contentShape(Rectangle())
                    .frame(width: handleSize + 12, height: handleSize + 12)
                    .gesture(drag(h))
            }
            .position(p)
        }
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

    private func drag(_ handle: Handle) -> some Gesture {
        DragGesture(minimumDistance: 1)
            .onChanged { value in
                if dragging == nil {
                    dragging = handle
                    startRect = CGRect(x: CGFloat(engine.cropX), y: CGFloat(engine.cropY),
                                       width: CGFloat(engine.cropW),
                                       height: CGFloat(engine.cropH))
                }
                let base = imageRect
                guard let start = startRect, base.width > 0, base.height > 0 else { return }

                let dx = value.translation.width / base.width
                let dy = value.translation.height / base.height

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

                // A crop cannot invert or leave the frame.
                r.size.width = min(max(r.size.width, 0.05), 1)
                r.size.height = min(max(r.size.height, 0.05), 1)
                r.origin.x = min(max(r.origin.x, 0), 1 - r.size.width)
                r.origin.y = min(max(r.origin.y, 0), 1 - r.size.height)

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

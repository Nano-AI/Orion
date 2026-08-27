import CoreGraphics

extension CanvasLayout {

    /// The frame ↔ display map, as the two projective 3×3s the engine hands
    /// out — `orion_engine_display_map`, which is `mask::displayMatrix` and
    /// its inverse under the geometry currently applied.
    ///
    /// A mask is stored in **frame** coordinates (the whole sensor frame,
    /// unturned and uncropped) and the canvas shows the **displayed** picture
    /// (cropped, turned, straightened, corrected). Every overlay point and
    /// every gesture crosses between the two through this struct, and only
    /// through it: it is a pure value, so `orion-viewport-tests` can drive
    /// the whole gesture layer with hand-built matrices and no engine.
    ///
    /// ⚠ The identity default is what a photograph with no geometry has, and
    /// it is also what keeps every pure test that predates frame storage
    /// meaning what it meant: with `fd` at identity, frame and display
    /// coordinates are the same numbers.
    struct FrameDisplayMap: Equatable {
        /// Row-major, frame → display.
        var toDisplay: [CGFloat] = [1, 0, 0, 0, 1, 0, 0, 0, 1]
        /// Row-major, display → frame.
        var toFrame: [CGFloat] = [1, 0, 0, 0, 1, 0, 0, 0, 1]

        static let identity = FrameDisplayMap()

        private static func apply(_ m: [CGFloat], _ p: CGPoint) -> CGPoint {
            let w = m[6] * p.x + m[7] * p.y + m[8]
            let d = abs(w) > 1e-9 ? w : 1e-9
            return CGPoint(x: (m[0] * p.x + m[1] * p.y + m[2]) / d,
                           y: (m[3] * p.x + m[4] * p.y + m[5]) / d)
        }

        /// A frame point, on the displayed picture.
        func display(_ f: CGPoint) -> CGPoint { Self.apply(toDisplay, f) }

        /// A displayed point, in the frame.
        func frame(_ d: CGPoint) -> CGPoint { Self.apply(toFrame, d) }
    }
}

extension CanvasLayout.PictureMap {

    /// A frame point, in the view: through the geometry map, then the
    /// viewport's own affine one.
    func framePoint(_ f: CGPoint) -> CGPoint { point(fd.display(f)) }

    /// A view point, in frame coordinates — the space a mask is stored in.
    func frameUnit(_ p: CGPoint) -> CGPoint { fd.frame(unit(p)) }

    /// The view-points-per-frame-unit scale at `f`, per axis, by central
    /// differences — the composed map is projective, so it has no single
    /// scale, only one at a point. What the brush cursor and the rotate
    /// handle's stem need.
    func frameScale(at f: CGPoint) -> CGSize {
        let h: CGFloat = 5e-4
        let x0 = framePoint(CGPoint(x: f.x - h, y: f.y))
        let x1 = framePoint(CGPoint(x: f.x + h, y: f.y))
        let y0 = framePoint(CGPoint(x: f.x, y: f.y - h))
        let y1 = framePoint(CGPoint(x: f.x, y: f.y + h))
        return CGSize(width: hypot(x1.x - x0.x, x1.y - x0.y) / (2 * h),
                      height: hypot(y1.x - y0.x, y1.y - y0.y) / (2 * h))
    }
}

import CoreGraphics
import Foundation

/// Where the photo sits inside the canvas view, and how a straightened frame
/// bounds the crop.
///
/// The renderer, the crop overlay and the engine all need this geometry. While
/// each worked it out for itself they disagreed, and the handles landed on one
/// rectangle while the pixels were drawn on another. One place, one answer —
/// the engine is given these numbers rather than deriving its own.
enum CanvasLayout {

    /// The frame's rectangle inside a view of `size`, letterboxed to fit.
    ///
    /// While the crop tool is open the engine renders the preview canvas into a
    /// texture of the frame's own aspect, so this is that canvas — no growth
    /// factor, no inset. The extra context is inside the picture rather than
    /// spilling past it.
    static func frameRect(imageAspect: CGFloat, in size: CGSize) -> CGRect {
        guard imageAspect > 0, size.width > 0, size.height > 0 else { return .zero }

        let viewAspect = size.width / size.height
        var w = size.width, h = size.height
        if imageAspect > viewAspect { h = w / imageAspect } else { w = h * imageAspect }

        return CGRect(x: (size.width - w) / 2, y: (size.height - h) / 2,
                      width: w, height: h)
    }

    /// Where the picture is actually drawn, once zoom has had its say.
    ///
    /// `frameRect` is where it sits at fit. Zoom in and the quad grows until it
    /// covers the whole view, and anything positioned against the fit rectangle
    /// is then pointing at pixels that are no longer there — which is what put
    /// the compare divider, its labels and its grab band somewhere the split
    /// was not.
    ///
    /// `quadScale` is the renderer's own, in NDC half-extents: the quad spans
    /// −q…+q about the center, so it covers `q` of the view on each axis.
    static func drawnRect(quadScale q: CGSize, in size: CGSize) -> CGRect {
        guard size.width > 0, size.height > 0 else { return .zero }

        let w = size.width * max(min(q.width, 1), 0)
        let h = size.height * max(min(q.height, 1), 0)
        return CGRect(x: (size.width - w) / 2, y: (size.height - h) / 2,
                      width: w, height: h)
    }

    // MARK: The rotated frame

    /// How far a frame of aspect `a` reaches on each axis once turned by
    /// `angleDeg`, as a multiple of a rectangle of size (w, h).
    ///
    /// Rotating a Wc x Hc rectangle about its own center gives an axis-aligned
    /// bounding box of Wc|cos| + Hc|sin| by Wc|sin| + Hc|cos|. Everything below
    /// is that identity, expressed in coordinates normalized to the frame — so
    /// the aspect has to appear, because normalized x and y are not the same
    /// number of pixels.
    private static func extent(w: CGFloat, h: CGFloat,
                               aspect a: CGFloat, angleDeg: CGFloat) -> CGSize {
        let t = angleDeg * .pi / 180
        let c = abs(cos(t)), s = abs(sin(t))
        return CGSize(width: w * c + h * s / a,
                      height: w * s * a + h * c)
    }

    /// The picture turns about the frame's center, never the crop's.
    ///
    /// Pivoting on the crop meant that dragging the rectangle re-rotated the
    /// picture beneath it — the image visibly swam out from under the box.
    /// About the frame's center the turned frame is a fixed quadrilateral and
    /// the crop is simply a window onto it, which is what Photoshop and
    /// Lightroom both do.
    static let pivot = CGPoint(x: 0.5, y: 0.5)

    /// Shrinks and slides a crop until its turned form lies wholly inside the
    /// frame.
    ///
    /// This is the rule that stops a straightened export having transparent
    /// wedges in its corners: the crop is what gets sampled, so anything it
    /// reaches past the turned frame has nothing behind it. It is also how
    /// Lightroom behaves — crank the angle and the rectangle gives ground
    /// rather than the picture growing holes.
    static func constrainedCrop(_ crop: CGRect, frameAspect a: CGFloat,
                                angleDeg: CGFloat) -> CGRect {
        guard a > 0 else { return crop }

        var r = clampedCrop(crop)
        let center = CGPoint(x: r.midX, y: r.midY)

        // Size first. A rectangle fits inside an axis-aligned box only if its
        // bounding box does, so this condition is exact rather than a bound.
        let reach = extent(w: r.width, h: r.height, aspect: a, angleDeg: angleDeg)
        let scale = min(1, min(1 / max(reach.width, 1e-6), 1 / max(reach.height, 1e-6)))
        if scale < 1 {
            r.size.width *= scale
            r.size.height *= scale
            r.origin.x = center.x - r.width / 2
            r.origin.y = center.y - r.height / 2
        }

        // Then position. The crop is axis-aligned where it is drawn, but the
        // frame it has to stay inside is turned, so the room it has runs along
        // the *frame's* axes — clamping against the upright edges would let a
        // corner poke out while every side still looked in bounds.
        //
        // Worked in units where the height is 1 and the width is `a`, because
        // a rotation is only a rotation when both axes have the same scale.
        let t = angleDeg * .pi / 180
        let c = cos(t), s = sin(t)
        let hw = r.width * a / 2, hh = r.height / 2

        var d = CGPoint(x: (r.midX - pivot.x) * a, y: r.midY - pivot.y)
        var u = d.x * c - d.y * s
        var v = d.x * s + d.y * c

        let limitU = max(0, a / 2 - (hw * abs(c) + hh * abs(s)))
        let limitV = max(0, 0.5 - (hw * abs(s) + hh * abs(c)))
        u = min(max(u, -limitU), limitU)
        v = min(max(v, -limitV), limitV)

        d = CGPoint(x: u * c + v * s, y: -u * s + v * c)
        r.origin.x = pivot.x + d.x / a - r.width / 2
        r.origin.y = pivot.y + d.y - r.height / 2

        return r
    }

    // MARK: The crop tool's preview canvas

    /// The region the crop preview renders, in the same normalized coordinates
    /// as the crop rectangle.
    ///
    /// It has to cover the frame's rotated bounding box. A fixed multiple
    /// cannot: a 3:2 frame at 45 degrees reaches 1.77x its short side, and the
    /// old constant 1.42 clipped the corners off anything past about 17.
    ///
    /// Square on both axes so the engine can sample it into a frame-shaped
    /// texture without stretching, which is what keeps the preview's cost flat
    /// as the angle grows.
    static func previewCanvas(frameAspect a: CGFloat,
                              angleDeg: CGFloat) -> (origin: CGPoint, size: CGFloat) {
        guard a > 0 else { return (.zero, 1) }

        let reach = extent(w: 1, h: 1, aspect: a, angleDeg: angleDeg)
        // A little air so the frame's own edge is visible rather than flush
        // against the canvas edge, where it reads as a crop rather than a rim.
        let m = max(reach.width, reach.height) * 1.06

        // Concentric with the frame, because the frame turns about its own
        // center. Nothing here depends on the crop — that is deliberate. When
        // the canvas followed the crop, dragging the rectangle moved the
        // picture underneath it, which is the "it comes unlocked from the
        // image" everyone hits within about ten seconds of trying it.
        return (CGPoint(x: pivot.x - m / 2, y: pivot.y - m / 2), m)
    }

    /// A rectangle given in frame coordinates, placed on the preview canvas.
    /// Both are the same coordinate system, so this is only a change of origin
    /// and scale — but it is the change the overlay kept getting wrong.
    static func onCanvas(_ r: CGRect, canvasOrigin: CGPoint,
                         canvasSize: CGFloat) -> CGRect {
        guard canvasSize > 0 else { return r }
        return CGRect(x: (r.origin.x - canvasOrigin.x) / canvasSize,
                      y: (r.origin.y - canvasOrigin.y) / canvasSize,
                      width: r.width / canvasSize,
                      height: r.height / canvasSize)
    }

    /// A normalized rectangle placed inside a view rectangle.
    static func inView(_ unit: CGRect, in frame: CGRect) -> CGRect {
        CGRect(x: frame.minX + unit.origin.x * frame.width,
               y: frame.minY + unit.origin.y * frame.height,
               width: unit.width * frame.width,
               height: unit.height * frame.height)
    }

    /// A crop cannot invert or leave the frame. Normalized in, normalized out.
    static func clampedCrop(_ r: CGRect) -> CGRect {
        var r = r
        r.size.width = min(max(r.size.width, 0.05), 1)
        r.size.height = min(max(r.size.height, 0.05), 1)
        r.origin.x = min(max(r.origin.x, 0), 1 - r.size.width)
        r.origin.y = min(max(r.origin.y, 0), 1 - r.size.height)
        return r
    }

    // MARK: The picture on screen

    /// The map between normalized picture coordinates and view points.
    ///
    /// The renderer draws the picture across `rect` and shows `visible` of it
    /// about the viewport's centre; this is the inverse of exactly that, built
    /// from the same two numbers that go into the vertex shader. An overlay
    /// made from anything else is a second opinion about where the pixels are,
    /// and this repository has already shipped the bug where the second opinion
    /// was wrong.
    ///
    /// ⚠️ **The map is anisotropic.** A normalized picture is a unit square; the
    /// rectangle it is drawn in is not. So a normalized circle is a screen
    /// ellipse, and two normalized vectors at right angles are not at right
    /// angles on screen. Anything that wants an angle has to say which space it
    /// means — see `maskDrag`.
    struct PictureMap {
        /// Where the picture covers the view.
        var rect: CGRect = .zero
        /// Fraction of the picture on screen, per axis. 1 means all of it.
        var visible = CGSize(width: 1, height: 1)
        /// The normalized coordinate sitting at `rect`'s top-left corner.
        var origin: CGPoint = .zero

        /// View points per unit of normalized coordinate.
        var scale: CGSize {
            CGSize(width: rect.width / max(visible.width, 1e-9),
                   height: rect.height / max(visible.height, 1e-9))
        }

        /// A normalized picture point, in the view.
        func point(_ u: CGPoint) -> CGPoint {
            let s = scale
            return CGPoint(x: rect.minX + (u.x - origin.x) * s.width,
                           y: rect.minY + (u.y - origin.y) * s.height)
        }

        /// A view point, back in normalized picture coordinates.
        func unit(_ p: CGPoint) -> CGPoint {
            let s = scale
            guard s.width > 1e-12, s.height > 1e-12 else { return origin }
            return CGPoint(x: origin.x + (p.x - rect.minX) / s.width,
                           y: origin.y + (p.y - rect.minY) / s.height)
        }

        /// A displacement, which has no origin. A drag translation is one of
        /// these, and passing it through `unit` instead would add the origin in
        /// twice.
        func unitVector(_ d: CGSize) -> CGSize {
            let s = scale
            guard s.width > 1e-12, s.height > 1e-12 else { return .zero }
            return CGSize(width: d.width / s.width, height: d.height / s.height)
        }
    }

    /// The map the renderer is using right now.
    ///
    /// `quadScale` and `visible` come from `Viewport`, unmodified — they are
    /// the values the shader is handed, so an overlay built on them cannot
    /// drift from the picture. At fit this reduces to `frameRect`, which is
    /// asserted rather than assumed.
    static func pictureMap(quadScale q: CGSize, visible: CGSize,
                           center: CGPoint, in size: CGSize) -> PictureMap {
        var m = PictureMap()
        m.rect = drawnRect(quadScale: q, in: size)
        m.visible = CGSize(width: max(visible.width, 1e-9),
                           height: max(visible.height, 1e-9))
        m.origin = CGPoint(x: center.x - m.visible.width / 2,
                           y: center.y - m.visible.height / 2)
        return m
    }

    // MARK: Gradient masks

    /// A gradient mask in the coordinates it is *stored* in: normalized against
    /// the displayed picture — cropped, turned and straightened.
    ///
    /// Nothing here repeats `pipe/MaskGeometry.h`. The engine owns the step from
    /// these coordinates to the frame `develop:linear` sees, and it is the only
    /// thing that should: two implementations of that transform agreeing today
    /// is how they drift apart tomorrow.
    struct MaskPlacement: Equatable {
        var kind: Int = 0                    // 0 none, 1 linear, 2 radial
        var centre = CGPoint(x: 0.5, y: 0.5)
        var angle: CGFloat = 0               // radians
        var length: CGFloat = 0.5            // linear: zero-to-full distance
        var radius = CGSize(width: 0.3, height: 0.3)   // radial semi-axes
        var feather: CGFloat = 0.5
        var roundness: CGFloat = 2
        var invert = false

        /// The mask's own axes, read off `mask_gradient.slang` rather than
        /// guessed: it forms `u = ( c·e.x + s·e.y)/rx` and
        /// `v = (−s·e.x + c·e.y)/ry`, so +X is (cos, sin) and +Y is (−sin, cos).
        var axisX: CGSize { CGSize(width: cos(angle), height: sin(angle)) }
        var axisY: CGSize { CGSize(width: -sin(angle), height: cos(angle)) }

        /// Where the effect is zero and where it is full. The engine derives
        /// the shader's two endpoints from centre and angle the same way, so a
        /// linear gradient needs no separate angle once these exist.
        var zeroEnd: CGPoint { along(axisX, -length / 2) }
        var fullEnd: CGPoint { along(axisX, length / 2) }

        func along(_ a: CGSize, _ d: CGFloat) -> CGPoint {
            CGPoint(x: centre.x + a.width * d, y: centre.y + a.height * d)
        }

        /// A handle's spot in normalized picture coordinates.
        ///
        /// `rotate` is absent on purpose: it sits a fixed number of *view*
        /// points beyond the +X handle so it stays a constant size on screen at
        /// any zoom, which no normalized position can express.
        func handleUnit(_ h: MaskHandle) -> CGPoint? {
            switch h {
            case .centre:  return centre
            case .zeroEnd: return zeroEnd
            case .fullEnd: return fullEnd
            case .plusX:   return along(axisX, radius.width)
            case .minusX:  return along(axisX, -radius.width)
            case .plusY:   return along(axisY, radius.height)
            case .minusY:  return along(axisY, -radius.height)
            case .rotate, .body: return nil
            }
        }
    }

    /// What a press can grab.
    enum MaskHandle: Hashable {
        case centre, zeroEnd, fullEnd
        case plusX, minusX, plusY, minusY
        case rotate
        case body
    }

    /// Matches the crop overlay's 44pt box, so the two tools feel the same
    /// under the hand.
    static let maskHandleBox: CGFloat = 44

    /// How far past the +X handle the rotate lollipop sits, in view points.
    static let maskRotateStem: CGFloat = 34

    /// The ranges the panel's sliders offer.
    ///
    /// A drag must not produce a mask the sliders cannot show, or the canvas
    /// and the panel disagree about the state and the next touch of a slider
    /// silently snaps the mask somewhere the photographer did not put it.
    static let maskCentreRange: ClosedRange<CGFloat> = 0...1
    static let maskLengthRange: ClosedRange<CGFloat> = 0.05...1.5
    static let maskRadiusRange: ClosedRange<CGFloat> = 0.02...1

    /// Which handles a mask of this kind shows.
    static func maskHandles(_ m: MaskPlacement) -> [MaskHandle] {
        switch m.kind {
        case 1: return [.zeroEnd, .fullEnd, .centre]
        case 2: return [.plusX, .minusX, .plusY, .minusY, .rotate, .centre]
        default: return []
        }
    }

    /// A handle's position in the view.
    static func maskHandlePoint(_ h: MaskHandle, _ m: MaskPlacement,
                                _ map: PictureMap) -> CGPoint {
        if h == .rotate {
            // The stem's direction is the mask's +X axis *as drawn*, which is
            // not the same screen direction as (cos, sin) unless the picture is
            // square. Taking it through the map rather than reusing the angle
            // is what keeps the lollipop on the end of the axis it belongs to.
            let s = map.scale
            var dx = m.axisX.width * s.width
            var dy = m.axisX.height * s.height
            let n = max(hypot(dx, dy), 1e-9)
            dx /= n; dy /= n
            let base = map.point(m.along(m.axisX, m.radius.width))
            return CGPoint(x: base.x + dx * maskRotateStem,
                           y: base.y + dy * maskRotateStem)
        }
        guard let u = m.handleUnit(h) else { return map.point(m.centre) }
        return map.point(u)
    }

    /// Which handle a press at `p` takes.
    ///
    /// Nearest wins among the handles rather than a fixed order, because on a
    /// small mask their boxes overlap and a fixed order leaves one of them
    /// permanently unreachable. The body is tested last: it is the largest
    /// target and would otherwise swallow every handle standing on it.
    static func maskHit(_ p: CGPoint, _ m: MaskPlacement,
                        _ map: PictureMap) -> MaskHandle? {
        guard m.kind != 0 else { return nil }

        let reach = maskHandleBox / 2
        var best: (handle: MaskHandle, distance: CGFloat)?
        for h in maskHandles(m) {
            let q = maskHandlePoint(h, m, map)
            let d = hypot(p.x - q.x, p.y - q.y)
            guard d <= reach else { continue }
            if best == nil || d < best!.distance { best = (h, d) }
        }
        if let b = best { return b.handle }

        return maskBodyContains(map.unit(p), m) ? .body : nil
    }

    /// Inside the part of the picture the mask is acting on.
    static func maskBodyContains(_ q: CGPoint, _ m: MaskPlacement) -> Bool {
        switch m.kind {
        case 1:
            // The band between the zero and full lines — the ramp itself.
            let t = maskLinearT(q, m)
            return t >= 0 && t <= 1
        case 2:
            return maskRadialR(q, m) <= 1
        default:
            return false
        }
    }

    // MARK: What the shader says, in Swift

    /// Perlin's smootherstep, as `mask_gradient.slang` uses it.
    static func maskSmootherstep(_ t: CGFloat) -> CGFloat {
        let x = min(max(t, 0), 1)
        return x * x * x * (x * (x * 6 - 15) + 10)
    }

    /// The linear gradient's ramp parameter, transcribed from the shader.
    static func maskLinearT(_ q: CGPoint, _ m: MaskPlacement) -> CGFloat {
        let z = m.zeroEnd, f = m.fullEnd
        let dx = f.x - z.x, dy = f.y - z.y
        let denom = dx * dx + dy * dy
        guard denom > 1e-9 else { return 1 }
        return ((q.x - z.x) * dx + (q.y - z.y) * dy) / denom
    }

    /// The radial gradient's superellipse radius, transcribed from the shader.
    /// 1 is the boundary.
    static func maskRadialR(_ q: CGPoint, _ m: MaskPlacement) -> CGFloat {
        let c = cos(m.angle), s = sin(m.angle)
        let ex = q.x - m.centre.x, ey = q.y - m.centre.y
        let u = ( c * ex + s * ey) / max(m.radius.width, 1e-6)
        let v = (-s * ex + c * ey) / max(m.radius.height, 1e-6)
        let n = max(m.roundness, 0.1)
        return pow(pow(abs(u), n) + pow(abs(v), n), 1 / n)
    }

    /// The coverage the mask kernel would write at `q`.
    ///
    /// A second implementation of `mask_gradient.slang`, and deliberately so:
    /// it exists to be the oracle the *overlay* is checked against, so a test
    /// can ask whether the outline is drawn where the falloff actually is
    /// rather than merely whether an outline was drawn. Transcribed from the
    /// shader, which is the only place the maths is allowed to change.
    static func maskAlpha(_ q: CGPoint, _ m: MaskPlacement) -> CGFloat {
        var alpha: CGFloat = 1
        switch m.kind {
        case 1:
            alpha = maskSmootherstep(maskLinearT(q, m))
        case 2:
            let r = maskRadialR(q, m)
            let f = min(max(m.feather, 0), 0.999)
            alpha = 1 - maskSmootherstep((r - (1 - f)) / max(f, 1e-3))
        default:
            alpha = 1
        }
        return m.invert ? 1 - alpha : alpha
    }

    // MARK: What the overlay draws

    /// A radial mask's boundary at a given level in its own metric — 1 is the
    /// edge of the effect, `1 − feather` is where the falloff begins.
    ///
    /// Sampled in the mask's space and mapped out point by point, never drawn
    /// as a screen-space ellipse. The shader is isotropic in *normalized*
    /// coordinates and the picture is not square, so a screen circle is not the
    /// mask's boundary — it is a curve near it that parts company as the frame
    /// gets further from square.
    static func maskOutline(_ m: MaskPlacement, at level: CGFloat,
                            samples: Int = 128) -> [CGPoint] {
        guard m.kind == 2, samples >= 3, level > 0 else { return [] }

        let n = max(m.roundness, 0.1)
        let c = cos(m.angle), s = sin(m.angle)
        var out: [CGPoint] = []
        out.reserveCapacity(samples)

        for i in 0..<samples {
            let t = CGFloat(i) / CGFloat(samples) * 2 * .pi
            // The superellipse |u|^n + |v|^n = level^n, parametrized so that
            // n = 2 gives the circle back.
            let ct = cos(t), st = sin(t)
            let u = copysign(pow(abs(ct), 2 / n), ct) * level
            let v = copysign(pow(abs(st), 2 / n), st) * level
            let ex = u * m.radius.width, ey = v * m.radius.height
            out.append(CGPoint(x: m.centre.x + c * ex - s * ey,
                               y: m.centre.y + s * ex + c * ey))
        }
        return out
    }

    /// One of a linear gradient's iso-alpha lines: `t = 0` is where the effect
    /// is zero, `0.5` the centre, `1` where it is full.
    ///
    /// Perpendicular to the ramp *in normalized coordinates*, which is not
    /// perpendicular on screen. Drawing these square to the ramp as it appears
    /// would put the three lines at an angle to the bands the shader is
    /// actually laying down — visible on any frame that is not square, which is
    /// every frame.
    static func maskIsoLine(_ m: MaskPlacement, at t: CGFloat,
                            halfLength: CGFloat = 4) -> (CGPoint, CGPoint) {
        let mid = m.along(m.axisX, (t - 0.5) * m.length)
        let a = m.axisY
        return (CGPoint(x: mid.x - a.width * halfLength,
                        y: mid.y - a.height * halfLength),
                CGPoint(x: mid.x + a.width * halfLength,
                        y: mid.y + a.height * halfLength))
    }

    // MARK: Dragging

    /// What a drag does to the mask.
    ///
    /// The whole gesture lives here rather than in the view, so it can be
    /// checked without a window — and the check that matters is a round trip:
    /// drag a handle to a point and the handle must redraw *at* that point.
    ///
    /// `grab` is where the press landed and `p` is where the cursor is now.
    /// Both in view coordinates.
    ///
    /// ⚠️ **Every angle here is taken in normalized space**, because that is the
    /// space the shader measures in and therefore the only one the stored angle
    /// can mean. Taking `atan2` on screen instead and converting afterwards
    /// makes the handle slide out from under the cursor as it is dragged, since
    /// the handle is redrawn from the stored angle through the anisotropic map.
    static func maskDrag(_ h: MaskHandle, from grab: CGPoint, to p: CGPoint,
                         start: MaskPlacement, _ map: PictureMap) -> MaskPlacement {
        var m = start

        switch h {
        case .centre, .body:
            let d = map.unitVector(CGSize(width: p.x - grab.x, height: p.y - grab.y))
            m.centre = CGPoint(
                x: clamp(start.centre.x + d.width, maskCentreRange),
                y: clamp(start.centre.y + d.height, maskCentreRange))

        case .zeroEnd, .fullEnd:
            // An endpoint carries angle and length at once, which is why a
            // linear gradient needs no rotate handle: the point goes exactly
            // where the cursor is and the angle is whatever that implies.
            let q = map.unit(p)
            let dx = q.x - start.centre.x, dy = q.y - start.centre.y
            let r = hypot(dx, dy)
            guard r > 1e-9 else { break }
            m.length = clamp(2 * r, maskLengthRange)
            m.angle = (h == .fullEnd) ? atan2(dy, dx) : atan2(-dy, -dx)

        case .plusX, .minusX, .plusY, .minusY:
            // Size only, and the drag is projected onto the axis it belongs to
            // — pulling a side sideways must not quietly rotate the mask. The
            // rotate handle is the one thing that changes the angle.
            let q = map.unit(p)
            let dx = q.x - start.centre.x, dy = q.y - start.centre.y
            let onX = (h == .plusX || h == .minusX)
            let axis = onX ? start.axisX : start.axisY
            let along = abs(dx * axis.width + dy * axis.height)
            if onX {
                m.radius.width = clamp(along, maskRadiusRange)
            } else {
                m.radius.height = clamp(along, maskRadiusRange)
            }

        case .rotate:
            let q = map.unit(p)
            let dx = q.x - start.centre.x, dy = q.y - start.centre.y
            guard hypot(dx, dy) > 1e-9 else { break }
            m.angle = atan2(dy, dx)
        }

        return m
    }

    private static func clamp(_ v: CGFloat, _ r: ClosedRange<CGFloat>) -> CGFloat {
        min(max(v, r.lowerBound), r.upperBound)
    }

    // MARK: Brush strokes

    /// How far apart dabs are laid, as a fraction of the brush radius.
    ///
    /// A stroke is a row of stamps, so the spacing is what decides whether it
    /// reads as a line or as a string of beads. A quarter of the radius is the
    /// usual figure in paint engines and it is comfortably inside the point
    /// where the scalloping on the edge of a stroke becomes visible.
    static let brushSpacing: CGFloat = 0.25

    /// The dab centres a drag from `a` to `b` should add, given what the stroke
    /// already ends with.
    ///
    /// A pointer delivers a handful of positions a second and a fast hand moves
    /// a long way between two of them, so stamping once per event draws a
    /// dotted line. This walks the segment at a fixed spacing instead, which is
    /// what makes a quick stroke and a slow one lay down the same paint.
    ///
    /// `carry` is the distance left over from the previous segment, so spacing
    /// is continuous across the whole stroke rather than restarting at every
    /// event — restarting is what clusters dabs at the corners of a gesture.
    static func brushDabs(from a: CGPoint, to b: CGPoint, radius: CGFloat,
                          carry: inout CGFloat) -> [CGPoint] {
        let step = max(radius * brushSpacing, 1e-4)
        let dx = b.x - a.x, dy = b.y - a.y
        let length = hypot(dx, dy)

        guard length > 1e-9 else { return [] }

        var out: [CGPoint] = []
        // Distance along this segment at which the next dab falls.
        var t = step - carry
        while t <= length {
            let f = t / length
            out.append(CGPoint(x: a.x + dx * f, y: a.y + dy * f))
            t += step
        }
        carry = length - (t - step)
        return out
    }

    /// The brush's outline on screen: a circle in *normalized* coordinates, so
    /// on a picture that is not square it is drawn as an ellipse — because that
    /// is the shape the kernel actually stamps. `mask_brush.slang` measures its
    /// distance in normalized space, exactly as the gradients do.
    static func brushCursor(at unit: CGPoint, radius: CGFloat,
                            _ map: PictureMap, samples: Int = 64) -> [CGPoint] {
        guard samples >= 3, radius > 0 else { return [] }
        var out: [CGPoint] = []
        out.reserveCapacity(samples)
        for i in 0..<samples {
            let t = CGFloat(i) / CGFloat(samples) * 2 * .pi
            out.append(map.point(CGPoint(x: unit.x + cos(t) * radius,
                                         y: unit.y + sin(t) * radius)))
        }
        return out
    }
}

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
    /// −q…+q about the centre, so it covers `q` of the view on each axis.
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
    /// Rotating a Wc x Hc rectangle about its own centre gives an axis-aligned
    /// bounding box of Wc|cos| + Hc|sin| by Wc|sin| + Hc|cos|. Everything below
    /// is that identity, expressed in coordinates normalised to the frame — so
    /// the aspect has to appear, because normalised x and y are not the same
    /// number of pixels.
    private static func extent(w: CGFloat, h: CGFloat,
                               aspect a: CGFloat, angleDeg: CGFloat) -> CGSize {
        let t = angleDeg * .pi / 180
        let c = abs(cos(t)), s = abs(sin(t))
        return CGSize(width: w * c + h * s / a,
                      height: w * s * a + h * c)
    }

    /// The picture turns about the frame's centre, never the crop's.
    ///
    /// Pivoting on the crop meant that dragging the rectangle re-rotated the
    /// picture beneath it — the image visibly swam out from under the box.
    /// About the frame's centre the turned frame is a fixed quadrilateral and
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
        let centre = CGPoint(x: r.midX, y: r.midY)

        // Size first. A rectangle fits inside an axis-aligned box only if its
        // bounding box does, so this condition is exact rather than a bound.
        let reach = extent(w: r.width, h: r.height, aspect: a, angleDeg: angleDeg)
        let scale = min(1, min(1 / max(reach.width, 1e-6), 1 / max(reach.height, 1e-6)))
        if scale < 1 {
            r.size.width *= scale
            r.size.height *= scale
            r.origin.x = centre.x - r.width / 2
            r.origin.y = centre.y - r.height / 2
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

    /// The region the crop preview renders, in the same normalised coordinates
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
        // centre. Nothing here depends on the crop — that is deliberate. When
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

    /// A normalised rectangle placed inside a view rectangle.
    static func inView(_ unit: CGRect, in frame: CGRect) -> CGRect {
        CGRect(x: frame.minX + unit.origin.x * frame.width,
               y: frame.minY + unit.origin.y * frame.height,
               width: unit.width * frame.width,
               height: unit.height * frame.height)
    }

    /// A crop cannot invert or leave the frame. Normalised in, normalised out.
    static func clampedCrop(_ r: CGRect) -> CGRect {
        var r = r
        r.size.width = min(max(r.size.width, 0.05), 1)
        r.size.height = min(max(r.size.height, 0.05), 1)
        r.origin.x = min(max(r.origin.x, 0), 1 - r.size.width)
        r.origin.y = min(max(r.origin.y, 0), 1 - r.size.height)
        return r
    }
}

// The picture map — where the photograph actually sits in the view.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// The compare divider has to be placed against the rectangle the picture
    /// actually covers, which is not the one it covers at fit.
    ///
    /// The split happens across the drawn quad in the canvas shader. The panel
    /// was drawing the divider, the labels and the grab band against the *fit*
    /// rectangle, so the moment you zoomed in the line stopped marking the
    /// boundary and the grab band stopped being over it.
    static func testDrawnRectFollowsTheZoom() {
        let view = CGSize(width: 1200, height: 800)

        for image in [landscape, portrait, 1.0] as [CGFloat] {
            let v = Viewport()
            let viewAspect = view.width / view.height

            // At fit the two derivations must agree exactly, or the crop tool
            // and the compare divider would disagree about where the photo is.
            let fitQuad = v.quadScale(imageAspect: image, viewAspect: viewAspect)
            let drawn = CanvasLayout.drawnRect(quadScale: fitQuad, in: view)
            let fitted = CanvasLayout.frameRect(imageAspect: image, in: view)
            near(drawn.minX, fitted.minX, 1e-6, "at fit the drawn rect starts where the frame does (x, \(image))")
            near(drawn.minY, fitted.minY, 1e-6, "at fit the drawn rect starts where the frame does (y, \(image))")
            near(drawn.width, fitted.width, 1e-6, "at fit the drawn rect is the frame's width (\(image))")
            near(drawn.height, fitted.height, 1e-6, "at fit the drawn rect is the frame's height (\(image))")

            // Zoomed in far enough, the picture covers the whole view — so a
            // divider at split 0.5 belongs at the middle of the *view*, not at
            // the middle of the letterboxed rectangle.
            v.zoomBy(8.0, anchor: CGPoint(x: 0.5, y: 0.5),
                     visible: CGSize(width: 1, height: 1))
            let zoomedQuad = v.quadScale(imageAspect: image, viewAspect: viewAspect)
            let zoomed = CanvasLayout.drawnRect(quadScale: zoomedQuad, in: view)
            near(zoomed.width, view.width, 1e-6, "zoomed in, the picture spans the view's width (\(image))")
            near(zoomed.height, view.height, 1e-6, "zoomed in, the picture spans the view's height (\(image))")

            // And the two must actually differ where the image is letterboxed,
            // or the test would pass on the broken code it was written for.
            if abs(image - viewAspect) > 0.01 {
                report(zoomed.width > fitted.width + 1 || zoomed.height > fitted.height + 1,
                       "zoom changes where the picture is (\(image))")
            }

            // Never past the view: a rectangle wider than the canvas would put
            // the divider's grab band outside anything that can be clicked.
            for z in [1.0, 1.5, 2.0, 4.0, 16.0] as [CGFloat] {
                let w = Viewport()
                w.zoomBy(z, anchor: CGPoint(x: 0.5, y: 0.5),
                         visible: CGSize(width: 1, height: 1))
                let r = CanvasLayout.drawnRect(
                    quadScale: w.quadScale(imageAspect: image, viewAspect: viewAspect),
                    in: view)
                report(r.width <= view.width + 1e-6 && r.height <= view.height + 1e-6,
                       "the drawn rect stays inside the view (image \(image), zoom \(z))")
                report(r.width > 0 && r.height > 0,
                       "the drawn rect is not empty (image \(image), zoom \(z))")
                near(r.midX, view.width / 2, 1e-6, "the drawn rect stays centered (x, zoom \(z))")
                near(r.midY, view.height / 2, 1e-6, "the drawn rect stays centered (y, zoom \(z))")
            }
        }
    }

    /// A control is marked modified when, and only when, its readout differs
    /// from the one it would show at its base.
    ///
    /// The accent color is the only thing telling you which controls you have
    /// touched, and the only thing telling you the number is clickable. Get the
    /// comparison wrong in one direction and half the panel glows for edits
    /// nobody made; get it wrong in the other and a moved slider offers no way
    /// back.
    static func testModifiedTracksTheReadout() {
        // Dragged back to where it started, but landing on float noise rather
        // than exactly on the base — the case a plain != would get wrong.
        report(!AdjustmentMath.isModified(1e-8, from: 0, decimals: 2),
               "float noise is not an edit")
        report(!AdjustmentMath.isModified(0.15 + 0.15 + 0.15, from: 0.45, decimals: 2),
               "an accumulated drag back to the base is not an edit")

        // Anything the readout would print differently is an edit, including
        // the smallest step it can show.
        report(AdjustmentMath.isModified(0.01, from: 0, decimals: 2),
               "one step at two decimals is an edit")
        report(AdjustmentMath.isModified(-0.01, from: 0, decimals: 2),
               "one step down is an edit")
        report(!AdjustmentMath.isModified(0.0005, from: 0, decimals: 2),
               "half a printed step is not an edit")

        // Temperature reads whole kelvin, so its tolerance has to scale with
        // the readout rather than being a fixed epsilon.
        report(AdjustmentMath.isModified(3426, from: 3425, decimals: 0),
               "one kelvin is an edit")
        report(!AdjustmentMath.isModified(3425.02, from: 3425, decimals: 0),
               "a fiftieth of a kelvin is not an edit")

        // The bases that are not zero. Contrast opens at 1.15 and sharpen
        // radius at 1, and a control that called its own default an edit would
        // light up the panel on every photo before it was touched.
        report(!AdjustmentMath.isModified(1.15, from: 1.15, decimals: 2),
               "contrast at its base is not an edit")
        report(AdjustmentMath.isModified(1.16, from: 1.15, decimals: 2),
               "contrast one step off its base is an edit")

        // Every default in DevelopState against itself: nothing may read as
        // modified on a photo nobody has touched.
        let fresh = DevelopState()
        let bases: [(String, Float, Int)] = [
            ("exposure", fresh.exposureEv, 2), ("contrast", fresh.contrast, 2),
            ("highlights", fresh.highlights, 2), ("shadows", fresh.shadows, 2),
            ("whites", fresh.whites, 2), ("blacks", fresh.blacks, 2),
            ("vibrance", fresh.vibrance, 2), ("saturation", fresh.saturation, 2),
            ("recovery", fresh.highlightRecovery, 2),
            ("denoise luma", fresh.denoiseLuma, 2),
            ("denoise color", fresh.denoiseColor, 2),
            ("distortion", fresh.lensDistortion, 2),
            ("vignetting", fresh.lensVignette, 2),
            ("sharpen amount", fresh.sharpenAmount, 2),
            ("sharpen radius", fresh.sharpenRadius, 1),
            ("sharpen masking", fresh.sharpenMasking, 2),
            ("straighten", fresh.straightenDeg, 1),
        ]
        for (name, base, decimals) in bases {
            report(!AdjustmentMath.isModified(base, from: base, decimals: decimals),
                   "\(name) is unmarked at its own default")
        }
    }

    /// The percentage in the toolbar is true magnification, not zoom.
    static func testPercent() {
        let v = Viewport()
        v.fitScale = 0.25            // a 24MP frame fitted to a small window
        report(v.percent == 25, "fit on a large image reports 25%, got \(v.percent)")

        v.toggleFitAndActual()
        report(v.percent == 100, "toggling to actual size reports 100%, got \(v.percent)")
        report(!v.isFit, "actual size is not fit")

        v.toggleFitAndActual()
        report(v.isFit, "toggling again returns to fit")
        near(v.center.x, 0.5, 1e-6, "returning to fit recenters")
    }

    // MARK: The picture on screen

    /// The map an overlay is placed with, and the rectangle the renderer
    /// letterboxes into, have to be the same rectangle at fit. They are derived
    /// two different ways — one from the quad the vertex shader gets, one from
    /// the aspect — and the whole reason `CanvasLayout` exists is that those two
    /// derivations once disagreed and the crop handles landed off the pixels.
    static func testPictureMapMatchesTheFitRectangle() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait, 1.0] as [CGFloat] {
            let v = Viewport()
            let map = CanvasLayout.pictureMap(
                quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                visible: v.visibleFraction(imageAspect: image, viewAspect: viewAspect),
                center: v.center, in: view)
            let fit = CanvasLayout.frameRect(imageAspect: image, in: view)

            near(map.rect.minX, fit.minX, 1e-6, "map x matches frameRect (\(image))")
            near(map.rect.minY, fit.minY, 1e-6, "map y matches frameRect (\(image))")
            near(map.rect.width, fit.width, 1e-6, "map w matches frameRect (\(image))")
            near(map.rect.height, fit.height, 1e-6, "map h matches frameRect (\(image))")

            // The picture's centre is the rectangle's centre.
            let c = map.point(CGPoint(x: 0.5, y: 0.5))
            near(c.x, fit.midX, 1e-6, "picture centre is the rect centre x (\(image))")
            near(c.y, fit.midY, 1e-6, "picture centre is the rect centre y (\(image))")

            // And the corners are the corners.
            let tl = map.point(.zero)
            near(tl.x, fit.minX, 1e-6, "picture origin is the rect origin x (\(image))")
            near(tl.y, fit.minY, 1e-6, "picture origin is the rect origin y (\(image))")
        }
    }

    /// View to normalized and back, at zoom and off centre. An overlay that
    /// only round-trips at fit is an overlay that comes off the picture the
    /// moment anybody zooms in, which is when a mask is placed carefully.
    static func testPictureMapRoundTrips() {
        let view = CGSize(width: 1400, height: 900)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait, 1.0] as [CGFloat] {
            for zoom in [1.0, 2.0, 6.0] as [CGFloat] {
                let v = Viewport()
                v.zoomBy(zoom, anchor: CGPoint(x: 0.3, y: 0.7),
                         visible: CGSize(width: 1, height: 1))
                let vis = v.visibleFraction(imageAspect: image, viewAspect: viewAspect)
                v.clamp(to: vis)
                let map = CanvasLayout.pictureMap(
                    quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                    visible: vis, center: v.center, in: view)

                for p in [CGPoint(x: 310, y: 220), CGPoint(x: 900, y: 640),
                          CGPoint(x: 700, y: 450)] {
                    let back = map.point(map.unit(p))
                    near(back.x, p.x, 1e-6, "view→unit→view x (\(image), \(zoom)x)")
                    near(back.y, p.y, 1e-6, "view→unit→view y (\(image), \(zoom)x)")
                }

                // A displacement must not pick up the origin on the way through.
                let d = CGSize(width: 60, height: -25)
                let u = map.unitVector(d)
                let a = map.point(CGPoint(x: 0.4, y: 0.4))
                let b = map.point(CGPoint(x: 0.4 + u.width, y: 0.4 + u.height))
                near(b.x - a.x, d.width, 1e-6, "unitVector is a pure delta x")
                near(b.y - a.y, d.height, 1e-6, "unitVector is a pure delta y")
            }
        }
    }

    /// The map is pinned to the renderer's own uv arithmetic, edge for edge.
    ///
    /// ⚠️ **This test exists because the round trip above does not imply it.**
    /// `point(unit(p)) == p` holds for *any* invertible map, so replacing the
    /// pan with a constant origin passed all 3100 checks — the overlay would
    /// have sat still while the picture panned under it, which is the compare
    /// divider's old bug wearing a different hat. What follows measures the
    /// origin against `ImageCanvas.transform`: `uvMin = centre − visible/2` at
    /// the quad's leading edge and `uvMin + uvSize` at its trailing one.
    static func testPictureMapFollowsThePan() {
        let view = CGSize(width: 1400, height: 900)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait, 1.0] as [CGFloat] {
            for zoom in [1.0, 2.5, 8.0] as [CGFloat] {
                for anchor in [CGPoint(x: 0.2, y: 0.8), CGPoint(x: 0.9, y: 0.1)] {
                    let v = Viewport()
                    v.zoomBy(zoom, anchor: anchor, visible: CGSize(width: 1, height: 1))
                    let vis = v.visibleFraction(imageAspect: image, viewAspect: viewAspect)
                    v.clamp(to: vis)
                    let map = CanvasLayout.pictureMap(
                        quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                        visible: vis, center: v.center, in: view)

                    let tag = "\(image), \(zoom)x, anchor \(anchor.x)"

                    // The renderer's uvMin and uvMin + uvSize, in normalized
                    // image coordinates.
                    let lead = map.unit(CGPoint(x: map.rect.minX, y: map.rect.minY))
                    near(lead.x, v.center.x - vis.width / 2, 1e-9,
                         "leading edge is the renderer's uvMin x (\(tag))")
                    near(lead.y, v.center.y - vis.height / 2, 1e-9,
                         "leading edge is the renderer's uvMin y (\(tag))")

                    let trail = map.unit(CGPoint(x: map.rect.maxX, y: map.rect.maxY))
                    near(trail.x, v.center.x + vis.width / 2, 1e-9,
                         "trailing edge is uvMin + uvSize x (\(tag))")
                    near(trail.y, v.center.y + vis.height / 2, 1e-9,
                         "trailing edge is uvMin + uvSize y (\(tag))")

                    // Whatever the zoom, the viewport's centre is drawn at the
                    // centre of the rectangle the picture covers.
                    let mid = map.point(v.center)
                    near(mid.x, map.rect.midX, 1e-6, "viewport centre is drawn centred x (\(tag))")
                    near(mid.y, map.rect.midY, 1e-6, "viewport centre is drawn centred y (\(tag))")
                }
            }
        }

        // Pan, and the picture must move under a fixed point of the view — by
        // exactly the distance panned. A map that ignored the pan would leave
        // this unchanged, and an overlay built on it would slide off the photo.
        for image in [landscape, portrait] as [CGFloat] {
            let v = Viewport()
            v.zoomBy(4, anchor: CGPoint(x: 0.5, y: 0.5), visible: CGSize(width: 1, height: 1))
            let vis = v.visibleFraction(imageAspect: image, viewAspect: viewAspect)
            v.clamp(to: vis)

            func mapNow() -> CanvasLayout.PictureMap {
                CanvasLayout.pictureMap(
                    quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                    visible: vis, center: v.center, in: view)
            }

            let probe = CGPoint(x: 700, y: 450)
            let before = mapNow().unit(probe)
            let was = v.center

            v.pan(by: CGSize(width: 0.05, height: -0.03), visible: vis)
            v.clamp(to: vis)
            let after = mapNow().unit(probe)

            let moved = hypot(after.x - before.x, after.y - before.y)
            report(moved > 1e-6, "panning moves the picture under a fixed point (\(image))",
                   String(format: "moved %.9f", moved))

            // And by the centre's own displacement, not some other amount.
            near(after.x - before.x, v.center.x - was.x, 1e-9,
                 "the picture moves by the pan x (\(image))")
            near(after.y - before.y, v.center.y - was.y, 1e-9,
                 "the picture moves by the pan y (\(image))")
        }
    }

    // MARK: Gradient masks
}

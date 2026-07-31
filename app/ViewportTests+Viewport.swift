// Fit, zoom, clamping, the crop rectangle and the drawn quad.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// At fit the whole image must be visible on both axes. Getting this
    /// backwards silently crops the frame — which it did.
    static func testFitShowsEverything() {
        for (image, view) in [(landscape, 1.8), (landscape, 0.9),
                              (portrait, 1.8), (portrait, 0.9),
                              (1.0, 1.0)] as [(CGFloat, CGFloat)] {
            let v = Viewport()
            let f = v.visibleFraction(imageAspect: image, viewAspect: view)
            near(f.width, 1.0, 1e-6,
                 "fit shows full width (image \(image), view \(view))")
            near(f.height, 1.0, 1e-6,
                 "fit shows full height (image \(image), view \(view))")
        }
    }

    /// Doubling the zoom halves what you can see, on the axis that fit was
    /// constrained by.
    static func testZoomShrinksVisible() {
        let v = Viewport()
        v.zoomBy(2.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: CGSize(width: 1, height: 1))
        near(v.zoom, 2.0, 1e-6, "zoomBy multiplies")

        // Wide image in a narrow view: width is the constrained axis.
        let f = v.visibleFraction(imageAspect: landscape, viewAspect: 0.9)
        near(f.width, 0.5, 1e-6, "2x halves the visible width")
        report(f.height <= 1.0 + 1e-9, "visible height never exceeds the image")

        // Visible fraction must never exceed 1, at any zoom or aspect.
        for z in [1.0, 1.5, 4.0, 16.0] as [CGFloat] {
            let w = Viewport()
            w.zoomBy(z, anchor: CGPoint(x: 0.5, y: 0.5),
                     visible: CGSize(width: 1, height: 1))
            for image in [landscape, portrait, 1.0] as [CGFloat] {
                for view in [0.5, 1.0, 2.0] as [CGFloat] {
                    let f = w.visibleFraction(imageAspect: image, viewAspect: view)
                    report(f.width <= 1.0 + 1e-9 && f.height <= 1.0 + 1e-9,
                           "visible fraction stays within the image (z \(z))")
                    report(f.width > 0 && f.height > 0, "visible fraction is positive")
                }
            }
        }
    }

    /// The quad letterboxes at fit and fills the view once zoomed past it.
    static func testQuadLetterboxes() {
        let v = Viewport()

        // Wide image, narrow view: full width, letterboxed height.
        let wide = v.quadScale(imageAspect: landscape, viewAspect: 0.9)
        near(wide.width, 1.0, 1e-6, "wide image fills the view's width")
        report(wide.height < 1.0, "wide image is letterboxed vertically")

        // Tall image, wide view: full height, pillarboxed width.
        let tall = v.quadScale(imageAspect: portrait, viewAspect: 1.8)
        near(tall.height, 1.0, 1e-6, "tall image fills the view's height")
        report(tall.width < 1.0, "tall image is pillarboxed horizontally")

        // Matching aspects need no bars at all.
        let exact = v.quadScale(imageAspect: 1.5, viewAspect: 1.5)
        near(exact.width, 1.0, 1e-6, "matched aspect fills width")
        near(exact.height, 1.0, 1e-6, "matched aspect fills height")

        // Zoomed in, the image covers the whole view.
        v.zoomBy(4.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: CGSize(width: 1, height: 1))
        let zoomed = v.quadScale(imageAspect: landscape, viewAspect: 0.9)
        near(zoomed.width, 1.0, 1e-6, "zoomed image covers the width")
        near(zoomed.height, 1.0, 1e-6, "zoomed image covers the height")
    }

    /// Panning must never show past the edge of the frame.
    static func testClampKeepsFrameInView() {
        let v = Viewport()
        v.zoomBy(4.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: CGSize(width: 1, height: 1))
        let visible = v.visibleFraction(imageAspect: landscape, viewAspect: 1.0)

        // Shove the center far outside, then clamp.
        v.pan(by: CGSize(width: -10, height: -10), visible: visible)
        v.clamp(to: visible)
        report(v.center.x + visible.width / 2 <= 1.0 + 1e-6, "right edge stays inside")
        report(v.center.y + visible.height / 2 <= 1.0 + 1e-6, "bottom edge stays inside")

        v.pan(by: CGSize(width: 10, height: 10), visible: visible)
        v.clamp(to: visible)
        report(v.center.x - visible.width / 2 >= -1e-6, "left edge stays inside")
        report(v.center.y - visible.height / 2 >= -1e-6, "top edge stays inside")

        // At fit the center is pinned, so there is nothing to pan to.
        let f = Viewport()
        let full = f.visibleFraction(imageAspect: landscape, viewAspect: 1.0)
        f.pan(by: CGSize(width: 0.4, height: 0.4), visible: full)
        f.clamp(to: full)
        near(f.center.x, 0.5, 1e-6, "fit pins the center horizontally")
        near(f.center.y, 0.5, 1e-6, "fit pins the center vertically")
    }

    /// Zooming about a point keeps that point where it was.
    static func testZoomAnchoring() {
        let v = Viewport()
        let visible = CGSize(width: 1, height: 1)
        let anchor = CGPoint(x: 0.25, y: 0.75)

        v.zoomBy(2.0, anchor: anchor, visible: visible)
        // Center moves toward the anchor, not away from it.
        report(v.center.x < 0.5, "zoom moves the center toward the anchor (x)")
        report(v.center.y > 0.5, "zoom moves the center toward the anchor (y)")

        // Zoom is bounded: never below fit, never absurdly far in.
        let w = Viewport()
        w.zoomBy(0.01, anchor: CGPoint(x: 0.5, y: 0.5), visible: visible)
        near(w.zoom, 1.0, 1e-6, "zoom never goes below fit")

        for _ in 0..<40 {
            w.zoomBy(2.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: visible)
        }
        report(w.zoom <= 64.0 + 1e-6, "zoom is capped")
    }

    /// Hue to band. If this drifts from the centers in hsl_ops.slang, the
    /// targeted tool adjusts a band you did not click.
    static func testHueBands() {
        // Each band center must map to its own band.
        for (i, center) in TargetedAdjust.centers.enumerated() {
            let got = TargetedAdjust.band(forHue: center)
            report(got.rawValue == i,
                   "hue \(Int(center))deg maps to \(HueBand(rawValue: i)!.name)",
                   got.rawValue == i ? "" : "got \(got.name)")
        }

        // Wrapping: 359 degrees is red, not magenta.
        report(TargetedAdjust.band(forHue: 359) == .red, "hue wraps around zero")
        report(TargetedAdjust.band(forHue: 1) == .red, "just past zero is red")

        // Sky blue lands in blue, foliage in green.
        report(TargetedAdjust.band(forHue: 225) == .blue, "sky blue is the blue band")
        report(TargetedAdjust.band(forHue: 110) == .green, "foliage is the green band")

        // Hue extraction from RGB.
        near(TargetedAdjust.hue(r: 1, g: 0, b: 0) ?? -1, 0, 1e-6, "pure red is 0deg")
        near(TargetedAdjust.hue(r: 0, g: 1, b: 0) ?? -1, 120, 1e-6, "pure green is 120deg")
        near(TargetedAdjust.hue(r: 0, g: 0, b: 1) ?? -1, 240, 1e-6, "pure blue is 240deg")

        // Gray has no hue, and must refuse rather than pick one at random —
        // otherwise clicking a wall silently adjusts some band.
        report(TargetedAdjust.hue(r: 0.5, g: 0.5, b: 0.5) == nil, "gray has no hue")
        report(TargetedAdjust.hue(r: 0.5, g: 0.505, b: 0.5) == nil,
               "near-gray has no hue")

        // Scale invariance: the picker normalizes by peak, so a dark and a
        // bright version of the same color must land in the same band.
        let bright = TargetedAdjust.hue(r: 0.2, g: 0.4, b: 0.9)
        let dark = TargetedAdjust.hue(r: 0.02, g: 0.04, b: 0.09)
        if let b = bright, let d = dark {
            near(b, d, 0.5, "hue is independent of brightness")
        } else {
            report(false, "hue is independent of brightness", "one sample returned nil")
        }
    }

    /// A locked viewport rejects zoom and pan.
    ///
    /// The crop overlay draws from the fit rectangle, so any zoom desynchronises
    /// the handles from the pixels — a bug that shipped twice.
    static func testCropLock() {
        let v = Viewport()
        v.zoomBy(4.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: CGSize(width: 1, height: 1))
        report(v.zoom > 1, "zoom works when unlocked")

        v.locked = true
        near(v.zoom, 1.0, 1e-6, "locking returns to fit")

        v.zoomBy(4.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: CGSize(width: 1, height: 1))
        near(v.zoom, 1.0, 1e-6, "locked viewport rejects zoom")

        v.pan(by: CGSize(width: 0.3, height: 0.3), visible: CGSize(width: 0.5, height: 0.5))
        near(v.center.x, 0.5, 1e-6, "locked viewport rejects pan")

        v.toggleFitAndActual()
        near(v.zoom, 1.0, 1e-6, "locked viewport rejects the fit toggle")

        v.locked = false
        v.zoomBy(2.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: CGSize(width: 1, height: 1))
        near(v.zoom, 2.0, 1e-6, "unlocking restores zoom")
    }

    /// The overlay's rectangle must be the one the renderer draws on.
    ///
    /// Two copies of this arithmetic existed — the renderer's and the
    /// overlay's — and they disagreed, which put the white rectangle and the
    /// corner marks on a frame the pixels were not in.
    static func testFrameRectMatchesRenderer() {
        let views = [CGSize(width: 1400, height: 900), CGSize(width: 700, height: 1000),
                     CGSize(width: 1000, height: 1000)]

        for image in [landscape, portrait, 1.0] as [CGFloat] {
            for view in views {
                let frame = CanvasLayout.frameRect(imageAspect: image, in: view)

                let v = Viewport()
                let quad = v.quadScale(imageAspect: image,
                                       viewAspect: view.width / view.height)

                near(frame.width, view.width * quad.width, 1e-6,
                     "frame width matches the renderer (\(image), \(view))")
                near(frame.height, view.height * quad.height, 1e-6,
                     "frame height matches the renderer (\(image), \(view))")
                near(frame.midX, view.width / 2, 1e-6, "frame is centered horizontally")
                near(frame.midY, view.height / 2, 1e-6, "frame is centered vertically")
                near(frame.width / frame.height, image, 1e-4,
                     "frame keeps the image's aspect")
            }
        }
    }

    /// The rule that stops a straightened export having transparent corners.
    ///
    /// The crop is what gets sampled, so anything it reaches past the turned
    /// frame has nothing behind it. This checks the constraint directly: take
    /// the crop's four corners, turn them about the crop's own center by the
    /// straighten angle, and require every one to land inside the frame.
    static func testCropStaysInsideTurnedFrame() {
        let aspects: [CGFloat] = [landscape, portrait, 1.0, 16.0 / 9.0]
        let angles: [CGFloat] = [0, 1, 5, 15, 30, 45, 60, 90, -7, -45, -90]
        let asked = [CGRect(x: 0, y: 0, width: 1, height: 1),
                     CGRect(x: 0.05, y: 0.05, width: 0.9, height: 0.9),
                     CGRect(x: 0, y: 0, width: 0.5, height: 0.5),
                     CGRect(x: 0.6, y: 0.7, width: 0.4, height: 0.3),
                     CGRect(x: -0.2, y: 0.8, width: 1.4, height: 0.6)]

        for a in aspects {
            for angle in angles {
                for want in asked {
                    let r = CanvasLayout.constrainedCrop(want, frameAspect: a,
                                                         angleDeg: angle)
                    let what = "aspect \(a), \(angle)°, asked \(want)"

                    report(r.width > 0 && r.height > 0, "crop is non-empty — \(what)")
                    report(r.width <= want.width + 1e-6 && r.height <= want.height + 1e-6,
                           "crop never grows beyond what was asked — \(what)")

                    for c in turnedCorners(r, aspect: a, angleDeg: angle) {
                        report(c.x >= -1e-4 && c.x <= 1 + 1e-4
                               && c.y >= -1e-4 && c.y <= 1 + 1e-4,
                               "turned corner \(c) is inside the frame — \(what)")
                    }
                }
            }
        }
    }

    /// The crop's corners after the straighten, in frame coordinates. The
    /// picture turns about the frame's center, so that is what the rectangle
    /// turns about too.
    static func turnedCorners(_ r: CGRect, aspect a: CGFloat,
                              angleDeg: CGFloat) -> [CGPoint] {
        let t = angleDeg * .pi / 180
        let c = cos(t), s = sin(t)
        let mid = CanvasLayout.pivot

        return [CGPoint(x: r.minX, y: r.minY), CGPoint(x: r.maxX, y: r.minY),
                CGPoint(x: r.minX, y: r.maxY), CGPoint(x: r.maxX, y: r.maxY)].map { p in
            // Into units where a rotation is a rotation: height 1, width `a`.
            let dx = (p.x - mid.x) * a
            let dy = (p.y - mid.y)
            return CGPoint(x: mid.x + (dx * c - dy * s) / a,
                           y: mid.y + (dx * s + dy * c))
        }
    }

    /// An untouched crop at no angle must be left completely alone. The
    /// constraint runs on every angle tick, so a rounding drift here would
    /// quietly shrink the frame each time you touched the dial.
    static func testConstraintIsIdentityWhenStraight() {
        for a in [landscape, portrait, 1.0] as [CGFloat] {
            let full = CGRect(x: 0, y: 0, width: 1, height: 1)
            let r = CanvasLayout.constrainedCrop(full, frameAspect: a, angleDeg: 0)
            near(r.width, 1, 1e-9, "a full crop is untouched at 0° (aspect \(a))")
            near(r.height, 1, 1e-9, "a full crop keeps its height at 0° (aspect \(a))")
            near(r.origin.x, 0, 1e-9, "a full crop keeps its origin at 0° (aspect \(a))")

            // And repeated application must not creep.
            var creep = CGRect(x: 0.1, y: 0.2, width: 0.6, height: 0.5)
            for _ in 0..<50 {
                creep = CanvasLayout.constrainedCrop(creep, frameAspect: a, angleDeg: 12)
            }
            let once = CanvasLayout.constrainedCrop(
                CGRect(x: 0.1, y: 0.2, width: 0.6, height: 0.5),
                frameAspect: a, angleDeg: 12)
            near(creep.width, once.width, 1e-6,
                 "the constraint is idempotent (aspect \(a))")
        }
    }

    /// The preview canvas has to contain the whole turned frame, or the corners
    /// of the picture are clipped away — which the old fixed 1.42 did past
    /// about 17 degrees on a 3:2 frame.
    static func testPreviewCanvasCoversTheTurnedFrame() {
        for a in [landscape, portrait, 1.0, 16.0 / 9.0] as [CGFloat] {
            for angle in [0, 5, 17, 30, 45, 60, 90, -45] as [CGFloat] {
                let crop = CanvasLayout.constrainedCrop(
                    CGRect(x: 0, y: 0, width: 1, height: 1),
                    frameAspect: a, angleDeg: angle)
                let canvas = CanvasLayout.previewCanvas(frameAspect: a, angleDeg: angle)
                let what = "aspect \(a), \(angle)°"

                report(canvas.size >= 1 - 1e-9, "the canvas is at least the frame — \(what)")

                // Every corner of the frame, turned, must fall on the canvas.
                let full = CGRect(x: 0, y: 0, width: 1, height: 1)
                for c in turnedFrameCorners(full, about: CanvasLayout.pivot, aspect: a,
                                            angleDeg: -angle) {
                    let u = (c.x - canvas.origin.x) / canvas.size
                    let v = (c.y - canvas.origin.y) / canvas.size
                    report(u >= -1e-4 && u <= 1 + 1e-4 && v >= -1e-4 && v <= 1 + 1e-4,
                           "frame corner \(c) is on the canvas at (\(u), \(v)) — \(what)")
                }

                // And the crop must sit on the canvas too, or the white
                // rectangle is drawn somewhere the picture is not.
                let onCanvas = CanvasLayout.onCanvas(crop, canvasOrigin: canvas.origin,
                                                     canvasSize: canvas.size)
                report(onCanvas.minX >= -1e-4 && onCanvas.maxX <= 1 + 1e-4,
                       "the crop rectangle is on the canvas — \(what)")
            }
        }
    }

    /// The frame's corners after the picture turns about `pivot`.
    static func turnedFrameCorners(_ r: CGRect, about pivot: CGPoint, aspect a: CGFloat,
                                   angleDeg: CGFloat) -> [CGPoint] {
        let t = angleDeg * .pi / 180
        let c = cos(t), s = sin(t)
        return [CGPoint(x: r.minX, y: r.minY), CGPoint(x: r.maxX, y: r.minY),
                CGPoint(x: r.minX, y: r.maxY), CGPoint(x: r.maxX, y: r.maxY)].map { p in
            let dx = (p.x - pivot.x) * a
            let dy = (p.y - pivot.y)
            return CGPoint(x: pivot.x + (dx * c - dy * s) / a,
                           y: pivot.y + (dx * s + dy * c))
        }
    }

    /// Moving or resizing the crop must not move the picture.
    ///
    /// The preview canvas once followed the crop's center, so dragging the
    /// rectangle re-rotated the frame underneath it and the image came unstuck
    /// from the box. The canvas depends on the angle and the aspect, and on
    /// nothing else.
    static func testCanvasIgnoresTheCrop() {
        for a in [landscape, portrait, 1.0] as [CGFloat] {
            for angle in [0, 8, 30, 45, 90, -22] as [CGFloat] {
                let reference = CanvasLayout.previewCanvas(frameAspect: a, angleDeg: angle)

                for crop in [CGRect(x: 0, y: 0, width: 1, height: 1),
                             CGRect(x: 0, y: 0, width: 0.3, height: 0.3),
                             CGRect(x: 0.7, y: 0.6, width: 0.3, height: 0.4),
                             CGRect(x: 0.4, y: 0.05, width: 0.2, height: 0.2)] {
                    let constrained = CanvasLayout.constrainedCrop(
                        crop, frameAspect: a, angleDeg: angle)
                    let again = CanvasLayout.previewCanvas(frameAspect: a, angleDeg: angle)

                    near(again.size, reference.size, 1e-12,
                         "canvas size ignores crop \(constrained) — aspect \(a), \(angle)°")
                    near(again.origin.x, reference.origin.x, 1e-12,
                         "canvas origin ignores crop \(constrained) — aspect \(a), \(angle)°")
                }

                // And it is concentric with the frame, which is what the
                // engine turns the picture about.
                near(reference.origin.x + reference.size / 2, CanvasLayout.pivot.x, 1e-9,
                     "canvas is centered on the pivot horizontally — aspect \(a), \(angle)°")
                near(reference.origin.y + reference.size / 2, CanvasLayout.pivot.y, 1e-9,
                     "canvas is centered on the pivot vertically — aspect \(a), \(angle)°")
            }
        }
    }

    /// Corner handles sit on the crop rectangle's corners. They were drawn in
    /// an unsized path view that SwiftUI grew to the whole overlay, and landed
    /// nowhere near.
    static func testCornerHandlePositions() {
        let frame = CGRect(x: 40, y: 30, width: 600, height: 400)
        let unit = CGRect(x: 0.25, y: 0.5, width: 0.5, height: 0.5)
        let r = CanvasLayout.inView(unit, in: frame)

        near(r.minX, 40 + 150, 1e-6, "top-left handle x")
        near(r.minY, 30 + 200, 1e-6, "top-left handle y")
        near(r.maxX, 40 + 450, 1e-6, "bottom-right handle x")
        near(r.maxY, 30 + 400, 1e-6, "bottom-right handle y")

        for corner in [CGPoint(x: r.minX, y: r.minY), CGPoint(x: r.maxX, y: r.minY),
                       CGPoint(x: r.minX, y: r.maxY), CGPoint(x: r.maxX, y: r.maxY)] {
            report(frame.insetBy(dx: -1e-6, dy: -1e-6).contains(corner),
                   "handle \(corner) is on the frame")
        }
    }
}

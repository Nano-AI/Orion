import CoreGraphics
import Foundation

/// Viewport geometry tests.
///
/// This maths has produced two visible bugs — a stretched frame and a cropped
/// one — because it was only ever checked by looking at the screen. The
/// relationships below are the ones that were wrong.
enum ViewportTests {

    nonisolated(unsafe) static var checks = 0
    nonisolated(unsafe) static var failures = 0

    static func report(_ ok: Bool, _ what: String, _ detail: String = "") {
        checks += 1
        guard !ok else { return }
        failures += 1
        print("  FAIL  \(what)\(detail.isEmpty ? "" : " — \(detail)")")
    }

    static func near(_ got: CGFloat, _ want: CGFloat, _ tol: CGFloat, _ what: String) {
        let ok = abs(got - want) <= tol
        report(ok, what, ok ? "" : String(format: "got %.5f, want %.5f", got, want))
    }

    // Landscape 3:2 image; a taller-than-wide view and a wider-than-tall one.
    static let landscape: CGFloat = 6024.0 / 4024.0   // ~1.497
    static let portrait: CGFloat = 4024.0 / 6024.0    // ~0.668

    static func run() -> Int {
        print("Viewport\n")

        testFitShowsEverything()
        testZoomShrinksVisible()
        testQuadLetterboxes()
        testClampKeepsFrameInView()
        testZoomAnchoring()
        testPercent()
        testHueBands()
        testCropLock()
        testFrameRectMatchesRenderer()
        testCropStaysInsideTurnedFrame()
        testConstraintIsIdentityWhenStraight()
        testPreviewCanvasCoversTheTurnedFrame()
        testCanvasIgnoresTheCrop()
        testCornerHandlePositions()
        testCurveMatchesTheEngine()
        testCurvePointsStayOrdered()
        testModifiedTracksTheReadout()
        testDrawnRectFollowsTheZoom()
        testSidecarSurvivesAMissingField()
        testSidecarEscapingDoesNotCompound()
        testEditsSurviveAQuit()

        print("\n\(checks) checks, \(failures) failures")
        return failures
    }

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

    /// The panel's spline must be the engine's spline.
    ///
    /// The panel evaluates the curve itself, because it has to draw the line
    /// during a drag rather than wait for a render. Two implementations of one
    /// spline is a real risk of them quietly diverging, so these are values
    /// taken from `pipe/ToneCurve.cpp` — if the two ever disagree, the line you
    /// drew and the picture you got would stop matching, which is the kind of
    /// bug that wastes an evening.
    static func testCurveMatchesTheEngine() {
        let s = [CurvePoint(x: 0, y: 0), CurvePoint(x: 0.25, y: 0.14),
                 CurvePoint(x: 0.62, y: 0.78), CurvePoint(x: 1, y: 1)]
        let sWant: [Float] = [0, 0.0497819521, 0.1037092581, 0.1974559724, 0.3822722733, 0.5956153274, 0.7598698139, 0.8408008814, 0.8986957073, 0.9477383494, 1]

        for (i, want) in sWant.enumerated() {
            let x = Float(i) / 10
            near(CGFloat(CurveMath.evaluate(s, at: x)), CGFloat(want), 1e-6,
                 "S-curve matches the engine at x = \(x)")
        }

        // Endpoints away from the corners: a lifted black and a pulled white,
        // which is where an implementation that assumes 0,0 and 1,1 breaks.
        let lifted = [CurvePoint(x: 0, y: 0.1), CurvePoint(x: 0.5, y: 0.4),
                      CurvePoint(x: 1, y: 0.9)]
        let liftedWant: [Float] = [0.1000000015, 0.1576000154, 0.2128000110, 0.2692000270, 0.3303999901, 0.4000000060, 0.4839999974, 0.5820000172, 0.6880000234, 0.7960000038, 0.8999999762]

        for (i, want) in liftedWant.enumerated() {
            let x = Float(i) / 10
            near(CGFloat(CurveMath.evaluate(lifted, at: x)), CGFloat(want), 1e-6,
                 "lifted curve matches the engine at x = \(x)")
        }

        // The identity has to be exactly the identity, or every image picks up
        // a faint contrast change the moment the panel is opened.
        for i in 0...20 {
            let x = Float(i) / 20
            near(CGFloat(CurveMath.evaluate(ToneCurve.identity, at: x)), CGFloat(x), 1e-7,
                 "the identity curve is the identity at x = \(x)")
        }

        // Monotone means monotone: no control point arrangement may make the
        // output go backwards, which in a photo reads as a tonal reversal.
        let harsh = [CurvePoint(x: 0, y: 0), CurvePoint(x: 0.05, y: 0.6),
                     CurvePoint(x: 0.1, y: 0.62), CurvePoint(x: 0.9, y: 0.65),
                     CurvePoint(x: 1, y: 1)]
        var previous = CurveMath.evaluate(harsh, at: 0)
        var monotone = true
        for i in 1...400 {
            let y = CurveMath.evaluate(harsh, at: Float(i) / 400)
            if y < previous - 1e-6 { monotone = false; break }
            previous = y
        }
        report(monotone, "a steep curve never reverses")
    }

    /// A curve's control points must strictly ascend in x.
    ///
    /// The engine treats a non-ascending curve as malformed and falls back to
    /// the identity — correctly, since the interpolator assumes ordering. But
    /// the panel could produce one: clicking near the right edge appended a
    /// second point at x = 1, and the whole curve silently snapped back, which
    /// reads as the panel being broken rather than as input being rejected.
    static func testCurvePointsStayOrdered() {
        var points = ToneCurve.identity

        // Every click position, twice over, including the edges that broke it
        // and the repeats that broke the first fix.
        let clicks: [Float] = [-0.5, 0, 0.001, 0.25, 0.5, 0.75, 0.999, 1, 1.5,
                               0, 0.001, 1, 0.999, 0.5, 0.5, 0.25]

        for f in clicks {
            guard let placed = CurveMath.insertion(of: f, into: points) else { continue }
            points.insert(CurvePoint(x: placed.x,
                                     y: CurveMath.evaluate(points, at: placed.x)),
                          at: placed.index)

            var ascending = true
            for i in 1..<points.count where points[i].x <= points[i - 1].x {
                ascending = false
            }
            report(ascending, "points ascend after a click at \(f)")
            report(points[0].x == 0 && points[points.count - 1].x == 1,
                   "the endpoints survive a click at \(f)")
        }

        // And a curve packed with points must decline rather than duplicate.
        var packed = ToneCurve.identity
        for i in 1..<40 {
            let f = Float(i) / 40
            if let placed = CurveMath.insertion(of: f, into: packed) {
                packed.insert(CurvePoint(x: placed.x, y: placed.x), at: placed.index)
            }
        }
        var stillAscending = true
        for i in 1..<packed.count where packed[i].x <= packed[i - 1].x {
            stillAscending = false
        }
        report(stillAscending, "a densely packed curve still ascends")
    }

    /// A sidecar missing keys still restores the ones it has.
    ///
    /// Swift's synthesized decoder throws on a missing key rather than falling
    /// back to the property's default, and `Engine.restore` swallows that with a
    /// `try?`. Adding one field to `DevelopState` would therefore have discarded
    /// *every* adjustment in *every* sidecar already on disk, and the photo
    /// would simply have opened unedited with nothing said.
    static func testSidecarSurvivesAMissingField() {
        func decode(_ json: String) -> DevelopState? {
            guard let data = json.data(using: .utf8) else { return nil }
            return try? JSONDecoder().decode(DevelopState.self, from: data)
        }

        // A sidecar from a build that had only three of the fields.
        guard let sparse = decode(#"{"exposureEv":1.5,"contrast":1.4,"cropW":0.5}"#) else {
            report(false, "a sparse sidecar decodes at all")
            return
        }
        report(true, "a sparse sidecar decodes at all")
        near(CGFloat(sparse.exposureEv), 1.5, 1e-6, "the exposure it carried survives")
        near(CGFloat(sparse.contrast), 1.4, 1e-6, "the contrast it carried survives")
        near(CGFloat(sparse.cropW), 0.5, 1e-6, "the crop it carried survives")
        near(CGFloat(sparse.sharpenRadius), 1.0, 1e-6,
             "a field it never had falls back to its default, not to zero")
        report(sparse.hueShift.count == 8, "the bands are still the right length")

        // Empty and malformed both have to land on the defaults rather than
        // throwing, or one bad sidecar makes a photo look unopenable.
        guard let empty = decode("{}") else {
            report(false, "an empty sidecar decodes")
            return
        }
        report(true, "an empty sidecar decodes")
        report(empty == DevelopState(), "an empty sidecar is exactly the defaults")

        if let junk = decode(#"{"exposureEv":"not a number","tint":0.3}"#) {
            near(CGFloat(junk.tint), 0.3, 1e-6, "a bad field does not poison a good one")
            near(CGFloat(junk.exposureEv), 0, 1e-6, "a bad field falls back to its default")
        } else {
            report(false, "a sidecar with one bad field still decodes")
        }

        // A band of the wrong length would index out of bounds in the panel.
        if let short = decode(#"{"hueShift":[0.1,0.2]}"#) {
            report(short.hueShift.count == 8, "a short band array is refused, not trusted")
        } else {
            report(false, "a sidecar with a short band array still decodes")
        }

        // The pre-rename spelling. A photo finished before the interface moved
        // to American spelling must not lose its noise reduction.
        if let old = decode(#"{"denoiseColour":2.4}"#) {
            near(CGFloat(old.denoiseColor), 2.4, 1e-6,
                 "a sidecar written as denoiseColour still restores")
        } else {
            report(false, "a pre-rename sidecar decodes")
        }

        // And a full round trip has to be exact, or undo and the sidecar would
        // disagree about what was saved.
        var full = DevelopState()
        full.exposureEv = -1.25
        full.denoiseColor = 3.1
        full.curve.master = [CurvePoint(x: 0, y: 0.1), CurvePoint(x: 1, y: 0.9)]
        full.satShift[3] = 0.42
        if let data = try? JSONEncoder().encode(full),
           let back = try? JSONDecoder().decode(DevelopState.self, from: data) {
            report(back == full, "a full state round-trips unchanged")
        } else {
            report(false, "a full state round-trips at all")
        }
    }

    /// Edit, quit, reopen — the adjustments have to still be there.
    ///
    /// They were not. `saveDevelop` ran only when *switching* photos, so the
    /// common case — open one file, work on it, ⌘Q — lost everything. The
    /// sidecar round-trip above was passing the whole time, which is the point:
    /// a correct mechanism nobody triggers reads exactly like a working feature.
    ///
    /// The deferral is handed in, so the coalescing is checked by firing it
    /// rather than by sleeping.
    static func testEditsSurviveAQuit() {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-autosave-\(ProcessInfo.processInfo.processIdentifier)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let a = dir.appendingPathComponent("a.ARW")
        let b = dir.appendingPathComponent("b.ARW")

        /// What is on disk for a photo, decoded back the way `Editor.load` does.
        func onDisk(_ photo: URL) -> DevelopState? {
            guard let data = Sidecar.read(for: photo)?.develop else { return nil }
            return try? JSONDecoder().decode(DevelopState.self, from: data)
        }

        func edited(_ ev: Float) -> DevelopState {
            var s = DevelopState()
            s.exposureEv = ev
            return s
        }

        // --- The invariant, through the real sidecar on a real file. ---
        var fire: (() -> Void)?
        let save = Autosave(deferral: { fire = $0 })

        save.begin(url: a, saved: DevelopState())
        save.note(edited(1.5))
        report(onDisk(a) == nil, "nothing is written before the writes coalesce")
        report(save.isDirty, "the write is owed, not forgotten")

        fire?()
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), 1.5, 1e-6,
             "the edit reaches the sidecar without anybody asking")
        report(!save.isDirty, "and is not owed twice")

        // The ⌘Q that lands inside the window. This is the case that was losing
        // an entire session's work.
        save.note(edited(-2.25))
        report(onDisk(a)?.exposureEv == 1.5, "a later edit has not landed yet")
        save.flush()
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), -2.25, 1e-6,
             "quitting inside the coalescing window still writes")

        // Ratings live in the same file and must survive an edit write.
        Sidecar.merge(into: a) { $0.rating = 4 }
        save.note(edited(0.75))
        save.flush()
        report(Sidecar.read(for: a)?.rating == 4, "an autosave keeps the rating")
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), 0.75, 1e-6, "and carries the edit")

        // --- Nothing may be misfiled. ---
        // A write queued for a and not yet fired, then the photo switches: it
        // belongs to a. Reading the engine at fire time instead would put a's
        // work in b's sidecar, because `current` moves ~50 ms before the decode
        // finishes.
        save.begin(url: a, saved: edited(0.75))
        save.note(edited(3.0))
        save.begin(url: b, saved: DevelopState())
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), 3.0, 1e-6,
             "a pending write follows the photo it was queued for")
        report(onDisk(b) == nil, "and does not land on the photo that arrived")

        fire?()   // the stale timer from a's note must not write b's sidecar
        report(onDisk(b) == nil, "a timer left over from the previous photo writes nothing")

        // --- Restoring is not an edit. ---
        // Every open pushes renders; if those counted, opening a photo would
        // rewrite its sidecar, and opening a folder would rewrite all of them.
        let opened = edited(-1.0)
        save.begin(url: b, saved: opened)
        save.note(opened)
        save.flush()
        report(onDisk(b) == nil, "opening a photo does not write back what it just read")

        // --- Notes while nothing is open are dropped, not queued. ---
        save.stop()
        save.note(edited(9.0))
        save.flush()
        report(onDisk(b) == nil, "an edit with no photo in hand goes nowhere")

        // --- Last note wins. ---
        // `Engine.captureOriginal` applies a neutral state and then the real
        // one, back to back. A queue that appended would persist the neutral.
        save.begin(url: b, saved: opened)
        save.note(DevelopState())        // the neutral compare render
        save.note(edited(2.0))           // the edit, restored immediately after
        save.flush()
        near(CGFloat(onDisk(b)?.exposureEv ?? 0), 2.0, 1e-6,
             "the last state noted is the one written")
    }

    /// Escaping compounds unless reading undoes it.
    ///
    /// `Library.persist` reads, modifies and rewrites the whole sidecar on
    /// every rating change, so a label written as `R&amp;D` and read back as
    /// `R&amp;D` is escaped again on the next save. One save per gained layer,
    /// and the field is read from foreign sidecars — Lightroom writes labels.
    static func testSidecarEscapingDoesNotCompound() {
        let cases = ["R&D", "a < b", "a > b", "say \"hi\"", "&amp;", "plain",
                     "&lt;not a tag&gt;"]
        for original in cases {
            var text = original
            // Three round trips: one save is not enough to show compounding.
            for _ in 0..<3 {
                text = Sidecar.unescape(Sidecar.escapeForTests(text))
            }
            report(text == original,
                   "\(original) survives three save/load cycles unchanged",
                   text == original ? "" : "became \(text)")
        }
    }

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
}


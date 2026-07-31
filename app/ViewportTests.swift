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
        testMaskGroupSidecar()
        testSidecarEscapingDoesNotCompound()
        testEditsSurviveAQuit()

        testMattePngRoundTripsItsMidTones()
        testMatteKeepsItsOrientation()
        testMatteClampsRatherThanWraps()
        testMatteReferenceSurvivesTheSidecar()
        testMatteFileSitsBesideTheSidecar()

        testPictureMapMatchesTheFitRectangle()
        testPictureMapRoundTrips()
        testPictureMapFollowsThePan()
        testBatchNeverOverwrites()
        testBatchKeepsGoingAfterAFailure()
        testEveryFieldSurvivesTheSidecar()
        testPresetIsAPatch()
        testSyncKeysMatchTheStructPatch()
        testSyncLeavesUnknownWhiteBalanceAlone()
        testSyncPatchesOnlyItsGroups()
        testPresetNeverCarriesTheFrame()
        testPresetStoreRoundTrip()
        testMatteTurnsRoundTrip()
        testMatteTurnAgreesWithTheMaskTransform()
        testMattePreviewSize()
        testMaskOutlineLandsOnTheFalloff()
        testMaskIsoLinesAreIsoAlpha()
        testMaskEndpointLandsUnderTheCursor()
        testMaskRotateStaysOnTheCursorRay()
        testMaskBodyDragMovesByTheDrag()
        testMaskAxisDragDoesNotRotate()
        testMaskDragStaysInSliderRange()
        testMaskHitPrefersHandlesOverBody()
        testMaskAnglesAreNormalizedNotScreen()
        testBrushDabsAreEvenlySpaced()
        testBrushSpacingSurvivesTheEventRate()

        testSkyFillCannotSqueezeThroughADiagonal()
        testSkyEigenvalueProxyOrdersTheSameWay()
        testSkyFindsAHorizon()
        testSkyNeverAsksWhatSkyLooksLike()
        testSkyRefusesAFrameWithNone()
        testSkyEnergyPicksTheBorder()

        testCatalogueCoversEveryAdjustment()
        testCatalogueAgreesWithTheShaderAboutWhatIsLocal()
        testEveryRefusalGivesAReason()

        testSpotHitPrefersTheSource()
        testSpotHitPrefersTheTopmost()
        testSpotHandleHasAMinimumSize()
        testSpotDragStaysOnThePicture()

        testOnePhotoIsNotASelection()
        testModifiedClicksBuildASelection()
        testShiftClickIsARangeFromTheAnchor()
        testTheOpenPhotoCannotBeDeselected()
        testAFilterChangeCannotHideATarget()
        testTargetsComeBackInStripOrder()

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

    /// The mask group in the sidecar, and the single mask that came before it.
    ///
    /// The migration is the load-bearing half. Every photo finished between the
    /// gradient masks landing and mask groups landing has the flat `maskKind`
    /// keys and no `maskComponents` — and `localExposureEv` kept its own name
    /// through the change, so dropping the mask would not open those photos
    /// unedited. It would open them with the local exposure applied to the
    /// *whole frame*, which looks like a working editor and is worse than a
    /// crash.
    static func testMaskGroupSidecar() {
        func decode(_ json: String) -> DevelopState? {
            guard let data = json.data(using: .utf8) else { return nil }
            return try? JSONDecoder().decode(DevelopState.self, from: data)
        }

        // ── A pre-group sidecar: one linear mask, lifted into one component ──
        let legacy = #"""
        {"exposureEv":2.6,"localExposureEv":-1.6,"maskKind":1,"maskCentreX":0.46,
         "maskCentreY":0.44,"maskAngle":1.05,"maskLength":0.55,"maskInvert":true}
        """#
        if let s = decode(legacy) {
            report(s.maskComponents.count == 1,
                   "a pre-group sidecar's single mask becomes one component",
                   "\(s.maskComponents.count) components")
            if let m = s.maskComponents.first {
                report(m.kind == 1, "and keeps its kind")
                near(CGFloat(m.centreX), 0.46, 1e-6, "and its centre x")
                near(CGFloat(m.centreY), 0.44, 1e-6, "and its centre y")
                near(CGFloat(m.angle), 1.05, 1e-6, "and its angle")
                near(CGFloat(m.length), 0.55, 1e-6, "and its length")
                report(m.invert, "and its invert")
                report(m.compose == 0,
                       "and folds with add, the only op a single mask can have meant")
            }
            near(CGFloat(s.layers.first?.exposureEv ?? 0), -1.6, 1e-6,
                 "and the local exposure it was applying through that mask")
        } else {
            report(false, "a pre-group sidecar decodes at all")
        }

        // ── ⚠ A pre-LAYER sidecar: the single local set becomes layer 1 ──────
        //
        // Every photograph edited before layers existed carries exactly one set
        // of local adjustments, under the scalar keys `localExposureEv`,
        // `localContrast`, `localSaturation`, `localWarmth` and `localTint`.
        // Those keys no longer exist as stored properties, so the synthesised
        // encoder never writes them again — and if the decoder had stopped
        // reading them too, every local grade ever made would have opened at
        // zero, silently, on a photograph that still had its mask.
        //
        // This is the same shape as the two migrations this file has already
        // paid for: `localExposureEv` keeping its name through the group
        // change, and `MaskComponentState` encoding three range fields it never
        // decoded.
        let legacyLocal = #"""
        {"exposureEv":1.0,"maskKind":2,"localExposureEv":-1.6,"localContrast":0.4,
         "localSaturation":-0.7,"localWarmth":0.25,"localTint":-0.15}
        """#
        if let s = decode(legacyLocal) {
            report(s.layers.count == 1,
                   "a pre-layer sidecar produces exactly one layer",
                   "\(s.layers.count)")
            if let l = s.layers.first {
                near(CGFloat(l.exposureEv), -1.6, 1e-6, "carrying its local exposure")
                near(CGFloat(l.contrast), 0.4, 1e-6, "its contrast")
                near(CGFloat(l.saturation), -0.7, 1e-6, "its saturation")
                near(CGFloat(l.warmth), 0.25, 1e-6, "its warmth")
                near(CGFloat(l.tint), -0.15, 1e-6, "and its tint")
            }
        } else {
            report(false, "a pre-layer sidecar decodes")
        }

        // ⚠ And a sidecar carrying **both** — which a newer build writes,
        // because the encoder is synthesised from the stored properties and the
        // legacy keys may still be present from a hand edit — must prefer the
        // layers. Preferring the scalars would silently discard layers 2 and up,
        // which is the exact failure the mask-group migration had.
        let bothForms = #"""
        {"localExposureEv":-1.6,
         "layers":[{"exposureEv":0.5,"contrast":0,"saturation":0,"warmth":0,"tint":0},
                   {"exposureEv":-2.0,"contrast":0,"saturation":0,"warmth":0,"tint":0}]}
        """#
        if let s = decode(bothForms) {
            report(s.layers.count == 2,
                   "a sidecar with both forms keeps every layer",
                   "\(s.layers.count)")
            near(CGFloat(s.layers.first?.exposureEv ?? 0), 0.5, 1e-6,
                 "and takes the layer list rather than the legacy scalar")
        } else {
            report(false, "a sidecar with both forms decodes")
        }

        // A pre-group brush, whose stroke lived beside the mask rather than in it.
        let legacyBrush = #"""
        {"maskKind":3,"brushRadius":0.07,"brushFlow":0.55,"brushHardness":0.45,
         "brushStroke":[0.2,0.66,0.34,0.6]}
        """#
        if let s = decode(legacyBrush), let m = s.maskComponents.first {
            report(m.kind == 3 && m.brushStroke.count == 4,
                   "a pre-group brush's stroke moves inside its component",
                   "\(m.brushStroke.count) values")
            near(CGFloat(m.brushRadius), 0.07, 1e-6, "and the nib comes with it")
        } else {
            report(false, "a pre-group brush sidecar decodes")
        }

        // ── maskKind 0 was "no mask", which is an empty group, not an off row ──
        //
        // A live component that happens to cover nothing is not the same thing:
        // the engine would run a pass for it and `mask_count` would be one.
        if let s = decode(#"{"maskKind":0,"maskCentreX":0.3}"#) {
            report(s.maskComponents.isEmpty,
                   "a pre-group sidecar with no mask decodes to an empty group",
                   "\(s.maskComponents.count) components")
        } else {
            report(false, "a pre-group sidecar with no mask decodes")
        }

        // ── A component list present wins over legacy keys ──────────────────
        //
        // A file holding both was written by a newer build, and its legacy keys
        // are whatever that build's first row happened to be. Preferring them
        // would silently discard rows two and up.
        let both = #"""
        {"maskKind":1,"maskCentreX":0.9,
         "maskComponents":[{"kind":2,"centreX":0.25},{"kind":3,"compose":1}]}
        """#
        if let s = decode(both) {
            report(s.maskComponents.count == 2,
                   "a list plus legacy keys keeps the list, both rows",
                   "\(s.maskComponents.count) components")
            if s.maskComponents.count == 2 {
                report(s.maskComponents[0].kind == 2 && s.maskComponents[1].kind == 3,
                       "in the order it was written")
                report(s.maskComponents[1].compose == 1,
                       "with the second row's subtract intact")
                near(CGFloat(s.maskComponents[0].centreX), 0.25, 1e-6,
                     "and the list's geometry, not the legacy key's")
            }
        } else {
            report(false, "a sidecar with both forms decodes")
        }

        // ── A component missing fields falls back per field ─────────────────
        if let s = decode(#"{"maskComponents":[{"kind":2}]}"#),
           let m = s.maskComponents.first {
            near(CGFloat(m.roundness), 2.0, 1e-6,
                 "a component's absent field is its default, not zero")
            near(CGFloat(m.brushFlow), 0.5, 1e-6, "for the nib too")
        } else {
            report(false, "a sparse component decodes")
        }

        // An off row is dropped on the way in. It cannot render anything, and
        // keeping it would put a row in the panel that does nothing.
        if let s = decode(#"{"maskComponents":[{"kind":1},{"kind":0},{"kind":3}]}"#) {
            report(s.maskComponents.count == 2,
                   "an off component is dropped rather than listed",
                   "\(s.maskComponents.count) components")
            report(s.maskComponents.last?.kind == 3, "and the rows after it survive")
        } else {
            report(false, "a list with an off component decodes")
        }

        // ── Round trip, with a full group ───────────────────────────────────
        var full = DevelopState()
        var a = MaskComponentState()
        a.kind = 1; a.centreX = 0.3; a.angle = 0.8; a.length = 0.7
        var b = MaskComponentState()
        b.kind = 3; b.compose = 1; b.brushStroke = [0.1, 0.2, 0.3, 0.4]
        var c = MaskComponentState()
        c.kind = 2; c.compose = 2; c.invert = true; c.roundness = 4
        full.maskComponents = [a, b, c]
        full.layers = [LocalAdjustState(exposureEv: 1.75)]
        if let data = try? JSONEncoder().encode(full),
           let back = try? JSONDecoder().decode(DevelopState.self, from: data) {
            report(back == full, "a three-component group round-trips unchanged")
            report(back.maskComponents.map(\.compose) == [0, 1, 2],
                   "with every op in place — the fold order is the edit")
        } else {
            report(false, "a group round-trips at all")
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

    /// A matte survives being written down and read back.
    ///
    /// ⚠ **The ramp is the test.** Every earlier matte fixture in this file is
    /// binary — a disc, a half-plane — and a binary matte survives a wrong
    /// colour space, a wrong bit depth and a wrong byte order, because 0 and 1
    /// land on 0 and 1 however the curve between them is mangled. Only the
    /// mid-values can tell, and mid-values are exactly what a feathered
    /// selection is made of.
    ///
    /// The specific defect this exists to catch: CoreGraphics colour-manages
    /// greyscale. Write 0.5 through a Gamma-2.2 grey space and read it back as
    /// linear and it comes back near 0.22 or 0.73 — every feathered edge on
    /// every reopened photograph shifted, nothing crashing, nothing to see
    /// unless it is measured. That is the purple cast's shape exactly.
    static func testMattePngRoundTripsItsMidTones() {
        let w = 64, h = 40
        var alpha = [Float](repeating: 0, count: w * h)
        for y in 0..<h {
            for x in 0..<w { alpha[y * w + x] = Float(x) / Float(w - 1) }
        }

        guard let png = MatteStore.encode(alpha, width: w, height: h) else {
            report(false, "a matte encodes to PNG at all")
            return
        }
        report(true, "a matte encodes to PNG at all")

        guard let back = MatteStore.decode(png) else {
            report(false, "the PNG decodes back")
            return
        }
        report(back.width == w && back.height == h,
               "the matte comes back the same size",
               "got \(back.width)x\(back.height)")

        // 1/65535 is one step of the stored precision; 2 steps is the honest
        // tolerance for a round trip through a rounding on each side.
        var worst: Float = 0
        for i in 0..<(w * h) { worst = max(worst, abs(back.alpha[i] - alpha[i])) }
        report(worst <= 3.0 / 65535.0,
               "every value comes back within a couple of steps of 16-bit",
               String(format: "worst error %.6f", worst))

        // Named separately, because the aggregate above could be passed by a
        // curve that is right at the ends and wrong in between if the frame
        // were mostly ends. These are the values a gamma error moves furthest.
        for (x, want) in [(0, 0.0), (16, 16.0 / 63.0), (32, 32.0 / 63.0),
                          (48, 48.0 / 63.0), (63, 1.0)] {
            let got = CGFloat(back.alpha[x])
            near(got, CGFloat(want), 1e-4,
                 String(format: "column %d comes back at %.4f", x, want))
        }

        // ⚠ A matte is not square and never has been: a transposed write reads
        // perfectly on a square fixture and scrambles every real photograph.
        // The ramp varies along x only, so a transpose makes every row equal.
        let rowsDiffer = (0..<h).contains { y in
            abs(back.alpha[y * w + 1] - back.alpha[y * w + 0]) > 1e-4
        }
        report(rowsDiffer, "the ramp still runs along x, so the raster is not transposed")
    }

    /// The raster comes back the way up it went in.
    ///
    /// ⚠ The ramp above cannot see this and it is worth saying why: it varies
    /// along x only, so a **vertical flip leaves it identical**. And a flip is a
    /// live risk here rather than a hypothetical — a `CGBitmapContext` has a
    /// bottom-left origin while both `CGImage` rows and the matte's own frame
    /// coordinates run top-down, so the decode draws across that boundary. A
    /// flipped matte is not a broken-looking mask; it is a plausible selection
    /// of the wrong half of the photograph.
    ///
    /// One bright corner is enough, and it pins both axes at once.
    static func testMatteKeepsItsOrientation() {
        let w = 16, h = 10
        var alpha = [Float](repeating: 0, count: w * h)
        for y in 0..<(h / 2) {
            for x in 0..<(w / 2) { alpha[y * w + x] = 1 }
        }

        guard let png = MatteStore.encode(alpha, width: w, height: h),
              let back = MatteStore.decode(png) else {
            report(false, "the corner fixture round-trips")
            return
        }
        near(CGFloat(back.alpha[0]), 1, 1e-4, "the top-left corner is still set")
        near(CGFloat(back.alpha[w - 1]), 0, 1e-4, "the top-right corner is still clear")
        near(CGFloat(back.alpha[(h - 1) * w]), 0, 1e-4,
             "the bottom-left corner is still clear — a vertical flip fails here")
        near(CGFloat(back.alpha[(h - 1) * w + w - 1]), 0, 1e-4,
             "the bottom-right corner is still clear")
    }

    /// Out-of-range input is clamped, not wrapped.
    ///
    /// The mask kernel saturates, so a producer handing over 1.2 means
    /// "covered". Converting that to UInt16 without clamping wraps it to near
    /// zero — a hole punched through the middle of a selection, in exactly the
    /// places the producer was most confident about.
    static func testMatteClampsRatherThanWraps() {
        let alpha: [Float] = [-0.5, 0, 0.5, 1, 1.5, 2]
        guard let png = MatteStore.encode(alpha, width: 6, height: 1),
              let back = MatteStore.decode(png) else {
            report(false, "an out-of-range matte still encodes")
            return
        }
        near(CGFloat(back.alpha[0]), 0, 1e-4, "-0.5 clamps to 0")
        near(CGFloat(back.alpha[2]), 0.5, 1e-4, "0.5 is untouched")
        near(CGFloat(back.alpha[4]), 1, 1e-4, "1.5 clamps to 1, not to near-zero")
        near(CGFloat(back.alpha[5]), 1, 1e-4, "2.0 clamps to 1")
    }

    /// The reference survives the sidecar, and the sweep keeps what is named.
    ///
    /// ⚠ `MaskComponentState` names its coding keys `Key` rather than
    /// `CodingKeys`, so Swift synthesises the *encoder* from the stored
    /// properties while the decoder reads the hand-written list. A field added
    /// to the struct is therefore written immediately and read back never —
    /// which is what happened to `rangeLo`/`rangeHi`/`rangeSoft` for five
    /// sessions. `matteId` failing that way would mean every saved matte was
    /// written to disk, referenced in the sidecar, and orphaned on the next
    /// open — swept away by this feature's own cleanup.
    static func testMatteReferenceSurvivesTheSidecar() {
        var state = DevelopState()
        var c = MaskComponentState()
        c.kind = 4
        c.matteId = "b3c1f0de-0000-4000-8000-000000000001"
        c.matteSource = "Sky"
        state.maskComponents = [c]

        guard let data = try? JSONEncoder().encode(state),
              let back = try? JSONDecoder().decode(DevelopState.self, from: data),
              let got = back.maskComponents.first else {
            report(false, "a state carrying a matte reference round-trips")
            return
        }
        report(got.matteId == c.matteId, "the matte id survives the sidecar",
               "got \(got.matteId ?? "nil")")
        report(got.matteSource == "Sky", "the matte's producer survives the sidecar",
               "got \(got.matteSource ?? "nil")")
        report(MatteStore.referenced(back.maskComponents) == [c.matteId!],
               "the sweep would keep exactly the file that is referenced")

        // A component with no matte contributes nothing to keep, or every
        // photograph would preserve an id-less entry forever.
        report(MatteStore.referenced([MaskComponentState()]).isEmpty,
               "a component with no matte references no file")
    }

    /// The filename is derived from the photograph, and lands beside its
    /// sidecar rather than inside some other photograph's namespace.
    static func testMatteFileSitsBesideTheSidecar() {
        let photo = URL(fileURLWithPath: "/pics/_PIC8095.ARW")
        let matte = MatteStore.url(photo: photo, id: "abc")
        let sidecar = Sidecar.url(for: photo)
        report(matte.deletingLastPathComponent() == sidecar.deletingLastPathComponent(),
               "a matte is written into the same folder as the sidecar")
        report(matte.lastPathComponent == "_PIC8095.orion-matte-abc.png",
               "the matte's name carries the photograph's basename",
               matte.lastPathComponent)
        // ⚠ Two raws of the same basename and different extensions are one
        // photograph's worth of names apart in Finder and must not collide —
        // they are separate photographs with separate edits.
        let other = MatteStore.url(photo: URL(fileURLWithPath: "/pics/_PIC8096.ARW"),
                                   id: "abc")
        report(matte != other, "two photographs do not share a matte file")
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

    /// The one that matters: the outline is drawn where the falloff actually
    /// is.
    ///
    /// `maskAlpha` is a transcription of `mask_gradient.slang`, so this asks
    /// the shader's own question rather than "did we draw an ellipse" — every
    /// point of the boundary curve must be exactly the end of the ramp, and
    /// every point of the inner curve exactly the start of it. A test that only
    /// checked the outline was closed and centred would pass on a plain screen
    /// circle, which is the wrong curve on every frame that is not square.
    static func testMaskOutlineLandsOnTheFalloff() {
        for roundness in [2.0, 4.0, 8.0] as [CGFloat] {
            for feather in [0.2, 0.5, 0.8] as [CGFloat] {
                for angle in [0.0, 0.4, 1.1, -0.9] as [CGFloat] {
                    var m = CanvasLayout.MaskPlacement()
                    m.kind = 2
                    m.centre = CGPoint(x: 0.42, y: 0.55)
                    m.radius = CGSize(width: 0.3, height: 0.18)
                    m.angle = angle
                    m.feather = feather
                    m.roundness = roundness

                    let tag = "r\(roundness) f\(feather) a\(angle)"

                    var worstEdge: CGFloat = 0
                    var worstInner: CGFloat = 0
                    for p in CanvasLayout.maskOutline(m, at: 1, samples: 64) {
                        worstEdge = max(worstEdge, abs(CanvasLayout.maskAlpha(p, m) - 0))
                    }
                    for p in CanvasLayout.maskOutline(m, at: 1 - feather, samples: 64) {
                        worstInner = max(worstInner, abs(CanvasLayout.maskAlpha(p, m) - 1))
                    }
                    near(worstEdge, 0, 1e-5, "outline is the end of the ramp (\(tag))")
                    near(worstInner, 0, 1e-5, "inner outline starts the ramp (\(tag))")

                    // And the boundary really is a boundary: a hair inside is
                    // covered, a hair outside is not.
                    let e = CanvasLayout.maskOutline(m, at: 0.98, samples: 16)
                    let o = CanvasLayout.maskOutline(m, at: 1.02, samples: 16)
                    report(e.allSatisfy { CanvasLayout.maskAlpha($0, m) > 0 },
                           "inside the outline is covered (\(tag))")
                    report(o.allSatisfy { CanvasLayout.maskAlpha($0, m) <= 1e-9 },
                           "outside the outline is not (\(tag))")
                }
            }
        }

        // A hard-edged mask has nothing to draw between the two curves, and the
        // overlay must not imply a feather it does not have.
        var hard = CanvasLayout.MaskPlacement()
        hard.kind = 2
        hard.feather = 0
        let outer = CanvasLayout.maskOutline(hard, at: 1, samples: 32)
        let inner = CanvasLayout.maskOutline(hard, at: 1, samples: 32)
        var gap: CGFloat = 0
        for (a, b) in zip(outer, inner) { gap = max(gap, hypot(a.x - b.x, a.y - b.y)) }
        near(gap, 0, 1e-9, "at feather 0 the two curves coincide")
    }

    /// A linear gradient's three lines are lines of constant coverage.
    ///
    /// Perpendicularity is measured in normalized coordinates, not on screen —
    /// so the check is that every point along the drawn line has the *same*
    /// ramp parameter, which is the property the shader defines and the one a
    /// screen-space right angle would break on any non-square frame.
    static func testMaskIsoLinesAreIsoAlpha() {
        for angle in [0.0, 0.3, 1.2, -0.7, 2.9] as [CGFloat] {
            for length in [0.2, 0.6, 1.4] as [CGFloat] {
                var m = CanvasLayout.MaskPlacement()
                m.kind = 1
                m.centre = CGPoint(x: 0.45, y: 0.52)
                m.angle = angle
                m.length = length

                for (t, want) in [(0.0, 0.0), (0.5, 0.5), (1.0, 1.0)] as [(CGFloat, CGFloat)] {
                    let (a, b) = CanvasLayout.maskIsoLine(m, at: t)
                    let tag = "t\(t) a\(angle) l\(length)"

                    // Both ends, and points along the way.
                    for s in [0.0, 0.25, 0.5, 0.75, 1.0] as [CGFloat] {
                        let q = CGPoint(x: a.x + (b.x - a.x) * s,
                                        y: a.y + (b.y - a.y) * s)
                        near(CanvasLayout.maskLinearT(q, m), want, 1e-6,
                             "iso line holds its parameter (\(tag), s\(s))")
                    }
                }

                // The zero and full lines pass through the two endpoints the
                // engine hands the shader.
                let (z0, z1) = CanvasLayout.maskIsoLine(m, at: 0)
                let d = hypot(z1.x - z0.x, z1.y - z0.y)
                report(d > 1, "the iso line is long enough to cross the frame")
                near(CanvasLayout.maskLinearT(m.zeroEnd, m), 0, 1e-6,
                     "zero endpoint is t = 0 (a\(angle))")
                near(CanvasLayout.maskLinearT(m.fullEnd, m), 1, 1e-6,
                     "full endpoint is t = 1 (a\(angle))")
            }
        }
    }

    /// Drag an endpoint and it lands under the cursor — exactly, not nearly.
    ///
    /// This is the property that says the angle is being taken in the space the
    /// handle is drawn from. Take `atan2` on screen instead and the handle
    /// redraws off the cursor by an amount that grows with how far the frame is
    /// from square, which reads as the control "slipping".
    static func testMaskEndpointLandsUnderTheCursor() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait, 1.0] as [CGFloat] {
            let v = Viewport()
            let map = CanvasLayout.pictureMap(
                quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                visible: v.visibleFraction(imageAspect: image, viewAspect: viewAspect),
                center: v.center, in: view)

            var m = CanvasLayout.MaskPlacement()
            m.kind = 1
            m.centre = CGPoint(x: 0.5, y: 0.5)

            let grab = CanvasLayout.maskHandlePoint(.fullEnd, m, map)
            for target in [CGPoint(x: 700, y: 430), CGPoint(x: 540, y: 330),
                           CGPoint(x: 660, y: 500)] {
                for handle in [CanvasLayout.MaskHandle.fullEnd, .zeroEnd] {
                    let moved = CanvasLayout.maskDrag(handle, from: grab, to: target,
                                                      start: m, map)
                    let where_ = CanvasLayout.maskHandlePoint(handle, moved, map)
                    near(where_.x, target.x, 1e-6,
                         "\(handle) lands on the cursor x (\(image))")
                    near(where_.y, target.y, 1e-6,
                         "\(handle) lands on the cursor y (\(image))")

                    // The centre does not move when an end is pulled.
                    near(moved.centre.x, m.centre.x, 1e-9, "endpoint drag keeps the centre x")
                    near(moved.centre.y, m.centre.y, 1e-9, "endpoint drag keeps the centre y")
                }
            }
        }
    }

    /// The rotate handle keeps a fixed screen distance and stays on the ray
    /// from the centre through the cursor. It cannot sit *on* the cursor —
    /// it is a lollipop at a fixed stem length — so the property to check is
    /// the direction, and on screen, which is where the hand is.
    static func testMaskRotateStaysOnTheCursorRay() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait] as [CGFloat] {
            let v = Viewport()
            let map = CanvasLayout.pictureMap(
                quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                visible: v.visibleFraction(imageAspect: image, viewAspect: viewAspect),
                center: v.center, in: view)

            var m = CanvasLayout.MaskPlacement()
            m.kind = 2
            m.centre = CGPoint(x: 0.5, y: 0.5)
            let origin = map.point(m.centre)
            let grab = CanvasLayout.maskHandlePoint(.rotate, m, map)

            for target in [CGPoint(x: 800, y: 300), CGPoint(x: 400, y: 620),
                           CGPoint(x: 620, y: 200), CGPoint(x: 300, y: 400)] {
                let moved = CanvasLayout.maskDrag(.rotate, from: grab, to: target,
                                                  start: m, map)
                let h = CanvasLayout.maskHandlePoint(.rotate, moved, map)

                let a = CGPoint(x: target.x - origin.x, y: target.y - origin.y)
                let b = CGPoint(x: h.x - origin.x, y: h.y - origin.y)
                let cross = a.x * b.y - a.y * b.x
                let dot = a.x * b.x + a.y * b.y
                let scale = max(hypot(a.x, a.y) * hypot(b.x, b.y), 1e-9)

                near(cross / scale, 0, 1e-6, "rotate handle is on the cursor ray (\(image))")
                report(dot > 0, "rotate handle is on the cursor's side (\(image))")

                // Fixed stem: the handle sits a constant distance beyond the
                // +X handle however the picture is shaped or turned.
                let edge = CanvasLayout.maskHandlePoint(.plusX, moved, map)
                near(hypot(h.x - edge.x, h.y - edge.y),
                     CanvasLayout.maskRotateStem, 1e-6,
                     "the stem keeps its length (\(image))")

                // Rotating changes nothing but the angle.
                near(moved.radius.width, m.radius.width, 1e-9, "rotate keeps radius x")
                near(moved.radius.height, m.radius.height, 1e-9, "rotate keeps radius y")
                near(moved.centre.x, m.centre.x, 1e-9, "rotate keeps the centre")
            }
        }
    }

    /// Dragging the body moves the mask by the distance the hand moved — on
    /// screen, which is the only place the photographer can judge it.
    static func testMaskBodyDragMovesByTheDrag() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait] as [CGFloat] {
            for zoom in [1.0, 3.0] as [CGFloat] {
                let v = Viewport()
                v.zoomBy(zoom, anchor: CGPoint(x: 0.5, y: 0.5),
                         visible: CGSize(width: 1, height: 1))
                let vis = v.visibleFraction(imageAspect: image, viewAspect: viewAspect)
                v.clamp(to: vis)
                let map = CanvasLayout.pictureMap(
                    quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                    visible: vis, center: v.center, in: view)

                var m = CanvasLayout.MaskPlacement()
                m.kind = 2
                m.centre = CGPoint(x: 0.5, y: 0.5)

                let grab = map.point(m.centre)
                let delta = CGSize(width: 37, height: -21)
                let to = CGPoint(x: grab.x + delta.width, y: grab.y + delta.height)

                for handle in [CanvasLayout.MaskHandle.body, .centre] {
                    let moved = CanvasLayout.maskDrag(handle, from: grab, to: to,
                                                      start: m, map)
                    let now = map.point(moved.centre)
                    near(now.x - grab.x, delta.width, 1e-6,
                         "\(handle) drag moves by the drag x (\(image), \(zoom)x)")
                    near(now.y - grab.y, delta.height, 1e-6,
                         "\(handle) drag moves by the drag y (\(image), \(zoom)x)")

                    near(moved.angle, m.angle, 1e-9, "moving does not rotate")
                    near(moved.radius.width, m.radius.width, 1e-9, "moving does not resize")
                }
            }
        }
    }

    /// Pulling a side changes that side and nothing else.
    ///
    /// The temptation is to let an axis handle set the angle too, the way a
    /// linear endpoint does. It must not: a photographer nudging a mask wider
    /// would find it had quietly turned, and the angle would drift a little on
    /// every size adjustment with nothing on screen explaining it.
    static func testMaskAxisDragDoesNotRotate() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height
        let v = Viewport()
        let map = CanvasLayout.pictureMap(
            quadScale: v.quadScale(imageAspect: landscape, viewAspect: viewAspect),
            visible: v.visibleFraction(imageAspect: landscape, viewAspect: viewAspect),
            center: v.center, in: view)

        for angle in [0.0, 0.6, -1.3] as [CGFloat] {
            var m = CanvasLayout.MaskPlacement()
            m.kind = 2
            m.centre = CGPoint(x: 0.5, y: 0.5)
            m.angle = angle

            for handle in [CanvasLayout.MaskHandle.plusX, .minusX, .plusY, .minusY] {
                let grab = CanvasLayout.maskHandlePoint(handle, m, map)
                // Deliberately off the axis, which is what a real hand does.
                let to = CGPoint(x: grab.x + 44, y: grab.y + 61)
                let moved = CanvasLayout.maskDrag(handle, from: grab, to: to,
                                                  start: m, map)

                near(moved.angle, angle, 1e-12, "\(handle) does not rotate (a\(angle))")
                near(moved.centre.x, m.centre.x, 1e-12, "\(handle) does not move x")
                near(moved.centre.y, m.centre.y, 1e-12, "\(handle) does not move y")

                let onX = (handle == .plusX || handle == .minusX)
                near(onX ? moved.radius.height : moved.radius.width,
                     onX ? m.radius.height : m.radius.width, 1e-12,
                     "\(handle) leaves the other axis alone")

                // Pulling outward from the edge grows the mask on that axis.
                let out = map.unit(to)
                let axis = onX ? m.axisX : m.axisY
                let reach = abs((out.x - m.centre.x) * axis.width
                              + (out.y - m.centre.y) * axis.height)
                near(onX ? moved.radius.width : moved.radius.height,
                     min(max(reach, 0.02), 1), 1e-9,
                     "\(handle) takes the drag's reach along its own axis")
            }
        }
    }

    /// A drag can never produce a mask the panel cannot show.
    ///
    /// The sliders and the canvas write the same variables. If a drag can leave
    /// a value outside a slider's range, the two disagree about the state and
    /// the next touch of that slider snaps the mask somewhere nobody put it.
    static func testMaskDragStaysInSliderRange() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height
        let v = Viewport()
        let map = CanvasLayout.pictureMap(
            quadScale: v.quadScale(imageAspect: landscape, viewAspect: viewAspect),
            visible: v.visibleFraction(imageAspect: landscape, viewAspect: viewAspect),
            center: v.center, in: view)

        // Far outside the view on every side, which is where a determined drag
        // ends up.
        let wild = [CGPoint(x: -4000, y: -3000), CGPoint(x: 9000, y: -2000),
                    CGPoint(x: 9000, y: 7000), CGPoint(x: -5000, y: 6000),
                    CGPoint(x: 600, y: 400)]

        for kind in [1, 2] {
            var m = CanvasLayout.MaskPlacement()
            m.kind = kind
            m.centre = CGPoint(x: 0.5, y: 0.5)

            for handle in CanvasLayout.maskHandles(m) {
                let grab = CanvasLayout.maskHandlePoint(handle, m, map)
                for to in wild {
                    let r = CanvasLayout.maskDrag(handle, from: grab, to: to,
                                                  start: m, map)
                    let tag = "\(handle) kind \(kind)"
                    report(CanvasLayout.maskCentreRange.contains(r.centre.x),
                           "centre x stays in range (\(tag))", "\(r.centre.x)")
                    report(CanvasLayout.maskCentreRange.contains(r.centre.y),
                           "centre y stays in range (\(tag))", "\(r.centre.y)")
                    report(CanvasLayout.maskLengthRange.contains(r.length),
                           "length stays in range (\(tag))", "\(r.length)")
                    report(CanvasLayout.maskRadiusRange.contains(r.radius.width),
                           "radius x stays in range (\(tag))", "\(r.radius.width)")
                    report(CanvasLayout.maskRadiusRange.contains(r.radius.height),
                           "radius y stays in range (\(tag))", "\(r.radius.height)")
                    report(abs(r.angle) <= 3.15,
                           "angle stays in the slider's range (\(tag))", "\(r.angle)")
                }
            }
        }
    }

    /// A handle standing inside the mask still gets the press.
    ///
    /// The body is the biggest target on screen and every handle sits on it, so
    /// testing the body first makes all of them unreachable. Among handles the
    /// nearest wins rather than a fixed order, because on a small mask their
    /// boxes overlap and a fixed order strands one of them.
    static func testMaskHitPrefersHandlesOverBody() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height
        let v = Viewport()
        let map = CanvasLayout.pictureMap(
            quadScale: v.quadScale(imageAspect: landscape, viewAspect: viewAspect),
            visible: v.visibleFraction(imageAspect: landscape, viewAspect: viewAspect),
            center: v.center, in: view)

        for kind in [1, 2] {
            var m = CanvasLayout.MaskPlacement()
            m.kind = kind
            m.centre = CGPoint(x: 0.5, y: 0.5)

            for handle in CanvasLayout.maskHandles(m) {
                let at = CanvasLayout.maskHandlePoint(handle, m, map)
                report(CanvasLayout.maskHit(at, m, map) == handle,
                       "pressing a handle grabs it (\(handle), kind \(kind))",
                       "got \(String(describing: CanvasLayout.maskHit(at, m, map)))")
            }

            // Inside the mask but clear of every handle: the body.
            let inside = kind == 2
                ? map.point(CGPoint(x: 0.5 + 0.12, y: 0.5 + 0.06))
                : map.point(CGPoint(x: 0.5, y: 0.5 + 0.12))
            if CanvasLayout.maskHandles(m).allSatisfy({
                hypot(CanvasLayout.maskHandlePoint($0, m, map).x - inside.x,
                      CanvasLayout.maskHandlePoint($0, m, map).y - inside.y)
                    > CanvasLayout.maskHandleBox / 2
            }) {
                report(CanvasLayout.maskHit(inside, m, map) == .body,
                       "pressing the mask body grabs the body (kind \(kind))")
            }

            // Well outside it: nothing, so the press falls through to panning.
            let outside = CGPoint(x: 40, y: 40)
            report(CanvasLayout.maskHit(outside, m, map) == nil,
                   "pressing away from the mask grabs nothing (kind \(kind))")
        }

        // With no mask there is nothing to grab anywhere.
        let none = CanvasLayout.MaskPlacement()
        report(CanvasLayout.maskHit(CGPoint(x: 600, y: 400), none, map) == nil,
               "no mask, no handles")
    }

    /// The stored angle is measured in normalized coordinates, and on a frame
    /// that is not square that is *not* the angle it subtends on screen.
    ///
    /// This is the check that catches someone deciding the map is "really" a
    /// rotation and dropping the aspect out of it — the same simplification
    /// `pipe/MaskGeometry.h` guards against on the straighten. It has to differ
    /// on 3:2 and it has to agree on a square, or the difference is a bug
    /// rather than the geometry.
    static func testMaskAnglesAreNormalizedNotScreen() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        func screenAngle(_ image: CGFloat, _ angle: CGFloat) -> CGFloat {
            let v = Viewport()
            let map = CanvasLayout.pictureMap(
                quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                visible: v.visibleFraction(imageAspect: image, viewAspect: viewAspect),
                center: v.center, in: view)
            var m = CanvasLayout.MaskPlacement()
            m.kind = 1
            m.centre = CGPoint(x: 0.5, y: 0.5)
            m.angle = angle
            let a = map.point(m.centre)
            let b = map.point(m.fullEnd)
            return atan2(b.y - a.y, b.x - a.x)
        }

        // A square *picture*: normalized coordinates are a unit square, so the
        // map is a uniform scale only when the rectangle drawn is square too,
        // and the two angles then coincide.
        //
        // ⚠️ Not when the picture merely fills the view. The first version of
        // this check passed the view's own aspect here, which is a 3:2 picture
        // drawn edge to edge — a perfectly anisotropic map — and it read
        // 0.5117 rad against the 0.7 it asserted. The claim was "square" and
        // the measurement was "fills the view"; they are different questions.
        near(screenAngle(1.0, 0.7), 0.7, 1e-6,
             "a square picture makes the two angles agree")

        // 3:2 does not. The gap is the aspect, and it must be a real one — a
        // tolerance-sized difference would mean this test could not tell the
        // two implementations apart.
        let got = screenAngle(landscape, 0.7)
        report(abs(got - 0.7) > 0.05,
               "a 3:2 picture separates the stored angle from the screen angle",
               String(format: "screen %.4f rad vs stored 0.7000", got))

        // On axis, though, they must agree whatever the aspect — a scale along
        // the axes cannot turn a horizontal line.
        for image in [landscape, portrait, 1.0] as [CGFloat] {
            near(screenAngle(image, 0), 0, 1e-9, "0 is 0 at any aspect (\(image))")
        }
    }

    // MARK: Brush strokes

    /// Dabs land a fixed distance apart along the stroke.
    ///
    /// The spacing is what decides whether a stroke reads as a line or as a
    /// string of beads, so it is checked as an actual distance between
    /// consecutive dabs rather than as a count — a count is satisfied by dabs
    /// bunched at one end.
    static func testBrushDabsAreEvenlySpaced() {
        for radius in [0.02, 0.08, 0.3] as [CGFloat] {
            var carry: CGFloat = 0
            let a = CGPoint(x: 0.1, y: 0.2), b = CGPoint(x: 0.9, y: 0.75)
            let dabs = CanvasLayout.brushDabs(from: a, to: b, radius: radius,
                                              carry: &carry)
            let step = radius * CanvasLayout.brushSpacing
            report(!dabs.isEmpty, "a long drag lays dabs (radius \(radius))")

            // Consecutive gaps, including from the segment's start.
            var prev = a
            var worst: CGFloat = 0
            for (i, d) in dabs.enumerated() {
                let gap = hypot(d.x - prev.x, d.y - prev.y)
                // The first gap is a full step because carry started at zero.
                worst = max(worst, abs(gap - step))
                prev = d
                _ = i
            }
            near(worst, 0, 1e-9, "dabs are one step apart (radius \(radius))")

            // Every dab is on the segment, not near it.
            var offLine: CGFloat = 0
            let dx = b.x - a.x, dy = b.y - a.y
            let len = hypot(dx, dy)
            for d in dabs {
                let cross = abs((d.x - a.x) * dy - (d.y - a.y) * dx) / len
                offLine = max(offLine, cross)
            }
            near(offLine, 0, 1e-9, "dabs lie on the drag (radius \(radius))")

            // A tighter brush must lay more paint over the same distance.
            report(dabs.count == Int(len / step),
                   "the dab count is the distance over the step (radius \(radius))",
                   "\(dabs.count) vs \(Int(len / step))")
        }

        // A drag that does not move lays nothing, or resting the pointer would
        // pile dabs on one spot and burn a hole through the flow ramp.
        var carry: CGFloat = 0
        let still = CanvasLayout.brushDabs(from: CGPoint(x: 0.5, y: 0.5),
                                           to: CGPoint(x: 0.5, y: 0.5),
                                           radius: 0.1, carry: &carry)
        report(still.isEmpty, "a stationary pointer lays no paint")
    }

    /// The same stroke drawn with different event rates must lay the same dabs.
    ///
    /// This is the one that matters and the one a naive implementation fails: a
    /// pointer reports a handful of positions a second, so a fast hand jumps a
    /// long way between two events. Stamping once per event draws a dotted
    /// line, and restarting the spacing at every event clusters dabs wherever
    /// the hand slowed down — which is exactly at the corners of a gesture,
    /// where the extra paint is most visible.
    ///
    /// `carry` is what makes the two agree, so the test feeds one straight line
    /// through a coarse event stream and a fine one and demands the same dabs.
    static func testBrushSpacingSurvivesTheEventRate() {
        let a = CGPoint(x: 0.05, y: 0.5), b = CGPoint(x: 0.95, y: 0.5)
        let radius: CGFloat = 0.05

        func walk(steps: Int) -> [CGPoint] {
            var carry: CGFloat = 0
            var out: [CGPoint] = []
            var prev = a
            for i in 1...steps {
                let f = CGFloat(i) / CGFloat(steps)
                let p = CGPoint(x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f)
                out += CanvasLayout.brushDabs(from: prev, to: p, radius: radius,
                                              carry: &carry)
                prev = p
            }
            return out
        }

        let coarse = walk(steps: 3)     // a fast hand
        let fine   = walk(steps: 60)    // a slow one over the same path

        report(coarse.count == fine.count,
               "event rate does not change how much paint a stroke lays",
               "\(coarse.count) dabs vs \(fine.count)")

        if coarse.count == fine.count {
            var worst: CGFloat = 0
            for (p, q) in zip(coarse, fine) {
                worst = max(worst, hypot(p.x - q.x, p.y - q.y))
            }
            near(worst, 0, 1e-9, "and the dabs land in the same places")
        }

        // And the spacing holds across the event boundaries, which is the
        // property `carry` exists for. Measured over the coarse stream, where
        // the boundaries are furthest apart.
        var prev = a
        var worstGap: CGFloat = 0
        for d in coarse {
            worstGap = max(worstGap, abs(hypot(d.x - prev.x, d.y - prev.y)
                                         - radius * CanvasLayout.brushSpacing))
            prev = d
        }
        near(worstGap, 0, 1e-9, "spacing is continuous across events")
    }

    // MARK: Matte geometry — research/masking.md §5

    /// Undoing the turns has to be an exact permutation, and its own inverse
    /// under negation. Anything that resampled, dropped a row or transposed
    /// without reflecting would break one of these.
    static func testMatteTurnsRoundTrip() {
        // Deliberately non-square and with every value distinct, so a
        // transposition cannot hide behind symmetry. 4 wide, 3 tall.
        let w = 4, h = 3
        let src = (0..<(w * h)).map { Float($0) }

        for k in [0, 1, 2, 3, -1, -2, 5] {
            let r = MatteGeometry.undoTurns(src, width: w, height: h, turns: k)
            let odd = (((k % 4) + 4) % 4) % 2 != 0
            report(r.width == (odd ? h : w) && r.height == (odd ? w : h),
                   "turn \(k) swaps the dimensions exactly when it is odd",
                   "\(r.width)x\(r.height)")
            report(r.pixels.count == w * h && Set(r.pixels) == Set(src),
                   "turn \(k) is a permutation — every value survives, none is invented")

            // Turning back must give the original array, element for element.
            let back = MatteGeometry.undoTurns(r.pixels, width: r.width,
                                               height: r.height, turns: -k)
            report(back.pixels == src && back.width == w && back.height == h,
                   "and undoing turn \(k) restores the raster exactly")
        }
    }

    /// ⚠ The check that matters, and the reason this is not just a rotate
    /// helper: the raster turn must agree with the *point* transform the
    /// parametric masks already use.
    ///
    /// `mask::toFrame` in the engine is derived from "a quarter turn clockwise
    /// sends a frame point (x, y) to (1 - y, x) on screen". If the raster went
    /// the other way, a matte and a gradient placed on the same subject would
    /// land on opposite sides of the picture — each self-consistent, and the
    /// disagreement only visible with both on screen at once.
    static func testMatteTurnAgreesWithTheMaskTransform() {
        let w = 8, h = 5

        // One lit pixel, off-centre in both axes so no symmetry can rescue a
        // wrong answer.
        let sx = 6, sy = 1
        var src = [Float](repeating: 0, count: w * h)
        src[sy * w + sx] = 1

        for k in 1...3 {
            let r = MatteGeometry.undoTurns(src, width: w, height: h, turns: k)

            // Where the point transform says that pixel came from. Screen
            // coordinates normalized, run back through the same map
            // `mask::toFrame` applies: (x, y) -> (y, 1 - x), k times.
            var u = (Double(sx) + 0.5) / Double(w)
            var v = (Double(sy) + 0.5) / Double(h)
            for _ in 0..<k { let nu = v; let nv = 1 - u; u = nu; v = nv }

            let fx = Int(u * Double(r.width))
            let fy = Int(v * Double(r.height))
            let hit = r.pixels[fy * r.width + fx]
            report(hit == 1,
                   "the raster turn \(k) puts the pixel where mask::toFrame says it goes",
                   "expected 1 at \(fx),\(fy) of \(r.width)x\(r.height), got \(hit)")
        }
    }

    static func testMattePreviewSize() {
        // Capped on the long edge, aspect preserved.
        let a = MatteGeometry.previewSize(frameWidth: 6024, frameHeight: 4024,
                                          longEdge: 1024)
        report(a.width == 1024 && abs(a.height - 684) <= 1,
               "a landscape frame is capped on its long edge", "\(a.width)x\(a.height)")

        let b = MatteGeometry.previewSize(frameWidth: 4024, frameHeight: 6024,
                                          longEdge: 1024)
        report(b.height == 1024 && abs(b.width - 684) <= 1,
               "and a portrait one on its own long edge", "\(b.width)x\(b.height)")

        // ⚠ Never upscales. Handing a model more pixels than the photograph has
        // is inventing detail for it to segment.
        let c = MatteGeometry.previewSize(frameWidth: 640, frameHeight: 480,
                                          longEdge: 1024)
        report(c.width == 640 && c.height == 480,
               "a frame under the cap is passed at its own size", "\(c.width)x\(c.height)")
    }

    // MARK: Presets

    /// A photograph with something set in every group, so any field a preset
    /// wrongly copies shows up as a change.
    /// A state with **every** field set to a non-default value.
    ///
    /// ⚠ Exhaustive on purpose, and it was not at first: `maskRefine` was left
    /// at its default, so a mutation deleting its line from the decoder changed
    /// nothing and survived. A round-trip test is only as good as the state it
    /// round-trips — a field this function forgets is a field the suite cannot
    /// see. Anything added to `DevelopState` belongs here the same day.
    static func busyState() -> DevelopState {
        var s = DevelopState()
        s.temperatureK = 4200; s.tint = 0.3
        s.exposureEv = 1.1; s.contrast = 1.7
        s.highlights = -0.4; s.shadows = 0.35; s.whites = 0.2; s.blacks = -0.15
        s.highlightRecovery = 0.65
        s.vibrance = 0.4; s.saturation = -0.2
        s.hueShift[3] = 0.5; s.satShift[5] = -0.3; s.lumShift[1] = 0.25
        s.gradeShadow = [0.1, -0.2, 0.05]
        s.gradeMidtone = [-0.05, 0.15, 0.02]
        s.gradeHighlight = [0.2, 0.03, -0.12]
        s.sharpenAmount = 1.3; s.sharpenRadius = 2.2; s.sharpenMasking = 0.45
        s.denoiseLuma = 2.0; s.denoiseColor = 1.4
        s.lensDistortion = 0.4; s.lensVignette = -0.35
        s.lensCaRed = 0.22; s.lensCaBlue = -0.18
        s.clarity = 0.6; s.dehaze = 0.3; s.fusion = 0.7; s.lutStrength = 0.5

        var curve = ToneCurve()
        curve.master = [CurvePoint(x: 0, y: 0.05),
                        CurvePoint(x: 0.5, y: 0.42),
                        CurvePoint(x: 1, y: 0.97)]
        curve.red = [CurvePoint(x: 0, y: 0), CurvePoint(x: 0.6, y: 0.7),
                     CurvePoint(x: 1, y: 1)]
        s.curve = curve

        s.rotateQuarters = 1; s.straightenDeg = 4
        s.cropX = 0.1; s.cropY = 0.2; s.cropW = 0.7; s.cropH = 0.6

        var spot = SpotState()
        spot.destX = 0.31; spot.destY = 0.62
        spot.srcX = 0.44; spot.srcY = 0.58
        spot.radius = 0.035; spot.feather = 0.4; spot.heal = false
        s.spots = [spot]

        // ⚠ kind 1, not the default. A kind-0 component is "off" and the
        // decoder drops it on purpose, so a default-constructed one would make
        // this state fail a round trip for a reason that is not a bug.
        var m = MaskComponentState()
        m.kind = 1; m.compose = 2; m.invert = true; m.hidden = true
        m.startsLayer = true
        m.centreX = 0.4; m.centreY = 0.7; m.angle = 0.9; m.length = 0.33
        m.radiusX = 0.21; m.radiusY = 0.44; m.feather = 0.66; m.roundness = 3.5
        m.brushRadius = 0.05; m.brushFlow = 0.8; m.brushHardness = 0.15
        m.brushStroke = [0.1, 0.2, 0.3, 0.4]; m.brushErase = [0, 1]
        // ⚠ The range and colour fields too, and their absence here is what let
        // `rangeLo`, `rangeHi` and `rangeSoft` be written to every sidecar and
        // read back from none for five sessions. `DevelopState`'s own fixture
        // was made exhaustive in 2026-07-30e; the *nested* component's was not,
        // so the guard could not see the fields it was guarding.
        m.rangeLo = -3.25; m.rangeHi = 1.75; m.rangeSoft = 0.8
        m.colourR = 0.42; m.colourG = 0.11; m.colourB = 0.27
        m.colourTol = 0.19; m.colourSoft = 0.07
        s.maskComponents = [m]
        s.maskRefine = 0.72
        // ⚠ Two layers, not one: a fixture with a single layer cannot see a
        // round trip that drops every layer after the first.
        s.layers = [LocalAdjustState(exposureEv: 1.5, contrast: 0.42,
                                     saturation: -0.33, warmth: 0.27, tint: -0.19),
                    LocalAdjustState(exposureEv: -0.8, contrast: -0.2,
                                     saturation: 0.66, warmth: -0.4, tint: 0.31)]
        return s
    }

    /// ⚠ The property the whole design rests on: a preset touches its groups
    /// and *nothing else*. Checked one group at a time, because a preset that
    /// assigned the whole state would pass any test that only enabled all of
    /// them at once.
    static func testPresetIsAPatch() {
        let base = busyState()

        // A preset carrying defaults everywhere. Applying it with one group
        // enabled must move exactly that group's fields to the default and
        // leave every other field of `base` untouched.
        for group in PresetGroup.allCases {
            let p = Preset(name: "t", groups: [group], state: DevelopState())
            let out = p.applied(to: base)

            // Pick one witness field from each *other* group and demand it
            // survived. Fields, not the whole struct, so the failure message
            // says which group leaked.
            if group != .light {
                report(out.exposureEv == base.exposureEv,
                       "\(group.rawValue) leaves Light alone", "\(out.exposureEv)")
            }
            if group != .colour {
                report(out.vibrance == base.vibrance && out.hueShift == base.hueShift,
                       "\(group.rawValue) leaves Color alone", "\(out.vibrance)")
            }
            if group != .whiteBalance {
                report(out.temperatureK == base.temperatureK,
                       "\(group.rawValue) leaves White Balance alone",
                       "\(out.temperatureK)")
            }
            if group != .detail {
                report(out.sharpenAmount == base.sharpenAmount
                       && out.denoiseLuma == base.denoiseLuma,
                       "\(group.rawValue) leaves Detail alone", "\(out.sharpenAmount)")
            }
            if group != .effects {
                report(out.clarity == base.clarity && out.dehaze == base.dehaze,
                       "\(group.rawValue) leaves Effects alone", "\(out.clarity)")
            }
        }

        // And the group it *does* name is actually applied — the checks above
        // are all satisfied by a preset that does nothing whatever.
        let light = Preset(name: "t", groups: [.light], state: DevelopState())
        let out = light.applied(to: base)
        report(out.exposureEv == 0 && out.contrast == DevelopState().contrast,
               "and the group it names is applied",
               "\(out.exposureEv), \(out.contrast)")
    }

    /// ⚠ Geometry, dust and masks are never carried, under any group — not even
    /// all of them at once. A preset that reframed every photograph it touched
    /// would be unusable, and this is the check that says it cannot.
    static func testPresetNeverCarriesTheFrame() {
        let base = busyState()

        var look = DevelopState()
        look.rotateQuarters = 3
        look.straightenDeg = -9
        look.cropX = 0.4; look.cropY = 0.4; look.cropW = 0.2; look.cropH = 0.2
        look.spots = [SpotState(), SpotState()]
        look.maskComponents = [MaskComponentState(), MaskComponentState()]
        look.maskRefine = 0.9
        look.layers = [LocalAdjustState(exposureEv: -2)]

        let all = Preset(name: "everything", groups: Set(PresetGroup.allCases),
                         state: look)
        let out = all.applied(to: base)

        report(out.rotateQuarters == base.rotateQuarters
               && out.straightenDeg == base.straightenDeg,
               "no group carries the rotation or the straighten",
               "\(out.rotateQuarters), \(out.straightenDeg)")
        report(out.cropX == base.cropX && out.cropW == base.cropW,
               "nor the crop", "\(out.cropX), \(out.cropW)")
        report(out.spots == base.spots, "nor the dust spots",
               "\(out.spots.count) vs \(base.spots.count)")
        report(out.maskComponents == base.maskComponents
               && out.maskRefine == base.maskRefine
               && out.layers == base.layers,
               "nor the masks and their local adjustment",
               "\(out.maskComponents.count) vs \(base.maskComponents.count)")

        // Applying a preset twice is the same as applying it once — it is a
        // patch, so it has to be idempotent or a double click would compound.
        report(all.applied(to: out) == out, "and applying it twice changes nothing")
    }

    static func testPresetStoreRoundTrip() {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-presets-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let file = dir.appendingPathComponent("presets.json")
        defer { try? FileManager.default.removeItem(at: dir) }

        let store = PresetStore(url: file)
        let builtInCount = store.presets.count
        report(builtInCount > 0, "the built-in looks are present on a fresh install")

        var s = DevelopState()
        s.contrast = 1.9
        report(store.add(name: "Mine", groups: [.light], state: s),
               "a preset can be saved")

        // Saving the same name twice replaces rather than appends.
        s.contrast = 1.3
        store.add(name: "Mine", groups: [.light], state: s)
        report(store.presets.filter { $0.name == "Mine" }.count == 1,
               "and saving it again replaces it rather than piling up",
               "\(store.presets.filter { $0.name == "Mine" }.count)")

        let reopened = PresetStore(url: file)
        let mine = reopened.presets.first { $0.name == "Mine" }
        report(mine?.state.contrast == 1.3 && mine?.groups == [.light],
               "and it survives a reopen with its groups",
               "\(String(describing: mine?.state.contrast))")

        // ⚠ Built-ins are not written to disk, so improving one in a later
        // release reaches everybody rather than only new installs.
        let raw = (try? String(contentsOf: file, encoding: .utf8)) ?? ""
        report(!raw.contains("Monochrome"),
               "built-ins are not copied into the user's file")
        report(reopened.presets.count == builtInCount + 1,
               "and are not duplicated on reload",
               "\(reopened.presets.count) vs \(builtInCount + 1)")

        // A built-in cannot be deleted.
        if let builtIn = reopened.presets.first(where: { $0.builtIn }) {
            reopened.remove(builtIn)
            report(reopened.presets.contains(where: { $0.id == builtIn.id }),
                   "a built-in cannot be removed")
        }

        // An empty name or no groups is refused rather than saved as junk.
        report(!reopened.add(name: "   ", groups: [.light], state: s),
               "an empty name is refused")
        report(!reopened.add(name: "Nothing", groups: [], state: s),
               "and so is a preset that would change nothing")
    }

    // MARK: Copy, paste and sync

    /// ⚠ The check this pair of files exists to have.
    ///
    /// `Preset.applied(to:)` patches a *struct*; `SyncSettings.keys(for:)`
    /// patches its *JSON*. They are the same decision written twice — once
    /// against fields, once against key names — because sync must not decode a
    /// sidecar into a struct (see the note in SyncSettings). Two hand-written
    /// lists drift the first time someone adds a field to one of them.
    ///
    /// So: apply a group both ways to the same state and demand the results
    /// agree, field for field, for every group.
    static func testSyncKeysMatchTheStructPatch() {
        let base = busyState()
        var source = DevelopState()
        // Something different in every field a group can carry, so a key
        // missing from the list shows up as a field that did not move.
        source.temperatureK = 7100; source.tint = -0.44
        source.exposureEv = -0.9; source.contrast = 1.11; source.highlights = 0.6
        source.shadows = -0.5; source.whites = -0.3; source.blacks = 0.22
        source.highlightRecovery = 0.7
        source.vibrance = -0.6; source.saturation = 0.8
        source.hueShift[2] = -0.7; source.satShift[0] = 0.9; source.lumShift[7] = -0.4
        source.gradeShadow = [-0.3, 0.2, -0.1]
        source.gradeMidtone = [0.05, 0.05, 0.05]
        source.gradeHighlight = [0.2, -0.2, 0.1]
        source.curve = ToneCurve()
        source.sharpenAmount = 1.9; source.sharpenRadius = 2.4; source.sharpenMasking = 0.8
        source.denoiseLuma = 3.1; source.denoiseColor = 0.9
        source.lensDistortion = -0.6; source.lensVignette = 0.5
        source.lensCaRed = 0.3; source.lensCaBlue = -0.3
        source.clarity = -0.8; source.dehaze = 0.9; source.fusion = 0.4
        source.lutStrength = 0.25

        for group in PresetGroup.allCases {
            let viaStruct = SyncSettings.pasted(source: source, onto: base,
                                                groups: [group])

            // The same patch through the JSON path, then decoded back so the
            // two can be compared as states.
            guard let storedBase = try? JSONEncoder().encode(base),
                  let patched = SyncSettings.patched(stored: storedBase,
                                                     source: source,
                                                     groups: [group]),
                  let viaJson = try? JSONDecoder().decode(DevelopState.self,
                                                          from: patched)
            else {
                report(false, "\(group.rawValue) round-trips through JSON")
                continue
            }

            report(viaJson == viaStruct,
                   "\(group.rawValue): the JSON key list and the struct patch agree")
        }
    }

    /// ⚠ A photograph with no sidecar must keep its as-shot white balance.
    ///
    /// Its white balance is whatever the camera recorded and is only known once
    /// the file is decoded, so it is *absent* from storage rather than stored.
    /// Decode the sidecar into a `DevelopState` and the missing keys come back
    /// as the struct's defaults — 5500 K — and writing that back would
    /// rewhite-balance every untouched photograph in a selection to a number
    /// nobody chose. This is why sync patches keys and not structs.
    static func testSyncLeavesUnknownWhiteBalanceAlone() {
        var source = DevelopState()
        source.temperatureK = 8000
        source.tint = 0.5
        source.clarity = 0.75

        // No sidecar at all, and a paste that does not include White Balance.
        guard let out = SyncSettings.patched(stored: nil, source: source,
                                             groups: [.effects]),
              let obj = try? JSONSerialization.jsonObject(with: out) as? [String: Any]
        else {
            report(false, "a patch onto a photo with no sidecar produces JSON")
            return
        }

        report(obj["temperatureK"] == nil && obj["tint"] == nil,
               "no white balance is written to a photo that never had one",
               "keys: \(obj.keys.sorted().joined(separator: ", "))")
        report((obj["clarity"] as? Double).map { abs($0 - 0.75) < 1e-6 } ?? false,
               "and the group that was pasted is written",
               "\(String(describing: obj["clarity"]))")

        // With White Balance selected it *is* written — the photographer asked.
        guard let withWb = SyncSettings.patched(stored: nil, source: source,
                                                groups: [.effects, .whiteBalance]),
              let wbObj = try? JSONSerialization.jsonObject(with: withWb)
                as? [String: Any]
        else {
            report(false, "a white-balance patch produces JSON")
            return
        }
        report((wbObj["temperatureK"] as? Double).map { abs($0 - 8000) < 1e-6 } ?? false,
               "but it is written when the paste asks for it",
               "\(String(describing: wbObj["temperatureK"]))")
    }

    /// A target that already has settings keeps the ones the paste does not
    /// name — the same patch property as a preset, at the storage layer.
    static func testSyncPatchesOnlyItsGroups() {
        var target = DevelopState()
        target.exposureEv = 2.2
        target.clarity = -0.5
        target.cropX = 0.3; target.cropW = 0.4
        target.spots = [SpotState()]

        var source = DevelopState()
        source.exposureEv = -1.0
        source.clarity = 0.9
        source.cropX = 0.9; source.cropW = 0.05

        guard let stored = try? JSONEncoder().encode(target),
              let patched = SyncSettings.patched(stored: stored, source: source,
                                                 groups: [.effects]),
              let out = try? JSONDecoder().decode(DevelopState.self, from: patched)
        else {
            report(false, "the patch round-trips")
            return
        }

        report(out.clarity == source.clarity, "sync writes the group it names",
               "\(out.clarity)")
        report(out.exposureEv == target.exposureEv,
               "and leaves the groups it does not", "\(out.exposureEv)")

        // ⚠ And never the frame or the dust, whatever is selected.
        guard let everything = SyncSettings.patched(
                stored: stored, source: source,
                groups: Set(PresetGroup.allCases)),
              let all = try? JSONDecoder().decode(DevelopState.self, from: everything)
        else {
            report(false, "an all-groups patch round-trips")
            return
        }
        report(all.cropX == target.cropX && all.cropW == target.cropW,
               "no group syncs the crop", "\(all.cropX), \(all.cropW)")
        report(all.spots == target.spots, "nor the dust spots",
               "\(all.spots.count)")

        // The count sync reports is the count it wrote.
        var wrote: [URL: Data] = [:]
        let urls = (0..<3).map { URL(fileURLWithPath: "/tmp/orion-sync-\($0).ARW") }
        let n = SyncSettings.sync(source: source, groups: [.light], to: urls,
                                  read: { _ in nil },
                                  write: { url, data in wrote[url] = data })
        report(n == 3 && wrote.count == 3, "sync reports what it wrote", "\(n)")
    }

    /// ⚠ Every field of a fully-set state survives a sidecar round trip.
    ///
    /// This exists because two did not. `DevelopState` synthesises its
    /// *encoder* from the stored properties and hand-writes its *decoder*
    /// against a `Key` list — so a field added to the struct is written to
    /// every sidecar and never read back. `spots` and `maskRefine` were both
    /// in that state for two sessions: dust removal and guided feathering were
    /// saved faithfully and silently gone on reopen.
    ///
    /// The general check is the point. Testing the two that were broken would
    /// pin today's bug; this pins the shape of it, and the next field to be
    /// added fails here rather than in someone's photographs.
    static func testEveryFieldSurvivesTheSidecar() {
        let original = busyState()
        guard let data = try? JSONEncoder().encode(original),
              let back = try? JSONDecoder().decode(DevelopState.self, from: data)
        else {
            report(false, "a busy state encodes and decodes")
            return
        }

        report(back == original,
               "every field of a fully-set state survives the sidecar")

        // Named separately so a failure says which one, rather than only that
        // two structs differ.
        report(back.spots == original.spots, "the dust spots survive",
               "\(back.spots.count) of \(original.spots.count)")
        report(back.maskRefine == original.maskRefine,
               "the mask refinement survives",
               "\(back.maskRefine) vs \(original.maskRefine)")
        report(back.maskComponents == original.maskComponents,
               "the mask group survives",
               "\(back.maskComponents.count) of \(original.maskComponents.count)")
    }

    // MARK: Batch export

    /// ⚠ Nothing is overwritten, and two sources never collide.
    ///
    /// Export is the one operation here that writes files a photographer may
    /// already have, and a batch is where both ways of losing one live: a
    /// target already on disk, and two sources from different folders sharing a
    /// basename.
    static func testBatchNeverOverwrites() {
        let out = URL(fileURLWithPath: "/out")

        // Two different folders, same basename. Nothing on disk yet.
        let sources = [URL(fileURLWithPath: "/a/IMG_0001.ARW"),
                       URL(fileURLWithPath: "/b/IMG_0001.ARW"),
                       URL(fileURLWithPath: "/c/IMG_0002.ARW")]
        let jobs = BatchExport.plan(sources: sources, into: out, extension: "jpg",
                                    exists: { _ in false })

        report(jobs.count == 3, "every source gets a job", "\(jobs.count)")
        report(jobs[0].destination.lastPathComponent == "IMG_0001.jpg",
               "the first keeps its name", jobs[0].destination.lastPathComponent)
        report(jobs[1].destination.lastPathComponent == "IMG_0001-2.jpg",
               "the second is numbered rather than overwriting the first",
               jobs[1].destination.lastPathComponent)
        report(jobs[2].destination.lastPathComponent == "IMG_0002.jpg",
               "and an unrelated name is untouched",
               jobs[2].destination.lastPathComponent)

        // Every destination distinct — the property the numbering exists for,
        // checked directly rather than inferred from the three names above.
        report(Set(jobs.map(\.destination)).count == jobs.count,
               "no two jobs share a destination")

        // Now with something already on disk.
        let onDisk: Set<String> = ["/out/IMG_0001.jpg", "/out/IMG_0001-2.jpg"]
        let jobs2 = BatchExport.plan(sources: [sources[0]], into: out,
                                     extension: "jpg",
                                     exists: { onDisk.contains($0.path) })
        report(jobs2[0].destination.lastPathComponent == "IMG_0001-3.jpg",
               "an existing file is stepped over, not written through",
               jobs2[0].destination.lastPathComponent)

        // ⚠ And the two rules compose: one source collides with disk, the next
        // with the first source's *new* name.
        let jobs3 = BatchExport.plan(sources: [sources[0], sources[1]], into: out,
                                     extension: "jpg",
                                     exists: { $0.path == "/out/IMG_0001.jpg" })
        report(jobs3[0].destination.lastPathComponent == "IMG_0001-2.jpg"
               && jobs3[1].destination.lastPathComponent == "IMG_0001-3.jpg",
               "the in-batch and on-disk rules compose",
               jobs3.map(\.destination.lastPathComponent).joined(separator: ", "))
    }

    /// ⚠ One bad file does not abandon the rest, and cancelling stops promptly.
    ///
    /// A folder is likely to contain something the decoder cannot read, and
    /// losing forty good photographs to the eleventh being a stray PNG is not
    /// what anybody wants.
    static func testBatchKeepsGoingAfterAFailure() {
        struct Boom: LocalizedError { var errorDescription: String? { "no" } }

        let jobs = (1...5).map {
            BatchExport.Job(source: URL(fileURLWithPath: "/in/\($0).ARW"),
                            destination: URL(fileURLWithPath: "/out/\($0).jpg"))
        }

        var opened: [String] = []
        let outcome = BatchExport.run(
            jobs,
            openAndRestore: { url in
                opened.append(url.lastPathComponent)
                if url.lastPathComponent == "3.ARW" { throw Boom() }
            },
            exportTo: { _ in })

        report(outcome.written.count == 4 && outcome.failed.count == 1,
               "a failure is collected and the batch continues",
               "\(outcome.written.count) written, \(outcome.failed.count) failed")
        report(opened.count == 5, "every source was still attempted",
               "\(opened.count)")
        report(outcome.failed.first?.0.lastPathComponent == "3.ARW",
               "and the one that failed is named",
               outcome.failed.first?.0.lastPathComponent ?? "none")
        report(outcome.summary.contains("4") && outcome.summary.contains("1 failed"),
               "the summary says both numbers", outcome.summary)

        // Cancelling: stops before the next photograph, and says it stopped.
        var done = 0
        let stopped = BatchExport.run(
            jobs,
            openAndRestore: { _ in done += 1 },
            exportTo: { _ in },
            isCancelled: { done >= 2 })
        report(stopped.written.count == 2 && stopped.cancelled,
               "cancelling stops the batch and is reported",
               "\(stopped.written.count) written, cancelled \(stopped.cancelled)")
        report(stopped.summary.contains("stopped early"),
               "and the summary says so", stopped.summary)

        // Progress is reported once per photograph plus a final call, so a bar
        // reaches its end rather than stopping one short.
        var seen: [Int] = []
        _ = BatchExport.run(jobs, openAndRestore: { _ in }, exportTo: { _ in },
                            progress: { i, _ in seen.append(i) })
        report(seen.first == 0 && seen.last == jobs.count,
               "progress starts at zero and reaches the total",
               "\(seen)")
    }
}

// MARK: - Filmstrip multi-selection
//
// `PhotoSelection` decides what every batch operation acts on — a sync that
// rewrites forty sidecars, an export that writes forty files. Neither is
// undoable across photographs, so "which forty" is the load-bearing question
// and it is pure logic, which means it can be pinned exactly rather than looked
// at in a strip.

extension ViewportTests {

    /// Five frames, as the filmstrip would list them.
    static var strip: [URL] {
        (1...5).map { URL(fileURLWithPath: "/photos/\($0).ARW") }
    }

    /// ⚠ The rule the whole feature rests on: the photograph on the canvas is
    /// always in `chosen`, so a selection of *one* is the resting state of the
    /// interface and not a decision. Until there are two, a batch means
    /// everything in view.
    ///
    /// Get this wrong in the obvious direction — "act on the selection whenever
    /// it is non-empty" — and Export All silently exports one photograph, for
    /// every user, on every folder. It would look like the button being broken.
    static func testOnePhotoIsNotASelection() {
        let s = strip
        var sel = PhotoSelection()

        sel.focus(s[2])
        report(sel.count == 1, "the open photo is in the selection", "\(sel.count)")
        report(!sel.isExplicit, "one photo is not an explicit selection")
        report(sel.targets(in: s) == s,
               "so a batch acts on everything in view",
               "\(sel.targets(in: s).count) of \(s.count)")
        report(sel.summary(in: s).isEmpty,
               "and the strip says nothing about a selection", sel.summary(in: s))

        sel.click(s[3], modifiers: .command, in: s)
        report(sel.isExplicit && sel.targets(in: s) == [s[2], s[3]],
               "two photos is a selection, and a batch narrows to it",
               "\(sel.targets(in: s))")
        report(sel.summary(in: s) == "2 selected",
               "which the strip now says", sel.summary(in: s))
    }

    /// A plain click replaces; command adds and removes; and only a plain click
    /// asks for the photograph to be opened.
    ///
    /// ⚠ That last part is the one worth a check. A modified click returning a
    /// URL would make building a selection of forty frames forty raw decodes —
    /// about twenty seconds of the interface refusing to respond, for a gesture
    /// that was never a request to look at anything.
    static func testModifiedClicksBuildASelection() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[0])

        report(sel.click(s[2], modifiers: .none, in: s) == s[2],
               "a plain click opens the photo it landed on")
        report(sel.count == 1 && sel.contains(s[2]),
               "and collapses the selection onto it", "\(sel.count)")

        report(sel.click(s[4], modifiers: .command, in: s) == nil,
               "a command-click opens nothing")
        report(sel.targets(in: s) == [s[2], s[4]],
               "and adds to the selection", "\(sel.targets(in: s))")

        sel.click(s[4], modifiers: .command, in: s)
        report(!sel.isExplicit && sel.contains(s[2]),
               "command-clicking a selected photo removes it again")
    }

    /// Shift extends from the anchor, and the anchor does not move while it
    /// does — so sweeping a shift-click along the strip grows and shrinks one
    /// range instead of walking a two-frame window down it.
    static func testShiftClickIsARangeFromTheAnchor() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[1])

        sel.click(s[3], modifiers: .shift, in: s)
        report(sel.targets(in: s) == [s[1], s[2], s[3]],
               "shift selects the range from the anchor",
               "\(sel.targets(in: s))")

        // Back the other way, past the anchor. The anchor is still frame 1, so
        // this is 0...1 and not 0...3.
        sel.click(s[0], modifiers: .shift, in: s)
        report(sel.targets(in: s) == [s[0], s[1]],
               "and re-extending measures from the same anchor, not the last end",
               "\(sel.targets(in: s))")

        // Command with shift adds a second range rather than replacing.
        sel.click(s[2], modifiers: .command, in: s)   // anchor moves to 2
        sel.click(s[4], modifiers: [.command, .shift], in: s)
        report(sel.targets(in: s) == [s[0], s[1], s[2], s[3], s[4]],
               "command-shift unions a second range instead of replacing",
               "\(sel.targets(in: s))")
    }

    /// ⚠ The open photograph cannot be command-clicked out of the selection.
    ///
    /// Its settings are what a sync copies *from*, and its panel is what the
    /// photographer is reading while they decide. A sync that wrote the other
    /// thirty-nine and skipped the one on screen would be indefensible, and
    /// nothing on screen would say it had happened.
    static func testTheOpenPhotoCannotBeDeselected() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[2])
        sel.click(s[0], modifiers: .command, in: s)
        sel.click(s[2], modifiers: .command, in: s)

        report(sel.contains(s[2]),
               "command-clicking the open photo does not drop it")
        report(sel.targets(in: s) == [s[0], s[2]],
               "and the rest of the selection is untouched",
               "\(sel.targets(in: s))")

        // A range that skips it still contains it.
        sel.focus(s[4])
        sel.click(s[0], modifiers: .command, in: s)
        sel.click(s[1], modifiers: .shift, in: s)
        report(sel.contains(s[4]),
               "and a range that does not reach it keeps it anyway",
               "\(sel.targets(in: s))")
    }

    /// ⚠ Filtering to Rated must not leave a rejected frame in the target list.
    ///
    /// A selection is a set of URLs and the filter is a view over a different
    /// list; nothing connects them unless something does it on purpose. Without
    /// this, culling to the picks and pressing Export All writes the rejects
    /// that were selected before the filter moved — photographs the person
    /// cannot see, in a list they cannot check.
    static func testAFilterChangeCannotHideATarget() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[0])
        sel.selectAll(in: s)
        report(sel.targets(in: s).count == 5, "select-all takes everything in view")

        // Frames 1 and 3 are filtered out.
        let narrowed = [s[0], s[2], s[4]]
        sel.confine(to: narrowed)
        report(sel.targets(in: narrowed) == narrowed,
               "a filter change drops what it hides",
               "\(sel.targets(in: narrowed))")
        report(!sel.contains(s[1]) && !sel.contains(s[3]),
               "and the hidden frames are gone from the set, not merely unlisted")

        // Narrowing to nothing but the open photo is not a selection any more,
        // so a batch goes back to meaning everything in view.
        sel.confine(to: [s[0]])
        report(!sel.isExplicit && sel.targets(in: [s[0]]) == [s[0]],
               "and collapsing to one photo stops being a selection")
    }

    /// Targets come back in strip order, always — a `Set` has none, and a batch
    /// export names its files in the order it walks them.
    static func testTargetsComeBackInStripOrder() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[4])
        sel.click(s[0], modifiers: .command, in: s)
        sel.click(s[2], modifiers: .command, in: s)

        report(sel.targets(in: s) == [s[0], s[2], s[4]],
               "targets follow the strip, not the set",
               "\(sel.targets(in: s).map(\.lastPathComponent))")

        // And a photo that is selected but not in this list never appears.
        report(sel.targets(in: [s[2], s[4]]) == [s[2], s[4]],
               "a target the caller's list does not contain is not returned",
               "\(sel.targets(in: [s[2], s[4]]))")
    }
}

// MARK: - Dust spots on the canvas
//
// A spot has two handles a couple of radii apart, and at any useful size they
// overlap. Which one a press grabs is the whole usability of the tool, and it is
// pure geometry — so it is pinned here rather than discovered by dragging.

extension ViewportTests {

    /// A map with the picture filling a 1000x1000 view at 1:1, so a normalized
    /// unit is 1000 points and the arithmetic below is readable.
    static var spotMap: CanvasLayout.PictureMap {
        var m = CanvasLayout.PictureMap()
        m.rect = CGRect(x: 0, y: 0, width: 1000, height: 1000)
        m.visible = CGSize(width: 1, height: 1)
        m.origin = .zero
        return m
    }

    /// ⚠ **The source wins where the two discs overlap.**
    ///
    /// A destination can also be reached by dragging the spot's body, and the
    /// automatic source is placed two and a half radii away — so at any radius
    /// worth using the two circles intersect. Whichever is tested first is the
    /// one that can always be grabbed, and the source is the one with no other
    /// route to it. Test destinations first and the source becomes unreachable
    /// exactly when the spot is large, which is when someone most wants it.
    static func testSpotHitPrefersTheSource() {
        var s = CanvasLayout.SpotPlacement()
        s.destination = CGPoint(x: 0.5, y: 0.5)
        s.source = CGPoint(x: 0.56, y: 0.5)     // 60 pt away
        s.radius = 0.05                          // 50 pt: the discs overlap

        // A point inside both.
        let both = CGPoint(x: 530, y: 500)
        report(CanvasLayout.spotHit(both, [s], in: spotMap) == .source(0),
               "where a spot's two discs overlap, the press takes the source",
               "\(String(describing: CanvasLayout.spotHit(both, [s], in: spotMap)))")

        // And the destination is still reachable where only it covers.
        let onlyDest = CGPoint(x: 465, y: 500)
        report(CanvasLayout.spotHit(onlyDest, [s], in: spotMap) == .destination(0),
               "and the destination answers where only it covers")

        // Empty canvas is nil, or an armed tool could never place a new spot.
        report(CanvasLayout.spotHit(CGPoint(x: 900, y: 100), [s], in: spotMap) == nil,
               "and open canvas grabs nothing")
    }

    /// Later spots are drawn over earlier ones, so a press has to answer with
    /// the one on top. Otherwise placing a spot on top of another makes the new
    /// one — the one being looked at — the only one that cannot be adjusted.
    static func testSpotHitPrefersTheTopmost() {
        var a = CanvasLayout.SpotPlacement()
        a.destination = CGPoint(x: 0.5, y: 0.5)
        a.source = CGPoint(x: 0.9, y: 0.9)
        a.radius = 0.04
        var b = a
        b.destination = CGPoint(x: 0.51, y: 0.5)
        b.source = CGPoint(x: 0.1, y: 0.1)

        let hit = CanvasLayout.spotHit(CGPoint(x: 505, y: 500), [a, b], in: spotMap)
        report(hit == .destination(1),
               "the spot drawn last is the spot a press finds",
               "\(String(describing: hit))")
    }

    /// ⚠ Dust is *supposed* to be small. The size slider goes down to 0.004 of
    /// the frame, which at fit zoom on a 24 MP photograph is about three view
    /// points — a target nobody can hit with a mouse. The handle has a floor
    /// even though the spot it draws does not.
    static func testSpotHandleHasAMinimumSize() {
        var s = CanvasLayout.SpotPlacement()
        s.destination = CGPoint(x: 0.5, y: 0.5)
        s.source = CGPoint(x: 0.9, y: 0.9)
        s.radius = 0.004                         // 4 pt on this map

        let near = CGPoint(x: 500 + 8, y: 500)   // outside the disc, inside the handle
        report(CanvasLayout.spotHit(near, [s], in: spotMap) == .destination(0),
               "a spot smaller than the handle floor is still grabbable",
               "radius \(CanvasLayout.spotRadius(s, in: spotMap)) pt, "
             + "floor \(CanvasLayout.spotHandleMin) pt")

        // But the floor is a floor, not a free-for-all: well outside still misses.
        report(CanvasLayout.spotHit(CGPoint(x: 540, y: 500), [s], in: spotMap) == nil,
               "and the floor does not make the whole picture one big handle")
    }

    /// A drag cannot push a spot off the photograph. Both centres are
    /// normalized picture coordinates and the shader clamps its reads, so an
    /// off-picture source would silently heal from the edge pixel — a spot that
    /// looks placed and quietly does something else.
    static func testSpotDragStaysOnThePicture() {
        let far = CanvasLayout.spotDrag(to: CGPoint(x: -240, y: 1400), in: spotMap)
        report(far == CGPoint(x: 0, y: 1),
               "a drag past the edge clamps to the picture", "\(far)")

        let inside = CanvasLayout.spotDrag(to: CGPoint(x: 250, y: 750), in: spotMap)
        report(abs(inside.x - 0.25) < 1e-6 && abs(inside.y - 0.75) < 1e-6,
               "and a drag inside it maps straight through", "\(inside)")
    }
}

// MARK: - The adjustment catalogue
//
// The controls are defined once and rendered by scope, so the global panel and
// a mask's panel cannot drift. The catalogue is pure data, which is why it lives
// where this suite can reach it — the view that renders it cannot be tested
// here, but everything it decides can.

extension ViewportTests {

    /// ⚠ Every case of `AdjustmentID` has a spec. `AdjustmentCatalogue.spec`
    /// force-unwraps, and a missing entry would trap at the first draw — but
    /// the failure that matters is quieter: a control simply not appearing.
    static func testCatalogueCoversEveryAdjustment() {
        var missing: [String] = []
        for id in AdjustmentID.allCases where !AdjustmentCatalogue.all.contains(where: { $0.id == id }) {
            missing.append(id.rawValue)
        }
        report(missing.isEmpty, "every adjustment id has a spec", missing.joined(separator: ", "))

        // And no duplicates, which would make `spec(_:)` silently pick one.
        let ids = AdjustmentCatalogue.all.map(\.id.rawValue)
        report(Set(ids).count == ids.count, "and no id is specified twice",
               "\(ids.count) specs, \(Set(ids).count) distinct")

        // ⚠ Contrast is at different *stages* in its two scopes — globally the
        // display transform, locally the mask node — and that is the case a
        // single stage per adjustment would have got wrong. It is checked
        // rather than trusted, because the ordering readout is drawn from it.
        let contrast = AdjustmentCatalogue.spec(.contrast)
        report(contrast.global?.stage == .display && contrast.local?.stage == .tone,
               "contrast sits after the mask globally and inside it locally",
               "\(String(describing: contrast.global?.stage)) / \(String(describing: contrast.local?.stage))")

        // A spec with neither scope is a row that can never be drawn.
        let orphans = AdjustmentCatalogue.all.filter { $0.global == nil && $0.local == nil }
        report(orphans.isEmpty, "and every spec is reachable in some scope",
               orphans.map(\.title).joined(separator: ", "))
    }

    /// ⚠ **The load-bearing one.** The catalogue's `local` scopes are what the
    /// panel offers; `develop_linear` is what the engine actually applies. If
    /// those two disagree the interface shows a control that does nothing, or
    /// hides one that works — and both look like the feature being broken.
    ///
    /// The shader's local set is exposure, contrast, saturation, warmth and
    /// tint. Written out here rather than derived, because deriving it from the
    /// same table it is checking would prove nothing.
    static func testCatalogueAgreesWithTheShaderAboutWhatIsLocal() {
        let expected: Set<String> = ["exposure", "contrast", "saturation",
                                     "warmth", "localTint"]
        let offered = Set(AdjustmentCatalogue.localSet.map(\.id.rawValue))
        report(offered == expected,
               "the catalogue offers exactly the adjustments the shader applies locally",
               "offered \(offered.sorted()), shader has \(expected.sorted())")

        // ⚠ And white balance is not among them, which is the one people
        // expect to be. research/masking.md §2b: it runs before the demosaic.
        report(!offered.contains("temperature") && !offered.contains("tint"),
               "and white balance is not local, whatever the panel might suggest")
    }

    /// A refusal without a reason is a control that has simply gone missing.
    static func testEveryRefusalGivesAReason() {
        let silent = AdjustmentCatalogue.all.filter {
            $0.local == nil && $0.global != nil && $0.localRefusal == nil
        }
        report(silent.isEmpty,
               "every adjustment that cannot be local says why",
               silent.map(\.title).joined(separator: ", "))

        // The list the panel shows must not be empty, or the refusals were
        // dropped rather than answered.
        report(AdjustmentCatalogue.refusedLocally.count >= 5,
               "and the panel has a real list to show",
               "\(AdjustmentCatalogue.refusedLocally.count)")
    }
}

// MARK: - Sky detection
//
// research/sky-detection.md. Shen & Wang (2013): sky is smooth, the ground is
// not, so the boundary is where the gradient first becomes large scanning down
// a column — and the problem is choosing how large. Pure arithmetic, so it is
// pinned here on synthetic frames whose answer is known exactly.

extension ViewportTests {

    /// Builds an RGB image row-major from a per-pixel closure.
    static func frame(_ w: Int, _ h: Int,
                      _ f: (Int, Int) -> (Float, Float, Float)) -> [Float] {
        var out = [Float](repeating: 0, count: w * h * 3)
        for y in 0..<h {
            for x in 0..<w {
                let (r, g, b) = f(x, y)
                let i = (y * w + x) * 3
                out[i] = r; out[i + 1] = g; out[i + 2] = b
            }
        }
        return out
    }

    /// A flat sky over textured ground, with the horizon at a known row.
    /// The fill is four-connected, and a one-pixel diagonal gap does not leak.
    ///
    /// ⚠ `UNSOURCED.md` §23 has recorded this as **untested** since the sky
    /// detector shipped: "the synthetic frames have no one-pixel diagonal gap,
    /// and the mutation that adds diagonal neighbours survives." It is the
    /// difference between a sky mask and a mask of the entire photograph, so it
    /// is worth a fixture that has one.
    ///
    /// The frame is a wall of hard gradient separating a calm sky from calm
    /// ground, breached by two calm pixels that touch **only at their corner**:
    ///
    /// ```
    ///   sky   sky   sky   sky      <- calm, seeded from the top row
    ///   wall  GAP   wall  wall     <- row m,   gap at x = k
    ///   wall  wall  GAP   wall     <- row m+1, gap at x = k+1
    ///   ground ground ground       <- calm, and must stay unfilled
    /// ```
    ///
    /// Four-connected, the fill reaches the first gap and stops: that pixel's
    /// four neighbours are wall, wall, sky and wall. Eight-connected, it steps
    /// diagonally into the second gap and floods the ground — which is exactly
    /// how a fill escapes through a gap in a branch and takes the whole frame.
    static func testSkyFillCannotSqueezeThroughADiagonal() {
        let w = 32, h = 24
        let m = 10, k = 12
        let wall: Float = 1.0, calm: Float = 0.0
        let threshold: Float = 0.5

        var grad = [Float](repeating: calm, count: w * h)
        for x in 0..<w {
            grad[m * w + x] = wall
            grad[(m + 1) * w + x] = wall
        }
        grad[m * w + k] = calm
        grad[(m + 1) * w + k + 1] = calm

        let filled = SkyDetector.fill(grad: grad, width: w, height: h,
                                      threshold: threshold)

        // The sky above is reached, or the fixture proves nothing.
        report(filled[(m / 2) * w + w / 2],
               "the calm region above the wall is filled")
        report(filled[m * w + k],
               "and the first gap, which is joined to it edgewise, is filled")

        // ⚠ The load-bearing pair. The second gap touches the first only at a
        // corner, so a four-connected fill cannot enter it — and everything
        // below depends on that one step not being taken.
        report(!filled[(m + 1) * w + k + 1],
               "the diagonally-touching gap is NOT filled")

        var ground = 0
        for y in (m + 2)..<h {
            for x in 0..<w where filled[y * w + x] { ground += 1 }
        }
        report(ground == 0,
               "and no ground pixel is filled — a diagonal step floods all of it",
               "\(ground) of \(w * (h - m - 2)) ground pixels filled")
    }

    /// `largestVariance` is the exact largest eigenvalue, checked against an
    /// independent solver.
    ///
    /// ⚠ Recorded as untested in `UNSOURCED.md` §23, and writing the test is
    /// what showed the code was wrong. It used to return the largest **diagonal
    /// entry** of the covariance, with a comment claiming that "orders
    /// candidates the same way in every case measured" — true only because
    /// every case measured had the same covariance *shape*. Against populations
    /// wide in different channels it reorders a pair in 21.
    ///
    /// It is Smith's closed form now (CACM 1961). The oracle here is a Jacobi
    /// rotation — a different algorithm, iterative where the product's is
    /// closed-form, so agreement is evidence rather than a tautology.
    static func testSkyEigenvalueProxyOrdersTheSameWay() {
        // Jacobi for a 3x3 symmetric matrix. Enough sweeps to converge well
        // past the precision this comparison needs.
        func largestEigenvalue(_ mIn: [Double]) -> Double {
            var a = mIn
            for _ in 0..<64 {
                var p = 0, q = 1
                var off = 0.0
                for i in 0..<3 {
                    for j in 0..<3 where i != j {
                        if abs(a[i * 3 + j]) > off { off = abs(a[i * 3 + j]); p = i; q = j }
                    }
                }
                if off < 1e-14 { break }
                let app = a[p * 3 + p], aqq = a[q * 3 + q], apq = a[p * 3 + q]
                let theta = 0.5 * atan2(2 * apq, app - aqq)
                let c = cos(theta), s = sin(theta)
                var r = [Double](repeating: 0, count: 9)
                for i in 0..<3 { r[i * 3 + i] = 1 }
                r[p * 3 + p] = c; r[q * 3 + q] = c
                r[p * 3 + q] = -s; r[q * 3 + p] = s
                // a := Rᵀ a R
                var t = [Double](repeating: 0, count: 9)
                for i in 0..<3 { for j in 0..<3 {
                    var v = 0.0
                    for l in 0..<3 { v += r[l * 3 + i] * a[l * 3 + j] }
                    t[i * 3 + j] = v
                } }
                for i in 0..<3 { for j in 0..<3 {
                    var v = 0.0
                    for l in 0..<3 { v += t[i * 3 + l] * r[l * 3 + j] }
                    a[i * 3 + j] = v
                } }
            }
            return max(a[0], max(a[4], a[8]))
        }

        // Populations with correlated channels *and differing covariance
        // shapes*.
        //
        // ⚠ The first draft of this fixture varied only an overall spread, so
        // every channel's variance scaled together — and then the largest and
        // the *smallest* diagonal entry order the candidates identically. The
        // mutation replacing `max` with `min` survived it. A fixture where one
        // population is wide in red and the next is wide in blue is what makes
        // "largest" load-bearing rather than incidental.
        func population(_ seed: Int, _ spread: (Double, Double, Double))
            -> SkyDetector.Stats {
            var s = SkyDetector.Stats()
            var state = UInt64(seed &* 2_654_435_761 &+ 1)
            for _ in 0..<400 {
                state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
                let u = Double((state >> 33) % 10_000) / 10_000.0
                state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
                let v = Double((state >> 33) % 10_000) / 10_000.0
                // Correlated, so the covariance is genuinely off-diagonal and
                // the proxy is a strict lower bound rather than the exact value.
                s.add((0.20 + spread.0 * u,
                       0.15 + spread.1 * (0.8 * u + 0.2 * v),
                       0.10 + spread.2 * v))
            }
            return s
        }

        let shapes: [(Double, Double, Double)] = [
            (0.40, 0.05, 0.05),   // wide in red only
            (0.05, 0.05, 0.40),   // wide in blue only
            (0.05, 0.35, 0.05),   // wide in green only
            (0.30, 0.30, 0.02),   // flat-ish, red and green
            (0.02, 0.30, 0.30),   // flat-ish, green and blue
            (0.22, 0.20, 0.24),   // near-isotropic
            (0.45, 0.10, 0.02),   // strongly anisotropic
        ]

        var worst = 0.0
        var offDiagonal = false
        var diagonalWouldReorder = 0
        var exacts: [Double] = []
        for (i, shape) in shapes.enumerated() {
            let p = population(i * 17 + 3, shape)
            let got = p.largestVariance()
            let want = largestEigenvalue(p.covariance())
            worst = max(worst, abs(got - want) / max(want, 1e-12))
            exacts.append(want)

            // ⚠ The guard on the fixture. If the covariances were diagonal the
            // agreement above would be trivially true of the old code as well,
            // and this test would pin nothing.
            let c = p.covariance()
            if want > max(c[0], max(c[4], c[8])) * 1.02 { offDiagonal = true }
        }
        report(worst < 1e-9,
               "the closed form matches an independent Jacobi solve on every shape",
               String(format: "worst relative error %.3g", worst))
        report(offDiagonal,
               "and the covariances are genuinely off-diagonal, so that is not trivially true")

        // ⚠ What the old code got wrong, kept as a check rather than only as a
        // note: the largest diagonal entry reorders candidates against the true
        // eigenvalue. If this ever stops being true the fixture has gone bland
        // and stopped discriminating.
        for a in 0..<(shapes.count - 1) {
            for b in (a + 1)..<shapes.count {
                let ca = population(a * 17 + 3, shapes[a]).covariance()
                let cb = population(b * 17 + 3, shapes[b]).covariance()
                let diagA = max(ca[0], max(ca[4], ca[8]))
                let diagB = max(cb[0], max(cb[4], cb[8]))
                if (diagA < diagB) != (exacts[a] < exacts[b]) { diagonalWouldReorder += 1 }
            }
        }
        report(diagonalWouldReorder > 0,
               "and the fixture is sharp enough to see the diagonal shortcut reorder them",
               "the old proxy reorders \(diagonalWouldReorder) pairs")
    }

    static func testSkyFindsAHorizon() {
        let w = 64, h = 64, horizon = 24
        // ⚠ The ground is *noisy*, not merely darker. The energy is about how
        // uniform each half is, so a smooth dark ground would be as good a
        // "sky" as the sky — and a test with one would pass on a detector that
        // simply cut at the largest brightness step.
        let img = frame(w, h) { x, y in
            if y < horizon { return (0.55, 0.62, 0.80) }
            let n = Float((x &* 37 &+ y &* 61) % 23) / 23.0
            return (0.18 + n * 0.35, 0.16 + n * 0.30, 0.12 + n * 0.25)
        }

        switch SkyDetector.detect(rgb: img, width: w, height: h) {
        case .noSky(let why):
            report(false, "a flat sky over textured ground is found", why)
        case .found(let alpha, let coverage):
            let want = Double(horizon) / Double(h)
            report(abs(coverage - want) < 0.06,
                   "the horizon lands where it was drawn",
                   String(format: "%.3f against %.3f", coverage, want))
            // Two-sided: covered above, clear below.
            let above = alpha[(horizon / 2) * w + w / 2]
            let below = alpha[(horizon + (h - horizon) / 2) * w + w / 2]
            report(above > 0.5 && below < 0.5,
                   "with the sky covered and the ground not",
                   "above \(above), below \(below)")
        }
    }

    /// ⚠ **No hue prior.** The method scores how uniform each half is; it never
    /// asks whether the top is blue. A detector that had quietly grown a blue
    /// test would pass every other check here and fail on overcast — which is
    /// most of the photographs anyone reaches for this on.
    static func testSkyNeverAsksWhatSkyLooksLike() {
        let w = 64, h = 64, horizon = 30
        // A grey overcast sky. Nothing blue anywhere in the frame.
        let img = frame(w, h) { x, y in
            if y < horizon { return (0.78, 0.78, 0.77) }
            let n = Float((x &* 29 &+ y &* 53) % 19) / 19.0
            return (0.22 + n * 0.30, 0.20 + n * 0.26, 0.18 + n * 0.22)
        }
        switch SkyDetector.detect(rgb: img, width: w, height: h) {
        case .noSky(let why):
            report(false, "an overcast sky is found without a colour prior", why)
        case .found(_, let coverage):
            let want = Double(horizon) / Double(h)
            report(abs(coverage - want) < 0.08,
                   "an overcast sky is found without a colour prior",
                   String(format: "%.3f against %.3f", coverage, want))
        }
    }

    /// ⚠ A frame with no sky must say so. Returning "everything" is
    /// indistinguishable from the feature being broken — the failure the person
    /// matte had before it learned to report an empty result.
    static func testSkyRefusesAFrameWithNone() {
        let w = 48, h = 48
        // Texture everywhere, no smooth region touching the top.
        let noise = frame(w, h) { x, y in
            let n = Float((x &* 41 &+ y &* 67) % 17) / 17.0
            return (0.15 + n * 0.6, 0.14 + n * 0.55, 0.13 + n * 0.5)
        }
        if case .found(_, let c) = SkyDetector.detect(rgb: noise, width: w, height: h) {
            report(false, "a frame of pure texture reports no sky",
                   String(format: "covered %.3f", c))
        } else {
            report(true, "a frame of pure texture reports no sky")
        }

        // And a completely flat frame: no edges at all, so no horizon exists.
        let flat = frame(w, h) { _, _ in (0.5, 0.5, 0.5) }
        switch SkyDetector.detect(rgb: flat, width: w, height: h) {
        case .noSky:
            report(true, "and so does a frame with no edges in it")
        case .found(_, let c):
            report(false, "and so does a frame with no edges in it",
                   String(format: "covered %.3f", c))
        }
    }
}

extension ViewportTests {

    /// ⚠ **The energy, and the sky-to-the-bottom rule, both actually exercised.**
    ///
    /// The first three checks could not see either. Their sky is *perfectly*
    /// flat, so its gradient is exactly zero and every candidate threshold finds
    /// the same first exceedance — the horizon — whatever the energy says. Two
    /// mutations survived on that: scoring the sky alone and ignoring the
    /// ground, and treating a column with no edge as having no sky.
    ///
    /// This frame has **mild grain in the sky**, as a real one does, so
    /// different thresholds give genuinely different borders and the energy has
    /// to choose. And its ground stops two thirds of the way across, so the
    /// remaining columns have no strong edge anywhere and must be sky all the
    /// way down.
    static func testSkyEnergyPicksTheBorder() {
        let w = 64, h = 64, horizon = 34
        let img = frame(w, h) { x, y in
            let grain = Float((x &* 13 &+ y &* 7) % 5) / 5.0 * 0.02
            if y >= horizon && x < (w * 2) / 3 {
                let n = Float((x &* 43 &+ y &* 71) % 21) / 21.0
                return (0.20 + n * 0.40, 0.18 + n * 0.34, 0.15 + n * 0.28)
            }
            return (0.52 + grain, 0.60 + grain, 0.79 + grain)
        }

        switch SkyDetector.detect(rgb: img, width: w, height: h) {
        case .noSky(let why):
            report(false, "a grainy sky over a partial horizon is found", why)
        case .found(let alpha, let coverage):
            // Two thirds of the columns end at the horizon; the last third is
            // sky to the bottom. Expected coverage is the weighted mix.
            let want = (2.0 / 3.0) * (Double(horizon) / Double(h)) + (1.0 / 3.0)
            report(abs(coverage - want) < 0.08,
                   "the energy chooses the horizon even when thresholds disagree",
                   String(format: "%.3f against %.3f", coverage, want))

            // ⚠ A column with no edge in it is sky all the way down — the
            // paper's rule, and what makes a frame of nothing but sky come out
            // covered rather than empty.
            let openColumn = w - 4
            report(alpha[(h - 2) * w + openColumn] > 0.5,
                   "and a column with no edge in it is sky to the bottom",
                   "\(alpha[(h - 2) * w + openColumn])")

            // ...while a column that does have one still stops at it.
            report(alpha[(h - 2) * w + 4] < 0.5,
                   "while a column that has one still stops there",
                   "\(alpha[(h - 2) * w + 4])")
        }
    }
}

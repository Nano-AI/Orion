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

        // Shove the centre far outside, then clamp.
        v.pan(by: CGSize(width: -10, height: -10), visible: visible)
        v.clamp(to: visible)
        report(v.center.x + visible.width / 2 <= 1.0 + 1e-6, "right edge stays inside")
        report(v.center.y + visible.height / 2 <= 1.0 + 1e-6, "bottom edge stays inside")

        v.pan(by: CGSize(width: 10, height: 10), visible: visible)
        v.clamp(to: visible)
        report(v.center.x - visible.width / 2 >= -1e-6, "left edge stays inside")
        report(v.center.y - visible.height / 2 >= -1e-6, "top edge stays inside")

        // At fit the centre is pinned, so there is nothing to pan to.
        let f = Viewport()
        let full = f.visibleFraction(imageAspect: landscape, viewAspect: 1.0)
        f.pan(by: CGSize(width: 0.4, height: 0.4), visible: full)
        f.clamp(to: full)
        near(f.center.x, 0.5, 1e-6, "fit pins the centre horizontally")
        near(f.center.y, 0.5, 1e-6, "fit pins the centre vertically")
    }

    /// Zooming about a point keeps that point where it was.
    static func testZoomAnchoring() {
        let v = Viewport()
        let visible = CGSize(width: 1, height: 1)
        let anchor = CGPoint(x: 0.25, y: 0.75)

        v.zoomBy(2.0, anchor: anchor, visible: visible)
        // Centre moves toward the anchor, not away from it.
        report(v.center.x < 0.5, "zoom moves the centre toward the anchor (x)")
        report(v.center.y > 0.5, "zoom moves the centre toward the anchor (y)")

        // Zoom is bounded: never below fit, never absurdly far in.
        let w = Viewport()
        w.zoomBy(0.01, anchor: CGPoint(x: 0.5, y: 0.5), visible: visible)
        near(w.zoom, 1.0, 1e-6, "zoom never goes below fit")

        for _ in 0..<40 {
            w.zoomBy(2.0, anchor: CGPoint(x: 0.5, y: 0.5), visible: visible)
        }
        report(w.zoom <= 64.0 + 1e-6, "zoom is capped")
    }

    /// Hue to band. If this drifts from the centres in hsl_ops.slang, the
    /// targeted tool adjusts a band you did not click.
    static func testHueBands() {
        // Each band centre must map to its own band.
        for (i, centre) in TargetedAdjust.centres.enumerated() {
            let got = TargetedAdjust.band(forHue: centre)
            report(got.rawValue == i,
                   "hue \(Int(centre))deg maps to \(HueBand(rawValue: i)!.name)",
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

        // Grey has no hue, and must refuse rather than pick one at random —
        // otherwise clicking a wall silently adjusts some band.
        report(TargetedAdjust.hue(r: 0.5, g: 0.5, b: 0.5) == nil, "grey has no hue")
        report(TargetedAdjust.hue(r: 0.5, g: 0.505, b: 0.5) == nil,
               "near-grey has no hue")

        // Scale invariance: the picker normalises by peak, so a dark and a
        // bright version of the same colour must land in the same band.
        let bright = TargetedAdjust.hue(r: 0.2, g: 0.4, b: 0.9)
        let dark = TargetedAdjust.hue(r: 0.02, g: 0.04, b: 0.09)
        if let b = bright, let d = dark {
            near(b, d, 0.5, "hue is independent of brightness")
        } else {
            report(false, "hue is independent of brightness", "one sample returned nil")
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
        near(v.center.x, 0.5, 1e-6, "returning to fit recentres")
    }
}


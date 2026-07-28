import CoreGraphics
import Foundation

/// What part of the image is on screen, and at what magnification.
///
/// Zoom is expressed as a multiple of "fit": 1.0 shows the whole frame, and
/// `fitScale` converts that into actual pixel magnification so the toolbar can
/// report a true percentage. Centre is in normalised image coordinates, so it
/// survives a window resize without the view jumping.
@Observable
final class Viewport {

    private(set) var zoom: CGFloat = 1.0
    private(set) var center = CGPoint(x: 0.5, y: 0.5)

    /// Magnification at zoom 1.0 — set by the renderer once it knows the
    /// drawable and image sizes.
    var fitScale: CGFloat = 1.0

    /// Held at fit while the crop tool is open.
    ///
    /// The crop overlay is drawn in view coordinates from the *fit* rectangle;
    /// zooming moves the pixels without moving the rectangle, so the two
    /// disagree and the handles end up somewhere the image is not. Lightroom
    /// locks the view during crop for the same reason. Rejecting the input is
    /// better than letting it desynchronise.
    var locked = false {
        didSet { if locked { reset() } }
    }

    var isFit: Bool { abs(zoom - 1.0) < 0.001 }

    /// Actual on-screen magnification, which is what a photographer means by
    /// "100%": one image pixel per screen pixel.
    var percent: Int { Int((fitScale * zoom * 100).rounded()) }

    func reset() {
        zoom = 1.0
        center = CGPoint(x: 0.5, y: 0.5)
    }

    /// Toggles between fitting the frame and 1:1 pixels.
    func toggleFitAndActual() {
        guard !locked else { return }
        if isFit {
            zoom = max(1.0, 1.0 / max(fitScale, 0.0001))
        } else {
            reset()
        }
        clampCentre()
    }

    /// Zooms about a point given in normalised *view* coordinates (0..1), so
    /// the pixel under the cursor stays under the cursor.
    func zoomBy(_ factor: CGFloat, anchor: CGPoint, visible: CGSize) {
        guard !locked else { return }
        let old = zoom
        zoom = min(max(zoom * factor, 1.0), 64.0)
        guard zoom != old else { return }

        // The anchor's offset from centre, in image space, shrinks by exactly
        // the ratio of the two zoom levels.
        let dx = (anchor.x - 0.5) * visible.width
        let dy = (anchor.y - 0.5) * visible.height
        let shrink = 1 - (old / zoom)
        center.x += dx * shrink
        center.y += dy * shrink
        clampCentre()
    }

    /// Pans by a delta in normalised view coordinates.
    func pan(by delta: CGSize, visible: CGSize) {
        guard !locked else { return }
        center.x -= delta.width * visible.width
        center.y -= delta.height * visible.height
        clampCentre()
    }

    /// Fraction of the view each axis of the image covers at zoom 1. The
    /// smaller one is 1 (that axis is what "fit" is constrained by) and the
    /// other is less, which is the letterbox.
    private func fitCoverage(imageAspect: CGFloat, viewAspect: CGFloat) -> CGSize {
        imageAspect > viewAspect
            ? CGSize(width: 1.0, height: viewAspect / imageAspect)
            : CGSize(width: imageAspect / viewAspect, height: 1.0)
    }

    /// How much of the view the image covers, per axis. Below 1 means letterbox.
    func quadScale(imageAspect: CGFloat, viewAspect: CGFloat) -> CGSize {
        let base = fitCoverage(imageAspect: imageAspect, viewAspect: viewAspect)
        return CGSize(width: min(1.0, base.width * zoom),
                      height: min(1.0, base.height * zoom))
    }

    /// Visible fraction of the *image* on each axis.
    ///
    /// This is the reciprocal of how far the image extends past the view, not
    /// the coverage itself — getting that backwards silently crops the frame,
    /// which is exactly what it did.
    func visibleFraction(imageAspect: CGFloat, viewAspect: CGFloat) -> CGSize {
        let base = fitCoverage(imageAspect: imageAspect, viewAspect: viewAspect)
        return CGSize(width: min(1.0, 1.0 / (zoom * base.width)),
                      height: min(1.0, 1.0 / (zoom * base.height)))
    }

    private func clampCentre() {
        // Kept generous here; the renderer clamps precisely once it knows the
        // visible fraction, because that depends on the view's aspect.
        center.x = min(max(center.x, 0), 1)
        center.y = min(max(center.y, 0), 1)
    }

    /// Final clamp so the viewport never shows outside the frame.
    func clamp(to visible: CGSize) {
        let halfU = visible.width / 2
        let halfV = visible.height / 2
        center.x = visible.width  >= 1 ? 0.5 : min(max(center.x, halfU), 1 - halfU)
        center.y = visible.height >= 1 ? 0.5 : min(max(center.y, halfV), 1 - halfV)
    }
}

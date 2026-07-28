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
        center.x -= delta.width * visible.width
        center.y -= delta.height * visible.height
        clampCentre()
    }

    /// Visible fraction of the image on each axis, given the view's aspect.
    func visibleFraction(imageAspect: CGFloat, viewAspect: CGFloat) -> CGSize {
        // At fit, the constrained axis shows all of the image and the other
        // shows all of it too — the leftover becomes letterbox, not extra image.
        let baseU: CGFloat = imageAspect > viewAspect ? 1.0 : imageAspect / viewAspect
        let baseV: CGFloat = imageAspect > viewAspect ? viewAspect / imageAspect : 1.0
        return CGSize(width: min(1.0, baseU / zoom), height: min(1.0, baseV / zoom))
    }

    /// How much of the view the image covers, per axis. Below 1 means letterbox.
    func quadScale(imageAspect: CGFloat, viewAspect: CGFloat) -> CGSize {
        let w = imageAspect > viewAspect ? 1.0 : imageAspect / viewAspect
        let h = imageAspect > viewAspect ? viewAspect / imageAspect : 1.0
        return CGSize(width: min(1.0, w * zoom), height: min(1.0, h * zoom))
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

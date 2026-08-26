import Foundation
import Metal
import MetalKit
import AppKit

/// Crop, straighten, rotation and the frame the crop is expressed against.
///
/// The region is "where the picture is", as opposed to what colour it is. Both
/// halves of a geometry change live here: the constraint that pulls the crop
/// back inside a turned frame, and the callers that trip it.
///
/// The properties these read — `cropX`, `straightenDeg`, `rotateQuarters`, the
/// three perspective values — are **stored**, so they are in `Engine.swift` and
/// cannot move here. See the note at the top of that file.

extension Engine {

    /// The frame's width over its height, which is what every rotated-bounds
    /// calculation needs.
    var frameAspect: CGFloat {
        guard frameWidth > 0, frameHeight > 0 else { return 1 }
        return CGFloat(frameWidth) / CGFloat(frameHeight)
    }

    /// The region the crop preview renders, in crop coordinates. The overlay
    /// draws against the same numbers the engine renders from, which is the
    /// only way the white rectangle can be trusted to sit on the pixels.
    var previewCanvas: (origin: CGPoint, size: CGFloat) {
        guard cropPreview else { return (.zero, 1) }
        return CanvasLayout.previewCanvas(frameAspect: frameAspect,
                                          angleDeg: CGFloat(straightenDeg))
    }

    /// Sets the crop in one push, so a drag is a single render rather than four.
    func setCrop(x: Float, y: Float, w: Float, h: Float) {
        guard isLoaded else { return }
        let r = CanvasLayout.constrainedCrop(
            CGRect(x: CGFloat(x), y: CGFloat(y), width: CGFloat(w), height: CGFloat(h)),
            frameAspect: frameAspect, angleDeg: CGFloat(straightenDeg))

        suspended = true
        cropW = Float(r.width); cropH = Float(r.height)
        cropX = Float(r.origin.x); cropY = Float(r.origin.y)
        suspended = false
        pushAndRender()
    }

    /// Pulls the crop back inside the frame after the angle or the rotation
    /// changed under it. Silent — it runs on every angle tick.
    func constrainCrop() {
        guard isLoaded, !constraining else { return }
        constraining = true
        defer { constraining = false }

        let r = CanvasLayout.constrainedCrop(
            CGRect(x: CGFloat(cropX), y: CGFloat(cropY),
                   width: CGFloat(cropW), height: CGFloat(cropH)),
            frameAspect: frameAspect, angleDeg: CGFloat(straightenDeg))

        let was = suspended
        suspended = true
        cropW = Float(r.width); cropH = Float(r.height)
        cropX = Float(r.origin.x); cropY = Float(r.origin.y)
        suspended = was
    }

    /// Records one history entry when a crop drag finishes, rather than one per
    /// frame of the drag.
    func commitCropEdit() {
        history.record(state, label: "Crop")
        log.committed(state, label: "Crop")
    }

    /// Moves the crop without changing its size, clamped to the frame.
    func moveCrop(dx: Float, dy: Float) {
        guard isLoaded else { return }
        suspended = true
        cropX = min(max(cropX + dx, 0), 1 - cropW)
        cropY = min(max(cropY + dy, 0), 1 - cropH)
        suspended = false
        pushAndRender()
    }

    /// Common crop ratios. nil is freeform.
    func setAspect(_ ratio: Float?) {
        guard isLoaded else { return }
        guard let ratio else { return }

        // Fit the largest rectangle of this ratio inside the current frame,
        // centered, so choosing a ratio never crops information the user has
        // not asked to lose beyond what the ratio requires.
        let frame = Float(frameAspect)
        var w: Float = 1, h: Float = 1
        if ratio > frame { h = frame / ratio } else { w = ratio / frame }

        suspended = true
        cropW = w; cropH = h
        cropX = (1 - w) / 2; cropY = (1 - h) / 2
        suspended = false
        pushAndRender()
    }

    func resetCrop() {
        suspended = true
        cropX = 0; cropY = 0; cropW = 1; cropH = 1; straightenDeg = 0
        suspended = false
        guard isLoaded else { return }
        pushAndRender()
    }

    /// Total clockwise quarter turns — the camera's own EXIF orientation plus
    /// the user's rotation. A matte producer needs it; see `MatteGeometry`.
    var quarterTurns: Int {
        guard let handle, isLoaded else { return 0 }
        var t: Int32 = 0
        guard orion_engine_quarter_turns(handle, &t) == ORION_OK else { return 0 }
        return Int(t)
    }

    /// The camera's own turn, without the photographer's.
    ///
    /// ⚠ `quarterTurns` is the **sum** — `exifQuarters + rotateQuarters` — and
    /// that is the wrong number for anything working against `renderForAnalysis`,
    /// which resets `rotateQuarters` to zero before it renders. Reading the sum
    /// and then undoing it against a picture that only carried the EXIF turn
    /// lands a matte a quarter turn out **and** the wrong shape, so the engine
    /// refuses it. Only the camera's turn is a permutation this render kept.
    var exifQuarterTurns: Int {
        ((quarterTurns - Int(rotateQuarters)) % 4 + 4) % 4
    }

    /// Rotates by a quarter turn, wrapping. Clockwise is positive.
    /// A quarter turn swaps the frame's width and height, so a crop rectangle
    /// expressed against the old one no longer means anything. Resetting it is
    /// honest; carrying it over would silently reframe the picture.
    func rotate(_ turns: Int32) {
        suspended = true
        cropX = 0; cropY = 0; cropW = 1; cropH = 1
        // Predict the swap rather than waiting for the render to report it,
        // so the constraint that runs on the next edit has the right aspect.
        if turns % 2 != 0 { swap(&frameWidth, &frameHeight) }
        suspended = false
        // The render this triggers re-takes the held original: a quarter turn
        // swaps the output's width and height, and the split samples both
        // textures through one valid rectangle. `refreshOriginal` sees that.
        rotateQuarters = ((rotateQuarters + turns) % 4 + 4) % 4
    }
}

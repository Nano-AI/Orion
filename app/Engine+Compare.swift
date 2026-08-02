import Foundation
import Metal
import MetalKit
import AppKit

/// Split compare: the held as-shot render, and the geometry that invalidates
/// it.
///
/// The blit samples the edited texture and the held original through **one**
/// set of UVs. That is why the geometry is recorded and compared rather than
/// re-captured at each call site — hand-listing the sites is exactly what
/// failed before.

extension Engine {

    var comparing: Bool { compareSplit < 0.999 }

    /// The geometry the held original was rendered at.
    ///
    /// ⚠️ The blit samples the edited texture and the held original through
    /// **one** set of UVs, derived from the *edited* render's valid rectangle.
    /// So the moment a crop, a straighten or a quarter turn changes that
    /// rectangle, the held copy is read through the wrong window and the two
    /// halves stop being the same photograph. Measured: a crop under a live
    /// split put luma 0.7404 on the original side where 0.1432 belonged.
    ///
    /// Recorded and compared rather than re-captured at each call site, because
    /// hand-listing the sites is exactly what failed — `rotate` carried its own
    /// `captureOriginal()` call and `setCrop` carried nothing, so one of the two
    /// geometry controls was right by accident of who remembered.
    struct OriginalGeometry: Equatable {
        var rotateQuarters: Int32
        var straightenDeg: Float
        /// Perspective does not change the output *rectangle*, so it is not
        /// here for the reason the other fields are. It is here because the
        /// held original is rendered with the geometry copied across, and a
        /// split showing a corrected edit beside an uncorrected original is two
        /// different photographs.
        var perspectiveVertical: Float
        var perspectiveHorizontal: Float
        var perspectiveAspect: Float
        var cropX: Float, cropY: Float, cropW: Float, cropH: Float
        /// The crop preview renders the whole frame with the crop as context,
        /// which is a different output shape again.
        var cropPreview: Bool
    }

    private var currentGeometry: OriginalGeometry {
        OriginalGeometry(rotateQuarters: rotateQuarters, straightenDeg: straightenDeg,
                         perspectiveVertical: perspectiveVertical,
                         perspectiveHorizontal: perspectiveHorizontal,
                         perspectiveAspect: perspectiveAspect,
                         cropX: cropX, cropY: cropY, cropW: cropW, cropH: cropH,
                         cropPreview: cropPreview)
    }

    /// Renders the image as shot into a held texture, then restores the edit.
    /// Two renders once, rather than two renders per frame of the divider.
    func captureOriginal() {
        guard isLoaded, !capturingOriginal else { return }
        capturingOriginal = true
        defer { capturingOriginal = false }
        originalGeometry = currentGeometry

        let current = state
        var neutral = DevelopState()
        neutral.temperatureK = temperatureK      // as shot is not an edit
        neutral.tint = tint
        neutral.rotateQuarters = rotateQuarters
        neutral.straightenDeg = straightenDeg
        neutral.perspectiveVertical = perspectiveVertical
        neutral.perspectiveHorizontal = perspectiveHorizontal
        neutral.perspectiveAspect = perspectiveAspect
        neutral.cropX = cropX; neutral.cropY = cropY
        neutral.cropW = cropW; neutral.cropH = cropH

        apply(neutral)

        // Copy out, because the next render overwrites the pipeline's output.
        if let src = outputTexture, let device = metalDevice {
            let desc = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: src.pixelFormat, width: src.width, height: src.height,
                mipmapped: false)
            desc.usage = [.shaderRead, .shaderWrite]
            desc.storageMode = .shared

            if let copy = device.makeTexture(descriptor: desc),
               let queue = device.makeCommandQueue(),
               let buffer = queue.makeCommandBuffer(),
               let blit = buffer.makeBlitCommandEncoder() {
                blit.copy(from: src, to: copy)
                blit.endEncoding()
                buffer.commit()
                buffer.waitUntilCompleted()
                originalTexture = copy
            }
        }

        apply(current)
    }

    func setCompare(split: Double) {
        // The split goes first. captureOriginal re-renders twice, and every
        // render asks whether the held original is still good — which, while
        // the split still reads 1.0, means dropping it. So the original was
        // captured and thrown away in the same call, and compare showed the
        // edit on both sides.
        compareSplit = min(max(split, 0), 1)
        log.compare(compareSplit)
        refreshOriginal()
        generation &+= 1
    }

    func clearCompare() {
        log.compare(1.0)
        compareSplit = 1.0
        originalTexture = nil
        originalGeometry = nil
        generation &+= 1
    }

    /// Brings the held original into line with what compare needs right now:
    /// nothing when the split is off, and a render at the *current* geometry
    /// when it is on.
    ///
    /// Called from `render`, so it covers every route into a geometry change —
    /// the crop rectangle, the straighten slider, a quarter turn, the crop
    /// preview, an undo that walks back through any of them — rather than the
    /// handful of call sites someone remembered to annotate.
    ///
    /// It costs two full renders when it fires. That is affordable because the
    /// only geometry control reachable while comparing is the rotate button,
    /// which is a click: the crop tab clears compare on entry, so the sliders
    /// that would trip this per tick cannot be moved with a split up.
    func refreshOriginal() {
        guard !capturingOriginal else { return }
        guard comparing else {
            originalTexture = nil
            originalGeometry = nil
            return
        }
        guard originalTexture == nil || originalGeometry != currentGeometry else { return }
        captureOriginal()
    }
}

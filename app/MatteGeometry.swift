import CoreGraphics
import Foundation

/// Getting a raster matte into the space the mask kernel works in.
///
/// `research/masking.md` §5. A segmentation model is handed a picture that has
/// been through the display transform — that is the whole point, the models
/// were trained on ordinary photographs — and the display transform is the last
/// thing in the graph before geometry. So the matte comes back oriented like the
/// *screen*, while mask kind 4 requires **frame** coordinates: the sensor's own
/// orientation, uncropped and unturned, which is where `develop:linear` runs.
///
/// Two halves, and this file is the second.
///
/// The first half is arranged away rather than corrected: the render Vision is
/// given is taken with the crop reset, the straighten at zero and the user's
/// rotation at zero, so the only difference left between screen and frame is
/// the camera's own EXIF quarter turn. That is deliberate. A crop would leave
/// the matte with no data outside the crop rectangle, and a straighten would
/// need a resample — where a quarter turn needs neither, because it is an exact
/// permutation of the pixels.
///
/// ⚠ **This is the step most likely to be silently wrong**, and the failure is
/// not a crash: apply the turn twice, or the wrong way, and the matte is a
/// plausible-looking selection of something the photographer did not point at.
/// It is pure array logic on purpose, so `orion-viewport-tests` can pin it
/// without a GPU, a window or a model.
enum MatteGeometry {

    /// Rotates a raster **anticlockwise** by `turns` quarter turns — undoing
    /// `turns` clockwise turns the orientation node applied.
    ///
    /// Row-major, top-left origin, which is how both Metal textures and
    /// `CGImage` bitmaps are laid out. Returns the new dimensions along with
    /// the pixels, because an odd turn swaps them.
    ///
    /// `turns` is taken modulo 4 and negatives are handled, so a caller can
    /// pass the engine's total turns straight in without normalising first —
    /// normalising at the call site is exactly the sort of thing one of two
    /// callers forgets.
    static func undoTurns(_ src: [Float], width: Int, height: Int,
                          turns: Int) -> (pixels: [Float], width: Int, height: Int) {
        let k = ((turns % 4) + 4) % 4
        guard width > 0, height > 0, src.count >= width * height else {
            return (src, width, height)
        }
        if k == 0 { return (src, width, height) }

        let swaps = (k % 2) != 0
        let outW = swaps ? height : width
        let outH = swaps ? width : height
        var out = [Float](repeating: 0, count: outW * outH)

        // Written as a forward scatter from the source, not a gather into the
        // destination. Both are correct; the forward direction is the one that
        // reads like the sentence "a clockwise turn sends (x, y) to (h-1-y, x)",
        // which is the same sentence `mask::toFrame` is derived from, so the two
        // can be checked against each other by eye.
        for y in 0..<height {
            for x in 0..<width {
                let v = src[y * width + x]
                let dx: Int, dy: Int
                switch k {
                case 1:  dx = y;              dy = width - 1 - x
                case 2:  dx = width - 1 - x;  dy = height - 1 - y
                default: dx = height - 1 - y; dy = x
                }
                out[dy * outW + dx] = v
            }
        }
        return (out, outW, outH)
    }

    /// The size to hand a segmentation model, given the frame's own.
    ///
    /// Capped on the long edge. These models run at a fixed internal resolution
    /// and upsample whatever they are given, so past that cap the extra pixels
    /// buy nothing but readback and inference time — and the boundary is
    /// recovered afterwards by the guided refinement (`research/masking.md` §4),
    /// which works from the *full*-resolution photograph rather than from this.
    ///
    /// Never upscales: a frame smaller than the cap is passed at its own size.
    static func previewSize(frameWidth: Int, frameHeight: Int,
                            longEdge: Int) -> (width: Int, height: Int) {
        guard frameWidth > 0, frameHeight > 0, longEdge > 0 else { return (0, 0) }
        let longest = max(frameWidth, frameHeight)
        if longest <= longEdge { return (frameWidth, frameHeight) }

        let scale = Double(longEdge) / Double(longest)
        return (max(1, Int((Double(frameWidth) * scale).rounded())),
                max(1, Int((Double(frameHeight) * scale).rounded())))
    }
}

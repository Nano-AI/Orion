import Foundation
import Metal
import MetalKit
import AppKit

/// Swift-side owner of the C engine handle.
///
/// Note what is NOT here: no SwiftUI types, no view state. The model stays a
/// plain observable object so any panel built on it can be re-hosted in AppKit
/// without touching engine code (decision #26).
@Observable
final class Engine {

    enum Failure: LocalizedError {
        case create(String)
        case open(String)
        case export(String)

        var errorDescription: String? {
            switch self {
            case .create(let m): return "Could not start the engine: \(m)"
            case .open(let m):   return "Could not open that photo: \(m)"
            case .export(let m): return "Could not export: \(m)"
            }
        }
    }

    private(set) var camera = ""
    private(set) var imageWidth: UInt32 = 0
    private(set) var imageHeight: UInt32 = 0

    /// The whole frame after rotation, before any crop. The crop rectangle is
    /// normalised against this, and the crop tool's canvas is sized from it —
    /// `imageWidth` cannot stand in, because it is the *cropped* result.
    private(set) var frameWidth: UInt32 = 0
    private(set) var frameHeight: UInt32 = 0

    /// The frame's width over its height, which is what every rotated-bounds
    /// calculation needs.
    var frameAspect: CGFloat {
        guard frameWidth > 0, frameHeight > 0 else { return 1 }
        return CGFloat(frameWidth) / CGFloat(frameHeight)
    }
    private(set) var lastRenderMs: Double = 0
    private(set) var isLoaded = false

    var temperatureK: Float = 5500 { didSet { pushAndRender() } }
    var tint: Float = 0            { didSet { pushAndRender() } }
    var exposureEv: Float = 0      { didSet { pushAndRender() } }
    var highlights: Float = 0      { didSet { pushAndRender() } }
    var shadows: Float = 0         { didSet { pushAndRender() } }
    var whites: Float = 0          { didSet { pushAndRender() } }
    var blacks: Float = 0          { didSet { pushAndRender() } }
    var vibrance: Float = 0        { didSet { pushAndRender() } }
    var saturation: Float = 0      { didSet { pushAndRender() } }
    var contrast: Float = 1        { didSet { pushAndRender() } }

    /// Extra quarter turns clockwise, on top of the camera's own orientation.
    var rotateQuarters: Int32 = 0  { didSet { constrainCrop(); pushAndRender() } }

    /// Straighten angle. Turning the dial shrinks the crop rather than letting
    /// it reach past the turned frame, which is what would leave transparent
    /// wedges in the corners of the export.
    var straightenDeg: Float = 0   { didSet { constrainCrop(); pushAndRender() } }

    /// True while the crop tool is open.
    var cropPreview = false        { didSet { pushAndRender() } }

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
    private func constrainCrop() {
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

    private var constraining = false

    /// Records one history entry when a crop drag finishes, rather than one per
    /// frame of the drag.
    func commitCropEdit() { history.record(state, label: "Crop") }

    /// Moves the crop without changing its size, clamped to the frame.
    func moveCrop(dx: Float, dy: Float) {
        guard isLoaded else { return }
        suspended = true
        cropX = min(max(cropX + dx, 0), 1 - cropW)
        cropY = min(max(cropY + dy, 0), 1 - cropH)
        suspended = false
        pushAndRender()
    }
    var cropX: Float = 0           { didSet { pushAndRender() } }
    var cropY: Float = 0           { didSet { pushAndRender() } }
    var cropW: Float = 1           { didSet { pushAndRender() } }
    var cropH: Float = 1           { didSet { pushAndRender() } }

    /// Common crop ratios. nil is freeform.
    func setAspect(_ ratio: Float?) {
        guard isLoaded else { return }
        guard let ratio else { return }

        // Fit the largest rectangle of this ratio inside the current frame,
        // centred, so choosing a ratio never crops information the user has
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

    /// Tone curve, applied after the display transform. The engine has had the
    /// spline and the LUT since M2; nothing reached them until now.
    var curve = ToneCurve()       { didSet { pushAndRender() } }

    var sharpenAmount: Float = 0   { didSet { pushAndRender() } }
    var sharpenRadius: Float = 1   { didSet { pushAndRender() } }
    var sharpenMasking: Float = 0  { didSet { pushAndRender() } }

    /// Colour mixer, eight bands. Index order matches HueBand.allCases.
    var hueShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }
    var satShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }
    var lumShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }

    /// True while a slider is being dragged, so pushes are suppressed until
    /// the value settles. Not used yet — hook for degrade-then-refine.
    private var suspended = false

    /// Bumped after every successful render so the view knows to redraw.
    private(set) var generation: UInt64 = 0

    /// 128 bins per channel, packed R then G then B.
    private(set) var histogramBins: [UInt32] = []

    let history = EditHistory()

    /// Suppresses history recording while undo/redo is applying a snapshot —
    /// otherwise stepping back would immediately record the step as a new edit.
    private var restoring = false

    private var handle: OpaquePointer?

    init() throws {
        var h: OpaquePointer?
        let status = orion_engine_create(&h)
        guard status == ORION_OK, let h else {
            throw Failure.create(String(cString: orion_status_string(status)))
        }
        handle = h
    }

    deinit {
        if let handle { orion_engine_destroy(handle) }
    }

    var metalDevice: MTLDevice? {
        guard let handle, let raw = orion_engine_metal_device(handle) else { return nil }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLDevice
    }

    var outputTexture: MTLTexture? {
        guard let handle, let raw = orion_engine_output_texture(handle) else { return nil }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLTexture
    }

    func open(path: String) throws {
        guard let handle else { return }

        let status = orion_engine_open_raw(handle, path)
        guard status == ORION_OK else {
            throw Failure.open(errorText(status))
        }

        var w: UInt32 = 0, h: UInt32 = 0
        orion_engine_image_size(handle, &w, &h)
        imageWidth = w
        imageHeight = h
        camera = String(cString: orion_engine_camera(handle))

        // Reset to the camera's own settings before marking loaded, so the
        // didSet observers don't each trigger a render on a half-set model.
        suspended = true
        var asShot = OrionAdjustments()
        orion_engine_as_shot(handle, &asShot)
        temperatureK = asShot.temperature_k
        tint = asShot.tint
        exposureEv = 0; highlights = 0; shadows = 0; whites = 0; blacks = 0
        vibrance = 0; saturation = 0; contrast = 1
        suspended = false

        isLoaded = true
        history.reset(to: state)
        pushAndRender()
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
        rotateQuarters = ((rotateQuarters + turns) % 4 + 4) % 4
    }

    /// Returns every adjustment to its default, with white balance back to
    /// what the camera chose. One push, one render.
    func resetEdits() {
        guard isLoaded, let handle else { return }
        suspended = true
        var asShot = OrionAdjustments()
        orion_engine_as_shot(handle, &asShot)
        temperatureK = asShot.temperature_k
        tint = asShot.tint
        exposureEv = 0; highlights = 0; shadows = 0; whites = 0; blacks = 0
        vibrance = 0; saturation = 0; contrast = 1
        sharpenAmount = 0; sharpenRadius = 1; sharpenMasking = 0
        rotateQuarters = 0; straightenDeg = 0
        cropX = 0; cropY = 0; cropW = 1; cropH = 1
        hueShift = [Float](repeating: 0, count: 8)
        satShift = [Float](repeating: 0, count: 8)
        lumShift = [Float](repeating: 0, count: 8)
        suspended = false
        pushAndRender()
    }

    struct Sample {
        /// What is on screen. This is what a swatch must show.
        var display: (r: Double, g: Double, b: Double)
        /// The colour before any user adjustment. Hue bands derive from this,
        /// so adjusting a band cannot change which band you pick next.
        var scene: (r: Double, g: Double, b: Double)
    }

    func sample(u: Float, v: Float) -> Sample? {
        guard let handle, isLoaded else { return nil }
        var display = [Float](repeating: 0, count: 3)
        var scene = [Float](repeating: 0, count: 3)
        guard orion_engine_sample(handle, u, v, &display, &scene) == ORION_OK else {
            return nil
        }
        return Sample(
            display: (Double(display[0]), Double(display[1]), Double(display[2])),
            scene: (Double(scene[0]), Double(scene[1]), Double(scene[2])))
    }

    /// Per-channel histogram of the rendered image.
    func histogram(bins: Int = 128) -> [UInt32]? {
        guard let handle, isLoaded else { return nil }
        var out = [UInt32](repeating: 0, count: bins * 3)
        guard orion_engine_histogram(handle, &out, UInt32(bins)) == ORION_OK else {
            return nil
        }
        return out
    }

    func export(to path: String, quality: Float = 0.92,
                maxDimension: UInt32 = 0) throws {
        guard let handle else { return }
        var options = OrionExportOptions(format: -1, quality: quality,
                                         max_dimension: maxDimension)
        let status = orion_engine_export(handle, path, &options)
        guard status == ORION_OK else { throw Failure.export(errorText(status)) }
    }

    /// Captures every setting as a value, for undo.
    var state: DevelopState {
        DevelopState(
            temperatureK: temperatureK, tint: tint, exposureEv: exposureEv,
            highlights: highlights, shadows: shadows, whites: whites, blacks: blacks,
            vibrance: vibrance, saturation: saturation, contrast: contrast,
            rotateQuarters: rotateQuarters, straightenDeg: straightenDeg,
            cropX: cropX, cropY: cropY, cropW: cropW, cropH: cropH,
            sharpenAmount: sharpenAmount, sharpenRadius: sharpenRadius,
            sharpenMasking: sharpenMasking, curve: curve,
            hueShift: hueShift, satShift: satShift, lumShift: lumShift)
    }

    private func apply(_ s: DevelopState) {
        restoring = true
        suspended = true
        temperatureK = s.temperatureK; tint = s.tint; exposureEv = s.exposureEv
        highlights = s.highlights; shadows = s.shadows
        whites = s.whites; blacks = s.blacks
        vibrance = s.vibrance; saturation = s.saturation; contrast = s.contrast
        rotateQuarters = s.rotateQuarters; straightenDeg = s.straightenDeg
        cropX = s.cropX; cropY = s.cropY; cropW = s.cropW; cropH = s.cropH
        sharpenAmount = s.sharpenAmount; sharpenRadius = s.sharpenRadius
        sharpenMasking = s.sharpenMasking; curve = s.curve
        hueShift = s.hueShift; satShift = s.satShift; lumShift = s.lumShift
        suspended = false
        restoring = false
        pushAndRender()
    }

    /// Split compare: 1.0 means no split, lower values reveal the original
    /// from the left (or top).
    var compareSplit: Double = 1.0
    var compareVertical = true

    /// The unedited render, held so the split can show both at once without
    /// re-rendering on every drag of the divider.
    private(set) var originalTexture: MTLTexture?

    var comparing: Bool { compareSplit < 0.999 }

    /// Renders the image as shot into a held texture, then restores the edit.
    /// Two renders once, rather than two renders per frame of the divider.
    func captureOriginal() {
        guard isLoaded, let handle else { return }

        let current = state
        var neutral = DevelopState()
        neutral.temperatureK = temperatureK      // as shot is not an edit
        neutral.tint = tint
        neutral.rotateQuarters = rotateQuarters
        neutral.straightenDeg = straightenDeg
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
        if originalTexture == nil && split < 0.999 { captureOriginal() }
        compareSplit = min(max(split, 0), 1)
        generation &+= 1
    }

    func generationBump() { generation &+= 1 }

    func clearCompare() {
        compareSplit = 1.0
        originalTexture = nil
        generation &+= 1
    }

    /// The held original goes stale the moment an edit lands.
    private func invalidateOriginal() {
        if originalTexture != nil && compareSplit >= 0.999 { originalTexture = nil }
    }

    func undo() { if let s = history.undo() { apply(s) } }
    func redo() { if let s = history.redo() { apply(s) } }
    func jumpHistory(to index: Int) { if let s = history.jump(to: index) { apply(s) } }

    /// Names the control being changed, so history entries read like edits
    /// rather than like state dumps, and consecutive drags of one slider
    /// collapse into a single step.
    func edit(_ label: String, _ change: () -> Void) {
        change()
        if !restoring { history.record(state, label: label) }
    }

    private func pushAndRender() {
        guard isLoaded, !suspended, let handle else { return }
        var adj = OrionAdjustments(
            temperature_k: temperatureK, tint: tint,
            exposure_ev: exposureEv, highlights: highlights, shadows: shadows,
            whites: whites, blacks: blacks,
            vibrance: vibrance, saturation: saturation, contrast: contrast,
            rotate_quarters: rotateQuarters, straighten_deg: straightenDeg,
            crop_x: cropX, crop_y: cropY, crop_w: cropW, crop_h: cropH,
            crop_preview: cropPreview ? 1 : 0,
            preview_x: Float(previewCanvas.origin.x),
            preview_y: Float(previewCanvas.origin.y),
            preview_size: Float(previewCanvas.size),
            sharpen_amount: sharpenAmount, sharpen_radius: sharpenRadius,
            sharpen_masking: sharpenMasking,
            hue_shift: toTuple8(hueShift),
            sat_shift: toTuple8(satShift),
            lum_shift: toTuple8(lumShift),
            curve_master: curveChannel(curve.master),
            curve_red: curveChannel(curve.red),
            curve_green: curveChannel(curve.green),
            curve_blue: curveChannel(curve.blue))
        orion_engine_set_adjustments(handle, &adj)
        render()
    }

    private func render() {
        guard let handle else { return }
        var ms: Double = 0
        guard orion_engine_render(handle, &ms) == ORION_OK else { return }

        // Re-read the size every frame. A quarter turn swaps width and height,
        // and the canvas uses these to work out which part of the (square)
        // orientation texture is valid — stale values there tear the image.
        var w: UInt32 = 0, h: UInt32 = 0
        if orion_engine_image_size(handle, &w, &h) == ORION_OK {
            imageWidth = w
            imageHeight = h
        }
        var fw: UInt32 = 0, fh: UInt32 = 0
        if orion_engine_frame_size(handle, &fw, &fh) == ORION_OK {
            frameWidth = fw
            frameHeight = fh
        }

        lastRenderMs = ms
        invalidateOriginal()
        generation &+= 1
        scheduleHistogram()
    }

    /// A stand-in shown while the next photo decodes.
    private(set) var placeholder: NSImage?

    func showPlaceholder(_ image: NSImage?) {
        placeholder = image
        generation &+= 1
    }

    func clearPlaceholder() {
        guard placeholder != nil else { return }
        placeholder = nil
        generation &+= 1
    }

    private var histogramTask: Task<Void, Never>?

    /// The histogram reads back the whole output texture — ~96 MB at 24 MP —
    /// so recomputing it per render added tens of milliseconds to every slider
    /// tick. It is a readout nobody watches mid-drag, so it updates once the
    /// values settle instead.
    private func scheduleHistogram() {
        histogramTask?.cancel()
        histogramTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(160))
            guard !Task.isCancelled, let self else { return }
            if let bins = self.histogram() { self.histogramBins = bins }
        }
    }

    /// Swift imports a C float[8] as a 8-tuple, and there is no nicer bridge.
    /// A curve channel into its C form. The engine treats anything malformed
    /// as the identity, so truncating past the cap here is safe rather than
    /// silently wrong.
    private func curveChannel(_ points: [CurvePoint]) -> OrionCurveChannel {
        var c = OrionCurveChannel()
        let n = min(points.count, 16)
        c.count = Int32(n)
        withUnsafeMutableBytes(of: &c.x) { raw in
            let f = raw.bindMemory(to: Float.self)
            for i in 0..<n { f[i] = points[i].x }
        }
        withUnsafeMutableBytes(of: &c.y) { raw in
            let f = raw.bindMemory(to: Float.self)
            for i in 0..<n { f[i] = points[i].y }
        }
        return c
    }

    private func toTuple8(_ a: [Float]) -> (Float, Float, Float, Float,
                                            Float, Float, Float, Float) {
        let v = a.count >= 8 ? a : a + [Float](repeating: 0, count: 8 - a.count)
        return (v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
    }

    private func errorText(_ status: OrionStatus) -> String {
        guard let handle else { return String(cString: orion_status_string(status)) }
        let detail = String(cString: orion_last_error(handle))
        return detail.isEmpty ? String(cString: orion_status_string(status)) : detail
    }
}

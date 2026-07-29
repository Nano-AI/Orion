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
    /// normalized against this, and the crop tool's canvas is sized from it —
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
    /// The base rendering a file opens with.
    ///
    /// AgX is a *neutral* scene-referred transform, so straight out of it a
    /// photograph looks flatter than the camera's own JPEG — which is the
    /// picture the photographer saw on the back of the camera and thinks of as
    /// "the original colors". Every raw editor answers this the same way, by
    /// shipping a base contrast rather than showing the neutral transform raw.
    ///
    /// 1.15 was measured, not chosen by eye: against the embedded JPEG of the
    /// test frame it puts mean luma at 0.051 against the camera's 0.052, and
    /// mean saturation at 0.81 against 0.85. Drag the slider to 1.00 for the
    /// neutral transform.
    var contrast: Float = 1.45     { didSet { pushAndRender() } }

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

    /// Tone curve, applied after the display transform. The engine has had the
    /// spline and the LUT since M2; nothing reached them until now.
    var curve = ToneCurve()       { didSet { pushAndRender() } }

    /// Lens corrections. Manual, unless `lensProfileEnabled` is on and this
    /// photo's lens is in the database — then the measured coefficients replace
    /// both of these and the sliders are disabled rather than fighting the data.
    var lensDistortion: Float = 0 { didSet { pushAndRender() } }
    var lensVignette: Float = 0   { didSet { pushAndRender() } }

    /// Whether to apply the measured profile. On by default when one is found:
    /// a correction the lens is known to need is not a creative decision, and
    /// every other converter applies it without being asked.
    var lensProfileEnabled = true { didSet { pushAndRender() } }

    /// What the database knows about this photo's lens. Empty name means no
    /// match — which includes every manual lens, since those record no name.
    private(set) var lensProfileName = ""
    private(set) var lensProfileApproximate = false
    var hasLensProfile: Bool { !lensProfileName.isEmpty }
    var lensCaRed: Float = 0      { didSet { pushAndRender() } }
    var lensCaBlue: Float = 0     { didSet { pushAndRender() } }

    /// Highlight reconstruction. **Off by default.**
    ///
    /// It shipped defaulted on, and on a night frame it drew a magenta halo
    /// round every blown light. The fit that recovers a clipped channel is a
    /// linear model, and a linear model asked to reach far past the data it was
    /// fitted on will invent whatever it likes. The guards added since —
    /// fitting only on the highlight's shoulder, refusing to extrapolate past
    /// the brightest pixel that informed the fit, and capping each channel at a
    /// ratio the neighbourhood actually showed — make it much more
    /// conservative, but "much more conservative" is not the same as verified,
    /// and a correction that damages pictures should not be the default while
    /// that is still true.
    var highlightRecovery: Float = 0 { didSet { pushAndRender() } }

    /// Three-way colour grading. Each is [x, y, luminance] — the wheel's puck
    /// in the unit disc, then that zone's slope. The engine turns (x, y) into a
    /// zero-sum RGB offset, which is what keeps a wheel a colour control rather
    /// than a second brightness one. research/color-grading.md.
    var gradeShadow: [Float] = [0, 0, 0] { didSet { pushAndRender() } }
    var gradeMidtone: [Float] = [0, 0, 0] { didSet { pushAndRender() } }
    var gradeHighlight: [Float] = [0, 0, 0] { didSet { pushAndRender() } }

    /// Profiled wavelet denoise, in multiples of the frame's own measured
    /// noise level. Zero switches eight nodes off rather than running them at
    /// no strength.
    var denoiseLuma: Float = 0     { didSet { pushAndRender() } }
    var denoiseColor: Float = 0   { didSet { pushAndRender() } }

    /// How much of the loaded creative LUT to apply. The LUT itself is a file,
    /// loaded through `loadLut`, not an adjustment.
    var lutStrength: Float = 1     { didSet { pushAndRender() } }

    /// Exposure fusion. A power on the emitted gain, so zero is exact.
    var fusion: Float = 0          { didSet { pushAndRender() } }

    /// Dehaze. The dark channel prior's own omega, so zero is the identity.
    var dehaze: Float = 0          { didSet { pushAndRender() } }

    /// Local Laplacian clarity. Negative smooths detail, positive increases
    /// its contrast; the endpoints are the exponents Paris et al. illustrate.
    var clarity: Float = 0         { didSet { pushAndRender() } }

    var sharpenAmount: Float = 0   { didSet { pushAndRender() } }
    var sharpenRadius: Float = 1   { didSet { pushAndRender() } }
    var sharpenMasking: Float = 0  { didSet { pushAndRender() } }

    /// Color mixer, eight bands. Index order matches HueBand.allCases.
    var hueShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }
    var satShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }
    var lumShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }

    /// True while a slider is being dragged, so pushes are suppressed until
    /// the value settles. Not used yet — hook for degrade-then-refine.
    /// Called with the new state after every adjustment lands, so the shell can
    /// persist it. The engine deliberately knows nothing about files, URLs or
    /// sidecars — it reports *that* something changed and hands over the value;
    /// what that is worth is the shell's call.
    ///
    /// The state is passed rather than read back, so the callback need not
    /// capture the engine — which would be a retain cycle — and so a deferred
    /// write cannot pick up a *later* state than the one it was queued for.
    @ObservationIgnored var onEdit: ((DevelopState) -> Void)?

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

    /// The loaded creative LUT's title, or "" when none is loaded.
    private(set) var lutName: String = ""

    /// Why the last load failed, for the panel to show. A LUT that will not
    /// load is the user's file being wrong, not the app misbehaving, so the
    /// reason has to reach them — the parser names the line.
    private(set) var lutError: String?

    func loadLut(path: String, displayName: String) {
        guard let handle else { return }

        let status = orion_engine_load_lut(handle, path)
        guard status == ORION_OK else {
            lutError = errorText(status)
            return
        }

        lutError = nil
        var buffer = [CChar](repeating: 0, count: 256)
        orion_engine_lut_title(handle, &buffer, Int32(buffer.count))
        let title = String(cString: buffer)
        // A .cube is not obliged to carry a TITLE, and most do not. The file's
        // own name is what the user picked and what they will look for.
        lutName = title.isEmpty ? displayName : title
        pushAndRender()
    }

    func clearLut() {
        guard let handle else { return }
        orion_engine_clear_lut(handle)
        lutName = ""
        lutError = nil
        pushAndRender()
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

        var profile = OrionLensProfile()
        if orion_engine_lens_profile(handle, &profile) == ORION_OK, profile.found != 0 {
            let name = withUnsafeBytes(of: profile.lens) { raw in
                String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
            }
            let maker = withUnsafeBytes(of: profile.maker) { raw in
                String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
            }
            lensProfileName = name.hasPrefix(maker) || maker.isEmpty
                ? name : "\(maker) \(name)"
            lensProfileApproximate = profile.approximate != 0
        } else {
            lensProfileName = ""
            lensProfileApproximate = false
        }

        // The held original is the *previous* photo's unedited render. Keeping
        // it means compare shows one picture against another one entirely,
        // which is worse than showing nothing.
        originalTexture = nil

        // Reset to the camera's own settings before marking loaded, so the
        // didSet observers don't each trigger a render on a half-set model.
        suspended = true
        defaults = asShotState()
        assign(defaults)
        suspended = false

        isLoaded = true
        history.reset(to: state)
        pushAndRender()

        // Compare stays on across a switch — that is the point of it while
        // culling — so the new photo needs its own original captured now.
        if comparing { captureOriginal() }
    }

    /// Restores a state saved to a sidecar. Silent on malformed data: a
    /// sidecar written by a newer build should leave the photo openable.
    func restore(encoded: Data) {
        guard let s = try? JSONDecoder().decode(DevelopState.self, from: encoded) else {
            return
        }
        suspended = true
        assign(s)
        suspended = false
        history.reset(to: s)
        pushAndRender()
        if comparing { captureOriginal() }
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

        // The held original is the wrong shape now — a quarter turn swaps the
        // output's width and height, and the split was sampling a texture whose
        // valid rectangle no longer matches.
        if comparing { captureOriginal() }
    }

    /// Returns every adjustment to its default, with white balance back to
    /// what the camera chose. One push, one render.
    func resetEdits() {
        guard isLoaded else { return }
        suspended = true
        assign(defaults)
        suspended = false
        pushAndRender()
        history.record(state, label: "Reset")
    }

    struct Sample {
        /// What is on screen. This is what a swatch must show.
        var display: (r: Double, g: Double, b: Double)
        /// The color before any user adjustment. Hue bands derive from this,
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
                maxDimension: UInt32 = 0, space: Int32 = 0,
                rating: Int32 = -1, metadata: Int32 = 1) throws {
        guard let handle else { return }
        var options = OrionExportOptions(format: -1, quality: quality,
                                         max_dimension: maxDimension, space: space,
                                         rating: rating, metadata: metadata)
        let status = orion_engine_export(handle, path, &options)
        guard status == ORION_OK else { throw Failure.export(errorText(status)) }
    }

    /// Encodes with these options and reports the byte count without writing.
    /// Real work — a 24 MP JPEG is about a sixth of a second — so callers
    /// debounce it.
    func exportedSize(format: Int32, quality: Float, maxDimension: UInt32,
                      space: Int32 = 0) -> Int? {
        guard let handle else { return nil }
        // No rating and no metadata source: the estimate measures the pixels,
        // and a few hundred bytes of EXIF is below its resolution anyway.
        var options = OrionExportOptions(format: format, quality: quality,
                                         max_dimension: maxDimension, space: space,
                                         rating: -1, metadata: 1)
        var bytes: UInt64 = 0
        guard orion_engine_export_size(handle, &options, &bytes) == ORION_OK else {
            return nil
        }
        return Int(bytes)
    }

    /// Captures every setting as a value, for undo.
    var state: DevelopState {
        DevelopState(
            temperatureK: temperatureK, tint: tint, exposureEv: exposureEv,
            highlights: highlights, shadows: shadows, whites: whites, blacks: blacks,
            vibrance: vibrance, saturation: saturation, contrast: contrast,
            rotateQuarters: rotateQuarters, straightenDeg: straightenDeg,
            cropX: cropX, cropY: cropY, cropW: cropW, cropH: cropH,
            lensDistortion: lensDistortion, lensVignette: lensVignette,
            lensCaRed: lensCaRed, lensCaBlue: lensCaBlue,
            highlightRecovery: highlightRecovery,
            gradeShadow: gradeShadow, gradeMidtone: gradeMidtone,
            gradeHighlight: gradeHighlight,
            denoiseLuma: denoiseLuma, denoiseColor: denoiseColor,
            lutStrength: lutStrength,
            fusion: fusion, dehaze: dehaze, clarity: clarity,
            sharpenAmount: sharpenAmount, sharpenRadius: sharpenRadius,
            sharpenMasking: sharpenMasking, curve: curve,
            hueShift: hueShift, satShift: satShift, lumShift: lumShift)
    }

    /// Assigns every adjustment from a state, without rendering.
    ///
    /// The only place that lists the fields. Open and Reset each used to carry
    /// their own list, and each was missing whatever had been added since —
    /// Reset left denoise, the lens corrections and the curve applied, which
    /// makes a Reset button that does not reset.
    private func assign(_ s: DevelopState) {
        temperatureK = s.temperatureK; tint = s.tint; exposureEv = s.exposureEv
        highlights = s.highlights; shadows = s.shadows
        whites = s.whites; blacks = s.blacks
        vibrance = s.vibrance; saturation = s.saturation; contrast = s.contrast
        rotateQuarters = s.rotateQuarters; straightenDeg = s.straightenDeg
        cropX = s.cropX; cropY = s.cropY; cropW = s.cropW; cropH = s.cropH
        lensDistortion = s.lensDistortion; lensVignette = s.lensVignette
        lensCaRed = s.lensCaRed; lensCaBlue = s.lensCaBlue
        highlightRecovery = s.highlightRecovery
        gradeShadow = s.gradeShadow
        gradeMidtone = s.gradeMidtone
        gradeHighlight = s.gradeHighlight
        denoiseLuma = s.denoiseLuma; denoiseColor = s.denoiseColor
        lutStrength = s.lutStrength
        fusion = s.fusion
        dehaze = s.dehaze
        clarity = s.clarity
        sharpenAmount = s.sharpenAmount; sharpenRadius = s.sharpenRadius
        sharpenMasking = s.sharpenMasking; curve = s.curve
        hueShift = s.hueShift; satShift = s.satShift; lumShift = s.lumShift
    }

    private func apply(_ s: DevelopState) {
        restoring = true
        suspended = true
        assign(s)
        suspended = false
        restoring = false
        pushAndRender()
    }

    /// A fresh state carrying the camera's own white balance, which is the
    /// starting point rather than an edit.
    /// What every control returns to, and what the panel compares against to
    /// decide whether a control has been touched.
    ///
    /// A fresh `DevelopState` in all but white balance, which is the camera's
    /// own reading and so belongs to the photo rather than to the app —
    /// "reset temperature" has to mean the camera's number, not 5500 K.
    private(set) var defaults = DevelopState()

    private func asShotState() -> DevelopState {
        var fresh = DevelopState()
        guard let handle else { return fresh }
        var asShot = OrionAdjustments()
        orion_engine_as_shot(handle, &asShot)
        fresh.temperatureK = asShot.temperature_k
        fresh.tint = asShot.tint
        return fresh
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
        // The split goes first. captureOriginal re-renders twice, and every
        // render calls invalidateOriginal, which drops the held texture while
        // the split still reads 1.0 — so the original was captured and thrown
        // away in the same call, and compare showed the edit on both sides.
        compareSplit = min(max(split, 0), 1)
        if originalTexture == nil && comparing { captureOriginal() }
        generation &+= 1
    }

    func generationBump() { generation &+= 1 }

    /// Sixteen bits out of the tail of the graph instead of eight.
    ///
    /// The screen path is eight bits: the drawable is `bgra8Unorm`, so wider is
    /// bytes moved for precision nothing can show, and it costs about 3.5 ms of
    /// a 16 ms budget. Export widens the tail itself. This is here for the
    /// screenshot harness, which reads the output texture directly and measures
    /// to four decimal places — quantised to 8 bits, the differences this
    /// codebase hunts (0.0001 chroma) round to nothing.
    ///
    /// Reallocates two full-resolution textures. Not for a slider.
    func setWideOutput(_ wide: Bool) {
        guard let handle, isLoaded else { return }
        _ = orion_engine_set_wide_output(handle, wide ? 1 : 0)
        pushAndRender()
    }

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
            lens_distortion: lensDistortion, lens_vignette: lensVignette,
            lens_ca_red: lensCaRed, lens_ca_blue: lensCaBlue,
            lens_profile: (lensProfileEnabled && hasLensProfile) ? 1 : 0,
            highlight_recovery: highlightRecovery,
            grade_shadow: (gradeShadow[0], gradeShadow[1], gradeShadow[2]),
            grade_midtone: (gradeMidtone[0], gradeMidtone[1], gradeMidtone[2]),
            grade_highlight: (gradeHighlight[0], gradeHighlight[1], gradeHighlight[2]),
            denoise_luma: denoiseLuma, denoise_color: denoiseColor,
            lut_strength: lutStrength,
            fusion: fusion, dehaze: dehaze, clarity: clarity,
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
        onEdit?(state)
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

    /// A still of the developed image, drawn in place of the Metal canvas.
    ///
    /// Only the screenshot harness sets this: AppKit cannot capture a Metal
    /// layer, so a still is the only way to photograph the interface. It used
    /// to double as a stand-in during a photo switch, showing the camera's
    /// embedded JPEG until the render landed — but that preview carries its own
    /// orientation, so opening a portrait frame drew it landscape and then
    /// snapped. Holding the previous frame for the 26 ms decode is calmer than
    /// showing a picture that turns.
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

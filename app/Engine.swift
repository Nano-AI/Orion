import Foundation
import Metal

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
    var rotateQuarters: Int32 = 0  { didSet { pushAndRender() } }

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
        pushAndRender()
    }

    /// Rotates by a quarter turn, wrapping. Clockwise is positive.
    func rotate(_ turns: Int32) {
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
        rotateQuarters = 0
        hueShift = [Float](repeating: 0, count: 8)
        satShift = [Float](repeating: 0, count: 8)
        lumShift = [Float](repeating: 0, count: 8)
        suspended = false
        pushAndRender()
    }

    /// Rendered colour at normalised image coordinates.
    func sample(u: Float, v: Float) -> (r: Double, g: Double, b: Double)? {
        guard let handle, isLoaded else { return nil }
        var rgb = [Float](repeating: 0, count: 3)
        guard orion_engine_sample(handle, u, v, &rgb) == ORION_OK else { return nil }
        return (Double(rgb[0]), Double(rgb[1]), Double(rgb[2]))
    }

    func export(to path: String, quality: Float = 0.92, maxDimension: UInt32 = 0) throws {
        guard let handle else { return }
        var options = OrionExportOptions(format: -1, quality: quality,
                                         max_dimension: maxDimension)
        let status = orion_engine_export(handle, path, &options)
        guard status == ORION_OK else { throw Failure.export(errorText(status)) }
    }

    private func pushAndRender() {
        guard isLoaded, !suspended, let handle else { return }
        var adj = OrionAdjustments(
            temperature_k: temperatureK, tint: tint,
            exposure_ev: exposureEv, highlights: highlights, shadows: shadows,
            whites: whites, blacks: blacks,
            vibrance: vibrance, saturation: saturation, contrast: contrast,
            rotate_quarters: rotateQuarters,
            sharpen_amount: sharpenAmount, sharpen_radius: sharpenRadius,
            sharpen_masking: sharpenMasking,
            hue_shift: toTuple8(hueShift),
            sat_shift: toTuple8(satShift),
            lum_shift: toTuple8(lumShift))
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

        lastRenderMs = ms
        generation &+= 1
    }

    /// Swift imports a C float[8] as a 8-tuple, and there is no nicer bridge.
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

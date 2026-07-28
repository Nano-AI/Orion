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
            vibrance: vibrance, saturation: saturation, contrast: contrast)
        orion_engine_set_adjustments(handle, &adj)
        render()
    }

    private func render() {
        guard let handle else { return }
        var ms: Double = 0
        if orion_engine_render(handle, &ms) == ORION_OK {
            lastRenderMs = ms
            generation &+= 1
        }
    }

    private func errorText(_ status: OrionStatus) -> String {
        guard let handle else { return String(cString: orion_status_string(status)) }
        let detail = String(cString: orion_last_error(handle))
        return detail.isEmpty ? String(cString: orion_status_string(status)) : detail
    }
}

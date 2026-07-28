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
        case render(String)

        var errorDescription: String? {
            switch self {
            case .create(let m): return "Could not start the engine: \(m)"
            case .open(let m):   return "Could not open that photo: \(m)"
            case .render(let m): return "Rendering failed: \(m)"
            }
        }
    }

    private(set) var camera = ""
    private(set) var imageWidth: UInt32 = 0
    private(set) var imageHeight: UInt32 = 0
    private(set) var lastRenderMs: Double = 0
    private(set) var isLoaded = false

    var exposureEv: Float = 0 { didSet { pushAndRender() } }
    var black: Float = 0      { didSet { pushAndRender() } }
    var contrast: Float = 1   { didSet { pushAndRender() } }
    var saturation: Float = 1 { didSet { pushAndRender() } }

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
        isLoaded = true

        exposureEv = 0
        render()
    }

    private func pushAndRender() {
        guard isLoaded, let handle else { return }
        var adj = OrionAdjustments(exposure_ev: exposureEv, black: black,
                                   contrast: contrast, saturation: saturation)
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

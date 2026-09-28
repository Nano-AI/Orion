import CoreGraphics
import Foundation
import Vision

/// Vision's face rectangles, carried into the two spaces a caller needs them
/// in: display space, where `get_proxy --region` and `get_stats --region`
/// read, and frame space, where a mask component's `centerX`/`radiusX` live.
///
/// ⚠ **One implementation, two callers.** The `faces` scenario verb
/// (`Scenario+Report.swift`) prints these numbers and `Orion --agent faces`
/// returns them as JSON; a second copy of the Vision-box-to-frame-space
/// arithmetic would let one of them agree with the mask panel while the other
/// quietly did not. Decision #278's `measure`/`regionStats` rule, applied to
/// the detector.
///
/// ⚠ Vision jitters: the same photograph detected twice moves a box by around
/// 0.01, so nothing here should be asserted to more than two decimals.
enum AgentFaces {

    /// One face. `display` is the box in display space (top-left origin, the
    /// same fractions a `--region` takes); `center`/`radius` are that box
    /// carried through `Engine.frameDisplayMap` into frame space, ready to
    /// paste into a kind-2 component's centerX/centerY/radiusX/radiusY.
    struct Face {
        var display = CGRect.zero
        var centerX = 0.0, centerY = 0.0
        var radiusX = 0.0, radiusY = 0.0
    }

    enum Failure: Error, CustomStringConvertible {
        case noPhoto
        case render
        case detector(String)

        var description: String {
            switch self {
            case .noPhoto: return "no photo open"
            case .render: return "could not render for the detector"
            case .detector(let why): return "the face detector failed - \(why)"
            }
        }
    }

    /// The detector, at the size it is trained for: 1024 px on the long edge,
    /// never upscaled. Bigger buys nothing and costs a full-resolution read.
    @MainActor
    static func detect(engine: Engine) throws -> [Face] {
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0 else { throw Failure.noPhoto }
        let scale = min(1.0, 1024.0 / Double(max(w, h)))
        let size = (width: max(1, Int(Double(w) * scale)),
                    height: max(1, Int(Double(h) * scale)))
        guard let image = Screenshot.developedCGImage(engine, fitting: size) else {
            throw Failure.render
        }

        let request = VNDetectFaceRectanglesRequest()
        do { try VNImageRequestHandler(cgImage: image, options: [:]).perform([request]) }
        catch { throw Failure.detector(error.localizedDescription) }

        let map = engine.frameDisplayMap
        return (request.results ?? []).map { result in
            let b = result.boundingBox
            // Vision's origin is bottom-left; display space is top-left.
            let d = CGRect(x: b.minX, y: 1 - b.maxY, width: b.width, height: b.height)
            let c = map.frame(CGPoint(x: d.midX, y: d.midY))
            let ex = map.frame(CGPoint(x: d.maxX, y: d.midY))
            let ey = map.frame(CGPoint(x: d.midX, y: d.maxY))
            // The larger of the two, per axis: a turned or keystoned frame
            // sends the box's own edges off the axes, and a radius that took
            // only one of them would under-cover the face.
            return Face(display: d,
                        centerX: Double(c.x), centerY: Double(c.y),
                        radiusX: Double(max(abs(ex.x - c.x), abs(ey.x - c.x))),
                        radiusY: Double(max(abs(ex.y - c.y), abs(ey.y - c.y))))
        }
    }

    /// The `faces` verb's payload. Field names are what `mcp/server.ts`'s
    /// `detect_faces` hands the model, so they are not renamed casually.
    static func json(_ faces: [Face]) -> [[String: Any]] {
        faces.map { f in
            ["x": Double(f.display.minX), "y": Double(f.display.minY),
             "w": Double(f.display.width), "h": Double(f.display.height),
             "centerX": f.centerX, "centerY": f.centerY,
             "radiusX": f.radiusX, "radiusY": f.radiusY]
        }
    }
}

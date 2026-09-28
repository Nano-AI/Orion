import AppKit
import Foundation

/// The inspection half of the agent surface: a region of the developed
/// picture at native resolution, the numbers a region measures, and the face
/// boxes. Reuses the mechanisms the retired socket surface left behind -
/// `Screenshot.developedCGImage`, `Screenshot.regionStats`, `AgentFaces` -
/// rather than growing a second copy of any of them.
///
/// Its own file for the reason `AgentComposite.swift` is: `AgentCLI.swift`
/// and `AgentCLIDriver.swift` are both already at the size a three-file
/// feature should stay under.
///
/// ⚠ Why a region at all. A 42 MP frame at the 2048 px cap is a twentieth of
/// its own width: sharpening, skin texture, fringing and a mask's edge are all
/// below that floor, so a whole-frame proxy cannot show any of them and a
/// model looking at one will say the sharpening is fine because it cannot see
/// it. This is the only way to actually look.
extension AgentCLI {

    // MARK: proxy --region

    /// The developed picture rendered at full size, cropped to `region`, and
    /// scaled down **only** if the crop's long edge is over `max`. A crop that
    /// already fits comes back at native resolution, which is the whole point.
    @MainActor
    static func runProxyRegion(engine: Engine, region: Region, max: UInt32, out: String)
        throws -> [String: Any] {
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0 else { throw Failure.run("no photo open") }
        guard let full = Screenshot.developedCGImage(engine, fitting: (width: w, height: h))
        else { throw Failure.run("could not render \(w)x\(h)") }

        // Rounded out to whole pixels, then clamped: a region of 0.0001 must
        // still be a pixel rather than an empty crop CoreGraphics refuses.
        let x0 = Swift.min(w - 1, Swift.max(0, Int((region.x * Double(w)).rounded(.down))))
        let y0 = Swift.min(h - 1, Swift.max(0, Int((region.y * Double(h)).rounded(.down))))
        let cw = Swift.min(w - x0, Swift.max(1, Int((region.w * Double(w)).rounded())))
        let ch = Swift.min(h - y0, Swift.max(1, Int((region.h * Double(h)).rounded())))
        guard let crop = full.cropping(
                to: CGRect(x: x0, y: y0, width: cw, height: ch)) else {
            throw Failure.run("could not crop \(cw)x\(ch) at \(x0),\(y0)")
        }

        let cap = Int(max)
        let image: CGImage
        if cap > 0 && Swift.max(cw, ch) > cap {
            let scale = Double(cap) / Double(Swift.max(cw, ch))
            let sw = Swift.max(1, Int((Double(cw) * scale).rounded()))
            let sh = Swift.max(1, Int((Double(ch) * scale).rounded()))
            guard let ctx = CGContext(data: nil, width: sw, height: sh,
                                      bitsPerComponent: 8, bytesPerRow: 0,
                                      space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue),
                  let scaled = { () -> CGImage? in
                      ctx.interpolationQuality = .high
                      ctx.draw(crop, in: CGRect(x: 0, y: 0, width: sw, height: sh))
                      return ctx.makeImage()
                  }() else { throw Failure.run("could not scale the crop to \(sw)x\(sh)") }
            image = scaled
        } else {
            image = crop
        }

        // The same quality `proxy` writes at, through NSBitmapImageRep rather
        // than Engine.export: export renders the whole frame and this has a
        // crop in hand already.
        let rep = NSBitmapImageRep(cgImage: image)
        guard let jpeg = rep.representation(using: .jpeg,
                                            properties: [.compressionFactor: 0.85]) else {
            throw Failure.run("could not encode \(out) as a JPEG")
        }
        try jpeg.write(to: URL(fileURLWithPath: out), options: .atomic)
        return ["path": out, "width": image.width, "height": image.height,
                "bytes": jpeg.count]
    }

    // MARK: stats --region

    /// Every field of a `RegionStats.Stats`, for the display-space rectangle
    /// given. The product caller `RegionStats.stats` did not have until now -
    /// see `tools/check-wiring.py`, whose allowlist entry for it was written
    /// as a promise that this would arrive.
    @MainActor
    static func regionJSON(engine: Engine, region: Region) throws -> [String: Any] {
        let rect = CGRect(x: region.x, y: region.y, width: region.w, height: region.h)
        guard let s = Screenshot.regionStats(engine, region: rect) else {
            throw Failure.run("could not read the region - is a photo open")
        }
        return [
            "x": region.x, "y": region.y, "w": region.w, "h": region.h,
            "luma": s.luma, "saturation": s.saturation,
            "hue": s.hue, "hueStrength": s.hueStrength,
            "red": s.red, "green": s.green, "blue": s.blue,
            "clippedHigh": s.clippedHigh, "clippedLow": s.clippedLow,
            "shading": s.shading,
        ]
    }

    // MARK: faces

    @MainActor
    static func runFaces(raw: String, state: String?) throws -> [String: Any] {
        let url = URL(fileURLWithPath: raw)
        let sidecar = Sidecar.read(for: url)
        let engine = try openEngine(url: url, sidecarDevelop: sidecar?.develop, state: state)
        do {
            return ["path": raw, "faces": AgentFaces.json(try AgentFaces.detect(engine: engine))]
        } catch let error as AgentFaces.Failure {
            throw Failure.run(error.description)
        }
    }
}

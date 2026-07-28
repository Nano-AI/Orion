import AppKit
import Metal
import SwiftUI

/// Renders the real interface to a PNG, offscreen, with no window on screen.
///
/// Built because `screencapture` needs Screen Recording permission that a
/// terminal does not have, and because a screenshot you have to take by hand is
/// a screenshot nobody takes. This one is a command:
///
/// ```
/// Orion.app/Contents/MacOS/Orion --screenshot out.png --photo x.ARW --scene crop
/// ```
///
/// What it does and does not prove: the view hierarchy is the shipping one, so
/// layout, type, spacing and colour are real. The canvas is the engine's own
/// developed output, read back through the export path, drawn as a still rather
/// than through `MTKView` — AppKit's `cacheDisplay` does not capture a Metal
/// layer. Canvas-specific geometry stays the viewport suite's job.
enum Screenshot {

    struct Options {
        var output = "orion.png"
        var photo: String?
        var scene = "light"
        var size = CGSize(width: 1680, height: 1050)
        /// Region to report statistics for, normalised. Empty means no report.
        var measure: CGRect?
    }

    /// Parses the command line. Returns nil when this is an ordinary launch.
    static func options(_ arguments: [String]) -> Options? {
        guard arguments.contains("--screenshot") else { return nil }

        var o = Options()
        var i = 0
        while i < arguments.count {
            let flag = arguments[i]
            let next: String? = i + 1 < arguments.count ? arguments[i + 1] : nil
            switch flag {
            case "--screenshot": if let next { o.output = next; i += 1 }
            case "--photo":      if let next { o.photo = next; i += 1 }
            case "--scene":      if let next { o.scene = next; i += 1 }
            case "--measure":
                if let next {
                    let n = next.split(separator: ",").compactMap { Double($0) }
                    if n.count == 4 {
                        o.measure = CGRect(x: n[0], y: n[1], width: n[2], height: n[3])
                    }
                    i += 1
                }
            case "--size":
                if let next {
                    let parts = next.split(separator: "x").compactMap { Double($0) }
                    if parts.count == 2 { o.size = CGSize(width: parts[0], height: parts[1]) }
                    i += 1
                }
            default: break
            }
            i += 1
        }
        return o
    }

    /// Runs the capture and exits. Never returns.
    static func run(_ o: Options) -> Never {
        // A background accessory app: no Dock icon, no menu bar, no window.
        NSApplication.shared.setActivationPolicy(.accessory)

        guard let engine = try? Engine() else {
            fail("could not start the engine")
        }

        if let photo = o.photo {
            do { try engine.open(path: photo) }
            catch { fail("could not open \(photo) — \(error.localizedDescription)") }
            apply(scene: o.scene, to: engine)
            let image = developed(engine)
            if let region = o.measure { measure(engine, region: region) }
            engine.showPlaceholder(image)
        }

        // The export sheet is not reachable from the editor's own hierarchy in
        // a still, so it is rendered on its own.
        if o.scene == "export" {
            let settings = ExportSettings()
            settings.quality = 0.82
            settings.size = .custom
            settings.setCustom(width: 3000, sourceWidth: engine.imageWidth,
                               sourceHeight: engine.imageHeight)
            let measured = engine.exportedSize(
                format: settings.format.code, quality: Float(settings.quality),
                maxDimension: settings.longestEdge(sourceWidth: engine.imageWidth,
                                                   sourceHeight: engine.imageHeight))
            settings.measuredBytes = measured

            // Captured, not read back off the settings the panel is about to
            // clear — otherwise the panel measures the value it just erased.
            let estimate = settings.estimatedBytes(sourceWidth: engine.imageWidth,
                                                   sourceHeight: engine.imageHeight)
            FileHandle.standardError.write(Data(
                "orion: estimate \(estimate) bytes, measured \(measured ?? -1) bytes\n".utf8))

            let panel = ExportPanel(settings: settings,
                                    sourceWidth: engine.imageWidth,
                                    sourceHeight: engine.imageHeight,
                                    measure: { measured },
                                    onExport: {}, onCancel: {})
                .preferredColorScheme(.dark)

            let sheetSize = CGSize(width: 380, height: 520)
            guard let sheet = render(panel, size: sheetSize) else {
                fail("the export panel produced no image")
            }
            do { try sheet.write(to: URL(fileURLWithPath: o.output)) }
            catch { fail("could not write \(o.output)") }
            FileHandle.standardError.write(Data("orion: wrote \(o.output) (export)\n".utf8))
            exit(0)
        }

        let view = Editor(engine: engine, startTab: tab(for: o.scene))
            .frame(width: o.size.width, height: o.size.height)
            .preferredColorScheme(.dark)

        guard let png = render(view, size: o.size) else {
            fail("the view produced no image")
        }

        do { try png.write(to: URL(fileURLWithPath: o.output)) }
        catch { fail("could not write \(o.output) — \(error.localizedDescription)") }

        let note = "orion: wrote \(o.output) "
            + "(\(Int(o.size.width))x\(Int(o.size.height)), scene \(o.scene))\n"
        FileHandle.standardError.write(Data(note.utf8))
        exit(0)
    }

    // MARK: Scenes

    private static func tab(for scene: String) -> ToolTab {
        switch scene {
        case "colour":
            return .colour
        case "detail", "noisy", "denoise-off", "denoise-luma", "denoise-both":
            return .detail
        case "crop", "crop-angle":
            return .crop
        default:
            return .light
        }
    }

    /// Each scene is a state the interface can actually be in. Kept here rather
    /// than in a script so a scene is reviewable alongside the code it exercises.
    private static func apply(scene: String, to engine: Engine) {
        switch scene {
        case "light":
            engine.exposureEv = 2.6
            engine.highlights = -0.4
            engine.shadows = 0.45
        case "tone":
            engine.exposureEv = 4.2
            engine.highlights = -1.0
            engine.shadows = 0.8
            engine.blacks = -0.3
        case "curve":
            engine.exposureEv = 2.6
            engine.curve.master = [CurvePoint(x: 0, y: 0.04), CurvePoint(x: 0.28, y: 0.19),
                                   CurvePoint(x: 0.68, y: 0.79), CurvePoint(x: 1, y: 1)]
        case "colour":
            engine.exposureEv = 2.6
            engine.vibrance = 0.3
            engine.satShift[HueBand.blue.rawValue] = 0.4
        case "detail":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 1.6
            engine.denoiseColour = 2.4
            engine.sharpenAmount = 0.8
            engine.sharpenMasking = 0.4
        case "crop":
            engine.exposureEv = 2.6
            engine.cropPreview = true
            engine.setCrop(x: 0.1, y: 0.08, w: 0.62, h: 0.7)
        case "crop-angle":
            engine.exposureEv = 2.6
            engine.cropPreview = true
            engine.straightenDeg = 12
            engine.setCrop(x: 0.1, y: 0.08, w: 0.62, h: 0.7)
        case "noisy":
            engine.exposureEv = 2.6
        case "denoise-off":
            engine.exposureEv = 2.6
        case "denoise-luma":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 2.0
        case "denoise-both":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 2.0
            engine.denoiseColour = 3.0
        case "compare":
            engine.exposureEv = 2.6
            engine.setCompare(split: 0.5)
        default:
            break
        }
    }

    /// The engine's developed output, read straight off the GPU.
    ///
    /// Straight off rather than through the export path, because export flattens
    /// alpha — and alpha is exactly what says "no picture here" outside a turned
    /// frame. Flattened, the crop preview's surround came out black instead of
    /// the interface's own grey, which would have made every straighten
    /// screenshot a lie.
    private static func developed(_ engine: Engine) -> NSImage? {
        guard let src = engine.outputTexture else { return nil }

        // Only the top-left rectangle of the texture is live; the rest is the
        // slack the graph allocates so a rotation never needs a recompile.
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0, w <= src.width, h <= src.height else { return nil }

        let stride = w * 4
        var pixels = [UInt8](repeating: 0, count: stride * h)
        pixels.withUnsafeMutableBytes { raw in
            src.getBytes(raw.baseAddress!, bytesPerRow: stride,
                         from: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0)
        }

        // The pipeline writes BGRA; CoreGraphics is told so rather than the
        // channels being swapped by hand.
        let info: CGBitmapInfo = [.byteOrder32Little,
                                  CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)]
        guard let provider = CGDataProvider(data: Data(pixels) as CFData),
              let cg = CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32,
                               bytesPerRow: stride,
                               space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: info,
                               provider: provider, decode: nil, shouldInterpolate: true,
                               intent: .defaultIntent)
        else { return nil }

        return NSImage(cgImage: cg, size: NSSize(width: w, height: h))
    }

    /// Prints the mean and standard deviation of a region of the engine's
    /// output.
    ///
    /// Whether a filter works is a question about pixels, and a screenshot
    /// scaled to fit a review pane cannot answer it — noise that is obvious at
    /// 100% disappears into the downsampling. This reads the numbers.
    private static func measure(_ engine: Engine, region: CGRect) {
        guard let src = engine.outputTexture else { return }
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0 else { return }

        let x0 = max(0, min(w - 1, Int(region.minX * CGFloat(w))))
        let y0 = max(0, min(h - 1, Int(region.minY * CGFloat(h))))
        let rw = max(1, min(w - x0, Int(region.width * CGFloat(w))))
        let rh = max(1, min(h - y0, Int(region.height * CGFloat(h))))

        let stride = rw * 4
        var pixels = [UInt8](repeating: 0, count: stride * rh)
        pixels.withUnsafeMutableBytes { raw in
            src.getBytes(raw.baseAddress!, bytesPerRow: stride,
                         from: MTLRegionMake2D(x0, y0, rw, rh), mipmapLevel: 0)
        }

        // BGRA, and the alpha channel is not a measurement.
        var sums = [Double](repeating: 0, count: 3)
        var squares = [Double](repeating: 0, count: 3)
        let n = Double(rw * rh)
        for i in stride_pixels(count: rw * rh) {
            for c in 0..<3 {
                let v = Double(pixels[i * 4 + (2 - c)]) / 255
                sums[c] += v
                squares[c] += v * v
            }
        }

        var report = "orion: region \(rw)x\(rh) at (\(x0),\(y0))\n"
        for (c, name) in ["R", "G", "B"].enumerated() {
            let mean = sums[c] / n
            let variance = max(0, squares[c] / n - mean * mean)
            report += String(format: "  %@  mean %.5f  sd %.5f\n",
                             name, mean, variance.squareRoot())
        }
        FileHandle.standardError.write(Data(report.utf8))
    }

    private static func stride_pixels(count: Int) -> StrideTo<Int> {
        stride(from: 0, to: count, by: 1)
    }

    // MARK: Rendering

    private static func render<V: View>(_ view: V, size: CGSize) -> Data? {
        let hosting = NSHostingView(rootView: view)
        hosting.frame = CGRect(origin: .zero, size: size)

        // A window, even an invisible one: SwiftUI defers a good deal of work
        // until a view is in one, and a detached hierarchy renders empty.
        let window = NSWindow(contentRect: hosting.frame,
                              styleMask: [.borderless],
                              backing: .buffered, defer: false)
        window.contentView = hosting
        window.appearance = NSAppearance(named: .darkAqua)
        window.setFrameOrigin(NSPoint(x: -20000, y: -20000))
        window.orderFront(nil)

        // SwiftUI lays out and loads asynchronously. Turn the runloop until it
        // settles; capturing on the first pass gives a half-built interface.
        for _ in 0..<12 {
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        }
        hosting.layoutSubtreeIfNeeded()
        hosting.displayIfNeeded()

        guard let rep = hosting.bitmapImageRepForCachingDisplay(in: hosting.bounds) else {
            return nil
        }
        hosting.cacheDisplay(in: hosting.bounds, to: rep)
        window.orderOut(nil)
        return rep.representation(using: .png, properties: [:])
    }

    private static func fail(_ message: String) -> Never {
        FileHandle.standardError.write(Data("orion: \(message)\n".utf8))
        exit(1)
    }
}

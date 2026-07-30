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
/// layout, type, spacing and color are real. The canvas is the engine's own
/// developed output, read back through the export path, drawn as a still rather
/// than through `MTKView` — AppKit's `cacheDisplay` does not capture a Metal
/// layer. Canvas-specific geometry stays the viewport suite's job.
enum Screenshot {

    struct Options {
        var output = "orion.png"
        var photo: String?
        var scene = "light"
        var size = CGSize(width: 1680, height: 1050)
        /// Regions to report statistics for, normalized. Repeat the flag for
        /// several. Repeating beats one-region-per-run because each run pays a
        /// RAW decode and a full render, and a calibration sweep is dozens of
        /// runs.
        var measure: [CGRect] = []
        /// Exposure offset in EV, applied on top of whatever the scene sets.
        /// Exists so a calibration sweep is a loop over one flag rather than a
        /// scene per value.
        var exposure: Float?
        /// Base contrast override, for the same reason.
        var contrast: Float?
        /// Runs the Auto button, through the same facade call the panel uses.
        ///
        /// The bench has an auto-enhance probe, but it drives the policy
        /// directly — so the two disagreeing is invisible to it. This flag is
        /// the app's own path, which is where a reported failure was.
        var auto = false
        /// Repeats Auto, to catch a control that only misbehaves when the
        /// measurement it starts from is already an auto-enhanced frame.
        var autoTimes = 1
        /// Tone endpoints, set directly. The pair is what a reported black frame
        /// pointed at: whites low and blacks high put the white point below the
        /// black point, and nothing in the interface stops them crossing.
        var whites: Float?
        var blacks: Float?
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
                        o.measure.append(CGRect(x: n[0], y: n[1],
                                                width: n[2], height: n[3]))
                    }
                    i += 1
                }
            case "--exposure":
                if let next, let ev = Float(next) { o.exposure = ev; i += 1 }
            case "--contrast":
                if let next, let c = Float(next) { o.contrast = c; i += 1 }
            case "--whites":
                if let next, let v = Float(next) { o.whites = v; i += 1 }
            case "--blacks":
                if let next, let v = Float(next) { o.blacks = v; i += 1 }
            case "--auto":
                o.auto = true
                // An optional count, so `--auto 2` asks what a second press does.
                if let next, let n = Int(next), n > 0 { o.autoTimes = n; i += 1 }
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
            if let ev = o.exposure { engine.exposureEv = ev }
            if let c = o.contrast { engine.contrast = c }

            if let v = o.whites { engine.whites = v }
            if let v = o.blacks { engine.blacks = v }

            if o.auto {
                for pass in 1...o.autoTimes {
                    engine.autoEnhance()
                    FileHandle.standardError.write(Data(
                        ("orion: auto pass \(pass) — "
                         + String(format: "exposure %+.2f EV, whites %+.2f, "
                                  + "blacks %+.2f, lift %.2f, clarity %.2f\n",
                                  engine.exposureEv, engine.whites, engine.blacks,
                                  engine.fusion, engine.clarity)).utf8))
                }
            }

            // `--measure` needs the wide tail: it resolves differences that
            // eight-bit quantisation would erase, and the changes hunted in
            // this codebase are four decimal places wide. A plain screenshot
            // does not, and must not — the canvas should show what the screen
            // path produces, not a second rendering nobody sees.
            if !o.measure.isEmpty {
                engine.setWideOutput(true)
                for region in o.measure { measure(engine, region: region) }
                engine.setWideOutput(false)
            }

            let image = developed(engine)
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

        // Scan the photo's own folder so the filmstrip has frames in it.
        // Thumbnails stream in on a background queue, so the run loop below
        // has to turn enough times for them to arrive — an empty strip is a
        // screenshot of nothing.
        let library = MainActor.assumeIsolated { () -> Library in
            let lib = Library()
            if let photo = o.photo {
                let folder = URL(fileURLWithPath: photo).deletingLastPathComponent()
                // Fire and forget: the run loop below already waits for the
                // strip to fill, which is the same wait this would be.
                Task { await lib.open(folder: folder) }
            }
            return lib
        }
        // Thumbnails stream in on a background queue, so the run loop has to
        // turn enough times for them to arrive. An empty strip is a screenshot
        // of nothing, which is worse than no screenshot — it looks like a pass.
        for _ in 0..<60 {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
            let ready = MainActor.assumeIsolated {
                !library.loading && !library.photos.isEmpty
                    && library.photos.allSatisfy { $0.thumbnail != nil }
            }
            if ready { break }
        }

        let view = Editor(engine: engine, startTab: tab(for: o.scene),
                          startLibrary: library)
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
        case "color":
            return .color
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
        case "color":
            engine.exposureEv = 2.6
            engine.vibrance = 0.3
            engine.satShift[HueBand.blue.rawValue] = 0.4
        case "detail":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 1.6
            engine.denoiseColor = 2.4
            engine.sharpenAmount = 0.8
            engine.sharpenMasking = 0.4
        case "mask-off":
            // The control for `--measure`: the same frame and the same global
            // exposure with no mask, so the difference measured against
            // `mask-linear` is the mask's doing and not the scene's.
            engine.exposureEv = 2.6
        case "brush":
            // A stroke laid down the same way the canvas lays one: dabs at a
            // fixed spacing along a path, through CanvasLayout, so what the
            // screenshot shows is the real gesture rather than a hand-placed
            // list of points.
            engine.exposureEv = 2.6
            engine.maskKind = 3
            engine.localExposureEv = 2.2
            engine.brushRadius = 0.07
            engine.brushFlow = 0.55
            engine.brushHardness = 0.45
            var stroke: [CGPoint] = []
            var carry: CGFloat = 0
            let path = [CGPoint(x: 0.20, y: 0.66), CGPoint(x: 0.34, y: 0.60),
                        CGPoint(x: 0.48, y: 0.62), CGPoint(x: 0.62, y: 0.58),
                        CGPoint(x: 0.74, y: 0.55)]
            stroke.append(path[0])
            for i in 1..<path.count {
                stroke += CanvasLayout.brushDabs(from: path[i - 1], to: path[i],
                                                 radius: CGFloat(engine.brushRadius),
                                                 carry: &carry)
            }
            engine.setBrushStroke(stroke)
        case "mask-overlay":
            // The same linear mask as `mask-linear`, with the coverage painted
            // on. Measured against that scene, so the difference is the overlay
            // and nothing else.
            engine.exposureEv = 2.6
            engine.maskKind = 1
            engine.localExposureEv = -1.6
            engine.maskCentreX = 0.46
            engine.maskCentreY = 0.44
            engine.maskAngle = 1.05
            engine.maskLength = 0.55
            engine.maskOverlay = true
        case "mask-linear":
            // Placed and angled, because a gradient drawn square to the frame
            // proves nothing: the whole question is whether the three lines
            // stay perpendicular to the ramp in the space the shader measures,
            // and at zero degrees every wrong answer looks like the right one.
            engine.exposureEv = 2.6
            engine.maskKind = 1
            engine.localExposureEv = -1.6
            engine.maskCentreX = 0.46
            engine.maskCentreY = 0.44
            engine.maskAngle = 1.05
            engine.maskLength = 0.55
        case "mask-linear-feathered":
            // Identical to `mask-linear` but for the feather. The shader's
            // linear branch never reads that field, so the two must measure
            // the same — which is the evidence for hiding the control.
            engine.exposureEv = 2.6
            engine.maskKind = 1
            engine.localExposureEv = -1.6
            engine.maskCentreX = 0.46
            engine.maskCentreY = 0.44
            engine.maskAngle = 1.05
            engine.maskLength = 0.55
            engine.maskFeather = 0.02
        case "mask-radial":
            engine.exposureEv = 2.6
            engine.maskKind = 2
            engine.localExposureEv = 1.4
            engine.maskCentreX = 0.44
            engine.maskCentreY = 0.52
            engine.maskAngle = 0.6
            engine.maskRadiusX = 0.3
            engine.maskRadiusY = 0.17
            engine.maskFeather = 0.55
        case "mask-square":
            // A rounded-rectangle mask, which is the case that separates an
            // outline sampled from the shader's superellipse from one drawn as
            // an ellipse — at roundness 2 the two agree exactly.
            engine.exposureEv = 2.6
            engine.maskKind = 2
            engine.localExposureEv = 1.6
            engine.maskCentreX = 0.45
            engine.maskCentreY = 0.5
            engine.maskAngle = 0.35
            engine.maskRadiusX = 0.3
            engine.maskRadiusY = 0.22
            engine.maskRoundness = 6
            engine.maskFeather = 0.3
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
        case "asshot":
            break
        case "reset", "reset-hover":
            // A mix of touched and untouched controls, so the accent readout
            // can be read against the plain one in the same frame.
            engine.exposureEv = 2.6
            engine.highlights = -0.4
            engine.shadows = 0.45
            AdjustmentSlider.previewHover = (scene == "reset-hover")
        case "grade":
            engine.exposureEv = 2.6
            // Cool shadows, warm highlights — the split-tone every grading
            // panel gets used for first, so the screenshot shows the control
            // doing the thing it exists for.
            engine.gradeShadow = [-0.35, -0.55, -0.10]
            engine.gradeHighlight = [0.55, 0.35, 0.08]
        case "recover":
            engine.highlightRecovery = 1.0
        case "c110": engine.contrast = 1.10
        case "c120": engine.contrast = 1.20
        case "c130": engine.contrast = 1.30
        case "c140": engine.contrast = 1.40
        case "denoise-off":
            engine.exposureEv = 2.6
        case "denoise-luma":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 2.0
        case "denoise-both":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 2.0
            engine.denoiseColor = 3.0
        case "compare":
            engine.exposureEv = 2.6
            engine.setCompare(split: 0.5)
        // Distortion at each end of its travel. Negative k₁ is the case that
        // pushes the sample point past the frame edge, so it is the one that
        // says whether the picture still fills the frame.
        // Shadow wheel alone, hard over, so "does this colour the whole
        // picture" can be answered with a number instead of an impression.
        case "grade-shadow-only":
            engine.gradeShadow = [-0.9, -0.9, 0.0]
        case "lens-barrel":
            engine.exposureEv = 2.6
            engine.lensDistortion = -1.0
        case "lens-pincushion":
            engine.exposureEv = 2.6
            engine.lensDistortion = 1.0
        default:
            break
        }
    }

    /// The engine's developed output, read straight off the GPU.
    ///
    /// Straight off rather than through the export path, because export flattens
    /// alpha — and alpha is exactly what says "no picture here" outside a turned
    /// frame. Flattened, the crop preview's surround came out black instead of
    /// the interface's own gray, which would have made every straighten
    /// screenshot a lie.
    private static func developed(_ engine: Engine) -> NSImage? {
        guard let src = engine.outputTexture else { return nil }

        // Only the top-left rectangle of the texture is live; the rest is the
        // slack the graph allocates so a rotation never needs a recompile.
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0, w <= src.width, h <= src.height else { return nil }

        // CoreGraphics takes 16-bit unsigned, so the conversion happens here
        // rather than the readback pretending the texture is a format it is
        // not — which would read two pixels of noise for every one that
        // exists. The tail is eight bits on the screen path and sixteen around
        // an export, so `readNormalized` asks the texture which it is.
        let halves = readNormalized(src, width: w, height: h)
        var pixels = [UInt16](repeating: 0, count: w * h * 4)
        for i in 0..<(w * h) {
            for c in 0..<3 {
                let v = min(max(Float(halves[i * 4 + c]), 0), 1)
                pixels[i * 4 + c] = UInt16(v * 65535)
            }
            pixels[i * 4 + 3] = UInt16(min(max(Float(halves[i * 4 + 3]), 0), 1) * 65535)
        }

        let stride = w * 4 * MemoryLayout<UInt16>.size
        let info: CGBitmapInfo = [.byteOrder16Little,
                                  CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)]
        let data = pixels.withUnsafeBufferPointer { Data(buffer: $0) }
        guard let provider = CGDataProvider(data: data as CFData),
              let cg = CGImage(width: w, height: h, bitsPerComponent: 16, bitsPerPixel: 64,
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
    /// A region's mean luma and saturation, as numbers.
    ///
    /// Factored out of `measure` so a scenario can assert against the same
    /// arithmetic the printed report uses. Two implementations of "what does this
    /// patch measure" would let a scenario pass while the report it is supposed
    /// to correspond to says something else.
    /// `through` names the surface: the engine's own output by default, or the
    /// canvas composite, which is the only place the compare split exists.
    enum Surface { case output, canvas }

    static func regionStats(_ engine: Engine, region: CGRect,
                            through surface: Surface = .output)
        -> (luma: Double, saturation: Double)? {
        let source: MTLTexture? = surface == .canvas
            ? CanvasBlit.composite(engine: engine)
            : engine.outputTexture
        guard let src = source else { return nil }
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0 else { return nil }

        let x0 = max(0, min(w - 1, Int(region.minX * CGFloat(w))))
        let y0 = max(0, min(h - 1, Int(region.minY * CGFloat(h))))
        let rw = max(1, min(w - x0, Int(region.width * CGFloat(w))))
        let rh = max(1, min(h - y0, Int(region.height * CGFloat(h))))

        let pixels = readNormalized(src, width: rw, height: rh, x: x0, y: y0)
        var saturation = 0.0, luma = 0.0
        for i in 0..<(rw * rh) {
            let r = Double(min(max(Float(pixels[i * 4 + 0]), 0), 1))
            let g = Double(min(max(Float(pixels[i * 4 + 1]), 0), 1))
            let b = Double(min(max(Float(pixels[i * 4 + 2]), 0), 1))
            let mx = max(r, max(g, b)), mn = min(r, min(g, b))
            saturation += mx > 0.001 ? (mx - mn) / mx : 0
            luma += 0.2126 * r + 0.7152 * g + 0.0722 * b
        }
        let n = Double(rw * rh)
        return (luma / n, saturation / n)
    }

    /// The developed canvas as a PNG, for a scenario that wants to be looked at.
    static func writeCanvas(_ engine: Engine, to path: String) {
        guard let image = developed(engine),
              let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:])
        else { return }
        try? png.write(to: URL(fileURLWithPath: path))
    }

    private static func measure(_ engine: Engine, region: CGRect) {
        guard let src = engine.outputTexture else { return }
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0 else { return }

        let x0 = max(0, min(w - 1, Int(region.minX * CGFloat(w))))
        let y0 = max(0, min(h - 1, Int(region.minY * CGFloat(h))))
        let rw = max(1, min(w - x0, Int(region.width * CGFloat(w))))
        let rh = max(1, min(h - y0, Int(region.height * CGFloat(h))))

        let pixels = readNormalized(src, width: rw, height: rh, x: x0, y: y0)

        var sums = [Double](repeating: 0, count: 3)
        var squares = [Double](repeating: 0, count: 3)
        let n = Double(rw * rh)
        for i in 0..<(rw * rh) {
            for c in 0..<3 {
                let v = Double(min(max(Float(pixels[i * 4 + c]), 0), 1))
                sums[c] += v
                squares[c] += v * v
            }
        }

        // Mean saturation, the same way a camera JPEG would be measured, so
        // "the colors look washed" can be answered with a number instead of an
        // impression.
        var saturation = 0.0
        var luma = 0.0
        for i in 0..<(rw * rh) {
            let r = Double(min(max(Float(pixels[i * 4 + 0]), 0), 1))
            let g = Double(min(max(Float(pixels[i * 4 + 1]), 0), 1))
            let b = Double(min(max(Float(pixels[i * 4 + 2]), 0), 1))
            let mx = max(r, max(g, b)), mn = min(r, min(g, b))
            saturation += mx > 0.001 ? (mx - mn) / mx : 0
            luma += 0.2126 * r + 0.7152 * g + 0.0722 * b
        }

        var report = "orion: region \(rw)x\(rh) at (\(x0),\(y0))\n"
        for (c, name) in ["R", "G", "B"].enumerated() {
            let mean = sums[c] / n
            let variance = max(0, squares[c] / n - mean * mean)
            report += String(format: "  %@  mean %.5f  sd %.5f\n",
                             name, mean, variance.squareRoot())
        }
        report += String(format: "  mean saturation %.4f  mean luma %.4f\n",
                         saturation / n, luma / n)
        FileHandle.standardError.write(Data(report.utf8))
    }

    /// A region of the output texture, normalized, whichever format it is in.
    ///
    /// The tail of the graph writes eight bits for the screen — the drawable is
    /// `bgra8Unorm`, so wider is bytes moved for precision nothing can show —
    /// and sixteen only around an export. Downloading an `RGBA8Unorm` texture
    /// with a stride computed for half float does not fail; it returns
    /// nonsense, which is a worse outcome than an error.
    private static func readNormalized(_ texture: MTLTexture, width: Int, height: Int,
                                       x: Int = 0, y: Int = 0) -> [Float16] {
        guard texture.pixelFormat == .rgba16Float else {
            var bytes = [UInt8](repeating: 0, count: width * height * 4)
            bytes.withUnsafeMutableBytes { raw in
                texture.getBytes(raw.baseAddress!, bytesPerRow: width * 4,
                                 from: MTLRegionMake2D(x, y, width, height),
                                 mipmapLevel: 0)
            }
            return bytes.map { Float16(Float($0) / 255.0) }
        }

        let stride = width * 4 * MemoryLayout<Float16>.size
        var out = [Float16](repeating: 0, count: width * height * 4)
        out.withUnsafeMutableBytes { raw in
            texture.getBytes(raw.baseAddress!, bytesPerRow: stride,
                             from: MTLRegionMake2D(x, y, width, height), mipmapLevel: 0)
        }
        return out
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

import AppKit
import Metal

/// Pixels out of the engine, as pictures and as numbers.
///
/// The readback and the two arithmetics that read it: the developed frame a
/// still is drawn from, the eight-bit copy a segmentation model is handed, and
/// `regionStats` / `measure`, which answer "does this patch look washed" with a
/// number instead of an impression.
///
/// ⚠ One arithmetic, deliberately. `regionStats` was factored out of `measure`
/// so a scenario asserts against the numbers the printed report uses — two
/// implementations would let a scenario pass while the report it corresponds to
/// says something else.
extension Screenshot {

    /// The engine's developed output, read straight off the GPU.
    ///
    /// Straight off rather than through the export path, because export flattens
    /// alpha — and alpha is exactly what says "no picture here" outside a turned
    /// frame. Flattened, the crop preview's surround came out black instead of
    /// the interface's own gray, which would have made every straighten
    /// screenshot a lie.
    static func developed(_ engine: Engine) -> NSImage? {
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

    /// The developed picture as a `CGImage`, scaled to fit, eight bits.
    ///
    /// For anything that wants to *analyze* the render rather than measure it —
    /// today, handing a segmentation model an ordinary display-referred photo
    /// (research/masking.md §5). Eight bits and sRGB on purpose: that is what
    /// those models were trained on, and it is what the screen path already
    /// produces, so no second copy of the display transform is needed to get it.
    ///
    /// Drawn through CoreGraphics rather than sampled by hand, because the
    /// downscale wants a real filter and CoreGraphics has one.
    static func developedCGImage(_ engine: Engine,
                                 fitting size: (width: Int, height: Int)) -> CGImage? {
        guard let src = engine.outputTexture else { return nil }
        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        guard w > 0, h > 0, w <= src.width, h <= src.height,
              size.width > 0, size.height > 0 else { return nil }

        let halves = readNormalized(src, width: w, height: h)
        var pixels = [UInt8](repeating: 255, count: w * h * 4)
        for i in 0..<(w * h) {
            for c in 0..<3 {
                let v = min(max(Float(halves[i * 4 + c]), 0), 1)
                pixels[i * 4 + c] = UInt8(v * 255)
            }
            // Opaque. Alpha is zero outside a turned frame, and a segmentation
            // model handed transparent corners reads them as content.
            pixels[i * 4 + 3] = 255
        }

        let space = CGColorSpaceCreateDeviceRGB()
        let info: CGBitmapInfo = CGBitmapInfo(
            rawValue: CGImageAlphaInfo.noneSkipLast.rawValue)
        let data = pixels.withUnsafeBufferPointer { Data(buffer: $0) }
        guard let provider = CGDataProvider(data: data as CFData),
              let full = CGImage(width: w, height: h, bitsPerComponent: 8,
                                 bitsPerPixel: 32, bytesPerRow: w * 4,
                                 space: space, bitmapInfo: info,
                                 provider: provider, decode: nil,
                                 shouldInterpolate: true, intent: .defaultIntent)
        else { return nil }

        if size.width == w && size.height == h { return full }

        guard let ctx = CGContext(data: nil, width: size.width, height: size.height,
                                  bitsPerComponent: 8, bytesPerRow: 0,
                                  space: space, bitmapInfo: info.rawValue)
        else { return nil }
        ctx.interpolationQuality = .high
        ctx.draw(full, in: CGRect(x: 0, y: 0, width: size.width, height: size.height))
        return ctx.makeImage()
    }

    /// Which picture a measurement is read from. `regionStats(through:)` names
    /// one: the engine's own output by default, or the canvas composite, which
    /// is the only place the compare split exists.
    ///
    /// ⚠ `preview` reads the quarter-linear graph directly, which nothing else
    /// in the program does — the canvas shows it, but through `displayTexture`,
    /// and every measurement, the histogram and the export read the full one.
    /// It exists because a mutation that stopped fanning adjustments out to the
    /// preview survived every test: the settled picture was still right, and
    /// the only thing that had gone wrong was what the photographer saw *during*
    /// the drag, which nothing could see.
    /// ⚠ `analysis` is the picture *Vision* is handed — `renderForAnalysis`, with
    /// the crop, the straighten and the user's rotation neutralised. Nothing on
    /// screen ever shows it, so anything wrong with it is invisible to every
    /// other surface here. It exists because the coverage overlay was not being
    /// turned off around it: "Show mask" tints the render red wherever the group
    /// covers, and that tinted frame went to a segmentation model.
    enum Surface { case output, canvas, preview, analysis }

    /// A region's mean luma and saturation, as numbers.
    ///
    /// Factored out of `measure` so a scenario can assert against the same
    /// arithmetic the printed report uses. Two implementations of "what does this
    /// patch measure" would let a scenario pass while the report it is supposed
    /// to correspond to says something else.
    static func regionStats(_ engine: Engine, region: CGRect,
                            through surface: Surface = .output)
        -> (luma: Double, saturation: Double)? {
        if surface == .analysis {
            guard let (image, _) = engine.renderForAnalysis() else { return nil }
            return regionStats(of: image, region: region)
        }

        let source: MTLTexture?
        var w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        switch surface {
        case .canvas:  source = CanvasBlit.composite(engine: engine)
        case .output:  source = engine.outputTexture
        case .preview:
            source = engine.previewTexture
            let size = engine.previewSize
            w = Int(size.width); h = Int(size.height)
        case .analysis: source = nil   // handled above
        }
        guard let src = source else { return nil }
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

    /// The same two numbers over a `CGImage`, which is what the analysis render
    /// is. Drawn into an 8-bit RGBA context rather than read through
    /// `CGDataProvider`, so a source in any layout or color space arrives in
    /// one known one.
    private static func regionStats(of image: CGImage, region: CGRect)
        -> (luma: Double, saturation: Double)? {
        let w = image.width, h = image.height
        guard w > 0, h > 0 else { return nil }

        let x0 = max(0, min(w - 1, Int(region.minX * CGFloat(w))))
        let y0 = max(0, min(h - 1, Int(region.minY * CGFloat(h))))
        let rw = max(1, min(w - x0, Int(region.width * CGFloat(w))))
        let rh = max(1, min(h - y0, Int(region.height * CGFloat(h))))
        guard let crop = image.cropping(
                to: CGRect(x: x0, y: y0, width: rw, height: rh)) else { return nil }

        var bytes = [UInt8](repeating: 0, count: rw * rh * 4)
        let ok: Bool = bytes.withUnsafeMutableBytes { raw -> Bool in
            guard let ctx = CGContext(
                    data: raw.baseAddress, width: rw, height: rh,
                    bitsPerComponent: 8, bytesPerRow: rw * 4,
                    space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
            else { return false }
            ctx.draw(crop, in: CGRect(x: 0, y: 0, width: rw, height: rh))
            return true
        }
        guard ok else { return nil }

        var saturation = 0.0, luma = 0.0
        for i in 0..<(rw * rh) {
            let r = Double(bytes[i * 4 + 0]) / 255.0
            let g = Double(bytes[i * 4 + 1]) / 255.0
            let b = Double(bytes[i * 4 + 2]) / 255.0
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

    /// Prints the mean and standard deviation of a region of the engine's
    /// output.
    ///
    /// Whether a filter works is a question about pixels, and a screenshot
    /// scaled to fit a review pane cannot answer it — noise that is obvious at
    /// 100% disappears into the downsampling. This reads the numbers.
    static func measure(_ engine: Engine, region: CGRect) {
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
}

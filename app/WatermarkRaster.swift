// Where the watermark goes, and the coverage mask the engine blends through.
//
// Two halves. `WatermarkLayout` is pure geometry, testable without a font or a
// file. `WatermarkRaster` draws the placed mark with CoreText or `NSImage` (SVG
// is native since macOS 10.15, so no new dependency) and keeps only coverage.
// The engine never learns what the mark was - DECISIONS #284.

import AppKit
import CoreText
import Foundation

enum WatermarkLayout {

    /// A placed mark, in pixels, origin top-left with y down - the order the
    /// mask's rows are in. `angle` is counter-clockwise as the viewer sees it.
    struct Placed: Equatable {
        var center: CGPoint
        var size: CGSize
        var angle: Double

        /// The axis-aligned box the rotated mark occupies.
        var bounds: CGRect {
            let c = abs(cos(angle)), s = abs(sin(angle))
            let w = size.width * c + size.height * s
            let h = size.width * s + size.height * c
            return CGRect(x: center.x - w / 2, y: center.y - h / 2, width: w, height: h)
        }
    }

    /// `markAspect` is the mark's own width over height.
    ///
    /// ⚠ Shrunk to fit rather than allowed off the frame. A long name at a large
    /// size on a portrait frame would otherwise lose its last letters, and a
    /// mark with letters missing reads as a mistake rather than a signature.
    static func place(markAspect: Double, frame: CGSize, placement: Watermark.Placement,
                      size: Double, span: Double, margin: Double) -> Placed {
        let W = Double(frame.width), H = Double(frame.height)
        guard W > 0, H > 0, markAspect > 0 else {
            return Placed(center: .zero, size: .zero, angle: 0)
        }
        switch placement {
        case .anchor(let a):
            let m = margin * min(W, H)
            let room = CGSize(width: max(1, W - 2 * m), height: max(1, H - 2 * m))
            var h = size * min(W, H)
            var w = h * markAspect
            let fit = min(1, Double(room.width) / w, Double(room.height) / h)
            w *= fit; h *= fit
            let x = m + w / 2 + a.fx * (Double(room.width) - w)
            let y = m + h / 2 + a.fy * (Double(room.height) - h)
            return Placed(center: CGPoint(x: x, y: y), size: CGSize(width: w, height: h),
                          angle: 0)

        case .diagonal:
            let angle = atan2(H, W)
            var w = span * (W * W + H * H).squareRoot()
            var h = w / markAspect
            // The rotated box, not the mark, has to fit.
            let c = cos(angle), s = sin(angle)
            let fit = min(1, W / (w * c + h * s), H / (w * s + h * c))
            w *= fit; h *= fit
            return Placed(center: CGPoint(x: W / 2, y: H / 2),
                          size: CGSize(width: w, height: h), angle: angle)
        }
    }
}

enum WatermarkRaster {

    /// 8-bit coverage over the whole output frame, rows top first, opacity
    /// already multiplied in. What `OrionExportOptions.watermark_mask` takes.
    struct Mask {
        var bytes: [UInt8]
        var width: Int
        var height: Int
    }

    /// The mask for an export with these settings, or nil when the export is
    /// not watermarked. Every call site that honours the photographer's
    /// settings goes through here, so "switched off" means one thing.
    static func mask(for mark: Watermark, settings: ExportSettings,
                     sourceWidth: UInt32, sourceHeight: UInt32) -> Mask? {
        guard mark.enabled else { return nil }
        let (w, h) = settings.dimensions(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        return mask(for: mark, width: Int(w), height: Int(h))
    }

    /// Nil when there is nothing to draw - an empty name, no file - so an
    /// export never pays for a pass that changes nothing.
    static func mask(for mark: Watermark, width: Int, height: Int) -> Mask? {
        guard width > 0, height > 0, mark.isDrawable, mark.opacity > 0,
              let art = Art(mark.content) else { return nil }
        let placed = WatermarkLayout.place(
            markAspect: art.aspect, frame: CGSize(width: width, height: height),
            placement: mark.placement, size: mark.size, span: mark.span, margin: mark.margin)

        // Only the mark's own box is drawn, in RGBA - `NSImage` will not draw
        // into an alpha-only context - and its alpha copied into the frame.
        let box = placed.bounds.integral
            .intersection(CGRect(x: 0, y: 0, width: width, height: height))
        guard !box.isEmpty else { return nil }
        let bx = Int(box.minX), by = Int(box.minY)
        let bw = Int(box.width), bh = Int(box.height)

        var tile = [UInt8](repeating: 0, count: bw * bh * 4)
        let drawn = tile.withUnsafeMutableBytes { raw -> Bool in
            guard let ctx = CGContext(
                data: raw.baseAddress, width: bw, height: bh, bitsPerComponent: 8,
                bytesPerRow: bw * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
            // CoreGraphics is y-up, the layout is y-down: flip the center into
            // the tile, then rotate counter-clockwise, which y-up makes natural.
            ctx.translateBy(x: placed.center.x - CGFloat(bx),
                            y: CGFloat(by + bh) - placed.center.y)
            ctx.rotate(by: CGFloat(placed.angle))
            art.draw(in: ctx, size: placed.size)
            return true
        }
        guard drawn else { return nil }

        var bytes = [UInt8](repeating: 0, count: width * height)
        let opacity = min(max(mark.opacity, 0), 1)
        for y in 0..<bh {
            for x in 0..<bw {
                let a = Double(tile[(y * bw + x) * 4 + 3])
                bytes[(by + y) * width + bx + x] = UInt8((a * opacity).rounded())
            }
        }
        return Mask(bytes: bytes, width: width, height: height)
    }

    /// The mark's shape, whatever it came from, drawn centered on the origin.
    private enum Art {
        case text(CTLine, CGRect)
        case svg(NSImage)

        init?(_ content: Watermark.Content) {
            switch content {
            case .text(let s, let family, let bold):
                let line = CTLineCreateWithAttributedString(NSAttributedString(
                    string: s, attributes: [.font: Art.font(family, bold: bold)]))
                // The ink, not the typographic box: a margin measured from the
                // ascender would leave a caps-only name visibly off its corner.
                let ink = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)
                guard ink.width > 0, ink.height > 0 else { return nil }
                self = .text(line, ink)
            case .svg(let data, _):
                guard let image = NSImage(data: data), image.size.width > 0,
                      image.size.height > 0 else { return nil }
                self = .svg(image)
            }
        }

        var aspect: Double {
            switch self {
            case .text(_, let ink): Double(ink.width / ink.height)
            case .svg(let image):   Double(image.size.width / image.size.height)
            }
        }

        /// Vector all the way to the final transform, so a 60 MP export and a
        /// 1024 px one are equally crisp.
        func draw(in ctx: CGContext, size: CGSize) {
            switch self {
            case .text(let line, let ink):
                let s = size.height / ink.height
                ctx.scaleBy(x: s, y: s)
                ctx.textPosition = CGPoint(x: -ink.midX, y: -ink.midY)
                CTLineDraw(line, ctx)
            case .svg(let image):
                NSGraphicsContext.saveGraphicsState()
                NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
                image.draw(in: NSRect(x: -size.width / 2, y: -size.height / 2,
                                      width: size.width, height: size.height))
                NSGraphicsContext.restoreGraphicsState()
            }
        }

        /// Drawn at a nominal size and scaled, so the size is the layout's.
        static func font(_ family: String, bold: Bool) -> CTFont {
            let descriptor = CTFontDescriptorCreateWithAttributes(
                [kCTFontFamilyNameAttribute: family] as CFDictionary)
            let base = CTFontCreateWithFontDescriptor(descriptor, 200, nil)
            guard bold else { return base }
            return CTFontCreateCopyWithSymbolicTraits(base, 200, nil, .traitBold, .traitBold)
                ?? base
        }
    }
}

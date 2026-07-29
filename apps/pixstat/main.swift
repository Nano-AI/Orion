import CoreGraphics
import Foundation
import ImageIO

/*  orion-pixstat — mean sRGB over a region of any image ImageIO can read.
 *
 *  The counterpart to `--measure` in the screenshot harness: that one reads
 *  Orion's own output texture, this one reads everybody else's. Every
 *  cross-renderer number in feedback/2026-07-28-colour-investigation.md came
 *  through here — Orion against the camera's JPEG, against Apple's RAW
 *  rendering, against darktable's.
 *
 *  It lives in the repository rather than in a scratchpad because a measurement
 *  tool that can quietly vanish is how the sample corpus went back to two night
 *  frames and let a purple sky past two suites and a full review.
 *
 *      orion-pixstat <image> <x> <y> <w> <h>        region normalized 0..1
 *
 *  Prints per-channel means plus R/B and G/B, which are the numbers that matter
 *  when comparing renderers: they disagree about exposure and contrast, so
 *  ratios are the only fair comparison of *hue*.
 */

struct Bitmap {
    let width: Int
    let height: Int
    var pixels: [UInt8]   // RGBA8, row-major, origin top-left

    subscript(x: Int, y: Int) -> (Double, Double, Double) {
        let i = (y * width + x) * 4
        return (Double(pixels[i]) / 255.0,
                Double(pixels[i + 1]) / 255.0,
                Double(pixels[i + 2]) / 255.0)
    }
}

/// Decodes an image and applies its EXIF orientation by **remapping pixels**,
/// not by transforming a CGContext.
///
/// The transform version was written first and was wrong: it returned a
/// vertically flipped image for rotated files, so the first "sky" measurement
/// in the colour investigation actually sampled foliage and had to be redone.
/// Rotation composed with CoreGraphics' bottom-left origin is exactly the kind
/// of thing that reads correctly and is upside down in fact. Indexing is harder
/// to get wrong, and trivial to check:
///
///     orion-pixstat photo.JPG 0 0 1 0.05      // sky-up frame -> must be sky
func decode(_ path: String) -> Bitmap? {
    guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
          let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { return nil }

    let props = CGImageSourceCopyPropertiesAtIndex(src, 0, nil) as? [CFString: Any]
    let o = (props?[kCGImagePropertyOrientation] as? Int) ?? 1

    let w = img.width, h = img.height
    var raw = [UInt8](repeating: 0, count: w * h * 4)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    guard let ctx = CGContext(data: &raw, width: w, height: h, bitsPerComponent: 8,
                              bytesPerRow: w * 4, space: cs,
                              bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { return nil }
    ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))

    if o == 1 { return Bitmap(width: w, height: h, pixels: raw) }

    // Where each display pixel comes from in the stored buffer. All eight cases
    // written out rather than composed, because composing them is how the first
    // version got it backwards.
    let swaps = o >= 5
    let outW = swaps ? h : w, outH = swaps ? w : h
    var out = [UInt8](repeating: 0, count: outW * outH * 4)

    for y in 0..<outH {
        for x in 0..<outW {
            let sx: Int, sy: Int
            switch o {
            case 2: sx = w - 1 - x; sy = y              // mirror horizontal
            case 3: sx = w - 1 - x; sy = h - 1 - y      // 180
            case 4: sx = x;         sy = h - 1 - y      // mirror vertical
            case 5: sx = y;         sy = x              // transpose
            case 6: sx = y;         sy = h - 1 - x      // 90 clockwise
            case 7: sx = w - 1 - y; sy = h - 1 - x      // transverse
            case 8: sx = w - 1 - y; sy = x              // 90 counter-clockwise
            default: sx = x;        sy = y
            }
            let s = (sy * w + sx) * 4, d = (y * outW + x) * 4
            for c in 0..<4 { out[d + c] = raw[s + c] }
        }
    }
    return Bitmap(width: outW, height: outH, pixels: out)
}

let args = CommandLine.arguments
guard args.count >= 6 else {
    FileHandle.standardError.write(Data("usage: orion-pixstat <image> <x> <y> <w> <h>\n".utf8))
    exit(2)
}
guard let bmp = decode(args[1]) else {
    FileHandle.standardError.write(Data("cannot read \(args[1])\n".utf8))
    exit(1)
}

let rx = Double(args[2]) ?? 0, ry = Double(args[3]) ?? 0
let rw = Double(args[4]) ?? 1, rh = Double(args[5]) ?? 1
let x0 = max(0, min(bmp.width - 1, Int(rx * Double(bmp.width))))
let y0 = max(0, min(bmp.height - 1, Int(ry * Double(bmp.height))))
let w = max(1, min(bmp.width - x0, Int(rw * Double(bmp.width))))
let h = max(1, min(bmp.height - y0, Int(rh * Double(bmp.height))))

var sr = 0.0, sg = 0.0, sb = 0.0
for y in y0..<(y0 + h) {
    for x in x0..<(x0 + w) {
        let p = bmp[x, y]
        sr += p.0; sg += p.1; sb += p.2
    }
}
let n = Double(w * h)
let r = sr / n, g = sg / n, b = sb / n
let mx = max(r, max(g, b)), mn = min(r, min(g, b))
let luma = 0.2126 * r + 0.7152 * g + 0.0722 * b

print(String(format: "%@  %dx%d at (%d,%d)  R %.4f G %.4f B %.4f  R/B %.3f  G/B %.3f  luma %.4f  sat %.3f",
             (args[1] as NSString).lastPathComponent, w, h, x0, y0, r, g, b,
             r / max(b, 1e-6), g / max(b, 1e-6), luma, mx > 0 ? (mx - mn) / mx : 0))

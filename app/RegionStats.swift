import Foundation

/// The numbers a patch of the picture measures - one arithmetic for every
/// surface `measure` reads (the output texture, the canvas composite, the
/// preview, the analysis render) and for the test suite, which has no GPU.
/// Decision #278. SwiftUI-free and Metal-free on purpose, so
/// `orion-viewport-tests` can pin each field on a synthetic patch.
enum RegionStats {
    /// What a patch measures. Decision #278: the numbers an agent reads
    /// before it looks at pixels, all from one pass over the region.
    ///
    /// - `luma`, `saturation`: as they always were (Rec. 709 luma; HSV-style
    ///   saturation, `(max - min) / max`).
    /// - `red`/`green`/`blue`: channel means, display-referred, 0..1.
    /// - `hue`: degrees 0..360 of the chroma-weighted mean hue vector, so a
    ///   grey patch's stray pixels do not pick the number; 0 is red, 120
    ///   green, 240 blue. Meaningless where `saturation` is near zero.
    /// - `clippedHigh`: fraction of pixels with any channel at 254/255 or
    ///   above; `clippedLow`: fraction with every channel at 1/255 or below.
    /// - `shading`: relative mid-scale luminance variation - the standard
    ///   deviation over the mean of block means, the block a twelfth of the
    ///   patch's shorter side. It is the face's shading rather than its pores,
    ///   and 40% of it going missing is what `highlights -0.8` did to a face
    ///   under process 1 (research/tone-and-local-contrast.md). ⚠ The twelfth
    ///   is chosen, not sourced; `UNSOURCED.md` §19.
    struct Stats {
        var luma = 0.0, saturation = 0.0
        var red = 0.0, green = 0.0, blue = 0.0
        var hue = 0.0
        /// How much the patch agrees about its hue: the chroma-weighted mean
        /// vector's length over the total chroma, 0..1. A patch of one hue is
        /// 1; red and cyan in equal measure cancel to 0, and `hue` then means
        /// nothing, whatever number it holds.
        var hueStrength = 0.0
        var clippedHigh = 0.0, clippedLow = 0.0
        var shading = 0.0
    }

    /// The arithmetic, over interleaved RGB floats in 0..1. One function for
    /// every surface, so the texture path and the `CGImage` path cannot drift.
    static func stats(rgb: [Float], width: Int, height: Int) -> Stats {
        let n = width * height
        guard n > 0, rgb.count >= n * 3 else { return Stats() }
        var s = Stats()
        var hx = 0.0, hy = 0.0, chromaTotal = 0.0
        var lumas = [Double](repeating: 0, count: n)
        var high = 0, low = 0
        for i in 0..<n {
            let r = Double(min(max(rgb[i * 3], 0), 1))
            let g = Double(min(max(rgb[i * 3 + 1], 0), 1))
            let b = Double(min(max(rgb[i * 3 + 2], 0), 1))
            let mx = max(r, max(g, b)), mn = min(r, min(g, b))
            let chroma = mx - mn
            s.red += r; s.green += g; s.blue += b
            s.saturation += mx > 0.001 ? chroma / mx : 0
            let y = 0.2126 * r + 0.7152 * g + 0.0722 * b
            lumas[i] = y
            s.luma += y
            if mx >= 254.0 / 255.0 { high += 1 }
            if mx <= 1.0 / 255.0 { low += 1 }
            if chroma > 1e-6 {
                // The HSV hue, as a vector weighted by chroma.
                var h: Double
                if mx == r { h = (g - b) / chroma }
                else if mx == g { h = 2 + (b - r) / chroma }
                else { h = 4 + (r - g) / chroma }
                h *= 60
                if h < 0 { h += 360 }
                let rad = h * .pi / 180
                hx += chroma * cos(rad); hy += chroma * sin(rad)
                chromaTotal += chroma
            }
        }
        let count = Double(n)
        s.red /= count; s.green /= count; s.blue /= count
        s.saturation /= count; s.luma /= count
        s.clippedHigh = Double(high) / count
        s.clippedLow = Double(low) / count
        if chromaTotal > 0 {
            s.hueStrength = (hx * hx + hy * hy).squareRoot() / chromaTotal
            var h = atan2(hy, hx) * 180 / .pi
            if h < 0 { h += 360 }
            s.hue = h
        }

        // Shading: block means over a grid, then their spread.
        let block = max(2, min(width, height) / 12)
        let bw = width / block, bh = height / block
        if bw * bh >= 4 {
            var means = [Double](repeating: 0, count: bw * bh)
            for by in 0..<bh {
                for bx in 0..<bw {
                    var sum = 0.0
                    for y in (by * block)..<((by + 1) * block) {
                        for x in (bx * block)..<((bx + 1) * block) {
                            sum += lumas[y * width + x]
                        }
                    }
                    means[by * bw + bx] = sum / Double(block * block)
                }
            }
            let m = means.reduce(0, +) / Double(means.count)
            if m > 1e-6 {
                let v = means.reduce(0) { $0 + ($1 - m) * ($1 - m) } / Double(means.count)
                s.shading = v.squareRoot() / m
            }
        }
        return s
    }

}

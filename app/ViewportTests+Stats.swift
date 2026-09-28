import Foundation

/// `RegionStats.stats`, the one arithmetic every `measure` surface goes through
/// (#278): the fields an agent asserts on, each pinned on a synthetic patch
/// whose answer is known.
extension ViewportTests {
    static func testRegionStatsFields() {
        func patch(_ w: Int, _ h: Int, _ f: (Int, Int) -> (Float, Float, Float)) -> [Float] {
            var out = [Float](repeating: 0, count: w * h * 3)
            for y in 0..<h { for x in 0..<w {
                let (r, g, b) = f(x, y)
                out[(y * w + x) * 3] = r; out[(y * w + x) * 3 + 1] = g; out[(y * w + x) * 3 + 2] = b
            } }
            return out
        }
        let flat = RegionStats.stats(rgb: patch(48, 48) { _, _ in (0.5, 0.5, 0.5) }, width: 48, height: 48)
        report(abs(flat.luma - 0.5) < 1e-6 && flat.saturation == 0 && flat.shading == 0
               && flat.clippedHigh == 0 && flat.clippedLow == 0,
               "a flat grey patch: luma, no saturation, no shading, nothing clipped")

        let red = RegionStats.stats(rgb: patch(24, 24) { _, _ in (0.8, 0.1, 0.1) }, width: 24, height: 24)
        report(abs(red.hue) < 1e-3 && abs(red.red - 0.8) < 1e-6, "pure red is hue 0 and its own mean",
               "hue \(red.hue)")
        let green = RegionStats.stats(rgb: patch(24, 24) { _, _ in (0.1, 0.7, 0.1) }, width: 24, height: 24)
        report(abs(green.hue - 120) < 1e-3, "green is hue 120", "hue \(green.hue)")
        let blue = RegionStats.stats(rgb: patch(24, 24) { _, _ in (0.1, 0.1, 0.9) }, width: 24, height: 24)
        report(abs(blue.hue - 240) < 1e-3, "blue is hue 240", "hue \(blue.hue)")

        // Hue is chroma-weighted: a grey patch with one saturated pixel takes
        // that pixel's hue, and a patch of equal red and cyan cancels to none.
        let mixed = RegionStats.stats(rgb: patch(24, 24) { x, _ in x < 12 ? (1, 0, 0) : (0, 1, 1) },
                                     width: 24, height: 24)
        report(mixed.hueStrength < 1e-6 && mixed.saturation == 1,
               "opposite hues in equal chroma cancel, and hueStrength says so",
               "strength \(mixed.hueStrength)")
        report(red.hueStrength > 0.999, "a patch of one hue has full hue strength",
               "strength \(red.hueStrength)")

        let split = RegionStats.stats(rgb: patch(40, 40) { x, _ in x < 20 ? (1, 1, 1) : (0, 0, 0) },
                                     width: 40, height: 40)
        report(abs(split.clippedHigh - 0.5) < 1e-6 && abs(split.clippedLow - 0.5) < 1e-6,
               "half white, half black: both clipped fractions are one half",
               "\(split.clippedHigh) / \(split.clippedLow)")
        report(split.shading > 0.9, "and its shading is the full step", "\(split.shading)")

        // Shading is the mid scale, not the pores: a fine checkerboard has
        // no shading at all, a gradient across the patch has some.
        let checker = RegionStats.stats(rgb: patch(48, 48) { x, y in ((x + y) % 2 == 0) ? (0.9, 0.9, 0.9) : (0.1, 0.1, 0.1) },
                                       width: 48, height: 48)
        report(checker.shading < 1e-6, "a pixel checkerboard has no mid-scale shading", "\(checker.shading)")
        let ramp = RegionStats.stats(rgb: patch(48, 48) { x, _ in let v = 0.2 + 0.6 * Float(x) / 47; return (v, v, v) },
                                    width: 48, height: 48)
        report(ramp.shading > 0.2 && ramp.shading < 0.5, "a ramp across the patch has shading in between",
               "\(ramp.shading)")

        // Too small for four blocks: shading declines rather than inventing.
        let tiny = RegionStats.stats(rgb: patch(3, 3) { x, _ in (Float(x) / 2, 0, 0) }, width: 3, height: 3)
        report(tiny.shading == 0, "a patch too small for blocks reports no shading")
    }
}


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

    /// `noise` (research/denoise-masked.md §3) on three patches whose answer
    /// is worked out by hand.
    ///
    /// The checkerboard is the exact case: at 0.5 ± a with the sign s of
    /// (-1)^(x+y), a 3x3 window holds the centre (s), four edge neighbours
    /// (-s) and four corners (+s), so its mean is 0.5 + a*s/9 and every
    /// interior pixel sits a*s*8/9 from it. RMS a*8/9: 0.0444 for a = 0.05,
    /// not 0.05, and a formula that forgot the centre is in the window
    /// would read 0.05 * 10/9 here instead.
    static func testRegionStatsNoise() {
        func grey(_ w: Int, _ h: Int, _ f: (Int, Int) -> Float) -> [Float] {
            var out = [Float](repeating: 0, count: w * h * 3)
            for y in 0..<h { for x in 0..<w {
                let v = f(x, y)
                out[(y * w + x) * 3] = v; out[(y * w + x) * 3 + 1] = v; out[(y * w + x) * 3 + 2] = v
            } }
            return out
        }
        let flat = RegionStats.stats(rgb: grey(32, 32) { _, _ in 0.5 }, width: 32, height: 32)
        report(flat.noise < 1e-9, "a flat patch has no noise", "\(flat.noise)")

        let grain = RegionStats.stats(rgb: grey(32, 32) { x, y in (x + y) % 2 == 0 ? 0.55 : 0.45 },
                                      width: 32, height: 32)
        let expected = 0.05 * 8.0 / 9.0
        report(abs(grain.noise - expected) < 1e-6,
               "the same patch at ±0.05 alternating reads 0.05 * 8/9 against its 3x3 mean",
               "\(grain.noise), expected \(expected)")
        report(abs(grain.luma - flat.luma) < 1e-6, "and its luma is unmoved, so noise is a separate number")

        // A linear ramp is its own 3x3 mean everywhere, so a gradient - the
        // light falling across a wall - is not grain. The shading test above
        // reads the same ramp as substantial; the two numbers part here.
        let ramp = RegionStats.stats(rgb: grey(48, 48) { x, y in 0.2 + 0.006 * Float(x) + 0.004 * Float(y) },
                                     width: 48, height: 48)
        report(ramp.noise < 1e-6, "a smooth ramp reads as no noise", "\(ramp.noise)")

        let tiny = RegionStats.stats(rgb: grey(2, 8) { x, _ in Float(x) }, width: 2, height: 8)
        report(tiny.noise == 0, "a patch with no interior pixel reports no noise")
    }
}


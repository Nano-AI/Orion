// The track label gradients: the numbers behind them, pinned.
//
// A gradient is a decoration, but a decoration that *claims a number* has to
// be right about it: the hue mixer's ends promise what full travel does, and
// the shader is the authority on that. Everything else here pins reading
// direction (blue is left of amber, green left of magenta) and the moderation
// rule (#63) so a future tweak cannot quietly turn the panels loud.

import CoreGraphics
import Foundation

extension ViewportTests {

    static func testTrackTintEndpoints() {
        // The mixer's Hue ends are the shader's own numbers: kBandCenters
        // (already pinned by testHueBands) shifted by the ±30° full-travel
        // constant from hsl_ops.slang:128.
        report(TrackTint.hueTravel == 30,
               "the hue track's travel is the shader's 30 degrees")
        for band in HueBand.allCases {
            let e = TrackTint.hueEnds(for: band)
            let c = TargetedAdjust.centers[band.rawValue]
            report(e.from == c - 30 && e.to == c + 30,
                   "\(band.name)'s hue track ends at its center ∓ the travel",
                   "center \(c), got \(e.from)...\(e.to)")
        }

        // Reading directions. These are labels (decision #210), so what is
        // pinned is the *meaning*: which family of color each end names.
        func inRange(_ h: Double, _ lo: Double, _ hi: Double) -> Bool { h >= lo && h <= hi }
        report(inRange(TrackTint.temperatureEnds.from, 180, 260)
                   && inRange(TrackTint.temperatureEnds.to, 30, 60),
               "temperature reads blue to amber, warmer to the right")
        report(inRange(TrackTint.tintEnds.from, 90, 160)
                   && inRange(TrackTint.tintEnds.to, 280, 330),
               "tint reads green to magenta")
        report(inRange(TrackTint.fringeRCEnds.from, 0, 20)
                   && inRange(TrackTint.fringeRCEnds.to, 160, 200),
               "fringe R/C reads in the order its name spells")
        report(inRange(TrackTint.fringeBYEnds.from, 200, 260)
                   && inRange(TrackTint.fringeBYEnds.to, 40, 70),
               "fringe B/Y likewise")

        // The moderation rule: every label sits below the band swatches, so the
        // panels stay quieter than the controls they sit beside — #63's neutral
        // surround, applied to a rail.
        //
        // ⚠ **Against the swatch's own constants, not against 0.75/0.85.** The
        // literals were written when the swatch was that loud and stayed behind
        // when it was moderated, at which point the assertion was comparing a
        // live number against a remembered one — green whatever the swatch
        // does. The comparison is the whole claim, so it reads both ends.
        report(TrackTint.labelSaturation < HueBand.swatchSaturation
                   && TrackTint.labelBrightness < HueBand.swatchBrightness,
               "track labels are drawn quieter than the band swatches",
               "labels \(TrackTint.labelSaturation)/\(TrackTint.labelBrightness), "
                   + "swatches \(HueBand.swatchSaturation)/\(HueBand.swatchBrightness)")

        // And the swatches themselves stay inside #63: the rule is about the
        // panel, not about which panel element is loudest, so a rail that is
        // merely quieter than an even louder rail would still fail it.
        report(HueBand.swatchSaturation <= 0.65 && HueBand.swatchBrightness <= 0.80,
               "the band swatches are moderated too (#63)",
               "sat \(HueBand.swatchSaturation), bright \(HueBand.swatchBrightness)")

        // Presence starts at gray: the left extreme genuinely is a monochrome
        // picture, so the first stop must carry no chroma and sit at the end.
        let first = TrackTint.presence.stops.first
        report(first?.location == 0, "the presence track's gray stop is at the left end")
        report(TrackTint.presence.stops.count >= 4,
               "and gathers a spectrum toward the right",
               "\(TrackTint.presence.stops.count) stops")

        // The catalogue lookup names exactly the adjustments that mean a
        // color. Spelled as a roster so adding a tinted adjustment must
        // touch this list, the same shape as every catalogue check.
        let tinted = AdjustmentID.allCases.filter { TrackTint.forAdjustment($0) != nil }
        report(Set(tinted) == Set([.temperature, .tint, .warmth, .localTint,
                                   .vibrance, .saturation]),
               "the catalogue tints exactly the color-meaning adjustments",
               tinted.map(\.rawValue).joined(separator: ", "))
    }
}

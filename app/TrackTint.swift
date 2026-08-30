import SwiftUI

/// The label gradients a slider track can wear — what each extreme would do,
/// painted where the throw goes, the way every other editor labels its color
/// sliders.
///
/// ⚠ **Labels, not renderings** — the mask color swatch's doctrine. A track is
/// not a simulation of the picture (a temperature track through the real
/// Planckian locus would need the engine's own white balance maths for a
/// decoration), so these are fixed sRGB stops. Where a number *is* checkable it
/// is honest and pinned: the hue mixer's ends are the shader's own band centers
/// shifted by its own full-travel constant.
///
/// Saturation and brightness are moderated below `HueBand.swatch`'s 0.75/0.85 —
/// decision #63 keeps loud color off the panels so the photograph can be
/// judged against a neutral surround, and a rail of saturated rainbow would be
/// exactly what that rule exists to keep out. The numbers live here as data,
/// separate from the `Gradient` builders, so the viewport suite can grade them
/// without unpacking a SwiftUI `Color`.
enum TrackTint {

    // MARK: The numbers (tested)

    /// Full travel of a mixer Hue slider, in degrees — hsl_ops.slang:128
    /// applies `hue * 30.0f`, so ±1 on the slider is ±30° on the pixel.
    static let hueTravel: Double = 30

    /// The moderated chroma every label here is drawn at (#63).
    static let labelSaturation: Double = 0.55
    static let labelBrightness: Double = 0.65

    /// Where a band's Hue slider ends up at each extreme: the shader's own
    /// band center (`TargetedAdjust.centers`, pinned against kBandCenters by
    /// `testHueBands`) minus and plus the shader's own travel.
    static func hueEnds(for band: HueBand) -> (from: Double, to: Double) {
        let c = TargetedAdjust.centers[band.rawValue]
        return (c - hueTravel, c + hueTravel)
    }

    /// Fixed label ends, in degrees of hue. Temperature reads warmer to the
    /// right like every WB slider; tint green to magenta; the fringe pairs in
    /// the order their names spell.
    static let temperatureEnds = (from: 210.0, to: 45.0)   // blue → amber
    static let tintEnds        = (from: 130.0, to: 315.0)  // green → magenta
    static let fringeRCEnds    = (from: 5.0,   to: 185.0)  // red → cyan
    static let fringeBYEnds    = (from: 225.0, to: 55.0)   // blue → yellow

    // MARK: The builders

    private static func swatch(_ degrees: Double,
                               saturation: Double = labelSaturation,
                               brightness: Double = labelBrightness) -> Color {
        let wrapped = (degrees.truncatingRemainder(dividingBy: 360) + 360)
            .truncatingRemainder(dividingBy: 360)
        return Color(hue: wrapped / 360, saturation: saturation, brightness: brightness)
    }

    private static func span(_ ends: (from: Double, to: Double)) -> Gradient {
        Gradient(colors: [swatch(ends.from), swatch(ends.to)])
    }

    /// A band's Hue track: through the center, so the detent's neighborhood
    /// reads as "no shift" in the band's own color.
    static func hue(for band: HueBand) -> Gradient {
        let e = hueEnds(for: band)
        return Gradient(colors: [swatch(e.from),
                                 swatch(TargetedAdjust.centers[band.rawValue]),
                                 swatch(e.to)])
    }

    /// A band's Saturation track: the band's color drained to gray on the
    /// left, fuller than the label norm on the right.
    static func saturation(for band: HueBand) -> Gradient {
        let c = TargetedAdjust.centers[band.rawValue]
        return Gradient(colors: [swatch(c, saturation: 0),
                                 swatch(c, saturation: 0.75)])
    }

    /// A band's Luminance track: its color, dark to light.
    static func luminance(for band: HueBand) -> Gradient {
        let c = TargetedAdjust.centers[band.rawValue]
        return Gradient(colors: [swatch(c, brightness: 0.18),
                                 swatch(c, brightness: 0.92)])
    }

    static let temperature = span(temperatureEnds)
    static let tint        = span(tintEnds)
    /// Warmth is the local cast with the same reading direction as
    /// temperature: left cools, right warms.
    static let warmth      = span(temperatureEnds)
    static let localTint   = span(tintEnds)
    static let fringeRC    = span(fringeRCEnds)
    static let fringeBY    = span(fringeBYEnds)

    /// Vibrance and Saturation: gray at the left extreme, a spectrum gathering
    /// chroma toward the right — the left end genuinely is a monochrome
    /// picture, the right end is more of every hue.
    static let presence: Gradient = {
        var stops = [Gradient.Stop(color: swatch(0, saturation: 0), location: 0)]
        let hues: [Double] = [0, 60, 120, 180, 240, 300]
        for (i, h) in hues.enumerated() {
            let t = Double(i + 1) / Double(hues.count)
            stops.append(.init(color: swatch(h, saturation: labelSaturation * t),
                               location: 0.2 + 0.8 * t))
        }
        return Gradient(stops: stops)
    }()

    /// The little track under each grading wheel: what it moves is the zone's
    /// brightness, so it reads dark to light and nothing else.
    static let wheelLuma = Gradient(colors: [swatch(0, saturation: 0, brightness: 0.10),
                                             swatch(0, saturation: 0, brightness: 0.90)])

    /// The catalogue-driven rows' lookup, so `AdjustmentGroup` tints local
    /// Warmth, Tint and Saturation without the catalogue itself learning any
    /// SwiftUI — the spec stays pure data, and this switch is the one list of
    /// which adjustments mean a color.
    static func forAdjustment(_ id: AdjustmentID) -> Gradient? {
        switch id {
        case .temperature, .warmth:     return temperature
        case .tint, .localTint:         return tint
        case .vibrance, .saturation:    return presence
        default:                        return nil
        }
    }
}

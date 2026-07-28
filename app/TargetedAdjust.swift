import CoreGraphics
import Foundation

/// The targeted adjustment tool.
///
/// Click a colour in the photo and drag: it finds which hue band that pixel
/// belongs to and adjusts it. This is how people actually use a colour mixer —
/// "make *that* blue deeper" — rather than guessing which of eight swatches the
/// sky happens to fall into.
@Observable
final class TargetedAdjust {

    enum Mode: String, CaseIterable, Identifiable {
        case hue, saturation, luminance
        var id: String { rawValue }

        var title: String {
            switch self {
            case .hue:        "Hue"
            case .saturation: "Saturation"
            case .luminance:  "Luminance"
            }
        }

        var symbol: String {
            switch self {
            case .hue:        "paintpalette"
            case .saturation: "drop"
            case .luminance:  "sun.max"
            }
        }
    }

    var isActive = false
    var mode: Mode = .saturation

    /// The band under the cursor when the drag started, and what it read.
    private(set) var activeBand: HueBand?
    private(set) var sampledHue: Double = 0

    /// Which band a hue in degrees belongs to. Mirrors the band centres in
    /// hsl_ops.slang — if these drift apart, the tool adjusts the wrong band.
    static let centres: [Double] = [0, 30, 60, 120, 180, 240, 285, 320]

    static func band(forHue hue: Double) -> HueBand {
        var best = 0
        var bestDistance = Double.greatestFiniteMagnitude
        for (i, centre) in centres.enumerated() {
            let raw = abs(hue - centre)
            let d = min(raw, 360 - raw)
            if d < bestDistance { bestDistance = d; best = i }
        }
        return HueBand(rawValue: best) ?? .red
    }

    /// Hue in degrees from linear-ish display RGB.
    static func hue(r: Double, g: Double, b: Double) -> Double? {
        let maxc = max(r, max(g, b))
        let minc = min(r, min(g, b))
        let delta = maxc - minc

        // Near-grey has no meaningful hue; refuse rather than pick one at random.
        guard delta > 0.02 else { return nil }

        var h: Double
        if maxc == r      { h = (g - b) / delta + (g < b ? 6 : 0) }
        else if maxc == g { h = (b - r) / delta + 2 }
        else              { h = (r - g) / delta + 4 }

        h *= 60
        return h < 0 ? h + 360 : h
    }

    func begin(band: HueBand, hue: Double) {
        activeBand = band
        sampledHue = hue
    }

    func end() { activeBand = nil }
}

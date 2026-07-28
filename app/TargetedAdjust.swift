import CoreGraphics
import Foundation
import SwiftUI

/// Color mixer bands, in the order the engine expects.
enum HueBand: Int, CaseIterable, Identifiable {
    case red, orange, yellow, green, aqua, blue, purple, magenta
    var id: Int { rawValue }

    var name: String {
        switch self {
        case .red:     "Red"
        case .orange:  "Orange"
        case .yellow:  "Yellow"
        case .green:   "Green"
        case .aqua:    "Aqua"
        case .blue:    "Blue"
        case .purple:  "Purple"
        case .magenta: "Magenta"
        }
    }

    /// Swatch hue, matching the band centers in hsl_ops.slang.
    var swatch: Color {
        Color(hue: Double([0, 30, 60, 120, 180, 240, 285, 320][rawValue]) / 360.0,
              saturation: 0.75, brightness: 0.85)
    }
}

/// The targeted adjustment tool.
///
/// Click a color in the photo and drag: it finds which hue band that pixel
/// belongs to and adjusts it. This is how people actually use a color mixer —
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

    /// Live readout under the cursor, so you can see what you are about to
    /// pick instead of clicking blind.
    var hoverPoint: CGPoint?
    var hoverColor: CGColor?
    var hoverBand: HueBand?
    var hoverIsNeutral = false

    /// Set when a pick lands, so the panel can follow the selection.
    var lastPicked: HueBand?

    func clearHover() {
        hoverPoint = nil
        hoverColor = nil
        hoverBand = nil
        hoverIsNeutral = false
    }

    /// Which band a hue in degrees belongs to. Mirrors the band centers in
    /// hsl_ops.slang — if these drift apart, the tool adjusts the wrong band.
    static let centers: [Double] = [0, 30, 60, 120, 180, 240, 285, 320]

    static func band(forHue hue: Double) -> HueBand {
        var best = 0
        var bestDistance = Double.greatestFiniteMagnitude
        for (i, center) in centers.enumerated() {
            let raw = abs(hue - center)
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

        // Near-gray has no meaningful hue; refuse rather than pick one at random.
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
        lastPicked = band
    }

    func end() { activeBand = nil }
}

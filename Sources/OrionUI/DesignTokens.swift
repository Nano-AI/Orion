// GENERATED from design/tokens.json by design/build-tokens.py.
// Do not edit by hand — your changes will be overwritten.

import SwiftUI

public enum Orion {

    // MARK: Palette
    //
    // Neutrals are deliberately near-perfectly neutral: a tinted interface
    // reads as a color cast and corrupts the judgment the app exists to support.
    public enum Palette {
        /// App background, outermost shell
        public static let ground = Color(hex: 0x141416)
        /// Tool panels, toolbar, filmstrip
        public static let panel = Color(hex: 0x1B1B1D)
        /// Controls, cells, inset wells
        public static let raised = Color(hex: 0x232326)
        /// Canvas surround — what the photo is judged against
        public static let surround = Color(hex: 0x2A2A2C)
        /// Hairlines, borders, dividers
        public static let line = Color(hex: 0x313135)
        /// Scrollbar thumbs. Not slider tracks: AnalogTrack engraves its groove in `ground` deliberately, and tinted tracks draw their own label gradient (TrackTint)
        public static let rail = Color(hex: 0x3A3A3E)
        /// Primary text, slider thumbs
        public static let text = Color(hex: 0xE8E8EA)
        /// Labels, secondary text
        public static let dim = Color(hex: 0x8A8A90)
        /// Tertiary text, inactive icons, detents
        public static let faint = Color(hex: 0x5A5A60)
        /// Interactive and active states ONLY. Never decorative.
        public static let accent = Color(hex: 0x4DB6C4)
        /// Reject flag
        public static let reject = Color(hex: 0xC4574D)
        /// Star rating marks
        public static let star = Color(hex: 0xF0C674)
        /// Histogram red channel
        public static let chanR = Color(hex: 0xC4433A)
        /// Histogram green channel
        public static let chanG = Color(hex: 0x3AA84F)
        /// Histogram blue channel
        public static let chanB = Color(hex: 0x3A6FC4)
        /// Film base — the darkest value in the interface, under the strip
        public static let filmBase = Color(hex: 0x090807)
        /// Sprocket perforations and the rebate edge — a hole shows the light through
        public static let filmHole = Color(hex: 0x4C4A47)
    }

    /// The palette as components, for Metal and anything else that
    /// cannot take a SwiftUI Color. Same source, same numbers.
    public enum Components {
        public static let ground = SIMD3<Float>(0.0784, 0.0784, 0.0863)
        public static let panel = SIMD3<Float>(0.1059, 0.1059, 0.1137)
        public static let raised = SIMD3<Float>(0.1373, 0.1373, 0.1490)
        public static let surround = SIMD3<Float>(0.1647, 0.1647, 0.1725)
        public static let line = SIMD3<Float>(0.1922, 0.1922, 0.2078)
        public static let rail = SIMD3<Float>(0.2275, 0.2275, 0.2431)
        public static let text = SIMD3<Float>(0.9098, 0.9098, 0.9176)
        public static let dim = SIMD3<Float>(0.5412, 0.5412, 0.5647)
        public static let faint = SIMD3<Float>(0.3529, 0.3529, 0.3765)
        public static let accent = SIMD3<Float>(0.3020, 0.7137, 0.7686)
        public static let reject = SIMD3<Float>(0.7686, 0.3412, 0.3020)
        public static let star = SIMD3<Float>(0.9412, 0.7765, 0.4549)
        public static let chanR = SIMD3<Float>(0.7686, 0.2627, 0.2275)
        public static let chanG = SIMD3<Float>(0.2275, 0.6588, 0.3098)
        public static let chanB = SIMD3<Float>(0.2275, 0.4353, 0.7686)
        public static let filmBase = SIMD3<Float>(0.0353, 0.0314, 0.0275)
        public static let filmHole = SIMD3<Float>(0.2980, 0.2902, 0.2784)
    }

    // MARK: Space
    public enum Space {
        public static let xs: CGFloat = 2
        public static let sm: CGFloat = 6
        public static let md: CGFloat = 10
        public static let pad: CGFloat = 14
        public static let lg: CGFloat = 18
        public static let xl: CGFloat = 26
    }

    // MARK: Radius
    public enum Radius {
        public static let sm: CGFloat = 3
        public static let md: CGFloat = 5
    }

    // MARK: Type sizes
    public enum TypeSize {
        public static let xs: CGFloat = 10
        public static let sm: CGFloat = 11
        public static let base: CGFloat = 12
        public static let md: CGFloat = 13
    }

    // MARK: Layout
    public enum Layout {
        public static let toolbar: CGFloat = 44
        public static let filmstrip: CGFloat = 98
        public static let toolCol: CGFloat = 322
        public static let thumbW: CGFloat = 96
        public static let thumbH: CGFloat = 66
    }
}

extension Color {
    /// 0xRRGGBB literal, in the display-P3 space the app renders in.
    /// Tokens are authored as sRGB hex — the same numbers the HTML
    /// mockup renders in a browser, which is where the palette was
    /// judged. Building them as Display P3 would move every colour in
    /// the interface off the design it came from.
    init(hex: UInt32) {
        self.init(
            .sRGB,
            red:   Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >>  8) & 0xFF) / 255,
            blue:  Double( hex        & 0xFF) / 255
        )
    }
}

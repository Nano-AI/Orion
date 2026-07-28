// GENERATED from design/tokens.json by design/build-tokens.py.
// Do not edit by hand — your changes will be overwritten.

import SwiftUI

public enum Orion {

    // MARK: Palette
    //
    // Neutrals are deliberately near-perfectly neutral: a tinted interface
    // reads as a colour cast and corrupts the judgement the app exists to support.
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
        /// Slider tracks, scrollbar thumbs
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
    init(hex: UInt32) {
        self.init(
            .displayP3,
            red:   Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >>  8) & 0xFF) / 255,
            blue:  Double( hex        & 0xFF) / 255
        )
    }
}

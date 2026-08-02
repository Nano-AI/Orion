// What the export panel holds, with no SwiftUI in it.
//
// Split out of ExportPanel.swift 2026-08-01, when bit depth and output
// sharpening took the panel past the point where one file held both the model
// and the view. The rule from CLAUDE.md is that a view model is a plain
// `@Observable` with zero SwiftUI types inside, so any panel can be re-hosted in
// AppKit; keeping it in a file that imports SwiftUI made that a promise rather
// than a fact. This file imports Foundation and nothing else, so the compiler
// holds the line instead of a reviewer.

import Foundation
import Observation

/// Export settings, modeled on macOS Preview's export sheet.
///
/// Preview is the right reference because every Mac user already knows it. The
/// part that matters most is the **live size estimate**: a quality slider with
/// no size beside it is unreadable, which is exactly why Preview shows one.
@Observable
final class ExportSettings {

    enum Format: String, CaseIterable, Identifiable {
        case jpeg, png, tiff
        var id: String { rawValue }

        var title: String {
            switch self {
            case .jpeg: "JPEG"
            case .png:  "PNG"
            case .tiff: "TIFF"
            }
        }

        var ext: String { self == .jpeg ? "jpg" : rawValue }

        /// Matches OrionImageFormat in orion.h.
        var code: Int32 {
            switch self {
            case .png:  0
            case .jpeg: 1
            case .tiff: 2
            }
        }
        var isLossy: Bool { self == .jpeg }

        /// Whether the container can hold more than eight bits. JPEG cannot, so
        /// the control is greyed out rather than offering a setting that would
        /// not reach the file.
        var carriesDepth: Bool { self != .jpeg }

        /// Rough bytes per pixel at a given quality, for the size estimate.
        /// Empirical rather than derived — a real encode is the only exact
        /// answer, and running one per slider tick would be absurd. It is
        /// replaced by a measurement within a fifth of a second.
        func bytesPerPixel(quality: Double, depth: Depth) -> Double {
            let wide = depth == .sixteen
            switch self {
            // JPEG is an eight-bit container whatever the control says.
            case .jpeg: return 0.08 + 0.72 * pow(quality, 2.4)
            case .png:  return wide ? 4.2 : 2.1
            // Uncompressed: three or six bytes of color a pixel, near enough.
            case .tiff: return wide ? 6.0 : 3.0
            }
        }
    }

    enum Size: String, CaseIterable, Identifiable {
        case full, px4096, px2048, px1024, custom
        var id: String { rawValue }

        var title: String {
            switch self {
            case .full:   "Full size"
            case .px4096: "4096 px"
            case .px2048: "2048 px"
            case .px1024: "1024 px"
            case .custom: "Custom…"
            }
        }

        var longestEdge: UInt32 {
            switch self {
            case .full, .custom: 0
            case .px4096: 4096
            case .px2048: 2048
            case .px1024: 1024
            }
        }
    }

    /// What the file is tagged as, and converted to on the way out.
    ///
    /// ⚠️ The display transform ends in Rec.709 primaries and saturates there,
    /// so nothing Orion renders today falls outside sRGB. A wider space is
    /// converted and tagged correctly — which is what a print shop or a managed
    /// workflow asks for — but it cannot add saturation the transform never
    /// produced. The panel says so rather than implying otherwise.
    enum Space: String, CaseIterable, Identifiable {
        case srgb, displayP3, adobeRGB
        var id: String { rawValue }

        var title: String {
            switch self {
            case .srgb:      "sRGB"
            case .displayP3: "Display P3"
            case .adobeRGB:  "Adobe RGB"
            }
        }

        /// Matches OrionColorSpace in orion.h.
        var code: Int32 {
            switch self {
            case .srgb:      0
            case .displayP3: 1
            case .adobeRGB:  2
            }
        }

        var note: String {
            switch self {
            case .srgb:      "The safe choice. What the web and most screens expect."
            case .displayP3: "For Apple displays and print. Converted and tagged, "
                           + "though nothing Orion renders yet reaches past sRGB."
            case .adobeRGB:  "What some print shops ask for. Converted and tagged, "
                           + "though nothing Orion renders yet reaches past sRGB."
            }
        }
    }

    /// Bits per component in the written file.
    ///
    /// The raw values are the depths themselves and match `OrionBitDepth`.
    ///
    /// ⚠️ Eight is not a downgrade of sixteen — it is a different path. An
    /// eight-bit export renders through the narrow graph, which is the one that
    /// dithers, so it is what the screen shows. Sixteen renders wide and skips
    /// the dither because at sixteen bits there is nothing to hide.
    enum Depth: Int32, CaseIterable, Identifiable {
        case eight = 8, sixteen = 16
        var id: Int32 { rawValue }

        var title: String {
            switch self {
            case .eight:   "8-bit"
            case .sixteen: "16-bit"
            }
        }

        var note: String {
            switch self {
            case .eight:   "What every browser, viewer and print lab reads."
            case .sixteen: "Twice the file. Worth it only if the export is going "
                         + "back into an editor for more work."
            }
        }
    }

    /// Output sharpening: the correction for the softening a resize causes.
    ///
    /// Matches `OrionSharpen`. Applied after the resize, at the final size,
    /// which is the whole point — see `research/detail.md`.
    enum Sharpening: Int32, CaseIterable, Identifiable {
        case none = 0, screen = 1, print = 2
        var id: Int32 { rawValue }

        var title: String {
            switch self {
            case .none:   "None"
            case .screen: "Screen"
            case .print:  "Print"
            }
        }

        var note: String {
            switch self {
            case .none:   "The pixels as rendered. Right when the export is going "
                        + "into another editor."
            case .screen: "A light pass, for a file that will be looked at on a "
                        + "display."
            case .print:  "A stronger pass. Ink spreads on paper, so a print takes "
                        + "more than a screen does."
            }
        }
    }

    /// What the file carries from the RAW.
    ///
    /// The default strips location. A photo taken at home carries the home
    /// coordinates in its GPS block, and a file put on the web hands them to
    /// everyone who downloads it — silently, because nothing in an image viewer
    /// says so. Keeping them is a choice, and this is where it is made.
    enum Metadata: Int32, CaseIterable, Identifiable {
        case all = 0, noLocation = 1, none = 2
        var id: Int32 { rawValue }

        var title: String {
            switch self {
            case .all:        "Keep all"
            case .noLocation: "Strip location"
            case .none:       "Strip everything"
            }
        }

        var note: String {
            switch self {
            case .all:        "Camera, lens, exposure, date — and where the photo "
                            + "was taken. Anyone who downloads the file can read it."
            case .noLocation: "Camera, lens, exposure and date. GPS coordinates and "
                            + "IPTC place names are both removed."
            case .none:       "Nothing but the star rating and that Orion developed it."
            }
        }
    }

    var format: Format = .jpeg
    var quality: Double = 0.9
    var size: Size = .full
    var space: Space = .srgb
    var metadata: Metadata = .noLocation

    /// Eight bits, because a file handed to someone else should be eight bits.
    /// The engine's own default is sixteen — an unspecified export keeps what it
    /// was given — but the panel is never unspecified.
    var depth: Depth = .eight

    /// Off by default. Sharpening an export that is going back into an editor is
    /// damage, and there is no way for the panel to know which this is.
    var sharpening: Sharpening = .none

    /// Typed dimensions, used when `size` is `.custom`. The aspect is held, so
    /// entering either one sets the other — a free pair would let you squash
    /// the picture by accident, and nobody exporting a photo wants that.
    var customWidth: UInt32 = 0
    var customHeight: UInt32 = 0

    /// Measured by a real encode, not estimated. nil until the first one lands.
    var measuredBytes: Int?

    /// ⚠️ What the file will actually hold, which is not always what the
    /// control shows. Picking 16-bit and then switching to JPEG leaves the
    /// control on 16, and asking the engine for sixteen bits there would render
    /// the wide graph — the one that does **not** dither — only for ImageIO to
    /// round it to eight afterwards with nothing to break up the banding. Every
    /// call into the engine goes through this, not through `depth`.
    var effectiveDepth: Depth { format.carriesDepth ? depth : .eight }

    /// Pixel dimensions after resizing, given the source.
    func dimensions(sourceWidth: UInt32, sourceHeight: UInt32) -> (UInt32, UInt32) {
        if size == .custom {
            let w = max(1, customWidth), h = max(1, customHeight)
            return (w, h)
        }

        let longest = max(sourceWidth, sourceHeight)
        let limit = size.longestEdge
        guard limit > 0, longest > limit else { return (sourceWidth, sourceHeight) }

        let scale = Double(limit) / Double(longest)
        return (max(1, UInt32((Double(sourceWidth) * scale).rounded())),
                max(1, UInt32((Double(sourceHeight) * scale).rounded())))
    }

    /// What the engine is asked to cap the longest edge at. Custom dimensions
    /// are expressed the same way, because the export path resizes by longest
    /// edge and holding the aspect means the two are equivalent.
    func longestEdge(sourceWidth: UInt32, sourceHeight: UInt32) -> UInt32 {
        let (w, h) = dimensions(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        let longest = max(w, h)
        return longest >= max(sourceWidth, sourceHeight) ? 0 : longest
    }

    /// Sets one dimension and derives the other from the source's aspect.
    func setCustom(width: UInt32, sourceWidth: UInt32, sourceHeight: UInt32) {
        guard sourceWidth > 0, sourceHeight > 0 else { return }
        customWidth = max(1, width)
        customHeight = max(1, UInt32((Double(customWidth)
            * Double(sourceHeight) / Double(sourceWidth)).rounded()))
    }

    func setCustom(height: UInt32, sourceWidth: UInt32, sourceHeight: UInt32) {
        guard sourceWidth > 0, sourceHeight > 0 else { return }
        customHeight = max(1, height)
        customWidth = max(1, UInt32((Double(customHeight)
            * Double(sourceWidth) / Double(sourceHeight)).rounded()))
    }

    func estimatedBytes(sourceWidth: UInt32, sourceHeight: UInt32) -> Int {
        let (w, h) = dimensions(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        return Int(Double(w) * Double(h)
                   * format.bytesPerPixel(quality: quality, depth: effectiveDepth))
    }

    /// The measured size if one has come back, otherwise the estimate. The
    /// estimate exists only to fill the moment before the first encode lands;
    /// showing nothing there makes the panel look broken.
    func sizeText(sourceWidth: UInt32, sourceHeight: UInt32) -> String {
        let bytes = measuredBytes
            ?? estimatedBytes(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        let f = ByteCountFormatter()
        f.countStyle = .file
        f.allowedUnits = [.useMB, .useKB]
        return f.string(fromByteCount: Int64(bytes))
    }
}

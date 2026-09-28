// The one saved watermark, with no SwiftUI in it.
//
// Foundation only, for the same reason `ExportSettings.swift` is: the rule from
// CLAUDE.md is that a view model has zero SwiftUI types inside, and a file that
// cannot import SwiftUI keeps that a fact rather than a promise.
//
// ⚠ The mark is one fixed color, and that is the design, not a shortcut.
// A one-color mark is fully described by where it covers, so the engine never
// learns about text or SVG: `WatermarkRaster` turns this into an 8-bit coverage
// mask at the output size and the writer blends `gray` through it. An SVG's own
// colors are ignored; only its shape (alpha) is kept. DECISIONS #284.

import Foundation
import Observation

@Observable
final class Watermark {

    enum Content: Codable, Equatable {
        case text(String, fontFamily: String, bold: Bool)
        /// The file's bytes, copied in, so moving or deleting the original
        /// cannot break the mark. The name is only for the panel to show.
        case svg(Data, name: String)

        var isText: Bool { if case .text = self { true } else { false } }
    }

    /// The nine positions of a 3x3 grid. `fx`/`fy` are where the mark's own
    /// box sits in the frame: 0 is the left or top edge, 1 the right or bottom.
    enum Anchor: String, Codable, CaseIterable, Identifiable {
        case topLeft, top, topRight, left, center, right, bottomLeft, bottom, bottomRight
        var id: String { rawValue }

        /// Row-major in declaration order, which is also the order the panel's
        /// grid draws them in.
        private var index: Int { Anchor.allCases.firstIndex(of: self) ?? 8 }
        var fx: Double { Double(index % 3) / 2 }
        var fy: Double { Double(index / 3) / 2 }

        var title: String {
            ["top left", "top", "top right", "left", "center", "right",
             "bottom left", "bottom", "bottom right"][index]
        }
    }

    enum Placement: Codable, Equatable {
        case anchor(Anchor)
        /// Centered, and rotated to run bottom-left to top-right along the
        /// frame's own diagonal.
        case diagonal
    }

    /// The only color a mark can be: neutral gray, in sRGB. Settled with the
    /// developer - the control is how visible it is, not what it looks like.
    static let gray: (Float, Float, Float) = (0.5, 0.5, 0.5)

    /// Whether exports carry it. Saved with the mark, so the switch in the
    /// Export panel survives a relaunch while the rest of that panel is per
    /// session.
    var enabled = false
    var content: Content = .text("", fontFamily: "Helvetica Neue", bold: false)
    var placement: Placement = .anchor(.bottomRight)
    /// Anchored: the mark's height as a fraction of the frame's short edge.
    var size = 0.04
    /// Diagonal: the mark's length as a fraction of the frame's diagonal.
    var span = 0.6
    /// Anchored: the gap to the frame's edges, as a fraction of the short edge.
    var margin = 0.03
    /// 0..1, baked into the mask's values. Quite transparent by default.
    var opacity = 0.35

    /// Whether there is anything to draw. A mark switched on with no text and
    /// no file writes nothing rather than an empty pass.
    var isDrawable: Bool {
        switch content {
        case .text(let s, _, _): !s.trimmingCharacters(in: .whitespaces).isEmpty
        case .svg(let d, _):     !d.isEmpty
        }
    }

    /// One line for the Export panel's row: what the mark is and where it goes.
    var summary: String {
        guard isDrawable else { return "Nothing to draw yet" }
        let what: String
        switch content {
        case .text(let s, _, _): what = "\u{201C}\(s)\u{201D}"
        case .svg(_, let name):  what = name.isEmpty ? "SVG" : name
        }
        switch placement {
        case .anchor(let a): return "\(what), \(a.title)"
        case .diagonal:      return "\(what), diagonal"
        }
    }

    // ── Persistence ─────────────────────────────────────────────────────────
    //
    // JSON in Application Support, as `PresetStore` does it: one small file a
    // photographer can read, written atomically, with the reason a save failed
    // kept rather than swallowed.

    private struct Saved: Codable {
        var enabled: Bool
        var content: Content
        var placement: Placement
        var size, span, margin, opacity: Double
    }

    private let url: URL?

    /// Why the last save did not land, or nil if it did.
    private(set) var lastFailure: String?

    init(url: URL? = Watermark.defaultURL()) {
        self.url = url
        guard let url, let data = try? Data(contentsOf: url),
              let saved = try? JSONDecoder().decode(Saved.self, from: data) else { return }
        enabled = saved.enabled
        content = saved.content
        placement = saved.placement
        size = saved.size
        span = saved.span
        margin = saved.margin
        opacity = saved.opacity
    }

    static func defaultURL() -> URL? {
        guard let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                                  in: .userDomainMask).first
        else { return nil }
        let dir = base.appendingPathComponent("Orion", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("watermark.json")
    }

    @discardableResult
    func save() -> Bool {
        guard let url else {
            lastFailure = "Orion has nowhere to keep the watermark on this machine."
            return false
        }
        let saved = Saved(enabled: enabled, content: content, placement: placement,
                          size: size, span: span, margin: margin, opacity: opacity)
        do {
            try JSONEncoder().encode(saved).write(to: url, options: .atomic)
            lastFailure = nil
            return true
        } catch {
            lastFailure = "The watermark could not be saved. " + error.localizedDescription
            FileHandle.standardError.write(Data(
                "orion: watermark could not be saved - \(error.localizedDescription)\n".utf8))
            return false
        }
    }
}

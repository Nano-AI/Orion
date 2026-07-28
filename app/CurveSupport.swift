import Foundation

// The tone curve's value types. Kept apart from the panel that edits them and
// the history that stores them, so the geometry tests can link the maths
// without dragging SwiftUI and the engine handle in behind it.

/// One point of a tone curve. Input and output both 0..1, in display space.
struct CurvePoint: Equatable, Hashable {
    var x: Float
    var y: Float
}

/// The four channels a curve panel edits. Master applies to all three, then
/// each channel's own curve applies on top — which is how every editor's curve
/// panel behaves, and what the display shader already implements.
struct ToneCurve: Equatable {
    static let identity: [CurvePoint] = [CurvePoint(x: 0, y: 0), CurvePoint(x: 1, y: 1)]

    var master = identity
    var red = identity
    var green = identity
    var blue = identity

    var isIdentity: Bool {
        master == Self.identity && red == Self.identity
            && green == Self.identity && blue == Self.identity
    }

    subscript(channel: CurveChannel) -> [CurvePoint] {
        get {
            switch channel {
            case .master: master
            case .red:    red
            case .green:  green
            case .blue:   blue
            }
        }
        set {
            switch channel {
            case .master: master = newValue
            case .red:    red = newValue
            case .green:  green = newValue
            case .blue:   blue = newValue
            }
        }
    }
}

enum CurveChannel: String, CaseIterable, Identifiable {
    case master, red, green, blue
    var id: String { rawValue }

    var title: String {
        switch self {
        case .master: "RGB"
        case .red:    "R"
        case .green:  "G"
        case .blue:   "B"
        }
    }
}

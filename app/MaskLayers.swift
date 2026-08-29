import Foundation

/// The grouping a mask list is drawn from — pure functions over the component
/// array, with no SwiftUI and no Engine, so the viewport suite can grade them
/// without a window.
///
/// A *layer* is a maximal run of consecutive components: row 0 always begins
/// one (the engine forces its fold to start from zero whatever its flag says —
/// `DevelopMask.cpp`'s row-0 rule), and every later row with `startsLayer`
/// begins the next. This is the same derivation `Engine.selectedLayer` and
/// `layerCount` are computed from; they call here now, so the panel's cards and
/// the engine's layer table cannot disagree about where a mask begins.
enum MaskLayers {

    /// The stack as runs of component indices, in listed order. Empty in, empty
    /// out; otherwise every index appears exactly once and run boundaries fall
    /// on `startsLayer` (row 0 unconditionally).
    static func group(_ components: [MaskComponentState]) -> [[Int]] {
        var runs: [[Int]] = []
        for (i, m) in components.enumerated() {
            if i == 0 || m.startsLayer {
                runs.append([i])
            } else {
                runs[runs.count - 1].append(i)
            }
        }
        return runs
    }

    /// Which layer the component at `index` belongs to. Clamps, so a selection
    /// one past the end (mid-removal) still names a real layer.
    static func layerIndex(ofComponent index: Int, in components: [MaskComponentState]) -> Int {
        guard !components.isEmpty else { return 0 }
        let i = min(max(index, 0), components.count - 1)
        var layer = 0
        for m in components[..<i].dropFirst() where m.startsLayer { layer += 1 }
        if i > 0 && components[i].startsLayer { layer += 1 }
        return layer
    }

    /// What the panel calls a mask kind. Interface words, deliberately not on
    /// the state struct — the shader's `kind` is a number and the state has no
    /// business knowing what a photographer calls it.
    static func kindName(_ kind: Int32) -> String {
        switch kind {
        case 1:  return "Linear"
        case 2:  return "Radial"
        case 3:  return "Brush"
        case 4:  return "Selection"
        case 5:  return "Range"
        // ⚠ Kind 6 was missing once, so every Color range row was labelled
        // "Off" — the one word that says a row is doing nothing, on a row that
        // is doing something.
        case 6:  return "Color"
        default: return "Off"
        }
    }

    /// The layer's name as the card shows it: the starting component's chosen
    /// name, else a default. A whitespace-only name counts as unchosen, so
    /// clearing a rename gets the default back rather than a blank card.
    static func displayName(ofLayer layer: Int, in components: [MaskComponentState]) -> String {
        let runs = group(components)
        guard runs.indices.contains(layer) else { return "" }
        let start = runs[layer][0]
        if let chosen = components[start].name?
            .trimmingCharacters(in: .whitespacesAndNewlines), !chosen.isEmpty {
            return chosen
        }
        return defaultName(forLayer: layer, in: components)
    }

    /// "Radial 2": the starting component's kind, numbered among the layers
    /// that *start* with that kind, in stack order. Numbered even when it is
    /// the only one, so a card's name never changes just because a second
    /// radial arrived. A detected selection keeps its producer's word
    /// ("Subject", "Sky") over the generic kind, since that is what the row
    /// already says about itself.
    static func defaultName(forLayer layer: Int, in components: [MaskComponentState]) -> String {
        let runs = group(components)
        guard runs.indices.contains(layer) else { return "" }
        func word(_ i: Int) -> String {
            components[i].matteSource ?? kindName(components[i].kind)
        }
        let mine = word(runs[layer][0])
        var ordinal = 0
        for (l, run) in runs.enumerated() {
            if word(run[0]) == mine { ordinal += 1 }
            if l == layer { break }
        }
        return "\(mine) \(ordinal)"
    }
}

// The mask list's grouping and naming, and the nested struct's roster.
//
// `MaskLayers` is what the panel's layer cards and `Engine.selectedLayer` both
// read, so these are the checks that the two surfaces cannot disagree about
// where a mask begins or what it is called.

import CoreGraphics
import Foundation

extension ViewportTests {

    private static func comp(_ kind: Int32, starts: Bool = false,
                             name: String? = nil, source: String? = nil) -> MaskComponentState {
        var m = MaskComponentState()
        m.kind = kind
        m.startsLayer = starts
        m.name = name
        m.matteSource = source
        return m
    }

    static func testMaskLayersGrouping() {
        report(MaskLayers.group([]).isEmpty, "an empty stack has no layers")

        report(MaskLayers.group([comp(1)]) == [[0]], "a single row is a single layer")

        // ⚠ The row-0 rule: the engine folds the first row from zero whatever
        // its flag says, so the grouping must not care either — a sidecar whose
        // first component carries startsLayer=false is the pre-#197 shape.
        report(MaskLayers.group([comp(1, starts: true), comp(2)]) == [[0, 1]],
               "row 0 flagged as a start begins the one layer")
        report(MaskLayers.group([comp(1), comp(2)]) == [[0, 1]],
               "row 0 unflagged begins it just the same")

        let linked = [comp(1), comp(3), comp(5)]
        report(MaskLayers.group(linked) == [[0, 1, 2]],
               "rows that continue fold into one layer")

        let split = [comp(1), comp(3), comp(2, starts: true), comp(6)]
        report(MaskLayers.group(split) == [[0, 1], [2, 3]],
               "a break starts the next layer with everything after it")

        var hidden = comp(3); hidden.hidden = true
        report(MaskLayers.group([comp(1), hidden, comp(2, starts: true)]) == [[0, 1], [2]],
               "a hidden row keeps its place in its layer — the eye is not a break")

        let every = MaskLayers.group(split).flatMap { $0 }
        report(every == [0, 1, 2, 3], "every index appears exactly once, in order")

        report(MaskLayers.layerIndex(ofComponent: 3, in: split) == 1,
               "a continuing row belongs to the layer above it")
        report(MaskLayers.layerIndex(ofComponent: 99, in: split) == 1,
               "a selection past the end clamps to the last layer")
        report(MaskLayers.layerIndex(ofComponent: -2, in: split) == 0,
               "and one before the start clamps to the first")
        report(MaskLayers.layerIndex(ofComponent: 0, in: []) == 0,
               "an empty stack answers 0 rather than trapping")
    }

    static func testMaskLayerNames() {
        // Ordinals count layers *starting* with the kind, in stack order.
        let stack = [comp(2), comp(1, starts: true), comp(2, starts: true),
                     comp(3)]
        report(MaskLayers.displayName(ofLayer: 0, in: stack) == "Radial 1",
               "the first radial is Radial 1",
               MaskLayers.displayName(ofLayer: 0, in: stack))
        report(MaskLayers.displayName(ofLayer: 1, in: stack) == "Linear 1",
               "a lone linear still carries its number — a second one cannot rename it")
        report(MaskLayers.displayName(ofLayer: 2, in: stack) == "Radial 2",
               "the second radial counts only radial starts",
               MaskLayers.displayName(ofLayer: 2, in: stack))

        // A detected selection keeps its producer's word over the generic kind.
        let detected = [comp(4, source: "Sky")]
        report(MaskLayers.displayName(ofLayer: 0, in: detected) == "Sky 1",
               "a selection is named by what found it",
               MaskLayers.displayName(ofLayer: 0, in: detected))

        // A chosen name wins; whitespace is not a choice.
        let named = [comp(2, name: "Her face"), comp(1, starts: true, name: "   ")]
        report(MaskLayers.displayName(ofLayer: 0, in: named) == "Her face",
               "a chosen name replaces the default")
        report(MaskLayers.displayName(ofLayer: 1, in: named) == "Linear 1",
               "a blank rename falls back to the default rather than a blank card")

        // The layer is named by its *starting* row.
        let run = [comp(2, name: "Sky"), comp(3, name: "Not me")]
        report(MaskLayers.displayName(ofLayer: 0, in: run) == "Sky",
               "a continuing row's name does not name the layer")

        report(MaskLayers.displayName(ofLayer: 5, in: run).isEmpty,
               "a layer that does not exist has no name")
    }

    /// Every field of this fixture is off its default, which is what lets the
    /// roster test below mean what it says.
    private static func busyComponent() -> MaskComponentState {
        var m = MaskComponentState()
        m.kind = 6; m.compose = 1; m.invert = true; m.hidden = true
        m.startsLayer = true
        m.centerX = 0.3; m.centerY = 0.7; m.angle = 0.4; m.length = 0.9
        m.radiusX = 0.2; m.radiusY = 0.4; m.feather = 0.8; m.roundness = 3
        m.rangeLo = -1; m.rangeHi = 1; m.rangeSoft = 0.5
        m.colorR = 0.5; m.colorG = 0.6; m.colorB = 0.7
        m.colorTol = 0.2; m.colorSoft = 0.1
        m.brushRadius = 0.12; m.brushFlow = 0.9; m.brushHardness = 0.8
        m.brushStroke = [0.1, 0.2]; m.brushErase = [1]
        m.matteId = "m1"; m.matteSource = "Subject"
        m.name = "Named"
        return m
    }

    /// `DevelopState.fieldRoster`'s rule (#110), applied to the nested struct
    /// that never had it — which is exactly how `rangeLo`/`rangeHi`/`rangeSoft`
    /// were written to every sidecar and read back never for five sessions: the
    /// encoder is synthesized from the stored properties, the decoder reads the
    /// hand-written `Key`, and nothing compared the two.
    static func testMaskComponentRoster() {
        let mirror = Mirror(reflecting: MaskComponentState())
        let seen = Set(mirror.children.compactMap(\.label))

        report(seen.count == mirror.children.count,
               "every stored property of MaskComponentState is labelled",
               "\(seen.count) of \(mirror.children.count)")

        let added = seen.subtracting(MaskComponentState.fieldRoster).sorted()
        report(added.isEmpty,
               "MaskComponentState has no field the roster does not know about — "
                   + "a new one goes in the struct, the Key enum and init(from:)",
               added.joined(separator: ", "))

        let dropped = MaskComponentState.fieldRoster.subtracting(seen).sorted()
        report(dropped.isEmpty,
               "the component roster names no field that has been removed",
               dropped.joined(separator: ", "))

        // Off-default fixture, then the round trip that proves the decoder
        // reads every key the encoder writes. `String(describing:)` spans the
        // mixed field types the way testDevelopStateRoster's does.
        let fresh = Mirror(reflecting: MaskComponentState()).children.map {
            ($0.label ?? "?", String(describing: $0.value))
        }
        let busy = Mirror(reflecting: busyComponent()).children.map {
            ($0.label ?? "?", String(describing: $0.value))
        }
        var untouched: [String] = []
        for (a, b) in zip(fresh, busy) where a.1 == b.1 { untouched.append(a.0) }
        report(untouched.isEmpty,
               "busyComponent() moves every field off its default, so the round "
                   + "trip can actually see each one",
               untouched.joined(separator: ", "))

        if let data = try? JSONEncoder().encode(busyComponent()),
           let back = try? JSONDecoder().decode(MaskComponentState.self, from: data) {
            report(back == busyComponent(),
                   "a fully busy component round-trips unchanged — no field is "
                       + "write-only")
            report(back.name == "Named", "the name rides the sidecar")
        } else {
            report(false, "a busy component round-trips at all")
        }

        // A sidecar from before names existed leaves the field empty.
        if let old = try? JSONDecoder().decode(
            MaskComponentState.self, from: Data(#"{"kind":2}"#.utf8)) {
            report(old.name == nil, "an old sidecar's component has no name")
        } else {
            report(false, "an old component decodes")
        }
    }
}

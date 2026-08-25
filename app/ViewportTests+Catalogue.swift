// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

// MARK: - The adjustment catalogue
//
// The controls are defined once and rendered by scope, so the global panel and
// a mask's panel cannot drift. The catalogue is pure data, which is why it lives
// where this suite can reach it — the view that renders it cannot be tested
// here, but everything it decides can.

extension ViewportTests {

    /// ⚠ Every case of `AdjustmentID` has a spec. `AdjustmentCatalogue.spec`
    /// force-unwraps, and a missing entry would trap at the first draw — but
    /// the failure that matters is quieter: a control simply not appearing.
    static func testCatalogueCoversEveryAdjustment() {
        var missing: [String] = []
        for id in AdjustmentID.allCases where !AdjustmentCatalogue.all.contains(where: { $0.id == id }) {
            missing.append(id.rawValue)
        }
        report(missing.isEmpty, "every adjustment id has a spec", missing.joined(separator: ", "))

        // And no duplicates, which would make `spec(_:)` silently pick one.
        let ids = AdjustmentCatalogue.all.map(\.id.rawValue)
        report(Set(ids).count == ids.count, "and no id is specified twice",
               "\(ids.count) specs, \(Set(ids).count) distinct")

        // ⚠ Contrast is at different *stages* in its two scopes — globally the
        // display transform, locally the mask node — and that is the case a
        // single stage per adjustment would have got wrong. It is checked
        // rather than trusted, because the ordering readout is drawn from it.
        let contrast = AdjustmentCatalogue.spec(.contrast)
        report(contrast.global?.stage == .display && contrast.local?.stage == .tone,
               "contrast sits after the mask globally and inside it locally",
               "\(String(describing: contrast.global?.stage)) / \(String(describing: contrast.local?.stage))")

        // A spec with neither scope is a row that can never be drawn.
        let orphans = AdjustmentCatalogue.all.filter { $0.global == nil && $0.local == nil }
        report(orphans.isEmpty, "and every spec is reachable in some scope",
               orphans.map(\.title).joined(separator: ", "))
    }

    /// ⚠ **The load-bearing one.** The catalogue's `local` scopes are what the
    /// panel offers; `develop_linear` is what the engine actually applies. If
    /// those two disagree the interface shows a control that does nothing, or
    /// hides one that works — and both look like the feature being broken.
    ///
    /// The shader's local set is exposure, contrast, saturation, warmth, tint
    /// and the four tone bands. Written out here rather than derived, because
    /// deriving it from the same table it is checking would prove nothing.
    static func testCatalogueAgreesWithTheShaderAboutWhatIsLocal() {
        let expected: Set<String> = ["exposure", "contrast", "saturation",
                                     "warmth", "localTint",
                                     "highlights", "shadows", "whites", "blacks"]
        let offered = Set(AdjustmentCatalogue.localSet.map(\.id.rawValue))
        report(offered == expected,
               "the catalogue offers exactly the adjustments the shader applies locally",
               "offered \(offered.sorted()), shader has \(expected.sorted())")

        // ⚠ And white balance is not among them, which is the one people
        // expect to be. research/masking.md §2b: it runs before the demosaic.
        report(!offered.contains("temperature") && !offered.contains("tint"),
               "and white balance is not local, whatever the panel might suggest")
    }

    /// A refusal without a reason is a control that has simply gone missing.
    static func testEveryRefusalGivesAReason() {
        let silent = AdjustmentCatalogue.all.filter {
            $0.local == nil && $0.global != nil && $0.localRefusal == nil
        }
        report(silent.isEmpty,
               "every adjustment that cannot be local says why",
               silent.map(\.title).joined(separator: ", "))

        // The list the panel shows must not be empty, or the refusals were
        // dropped rather than answered.
        report(AdjustmentCatalogue.refusedLocally.count >= 5,
               "and the panel has a real list to show",
               "\(AdjustmentCatalogue.refusedLocally.count)")
    }
}

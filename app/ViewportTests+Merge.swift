import Foundation

/// The HDR merge flow's pure logic — eligibility, the default reference, and
/// output naming. Everything here runs with no engine and no GPU; the merge
/// itself is tested in `orion-tests` where a device exists.
extension ViewportTests {

    private static func frame(_ name: String, camera: String = "SONY ILCE-7RM3",
                              iso: Float = 100, shutter: Float,
                              aperture: Float = 8) -> HdrMergeFlow.Candidate {
        HdrMergeFlow.Candidate(url: URL(fileURLWithPath: "/shoot/\(name)"),
                               camera: camera, iso: iso, shutter: shutter,
                               aperture: aperture)
    }

    static func testMergeEligibility() {
        report(HdrMergeFlow.ineligibility([frame("a.arw", shutter: 0.01)]) != nil,
               "one frame is not a bracket")
        report(HdrMergeFlow.ineligibility([
            frame("a.arw", shutter: 0.01),
            frame("b.arw", shutter: 0.04),
        ]) == nil, "two frames of one camera qualify")
        report(HdrMergeFlow.ineligibility([
            frame("a.arw", shutter: 0.01),
            frame("b.arw", camera: "SONY ILCE-7M4", shutter: 0.04),
        ]) != nil, "two bodies refuse — one sensor's noise model, one merge")
        // A frame with no camera string (a sidecar-less file the index has
        // not met) must not veto the pair that does match.
        report(HdrMergeFlow.ineligibility([
            frame("a.arw", shutter: 0.01),
            frame("b.arw", camera: "", shutter: 0.04),
        ]) == nil, "an unknown camera string does not veto")
    }

    static func testMergeDefaultReference() {
        // The ladder is -2/0/+2 EV by shutter; the middle is the metered
        // frame regardless of selection order.
        let bracket = [
            frame("over.arw", shutter: 0.04),
            frame("under.arw", shutter: 0.0025),
            frame("metered.arw", shutter: 0.01),
        ]
        report(HdrMergeFlow.defaultReference(bracket) == 2,
               "the middle exposure is the default framing")

        // All EXIF unusable: degrade to the middle of the selection.
        let blank = [
            frame("a.arw", shutter: 0),
            frame("b.arw", shutter: 0),
            frame("c.arw", shutter: 0),
        ]
        report(HdrMergeFlow.defaultReference(blank) == 1,
               "no exposure data degrades to the middle of the order")
    }

    static func testMergeOutputNaming() {
        let ref = URL(fileURLWithPath: "/shoot/_PIC8220.ARW")

        let fresh = HdrMergeFlow.outputURL(reference: ref, exists: { _ in false })
        report(fresh.lastPathComponent == "_PIC8220-HDR.dng",
               "the merged file is named for its reference",
               fresh.lastPathComponent)
        report(fresh.deletingLastPathComponent().path == "/shoot",
               "and lands beside the sources")

        // Collisions walk -2, -3… and never overwrite — BatchExport's rule.
        var taken: Set<String> = ["/shoot/_PIC8220-HDR.dng",
                                  "/shoot/_PIC8220-HDR-2.dng"]
        let third = HdrMergeFlow.outputURL(reference: ref,
                                           exists: { taken.contains($0.path) })
        report(third.lastPathComponent == "_PIC8220-HDR-3.dng",
               "a taken name steps the suffix", third.lastPathComponent)
        report(!taken.contains(third.path), "and the answer is never a taken name")
    }
}

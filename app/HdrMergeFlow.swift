import Foundation

/// The HDR merge's pure logic: which selections qualify, which frame is the
/// default reference, and what the output is called. No engine and no
/// AppKit, so `orion-viewport-tests` checks all of it without a GPU — the
/// same split `BatchExport` and `PhotoSelection` use. The twenty lines that
/// need a device are in `HdrMergeDriver.swift`.
enum HdrMergeFlow {

    /// What the flow needs to know about one candidate frame — a projection
    /// of `Library.Photo`, so the logic never depends on the library type.
    struct Candidate {
        let url: URL
        let camera: String
        let iso: Float
        let shutter: Float
        let aperture: Float
    }

    /// Nil when the selection can merge; otherwise the sentence the footer
    /// shows. Two frames of the same camera is the floor — different bodies
    /// mean different sensors, and the merge's noise model and color
    /// metadata both assume one.
    static func ineligibility(_ candidates: [Candidate]) -> String? {
        guard candidates.count >= 2 else {
            return "Select at least two exposures to merge."
        }
        let cameras = Set(candidates.map(\.camera).filter { !$0.isEmpty })
        guard cameras.count <= 1 else {
            return "These photos come from different cameras: "
                 + cameras.sorted().joined(separator: ", ") + "."
        }
        return nil
    }

    /// t·ISO/N² — the same light-gathered measure the engine uses. Zero when
    /// the triplet is unusable, which sorts to the ladder's edge rather than
    /// crashing the ordering.
    static func lightGathered(_ c: Candidate) -> Double {
        guard c.shutter > 0, c.iso > 0, c.aperture > 0 else { return 0 }
        return Double(c.shutter) * Double(c.iso)
             / (Double(c.aperture) * Double(c.aperture))
    }

    /// The middle of the exposure ladder. A bracket's middle frame is the
    /// one metered as "correct", which is what a photographer expects the
    /// framing to come from — and with unusable EXIF everywhere it degrades
    /// to the middle of the selection order.
    static func defaultReference(_ candidates: [Candidate]) -> Int {
        guard !candidates.isEmpty else { return 0 }
        let order = candidates.indices.sorted {
            lightGathered(candidates[$0]) < lightGathered(candidates[$1])
        }
        return order[order.count / 2]
    }

    /// `<reference-stem>-HDR.dng` beside the sources, with `-2`, `-3`…
    /// suffixes on collision — the same rule `BatchExport.plan` applies, for
    /// the same reason: a merge must never overwrite a photograph.
    static func outputURL(reference: URL, exists: (URL) -> Bool) -> URL {
        let folder = reference.deletingLastPathComponent()
        let stem = reference.deletingPathExtension().lastPathComponent + "-HDR"
        let first = folder.appendingPathComponent(stem + ".dng")
        guard exists(first) else { return first }
        var n = 2
        while true {
            let next = folder.appendingPathComponent("\(stem)-\(n).dng")
            if !exists(next) { return next }
            n += 1
        }
    }
}

import Foundation

/// What travels with a photograph into the Trash.
///
/// Pure value logic - the directory listing comes in as strings, so
/// `orion-viewport-tests` can pin the sibling rules without a filesystem.
/// `Library.trash` does the part that needs one.
///
/// The siblings are the three files this repository writes beside a raw, each
/// taken from the type that owns its naming so the rules cannot drift:
/// `PHOTO.xmp` (`Sidecar.url`), `PHOTO.orion-snapshots.json`
/// (`SnapshotStore.url`), and `PHOTO.orion-matte-<id>.png` (`MatteStore`'s
/// prefix, matched against the listing because the ids are unknowable here).
///
/// ⚠ They go to the Trash *with* the photograph, not left behind: an edit
/// without its raw is unusable, an orphaned sidecar beside a re-shot filename
/// is a wrong edit waiting to be restored - and everything in the Trash is
/// recoverable together, which is the property the confirmation promises.
struct TrashPlan: Equatable {
    let photo: URL
    let siblings: [URL]

    /// The files to trash for one photograph, raw first.
    ///
    /// The matte match is on `BASE.orion-matte-` - the dot before the marker
    /// is part of the base name's own spelling, so `IMG_10`'s mattes can
    /// never be swept up with `IMG_1`'s.
    static func plan(for photo: URL, directoryListing: [String]) -> TrashPlan {
        var siblings: [URL] = []
        let folder = photo.deletingLastPathComponent()
        let listed = Set(directoryListing)

        for owned in [Sidecar.url(for: photo), SnapshotStore.url(photo: photo)]
        where listed.contains(owned.lastPathComponent) {
            siblings.append(owned)
        }

        let mattePrefix = "\(photo.deletingPathExtension().lastPathComponent).orion-matte-"
        for name in directoryListing.sorted()
        where name.hasPrefix(mattePrefix) && name.hasSuffix(".png") {
            siblings.append(folder.appendingPathComponent(name))
        }

        return TrashPlan(photo: photo, siblings: siblings)
    }

    /// How a batch of trashings went.
    struct Outcome {
        var trashed: [URL] = []
        /// The failures, folded into one sentence - never a dialog per
        /// frame (the `Library.persist` house rule). Nil when all went.
        var complaint: String?

        /// "Moved 2 of 3 photos to the Trash. IMG_4.arw could not be
        /// moved - <reason>." Nil complaint when nothing failed.
        ///
        /// The moved count is `trashed.count`, not `attempted - failures`:
        /// a failure can be a *sibling* of a photograph that did move, and
        /// the sentence must not report that photograph as still here.
        static func summarize(attempted: Int, trashed: [URL],
                              failures: [(name: String, reason: String)]) -> Outcome {
            guard !failures.isEmpty else {
                return Outcome(trashed: trashed, complaint: nil)
            }
            let moved = trashed.count
            var sentence = "Moved \(moved) of \(attempted) "
                + "photo\(attempted == 1 ? "" : "s") to the Trash."
            for failure in failures {
                sentence += " \(failure.name) could not be moved - \(failure.reason)."
            }
            return Outcome(trashed: trashed, complaint: sentence)
        }
    }
}

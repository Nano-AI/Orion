import Foundation

/// Exporting a list of photographs without holding a folder of them in memory.
///
/// The export path itself was built and tested in M1. What was missing is
/// running it over a list, and the interesting parts of that are not the loop:
/// they are the naming, the settings each photograph is exported *with*, and
/// what happens when one of them fails.
///
/// ## One engine, reused
///
/// `Engine.open` keeps its compiled graph whenever the next frame has the same
/// shape — sixteen shaders and about 2.5 GiB of textures that would otherwise
/// be rebuilt per photograph. So a batch is one engine opening files in turn,
/// not one engine each, and the memory is flat across a folder.
///
/// ⚠ **Each photograph must have its own sidecar restored before it is
/// exported.** The engine carries the previous photo's adjustments until
/// something replaces them, so a loop that only called `open` would export the
/// second frame with the first frame's grade — and it would look plausible,
/// which is the dangerous kind of wrong. `Engine.open` resets to as-shot and
/// `restore` puts the saved edit back; the driver does both, in that order, and
/// the order is the whole of it.
enum BatchExport {

    struct Job: Equatable {
        var source: URL
        var destination: URL
    }

    struct Outcome {
        var written: [URL] = []
        var failed: [(URL, String)] = []
        var canceled = false

        var summary: String {
            if written.isEmpty && failed.isEmpty { return "Nothing to export." }
            var parts: [String] = []
            parts.append(written.count == 1 ? "Exported 1 photo"
                                            : "Exported \(written.count) photos")
            if !failed.isEmpty {
                parts.append(failed.count == 1 ? "1 failed" : "\(failed.count) failed")
            }
            if canceled { parts.append("stopped early") }
            return parts.joined(separator: ", ") + "."
        }
    }

    /// Works out where each photograph is going, before anything is written.
    ///
    /// ⚠ **Nothing is ever overwritten, and two sources never collide.** An
    /// export is the one operation in this program that writes files a
    /// photographer may already have, and a batch is where the two ways of
    /// losing one both live: a target that already exists on disk, and two
    /// sources from different folders sharing a basename. Both get a numbered
    /// suffix — `IMG_0001-2.jpg` — which is what every other application does
    /// and what a photographer will recognize.
    ///
    /// `exists` is injected so the collision rules are testable without a
    /// filesystem, which is most of what makes them testable at all.
    static func plan(sources: [URL], into folder: URL, extension ext: String,
                     exists: (URL) -> Bool = { FileManager.default.fileExists(atPath: $0.path) })
        -> [Job] {

        var taken = Set<String>()
        var jobs: [Job] = []

        for source in sources {
            let stem = source.deletingPathExtension().lastPathComponent
            var candidate = folder.appendingPathComponent(stem)
                .appendingPathExtension(ext)
            var n = 1
            // Both tests, every time: `taken` catches a collision inside this
            // batch, `exists` catches one against what is already on disk.
            while taken.contains(candidate.path) || exists(candidate) {
                n += 1
                candidate = folder.appendingPathComponent("\(stem)-\(n)")
                    .appendingPathExtension(ext)
            }
            taken.insert(candidate.path)
            jobs.append(Job(source: source, destination: candidate))
        }
        return jobs
    }

    /// Runs the plan.
    ///
    /// `openAndRestore` is the step that makes each photograph its own: open it,
    /// then put its saved edit back. Injected so the loop, the error handling
    /// and the cancellation can be tested without a GPU or a RAW file — the
    /// export itself is already covered by the export tests, and what is
    /// untested without this is everything *around* it.
    ///
    /// ⚠ One failure does not stop the batch. A folder is likely to contain a
    /// file the decoder cannot read, and abandoning forty good photographs
    /// because the eleventh was a stray PNG is not what anybody wants. They are
    /// collected and reported together at the end.
    @discardableResult
    static func run(_ jobs: [Job],
                    openAndRestore: (URL) throws -> Void,
                    exportTo: (URL) throws -> Void,
                    progress: (Int, Int) -> Void = { _, _ in },
                    isCanceled: () -> Bool = { false }) -> Outcome {
        var outcome = Outcome()

        for (i, job) in jobs.enumerated() {
            if isCanceled() {
                outcome.canceled = true
                break
            }
            progress(i, jobs.count)
            do {
                try openAndRestore(job.source)
                try exportTo(job.destination)
                outcome.written.append(job.destination)
            } catch {
                outcome.failed.append((job.source, error.localizedDescription))
            }
        }
        progress(jobs.count, jobs.count)
        return outcome
    }
}

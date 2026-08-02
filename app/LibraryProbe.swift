import AppKit
import Foundation

/// `Orion --library-open <folder>` — one folder, opened three ways in one
/// process, and a verdict.
///
/// ## ⚠ Why this exists rather than another unit test
///
/// `orion-viewport-tests` pins every staleness rule in `PhotoIndex`, and would
/// stay green if `Library` never called it. That is the exact shape of failure
/// this repository has recorded six times in `repro/README.md`: a suite that was
/// never in a position to fail. So this drives the product's own `Library.open`
/// and fails when the wiring is not there.
///
/// | Pass | What it proves |
/// |---|---|
/// | **cold** — a database that has never seen the folder | the reference: every field read from the files |
/// | **warm** — the same database, seconds later | ⚠ zero misses, and **every field equal to cold**. A hit that returned different data fails here |
/// | **indexless** — no database at all | decision #9: delete the index and Orion behaves identically, only slower |
///
/// ⚠ **It does not assert that warm is faster.** `STATUS.md` records the same
/// binary measuring 8.97 and 44.53 ms an hour apart on this machine. The two
/// timings are printed, paired, from one process — and the pass/fail is the hit
/// count and the agreement, both of which are load-independent.
enum LibraryProbe {

    nonisolated(unsafe) private static var finished = false
    nonisolated(unsafe) private static var status: Int32 = 0

    static func wanted(_ arguments: [String]) -> URL? {
        guard let i = arguments.firstIndex(of: "--library-open"),
              i + 1 < arguments.count else { return nil }
        return URL(fileURLWithPath: arguments[i + 1], isDirectory: true)
    }

    static func run(_ folder: URL) -> Never {
        // A throwaway database, so "cold" is genuinely cold and the
        // photographer's own index is not disturbed by a measurement.
        let db = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("orion-probe-\(UUID().uuidString).sqlite3")
        defer { try? FileManager.default.removeItem(at: db) }

        Task { @MainActor in
            let index = PhotoIndex(at: db)
            let cold = await measure(Library(index: index), folder)
            let warm = await measure(Library(index: index), folder, resetting: index)
            let none = await measure(Library(index: PhotoIndex(at: nil)), folder)

            print("folder      \(folder.path)")
            print("photographs \(cold.photos.count)")
            print(String(format: "cold        %7.1f ms", cold.ms))
            print(String(format: "warm        %7.1f ms   (%.2fx)", warm.ms,
                         warm.ms > 0 ? cold.ms / warm.ms : 0))
            print(String(format: "indexless   %7.1f ms", none.ms))
            let s = index.stats
            let n = cold.photos.count
            print("warm hits   info \(s.infoHits)/\(n)  marks \(s.markHits)/\(n)  thumb \(s.thumbHits)/\(n)")
            print("warm misses info \(s.infoMisses)  marks \(s.markMisses)  thumb \(s.thumbMisses)")
            print(String(format: "cache       %.1f MB", Double(index.heldBytes) / 1_048_576))

            check(!cold.photos.isEmpty, "the folder has photographs in it")
            // ⚠ Hits, and not merely the absence of misses. The first draft
            // asserted `misses == 0`, which a `Library` that never consulted
            // the index at all satisfies perfectly — zero attempts, zero
            // misses, six green lines and a 4× slower open. It was written to
            // catch exactly that and could not have.
            check(s.infoHits == n, "a warm open takes every raw's fields from the index",
                  "\(s.infoHits) of \(n)")
            check(s.markHits == n, "a warm open takes every rating from the index",
                  "\(s.markHits) of \(n)")
            check(s.thumbHits == n, "a warm open takes every thumbnail from the index",
                  "\(s.thumbHits) of \(n)")
            check(s.infoMisses == 0, "a warm open reads no raw file", "\(s.infoMisses) missed")
            check(s.markMisses == 0, "a warm open reads no sidecar", "\(s.markMisses) missed")
            check(s.thumbMisses == 0, "a warm open decodes no preview", "\(s.thumbMisses) missed")
            agree(cold.photos, warm.photos, "warm")
            agree(cold.photos, none.photos, "indexless")
            finished = true
        }

        while !finished {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.01))
        }
        exit(status)
    }

    // MARK: One pass

    @MainActor
    private static func measure(_ library: Library, _ folder: URL,
                                resetting index: PhotoIndex? = nil) async
        -> (ms: Double, photos: [Library.Photo]) {
        index?.resetStats()
        let start = CFAbsoluteTimeGetCurrent()
        await library.open(folder: folder)
        // ⚠ `open` returns on the listing; the metadata and thumbnails stream in
        // behind it. Timing the listing alone would report the same number
        // whether the index existed or not, which is the measurement this whole
        // exercise is about not making.
        await library.loadTask?.value
        return ((CFAbsoluteTimeGetCurrent() - start) * 1000, library.photos)
    }

    // MARK: Verdicts

    private static func check(_ ok: Bool, _ what: String, _ detail: String = "") {
        print("  \(ok ? "ok  " : "FAIL") \(what)\(detail.isEmpty ? "" : " — \(detail)")")
        if !ok { status = 1 }
    }

    /// Every field a listing shows, compared photograph by photograph.
    ///
    /// ⚠ The load-bearing assertion of the whole feature. A cache that misses is
    /// slow; a cache that hits and answers differently is the bug this is built
    /// to prevent, and it is invisible to anything that only counts hits.
    private static func agree(_ reference: [Library.Photo], _ got: [Library.Photo],
                              _ label: String) {
        guard reference.count == got.count else {
            check(false, "\(label) lists the same photographs",
                  "\(got.count) against \(reference.count)")
            return
        }
        var wrong: [String] = []
        for (a, b) in zip(reference, got) {
            var bad: [String] = []
            if a.url != b.url { bad.append("url") }
            if a.rating != b.rating { bad.append("rating \(b.rating) ≠ \(a.rating)") }
            if a.rejected != b.rejected { bad.append("rejected") }
            if a.colorLabel != b.colorLabel { bad.append("label") }
            if a.width != b.width || a.height != b.height {
                bad.append("size \(b.width)x\(b.height) ≠ \(a.width)x\(a.height)")
            }
            if a.camera != b.camera { bad.append("camera '\(b.camera)' ≠ '\(a.camera)'") }
            if a.captured != b.captured { bad.append("captured") }
            if (a.thumbnail == nil) != (b.thumbnail == nil) { bad.append("thumbnail presence") }
            if let x = a.thumbnail?.size, let y = b.thumbnail?.size, x != y {
                bad.append("thumbnail \(y) ≠ \(x)")
            }
            if !bad.isEmpty { wrong.append("\(a.name): \(bad.joined(separator: ", "))") }
        }
        check(wrong.isEmpty, "\(label) agrees with cold on every field",
              wrong.prefix(3).joined(separator: "; "))
    }
}

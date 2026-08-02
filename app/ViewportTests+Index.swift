// The folder index: staleness, eviction, and what a corrupt database does.
//
// ⚠ Every test here is about the *invalidation*, not the caching. A cache that
// misses is slow and a cache that hits wrongly is a lie — a rating nobody set,
// a thumbnail of a photograph that has since been replaced — and it looks
// perfectly correct on screen. So each case below names the mutation it bites.

import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

extension ViewportTests {

    // MARK: Fixtures

    private static var fm: FileManager { .default }

    /// A throwaway folder with one "raw" file in it. The index never opens a
    /// raw — it only ever stats one — so any bytes will do.
    static func indexFixture(_ label: String, bytes: Int = 2048)
        -> (dir: URL, photo: URL, db: URL) {
        let dir = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("orion-index-\(label)-\(UUID().uuidString)")
        try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
        let photo = dir.appendingPathComponent("_PIC0001.ARW")
        fm.createFile(atPath: photo.path, contents: Data(repeating: 7, count: bytes))
        return (dir, photo, dir.appendingPathComponent("index.sqlite3"))
    }

    /// Forces a file's modification time to an exact nanosecond, so a test can
    /// hold one half of a stamp still while it moves the other.
    static func setMtime(_ url: URL, _ nanoseconds: Int64) {
        var times = [timespec(tv_sec: 0, tv_nsec: Int(UTIME_OMIT)),
                     timespec(tv_sec: Int(nanoseconds / 1_000_000_000),
                              tv_nsec: Int(nanoseconds % 1_000_000_000))]
        _ = url.withUnsafeFileSystemRepresentation { path -> Int32 in
            guard let path else { return -1 }
            return utimensat(AT_FDCWD, path, &times, 0)
        }
    }

    static func indexInfo() -> PhotoIndex.Info {
        PhotoIndex.Info(width: 6024, height: 4024, camera: "ILCE-7RM5",
                        iso: 400, shutter: 1.0 / 250.0, aperture: 2.8,
                        captured: Date(timeIntervalSince1970: 1_700_000_000))
    }

    /// Writes a sidecar and files the whole row, the way a first open does.
    static func indexWarm(_ index: PhotoIndex, _ photo: URL, rating: Int, label: String?) {
        var sidecar = Sidecar()
        sidecar.rating = rating
        sidecar.label = label
        sidecar.write(for: photo)
        let stamp = PhotoIndex.Stamp.of(photo)
        index.record(info: indexInfo(), for: photo, stamp: stamp)
        index.refreshMarks(for: photo)
        index.record(thumbnail: Data(repeating: 9, count: 512), for: photo, stamp: stamp)
    }

    // MARK: The folder a row is filed under

    /// A photograph reached through a symlink is filed under the folder it was
    /// **listed in**, not the folder its bytes live in.
    ///
    /// ⚠ **This retired the entire index in silence.** `dir` was the parent of
    /// the *resolved* file path while `plan` queries `WHERE dir = key(the folder
    /// you opened)`, so a folder of aliases — an imported card, a shoot
    /// symlinked into a working directory — missed on every field of every
    /// photograph, forever. Nothing looked wrong: the listing was right, the
    /// pictures were right, and the only symptom was an open that stayed slow.
    /// `--library-open` read 0/3 info hits, 0/3 marks, **3/3 thumbnails** — the
    /// thumbnails hitting because they are keyed by `path` alone. Decision #115.
    ///
    /// **Mutation:** put `(key as NSString).deletingLastPathComponent` back in
    /// `folderKey` and the last two checks fail.
    static func testARowIsFiledUnderTheFolderItWasListedIn() {
        let (dir, photo, db) = indexFixture("symlinked-file")
        defer { try? fm.removeItem(at: dir) }

        // A second folder holding an alias to the same photograph — the shape
        // `samples/` has, and the shape an imported card has.
        let alias = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("orion-index-alias-\(UUID().uuidString)")
        try? fm.createDirectory(at: alias, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: alias) }
        let linked = alias.appendingPathComponent("_PIC0001.ARW")
        try? fm.createSymbolicLink(at: linked, withDestinationURL: photo)

        // The identity is still the file's own, so one photograph is one row
        // however many ways it is reached.
        report(PhotoIndex.key(linked) == PhotoIndex.key(photo),
               "a symlink and its target are one row",
               "\(PhotoIndex.key(linked)) vs \(PhotoIndex.key(photo))")
        // …and the folder is the one it was listed in.
        report(PhotoIndex.folderKey(linked) == PhotoIndex.key(alias),
               "but it is filed under the folder it was listed in",
               "\(PhotoIndex.folderKey(linked)) vs \(PhotoIndex.key(alias))")
        report(PhotoIndex.folderKey(linked) != PhotoIndex.key(dir),
               "and not under the folder its bytes live in")

        // End to end: file it through the alias, then ask for the alias folder.
        let index = PhotoIndex(at: db)
        index.record(info: indexInfo(), for: linked, stamp: PhotoIndex.Stamp.of(linked))
        guard let row = index.plan(folder: alias, contents: [linked]).first else {
            report(false, "the aliased folder has a row"); return
        }
        report(row.info == indexInfo(),
               "a warm open of the aliased folder hits", String(describing: row.info))
        report(!row.needsInfo, "and does not re-read the raw")
    }

    // MARK: Cold, warm, and across a restart

    /// A fresh index vouches for nothing; a filed one vouches for every field.
    ///
    /// **Mutations:** `record(info:)` writing nothing fails the warm half.
    /// Returning the row without comparing stamps at all still passes here —
    /// which is why it is only the first of eight.
    static func testIndexIsColdBeforeItIsWarm() {
        let (dir, photo, db) = indexFixture("cold")
        defer { try? fm.removeItem(at: dir) }

        let index = PhotoIndex(at: db)
        report(index.available, "a fresh index opens")

        let cold = index.plan(folder: dir, contents: [photo])
        report(cold.count == 1, "the plan covers the listing", "\(cold.count) rows")
        guard let first = cold.first else { return }
        report(first.needsInfo, "a cold index cannot describe the raw")
        report(first.needsMarks, "a cold index cannot describe the sidecar")
        report(index.thumbnail(for: photo, stamp: first.photo) == nil,
               "and it holds no thumbnail")

        indexWarm(index, photo, rating: 3, label: "Blue")

        guard let hot = index.plan(folder: dir, contents: [photo]).first else {
            report(false, "the warm plan has a row"); return
        }
        report(hot.info == indexInfo(), "every field of the raw comes back",
               String(describing: hot.info))
        report(hot.marks?.rating == 3, "the rating comes back",
               "got \(hot.marks?.rating ?? -99)")
        report(hot.marks?.label == "Blue", "the label comes back")
        report(hot.marks?.rejected == false, "the reject flag comes back")
        report(index.thumbnail(for: photo, stamp: hot.photo)?.count == 512,
               "and the thumbnail comes back")

        // ⚠ The database is reopened rather than reused. Everything above
        // passes on an index that only ever lived in memory, and such an index
        // is cold on every launch — which is the entire feature, missing.
        let second = PhotoIndex(at: db)
        guard let again = second.plan(folder: dir, contents: [photo]).first else {
            report(false, "the reopened index has a row"); return
        }
        report(again.info == indexInfo(), "the row survives closing the database")
        report(again.marks?.rating == 3, "and so do the marks")
        report(second.thumbnail(for: photo, stamp: again.photo)?.count == 512,
               "and so does the thumbnail")
    }

    // MARK: Staleness — the raw

    /// A raw rewritten at the same length invalidates its own half and leaves
    /// the sidecar's alone.
    ///
    /// **Mutation:** drop `photo_mtime` from the comparison — the size is
    /// unchanged here, so the row is served stale and four checks fail.
    static func testARewrittenRawInvalidatesByMtime() {
        let (dir, photo, db) = indexFixture("mtime")
        defer { try? fm.removeItem(at: dir) }
        let index = PhotoIndex(at: db)
        indexWarm(index, photo, rating: 4, label: nil)
        let before = PhotoIndex.Stamp.of(photo)

        // Same bytes, same length, one second later.
        try? Data(repeating: 7, count: 2048).write(to: photo)
        setMtime(photo, before.mtime + 1_000_000_000)
        let after = PhotoIndex.Stamp.of(photo)
        report(after.size == before.size, "the fixture kept the file's length",
               "\(after.size) against \(before.size)")
        report(after.mtime != before.mtime, "and moved its mtime")

        guard let plan = index.plan(folder: dir, contents: [photo]).first else {
            report(false, "the plan still has a row"); return
        }
        report(plan.needsInfo, "a rewritten raw invalidates its dimensions")
        report(index.thumbnail(for: photo, stamp: plan.photo) == nil,
               "and its thumbnail — the picture may be a different picture now")
        report(plan.marks?.rating == 4,
               "⚠ and leaves the rating alone; touching the raw did not restar it",
               "got \(plan.marks?.rating ?? -99)")
    }

    /// A raw whose length changed while its mtime was forced back.
    ///
    /// **Mutation:** drop `photo_size` from the comparison. The mtime here is
    /// byte-for-byte the one that was filed, so nothing else can see it.
    static func testARewrittenRawInvalidatesBySizeAlone() {
        let (dir, photo, db) = indexFixture("size")
        defer { try? fm.removeItem(at: dir) }
        let index = PhotoIndex(at: db)
        indexWarm(index, photo, rating: 1, label: nil)
        let before = PhotoIndex.Stamp.of(photo)

        try? Data(repeating: 7, count: 4096).write(to: photo)
        setMtime(photo, before.mtime)
        let after = PhotoIndex.Stamp.of(photo)
        report(after.mtime == before.mtime,
               "the fixture put the mtime back to the nanosecond",
               "\(after.mtime) against \(before.mtime)")
        report(after.size != before.size, "and changed only the length")

        guard let plan = index.plan(folder: dir, contents: [photo]).first else {
            report(false, "the plan still has a row"); return
        }
        report(plan.needsInfo, "a resized raw invalidates its dimensions")
        report(index.thumbnail(for: photo, stamp: plan.photo) == nil,
               "and its thumbnail")
    }

    // MARK: Staleness — the sidecar

    /// ⚠ The one this whole design exists for.
    ///
    /// Pressing 4 on a photograph rated 3 rewrites the sidecar to **the same
    /// length, in the same second**, and does not touch the raw at all. Here
    /// the mtime is forced to the same second as the original so the case is
    /// deterministic rather than dependent on where the clock happened to be.
    ///
    /// **Mutations:** a single stamp taken from the raw serves the old rating
    /// and fails. A `Date`-based mtime at second granularity serves the old
    /// rating and fails. Keying the info or the thumbnail to the sidecar as
    /// well fails the two checks that say they survived.
    static func testARatingChangeInvalidatesTheMarksAndNothingElse() {
        let (dir, photo, db) = indexFixture("rating")
        defer { try? fm.removeItem(at: dir) }
        let index = PhotoIndex(at: db)
        indexWarm(index, photo, rating: 3, label: nil)

        let sidecar = Sidecar.url(for: photo)
        let was = PhotoIndex.Stamp.of(sidecar)

        var changed = Sidecar()
        changed.rating = 4          // one character, so the file is the same length
        changed.write(for: photo)
        let whole = (was.mtime / 1_000_000_000) * 1_000_000_000
        let fraction: Int64 = (was.mtime % 1_000_000_000) == 123_456_789
            ? 987_654_321 : 123_456_789
        setMtime(sidecar, whole + fraction)

        let now = PhotoIndex.Stamp.of(sidecar)
        report(now.size == was.size, "the rewritten sidecar is the same length",
               "\(now.size) against \(was.size)")
        report(now.mtime / 1_000_000_000 == was.mtime / 1_000_000_000,
               "and lands in the same second")
        report(now.mtime != was.mtime, "differing only in nanoseconds")

        guard let plan = index.plan(folder: dir, contents: [photo]).first else {
            report(false, "the plan still has a row"); return
        }
        report(plan.needsMarks,
               "⚠ a same-second, same-length sidecar rewrite still invalidates the rating")
        report(plan.info == indexInfo(),
               "and the raw's fields are untouched — the raw did not change")
        report(index.thumbnail(for: photo, stamp: plan.photo)?.count == 512,
               "and the thumbnail survives a star press")

        // And the re-read puts it right, from the file rather than from memory.
        report(index.refreshMarks(for: photo).rating == 4,
               "re-reading the sidecar reports what the file says")
        report(index.plan(folder: dir, contents: [photo]).first?.marks?.rating == 4,
               "and the row now carries it")
    }

    /// A sidecar that appears, and one that is taken away.
    ///
    /// **Mutation:** treat an absent sidecar as "nothing cached" rather than as
    /// its own stamp. Deleting a sidecar then serves the rating it used to
    /// have, forever.
    static func testADeletedSidecarClearsTheRating() {
        let (dir, photo, db) = indexFixture("nosidecar")
        defer { try? fm.removeItem(at: dir) }
        let index = PhotoIndex(at: db)

        // No sidecar at all: the absence is itself a cacheable fact.
        index.refreshMarks(for: photo)
        index.record(info: indexInfo(), for: photo, stamp: .of(photo))
        guard let bare = index.plan(folder: dir, contents: [photo]).first else {
            report(false, "a photograph with no sidecar still plans"); return
        }
        report(!bare.needsMarks, "the absence of a sidecar is cached, not re-read")
        report(bare.marks?.rating == 0, "and reads as unrated")

        var sidecar = Sidecar(); sidecar.rating = 5; sidecar.write(for: photo)
        report(index.plan(folder: dir, contents: [photo]).first?.needsMarks == true,
               "a sidecar appearing invalidates the cached absence")
        index.refreshMarks(for: photo)
        report(index.plan(folder: dir, contents: [photo]).first?.marks?.rating == 5,
               "and the new rating is filed")

        try? fm.removeItem(at: Sidecar.url(for: photo))
        report(index.plan(folder: dir, contents: [photo]).first?.needsMarks == true,
               "⚠ a sidecar being deleted invalidates the rating it held")
    }

    // MARK: A file that is no longer there

    /// The listing is the filesystem's, and the row goes with the file.
    ///
    /// **Mutations:** have `plan` return the union of the listing and its own
    /// rows — the first check fails, and a deleted photograph reappears in the
    /// strip. Drop the prune — the last check fails, because the row survives
    /// to answer for a file that came back.
    static func testADeletedFileLeavesTheIndex() {
        let (dir, photo, db) = indexFixture("deleted")
        defer { try? fm.removeItem(at: dir) }
        let other = dir.appendingPathComponent("_PIC0002.ARW")
        fm.createFile(atPath: other.path, contents: Data(repeating: 3, count: 1024))

        let index = PhotoIndex(at: db)
        indexWarm(index, photo, rating: 2, label: nil)
        let stamp = PhotoIndex.Stamp.of(photo)
        index.record(info: indexInfo(), for: other, stamp: .of(other))

        let bytes = try? Data(contentsOf: photo)
        try? fm.removeItem(at: photo)

        let plans = index.plan(folder: dir, contents: [other])
        report(plans.count == 1, "the plan lists what the folder holds and nothing else",
               "\(plans.count) rows")
        report(plans.first?.url == other, "and it is the file that is still there")

        // Put it back exactly as it was — same bytes, same mtime to the
        // nanosecond, so its stamp is the one that was filed. It must still
        // miss, because the row went with the file.
        try? bytes?.write(to: photo)
        setMtime(photo, stamp.mtime)
        report(PhotoIndex.Stamp.of(photo) == stamp,
               "the fixture restored the file's exact stamp")
        let back = index.plan(folder: dir, contents: [photo, other])
        report(back.first(where: { $0.url == photo })?.needsInfo == true,
               "⚠ the deleted file's row was pruned, not left to answer for its successor")
        report(index.thumbnail(for: photo, stamp: stamp) == nil,
               "and its thumbnail went with it")
        report(back.first(where: { $0.url == other })?.info == indexInfo(),
               "while the folder's other rows are untouched")
    }

    // MARK: A database that will not do

    /// Junk in the file degrades to a rescan. It does not crash, and it does
    /// not touch anything beside the photograph.
    ///
    /// **Mutation:** let the `sqlite3_prepare_v2` return code go unchecked —
    /// the first query returns rows from a nil statement and the process
    /// traps. Point the discard at the folder rather than at the `.sqlite3`
    /// file and the sidecar check fails.
    static func testACorruptDatabaseDegradesToARescan() {
        for (label, junk) in [("garbage", Data("this is not a database".utf8)),
                              ("truncated", Data(repeating: 0xAB, count: 4096))] {
            let (dir, photo, db) = indexFixture("corrupt-\(label)")
            defer { try? fm.removeItem(at: dir) }
            var sidecar = Sidecar(); sidecar.rating = 5; sidecar.write(for: photo)
            let matte = MatteStore.url(photo: photo, id: "keep")
            fm.createFile(atPath: matte.path, contents: Data([1, 2, 3]))
            try? junk.write(to: db)

            let index = PhotoIndex(at: db)
            let plans = index.plan(folder: dir, contents: [photo])
            report(plans.count == 1, "[\(label)] a corrupt index still plans the folder",
                   "\(plans.count) rows")
            report(plans.first?.needsInfo == true && plans.first?.needsMarks == true,
                   "[\(label)] and vouches for nothing, so the caller reads the files")
            report(index.thumbnail(for: photo, stamp: .of(photo)) == nil,
                   "[\(label)] and hands back no thumbnail")

            report(fm.fileExists(atPath: photo.path), "[\(label)] the photograph survives")
            report(fm.fileExists(atPath: Sidecar.url(for: photo).path),
                   "⚠ [\(label)] the sidecar survives — a broken cache never deletes work")
            report(fm.fileExists(atPath: matte.path), "[\(label)] and so does its matte")
            report(Sidecar.read(for: photo)?.rating == 5,
                   "[\(label)] and the sidecar still says what it said")

            // Writing against a broken index is a no-op, never a crash, and a
            // read after it still misses rather than inventing a row.
            index.record(info: indexInfo(), for: photo, stamp: .of(photo))
            index.refreshMarks(for: photo)
            let after = index.plan(folder: dir, contents: [photo])
            report(after.count == 1, "[\(label)] and it keeps planning afterwards")
            report(after.first?.info == nil || after.first?.info == indexInfo(),
                   "⚠ [\(label)] whatever it recovered to, it never answers with the wrong row")
        }
    }

    /// A database from another schema is dropped and rebuilt, not migrated.
    ///
    /// **Mutation:** keep the `CREATE TABLE IF NOT EXISTS` and drop the version
    /// check — the bogus `photos` table survives, every insert fails against
    /// it, and the index is silently useless rather than rebuilt.
    static func testAForeignSchemaIsRebuilt() {
        let (dir, photo, db) = indexFixture("schema")
        defer { try? fm.removeItem(at: dir) }

        // ⚠ A perfectly valid SQLite file whose `photos` table this build
        // cannot use — which is what a schema change looks like from the far
        // side. A `CREATE TABLE IF NOT EXISTS` finds the table already there
        // and leaves it, and every insert afterwards fails against columns it
        // does not have.
        report(PhotoIndex.executeForTests(at: db, """
            CREATE TABLE photos (whatever TEXT);
            CREATE TABLE thumbnails (whatever TEXT);
            PRAGMA user_version=\(PhotoIndex.schemaVersion + 41);
            """), "the foreign database is written")

        let index = PhotoIndex(at: db)
        report(index.available, "an index at an unknown schema version still opens")
        indexWarm(index, photo, rating: 2, label: nil)
        guard let plan = index.plan(folder: dir, contents: [photo]).first else {
            report(false, "the rebuilt index plans"); return
        }
        report(plan.info == indexInfo(), "and works exactly as a fresh one does")
        report(plan.marks?.rating == 2, "including the marks")
    }

    // MARK: Eviction

    /// Least recently *used*, not least recently written.
    ///
    /// **Mutations:** order the eviction scan by insertion instead of `used` —
    /// A goes and two checks fail. Drop the touch-on-read — same. Evict exactly
    /// to the budget instead of to the low mark — the last check fails, because
    /// the cache sits on the line and rescans on every insert after it.
    static func testTheThumbnailCacheEvictsTheLeastRecentlyUsed() {
        let (dir, _, db) = indexFixture("evict")
        defer { try? fm.removeItem(at: dir) }

        // 4 KB of budget, 1.5 KB a thumbnail: three fit, the fourth does not.
        let index = PhotoIndex(at: db, budgetBytes: 4096)
        nonisolated(unsafe) var now: Int64 = 0
        index.setClock { now }

        var files: [String: URL] = [:]
        func put(_ name: String) {
            let url = dir.appendingPathComponent("\(name).ARW")
            fm.createFile(atPath: url.path, contents: Data(repeating: 1, count: 16))
            files[name] = url
            index.record(thumbnail: Data(repeating: 0, count: 1500),
                         for: url, stamp: .of(url))
        }
        func held(_ name: String) -> Bool {
            guard let url = files[name] else { return false }
            return index.thumbnail(for: url, stamp: .of(url)) != nil
        }

        now = 0;      put("A")
        now = 10_000; put("B")
        report(index.heldBytes == 3000, "two thumbnails are 3000 bytes",
               "\(index.heldBytes)")

        // Reading A makes it the most recent. The gap is over an hour, which is
        // what the coarse touch requires — see `thumbnail(for:stamp:)`.
        now = 20_000
        report(held("A"), "A is still there to be read")

        now = 30_000; put("C")
        report(held("A"), "⚠ A survives: it was read most recently")
        report(held("C"), "C survives: it was written most recently")
        report(!held("B"), "⚠ B is gone: written second, never read")

        // ⚠ The low mark, on entries small enough to tell the two apart. At 4200
        // held against a 4096 budget, stopping at the budget frees one 200-byte
        // row and leaves the cache on the line — so the next insert scans and
        // evicts again, and every one after it. Going to 90% frees three.
        let small = PhotoIndex(at: dir.appendingPathComponent("small.sqlite3"),
                               budgetBytes: 4096)
        small.setClock { now }
        for i in 0..<21 {
            now = Int64(i) * 10_000
            let url = dir.appendingPathComponent("S\(i).ARW")
            fm.createFile(atPath: url.path, contents: Data(repeating: 1, count: 16))
            small.record(thumbnail: Data(repeating: 0, count: 200), for: url, stamp: .of(url))
        }
        report(small.heldBytes <= 4096 * 9 / 10,
               "the cache evicts down to the low mark, not merely under the budget",
               "\(small.heldBytes) bytes, low mark \(4096 * 9 / 10)")
        report(small.heldBytes > 4096 / 2,
               "and not further than that — eviction is not a flush",
               "\(small.heldBytes) bytes")
    }

    // MARK: The window between reading a sidecar and stamping it

    /// A sidecar that changes *while it is being read* is recorded nowhere.
    ///
    /// ⚠ This is the row that would be wrong and look right: the contents of
    /// the old file filed against the identity of the new one, so every later
    /// open is a confident hit reporting a rating nobody set. Stamping before
    /// the read produces it; stat, read, stat again refuses to.
    ///
    /// **Mutation:** record against the stamp taken before the read — the first
    /// two checks fail and the index serves rating 1 for a sidecar that says 5.
    static func testASidecarChangingUnderTheReadIsNotFiled() {
        let (dir, photo, db) = indexFixture("race")
        defer { try? fm.removeItem(at: dir) }
        let index = PhotoIndex(at: db)
        index.record(info: indexInfo(), for: photo, stamp: .of(photo))

        var first = Sidecar(); first.rating = 1; first.write(for: photo)

        // The second writer lands between the read and the re-stat, exactly
        // once, and rewrites the file to a different rating.
        nonisolated(unsafe) var fired = false
        PhotoIndex.marksReadWindow = {
            guard !fired else { return }
            fired = true
            var second = Sidecar(); second.rating = 5; second.write(for: photo)
        }
        let read = index.refreshMarks(for: photo)
        PhotoIndex.marksReadWindow = nil
        report(fired, "the fixture wrote the sidecar during the read")
        report(read.rating == 1, "the read saw the file as it was", "got \(read.rating)")

        guard let plan = index.plan(folder: dir, contents: [photo]).first else {
            report(false, "the folder still plans"); return
        }
        report(plan.needsMarks,
               "⚠ a sidecar that moved under the read is filed nowhere, so the next open re-reads")
        report(Sidecar.read(for: photo)?.rating == 5,
               "and the file itself is what the second writer left")
        report(index.refreshMarks(for: photo).rating == 5,
               "which a quiet re-read then picks up")
    }

    // MARK: Scaling

    /// The stored thumbnail is the picture, the right way up, at the right
    /// shape.
    ///
    /// **Mutation:** drop `kCGImageSourceCreateThumbnailWithTransform` — the
    /// portrait checks fail, and every portrait frame in the strip lies on its
    /// side with nothing crashing and nothing logged. Swap the width and height
    /// of the cap and the landscape checks fail.
    static func testAThumbnailKeepsItsShapeAndItsWayUp() {
        guard let landscape = indexJpeg(800, 500, orientation: nil),
              let shrunk = PhotoIndex.shrink(landscape, longEdge: 512),
              let got = indexSize(shrunk) else {
            report(false, "a landscape preview shrinks at all"); return
        }
        report(got.w == 512, "the long edge is capped", "\(got.w)")
        report(got.h == 320, "and the aspect ratio is kept", "\(got.w)x\(got.h)")
        report(shrunk.count < landscape.count, "and the stored bytes are fewer",
               "\(shrunk.count) against \(landscape.count)")

        // The fixture is bright in its top-left quadrant only, so a flip or a
        // transpose moves the bright corner somewhere this can see.
        if let corners = indexCorners(shrunk) {
            report(corners.topLeft > 0.6, "the bright corner is still top-left",
                   String(format: "%.2f", corners.topLeft))
            report(corners.topRight < 0.4, "and the top-right is still dark",
                   String(format: "%.2f", corners.topRight))
            report(corners.bottomLeft < 0.4, "and the bottom-left — a vertical flip fails here",
                   String(format: "%.2f", corners.bottomLeft))
        } else {
            report(false, "the shrunk thumbnail decodes")
        }

        // ⚠ Orientation 6 means "rotate 90° clockwise to display". An embedded
        // preview from a portrait frame is stored exactly like this, and the
        // re-encode writes no orientation tag at all — so if the rotation is
        // not baked in here it is lost.
        guard let tagged = indexJpeg(800, 500, orientation: 6),
              let turned = PhotoIndex.shrink(tagged, longEdge: 512),
              let size = indexSize(turned), let corners = indexCorners(turned) else {
            report(false, "a tagged preview shrinks at all"); return
        }
        report(size.h > size.w, "⚠ a portrait frame comes out portrait",
               "\(size.w)x\(size.h)")
        report(size.h == 512 && size.w == 320, "at the capped long edge",
               "\(size.w)x\(size.h)")
        report(corners.topRight > 0.6,
               "and the rotation went clockwise — the bright corner moved to the top right",
               String(format: "%.2f", corners.topRight))
        report(corners.topLeft < 0.4, "rather than staying put")
    }

    // MARK: Image fixtures

    /// A JPEG, black with a bright top-left quadrant, optionally tagged.
    static func indexJpeg(_ width: Int, _ height: Int, orientation: Int?) -> Data? {
        guard let ctx = CGContext(data: nil, width: width, height: height,
                                  bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
        else { return nil }
        ctx.setFillColor(CGColor(red: 0, green: 0, blue: 0, alpha: 1))
        ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
        ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 1))
        // A CGContext has a bottom-left origin; this is the image's top-left.
        ctx.fill(CGRect(x: 0, y: height / 2, width: width / 2, height: height / 2))
        guard let image = ctx.makeImage() else { return nil }

        let out = NSMutableData()
        guard let dest = CGImageDestinationCreateWithData(
            out, UTType.jpeg.identifier as CFString, 1, nil) else { return nil }
        var properties: [CFString: Any] = [kCGImageDestinationLossyCompressionQuality: 0.95]
        if let orientation { properties[kCGImagePropertyOrientation] = orientation }
        CGImageDestinationAddImage(dest, image, properties as CFDictionary)
        guard CGImageDestinationFinalize(dest) else { return nil }
        return out as Data
    }

    static func indexSize(_ data: Data) -> (w: Int, h: Int)? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
        return (image.width, image.height)
    }

    /// Luminance at three corners, five pixels in, on a 0…1 scale.
    static func indexCorners(_ data: Data)
        -> (topLeft: Double, topRight: Double, bottomLeft: Double)? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
        let w = image.width, h = image.height
        var pixels = [UInt8](repeating: 0, count: w * h * 4)
        guard let ctx = pixels.withUnsafeMutableBytes({ raw in
            CGContext(data: raw.baseAddress, width: w, height: h, bitsPerComponent: 8,
                      bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(),
                      bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
        }) else { return nil }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
        // ⚠ A bitmap context *draws* from a bottom-left origin and *stores*
        // scanlines top-first, which is why an upright image drawn into one
        // comes back upright. Row 0 is therefore the image's top row — the same
        // convention `indexJpeg` fills against, and it is written down here
        // because getting it backwards makes this fixture assert the exact
        // opposite of what it says.
        func at(_ x: Int, _ yFromTop: Int) -> Double {
            Double(pixels[(yFromTop * w + x) * 4]) / 255.0
        }
        return (at(5, 5), at(w - 6, 5), at(5, h - 6))
    }
}

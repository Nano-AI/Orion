import CoreGraphics
import Foundation
import ImageIO
import SQLite3
import UniformTypeIdentifiers

/// The folder index: a disposable cache of what a listing needs.
///
/// Opening a folder used to cost, per photograph, one LibRaw open for the
/// dimensions and the camera, one embedded-JPEG extraction for the thumbnail,
/// and one sidecar read for the rating — every time, on a folder you had opened
/// five minutes earlier. This remembers all three, keyed by what the files
/// looked like when it read them.
///
/// ## ⚠ It is a cache and nothing lives only here
///
/// Decision #9: XMP sidecars are the source of truth. Delete this database and
/// Orion behaves identically, only slower — the fallback is the code that was
/// already there. Nothing writes a rating *to* the index without writing it to a
/// sidecar first, and nothing reads a rating *from* the index unless the sidecar
/// it came from is byte-for-byte the file it was read from.
///
/// That also settles the tension with decision #79, which refused an
/// Application Support store keyed by a path "that dies the moment the
/// photograph moves". It refused it for **storage**. For a cache a moved
/// photograph is a key that misses, which costs one re-read and loses nothing.
///
/// ## ⚠ Two stamps, not one, because two files answer two questions
///
/// The raw file says how big the picture is and what camera took it. The
/// sidecar says what you thought of it. **Rating a photograph does not touch
/// the raw**, so an index keyed on the raw alone would show the rating it read
/// the first time, forever, and look completely correct doing it. So each row
/// carries both stamps and each half is validated against its own file:
///
/// | Fields | Valid while |
/// |---|---|
/// | width, height, camera, ISO, shutter, aperture, capture date | the raw's mtime and size are unchanged |
/// | rating, rejected, label | the sidecar's mtime and size are unchanged |
/// | thumbnail | the raw's mtime and size are unchanged |
///
/// The thumbnail hangs off the raw and **not** the sidecar deliberately: it is
/// the camera's own embedded preview, which no develop setting has ever
/// affected. Invalidating it on a star press would throw away a perfectly good
/// thumbnail sixty times during a cull.
///
/// A stamp is `(mtime, size)` from one `stat`, and the mtime is whole
/// **nanoseconds** rather than a `Date`. Second granularity leaves an hour-long
/// hole every time a file is rewritten within the same second at the same
/// length — which is exactly what an editor rewriting a sidecar does. The
/// remaining hole is a replacement that lands on the same nanosecond at the same
/// byte count, which no filesystem this runs on can produce by accident.
/// `(0, -1)` means the file is not there, and is distinct from a zero-byte file
/// that is; a NULL stamp means that half was never cached at all.
///
/// ## The thumbnail cache, and why blobs
///
/// Thumbnails live in the same database rather than in a folder of files.
/// Eviction and the payload then move in one transaction, so the cache cannot
/// leak files whose bookkeeping is gone — which this repository has already paid
/// for once, in the orphaned mattes of decision #87. It is also one file to
/// delete when a disposable cache needs disposing, and SQLite reads blobs of
/// this size faster than the filesystem opens files of it (Kennedy, "35% Faster
/// Than The Filesystem", sqlite.org/fasterthanfs.html, 2017; the underlying
/// crossover is Sears, van Ingen & Gray, "To BLOB or Not To BLOB",
/// MSR-TR-2006-45, which puts it near 256 KB).
///
/// **Eviction is LRU against a byte budget**, evicting down to 90% of it so the
/// scan is amortized rather than paid on every insert past the line. LRU because
/// culling is folder-shaped: you work through one shoot and then leave it, so
/// the least recently touched thumbnail is the one from the folder you are done
/// with. LFU would keep last month's favourite folder resident over today's, and
/// insertion order would evict the beginning of the folder you are still in.
/// `used` is only rewritten when it is more than an hour stale, so opening a
/// 500-frame folder is 500 reads and no writes.
///
/// ## ⚠ Corruption degrades, it does not crash and it never deletes a sidecar
///
/// Any SQLite call that reports `SQLITE_CORRUPT` or `SQLITE_NOTADB` — at open or
/// halfway through a query — takes the index out of service for the session:
/// every read misses, every write is dropped, and the caller reads the files.
/// At open, and only for those two codes, the database file is discarded and
/// rebuilt once, because an index that disables itself permanently loses the
/// feature silently on every future launch. `SQLITE_BUSY` and `SQLITE_LOCKED`
/// are deliberately excluded: those mean another process holds the file, and
/// discarding it would be deleting a live database.
///
/// The discard refuses any path that is not named `*.sqlite3`. Nothing in this
/// file opens, writes or unlinks anything beside a photograph.
final class PhotoIndex: @unchecked Sendable {

    /// Bumped when the columns change. A schema change **drops and rebuilds**;
    /// there is no migration, because a migration would imply the data is worth
    /// keeping, and it is not.
    static let schemaVersion: Int32 = 1

    /// Filmstrip cells are ~110pt wide, so 512 covers a 2× display with room to
    /// spare, and it is what the *live* path produces too — a cache that hands
    /// back a different picture from the one it stands in for is not a cache.
    static let thumbnailLongEdge = 512

    // MARK: What a file looked like

    /// One file's identity: modification time in whole nanoseconds and size in
    /// bytes, from a single `stat`.
    struct Stamp: Equatable, Sendable {
        var mtime: Int64 = 0
        var size: Int64 = -1

        /// A file that is not there. Distinct from an empty one, which stamps
        /// `(mtime, 0)`.
        static let absent = Stamp(mtime: 0, size: -1)
        var exists: Bool { size >= 0 }

        static func of(_ url: URL) -> Stamp {
            var st = Darwin.stat()
            // `fstatat(AT_FDCWD, …, 0)` is `stat`, spelled so that Swift cannot
            // mistake the call for the struct of the same name.
            let ok = url.withUnsafeFileSystemRepresentation { path -> Bool in
                guard let path else { return false }
                return fstatat(AT_FDCWD, path, &st, 0) == 0
            }
            guard ok else { return .absent }
            return Stamp(mtime: Int64(st.st_mtimespec.tv_sec) * 1_000_000_000
                                + Int64(st.st_mtimespec.tv_nsec),
                         size: Int64(st.st_size))
        }
    }

    /// What the raw file says. Mirrors `Library.RawSummary` plus the capture
    /// date, which `OrionRawInfo` has always carried and nothing read.
    struct Info: Equatable, Sendable {
        var width: UInt32 = 0
        var height: UInt32 = 0
        var camera: String = ""
        var iso: Float = 0
        var shutter: Float = 0
        var aperture: Float = 0
        var captured: Date?
    }

    /// What the sidecar says.
    struct Marks: Equatable, Sendable {
        var rating: Int = 0
        var rejected: Bool = false
        var label: String?
    }

    /// One photograph's answer for this folder open: the stamps that were just
    /// taken, and whichever halves the index could vouch for.
    struct Plan: Sendable {
        let url: URL
        let photo: Stamp
        let sidecar: Stamp
        let info: Info?
        let marks: Marks?

        var needsInfo: Bool { info == nil }
        var needsMarks: Bool { marks == nil }
    }

    /// Counted so a folder open can be shown to have been warm, rather than
    /// asserted to have been. `LibraryProbe` fails when a second open of the
    /// same folder still misses.
    struct Stats: Equatable, Sendable {
        var infoHits = 0, infoMisses = 0
        var markHits = 0, markMisses = 0
        var thumbHits = 0, thumbMisses = 0
    }

    // MARK: State

    private var db: OpaquePointer?
    private let lock = NSLock()
    private var bytesHeld: Int64 = 0
    private let budget: Int64
    private var counters = Stats()

    /// False once the index has taken itself out of service, and from the start
    /// when it could not be opened at all. Every path checks it.
    private var live = false

    /// Wall clock in seconds, as the LRU sees it. A seam, because eviction
    /// order is the one thing here that cannot be observed without controlling
    /// time — every insert in a test would otherwise share one timestamp and
    /// the policy would be untestable.
    private var clock: @Sendable () -> Int64 = { Int64(Date().timeIntervalSince1970) }
    func setClock(_ now: @escaping @Sendable () -> Int64) {
        lock.lock(); clock = now; lock.unlock()
    }

    var available: Bool { lock.lock(); defer { lock.unlock() }; return live }
    var stats: Stats { lock.lock(); defer { lock.unlock() }; return counters }
    func resetStats() { lock.lock(); counters = Stats(); lock.unlock() }

    /// `~/Library/Application Support/Orion/index.sqlite3`, or nil when there is
    /// no such directory — in which case Orion runs without an index.
    static var defaultURL: URL? {
        guard let base = try? FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask,
            appropriateFor: nil, create: true) else { return nil }
        return base.appendingPathComponent("Orion", isDirectory: true)
                   .appendingPathComponent("index.sqlite3")
    }

    /// - Parameter budgetBytes: the thumbnail cache's ceiling. 512 MB is about
    ///   ten thousand frames at this edge length, which is two or three shoots.
    init(at url: URL?, budgetBytes: Int64 = 512 << 20) {
        budget = budgetBytes
        guard let url else { return }
        try? FileManager.default.createDirectory(
            at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if open(url) { collectMissingFolders(); return }
        // One rebuild, and only for the two codes that mean "this is not a
        // usable database" rather than "somebody else is using it".
        guard discardable, url.pathExtension == "sqlite3" else { return }
        close()
        for suffix in ["", "-wal", "-shm"] {
            try? FileManager.default.removeItem(
                at: url.deletingLastPathComponent()
                       .appendingPathComponent(url.lastPathComponent + suffix))
        }
        _ = open(url)
    }

    deinit { close() }

    private var discardable = false

    private func open(_ url: URL) -> Bool {
        discardable = false
        let flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX
        guard sqlite3_open_v2(url.path, &db, flags, nil) == SQLITE_OK, db != nil else {
            note(sqlite3_errcode(db)); close(); return false
        }
        // WAL so a background write cannot block the listing; NORMAL because a
        // disposable cache does not need an fsync per transaction.
        for pragma in ["PRAGMA journal_mode=WAL", "PRAGMA synchronous=NORMAL",
                       "PRAGMA busy_timeout=2000", "PRAGMA foreign_keys=OFF"] {
            _ = run(pragma)
        }
        guard schemaIsCurrent() else { close(); return false }
        live = true
        bytesHeld = scalar("SELECT COALESCE(SUM(LENGTH(bytes)), 0) FROM thumbnails") ?? 0
        return true
    }

    private func close() {
        if db != nil { sqlite3_close_v2(db); db = nil }
        live = false
    }

    private func schemaIsCurrent() -> Bool {
        let found = Int32(scalar("PRAGMA user_version") ?? -1)
        if found != Self.schemaVersion {
            guard run("DROP TABLE IF EXISTS photos"),
                  run("DROP TABLE IF EXISTS thumbnails") else { return false }
        }
        let ddl = """
            CREATE TABLE IF NOT EXISTS photos (
              path TEXT PRIMARY KEY, dir TEXT NOT NULL,
              photo_mtime INTEGER, photo_size INTEGER,
              sidecar_mtime INTEGER, sidecar_size INTEGER,
              width INTEGER NOT NULL DEFAULT 0, height INTEGER NOT NULL DEFAULT 0,
              camera TEXT NOT NULL DEFAULT '', iso REAL NOT NULL DEFAULT 0,
              shutter REAL NOT NULL DEFAULT 0, aperture REAL NOT NULL DEFAULT 0,
              captured INTEGER,
              rating INTEGER NOT NULL DEFAULT 0,
              rejected INTEGER NOT NULL DEFAULT 0, label TEXT);
            CREATE INDEX IF NOT EXISTS photos_dir ON photos(dir);
            CREATE TABLE IF NOT EXISTS thumbnails (
              path TEXT PRIMARY KEY, dir TEXT NOT NULL,
              photo_mtime INTEGER NOT NULL, photo_size INTEGER NOT NULL,
              used INTEGER NOT NULL, bytes BLOB NOT NULL);
            CREATE INDEX IF NOT EXISTS thumbnails_used ON thumbnails(used);
            """
        guard run(ddl) else { return false }
        return run("PRAGMA user_version=\(Self.schemaVersion)")
    }

    // MARK: The folder open

    /// One query for the whole folder, plus a prune of everything the listing no
    /// longer contains.
    ///
    /// ⚠ The listing is `contents` and only `contents`. The index never adds a
    /// photograph to a folder: a file deleted outside Orion has to vanish, and
    /// a cache that could resurrect one would be the worst kind of stale.
    func plan(folder: URL, contents: [URL]) -> [Plan] {
        let stamps = contents.map { (url: $0,
                                     photo: Stamp.of($0),
                                     sidecar: Stamp.of(Sidecar.url(for: $0))) }
        lock.lock(); defer { lock.unlock() }
        guard live else {
            counters.infoMisses += contents.count
            counters.markMisses += contents.count
            return stamps.map { Plan(url: $0.url, photo: $0.photo, sidecar: $0.sidecar,
                                     info: nil, marks: nil) }
        }

        var rows: [String: (photo: Stamp, sidecar: Stamp, info: Info, marks: Marks)] = [:]
        let dir = Self.key(folder)
        if let stmt = prepare("""
            SELECT path, photo_mtime, photo_size, sidecar_mtime, sidecar_size,
                   width, height, camera, iso, shutter, aperture, captured,
                   rating, rejected, label FROM photos WHERE dir = ?
            """) {
            bind(stmt, 1, dir)
            while step(stmt) == SQLITE_ROW {
                let info = Info(width: UInt32(truncatingIfNeeded: sqlite3_column_int64(stmt, 5)),
                                height: UInt32(truncatingIfNeeded: sqlite3_column_int64(stmt, 6)),
                                camera: text(stmt, 7) ?? "",
                                iso: Float(sqlite3_column_double(stmt, 8)),
                                shutter: Float(sqlite3_column_double(stmt, 9)),
                                aperture: Float(sqlite3_column_double(stmt, 10)),
                                captured: date(stmt, 11))
                let marks = Marks(rating: Int(sqlite3_column_int64(stmt, 12)),
                                  rejected: sqlite3_column_int64(stmt, 13) != 0,
                                  label: text(stmt, 14))
                rows[text(stmt, 0) ?? ""] = (stamp(stmt, 1, 2), stamp(stmt, 3, 4), info, marks)
            }
            sqlite3_finalize(stmt)
        }

        var present = Set<String>()
        var plans: [Plan] = []
        plans.reserveCapacity(stamps.count)
        for s in stamps {
            let key = Self.key(s.url)
            present.insert(key)
            let row = rows[key]
            let info = (row?.photo == s.photo) ? row?.info : nil
            let marks = (row?.sidecar == s.sidecar) ? row?.marks : nil
            info == nil ? (counters.infoMisses += 1) : (counters.infoHits += 1)
            marks == nil ? (counters.markMisses += 1) : (counters.markHits += 1)
            plans.append(Plan(url: s.url, photo: s.photo, sidecar: s.sidecar,
                              info: info, marks: marks))
        }

        // Rows for files that are no longer in the folder. Done in Swift rather
        // than as a `NOT IN (…)` so a 500-frame folder cannot run into the bound
        // parameter limit.
        let gone = Set(rows.keys).subtracting(present)
        if !gone.isEmpty {
            _ = run("BEGIN")
            for path in gone {
                for table in ["photos", "thumbnails"] {
                    if let stmt = prepare("DELETE FROM \(table) WHERE path = ?") {
                        bind(stmt, 1, path); _ = step(stmt); sqlite3_finalize(stmt)
                    }
                }
            }
            _ = run("COMMIT")
            bytesHeld = scalar("SELECT COALESCE(SUM(LENGTH(bytes)), 0) FROM thumbnails") ?? bytesHeld
        }
        return plans
    }

    // MARK: Write-back

    func record(info: Info, for url: URL, stamp: Stamp) {
        lock.lock(); defer { lock.unlock() }
        guard live, let stmt = prepare("""
            INSERT INTO photos (path, dir, photo_mtime, photo_size, width, height,
                                camera, iso, shutter, aperture, captured)
            VALUES (?,?,?,?,?,?,?,?,?,?,?)
            ON CONFLICT(path) DO UPDATE SET
              photo_mtime=excluded.photo_mtime, photo_size=excluded.photo_size,
              width=excluded.width, height=excluded.height, camera=excluded.camera,
              iso=excluded.iso, shutter=excluded.shutter,
              aperture=excluded.aperture, captured=excluded.captured
            """) else { return }
        let key = Self.key(url)
        bind(stmt, 1, key); bind(stmt, 2, Self.folderKey(url))
        sqlite3_bind_int64(stmt, 3, stamp.mtime); sqlite3_bind_int64(stmt, 4, stamp.size)
        sqlite3_bind_int64(stmt, 5, Int64(info.width))
        sqlite3_bind_int64(stmt, 6, Int64(info.height))
        bind(stmt, 7, info.camera)
        sqlite3_bind_double(stmt, 8, Double(info.iso))
        sqlite3_bind_double(stmt, 9, Double(info.shutter))
        sqlite3_bind_double(stmt, 10, Double(info.aperture))
        if let captured = info.captured {
            sqlite3_bind_int64(stmt, 11, Int64(captured.timeIntervalSince1970))
        } else {
            sqlite3_bind_null(stmt, 11)
        }
        _ = step(stmt); sqlite3_finalize(stmt)
    }

    /// ⚠ `stamp` is the **sidecar's**, and `marks` must be what that very file
    /// says. Recording the marks Orion holds in memory against a sidecar it has
    /// not written yet is how a row goes wrong while looking right.
    func record(marks: Marks, for url: URL, stamp: Stamp) {
        lock.lock(); defer { lock.unlock() }
        guard live, let stmt = prepare("""
            INSERT INTO photos (path, dir, sidecar_mtime, sidecar_size,
                                rating, rejected, label)
            VALUES (?,?,?,?,?,?,?)
            ON CONFLICT(path) DO UPDATE SET
              sidecar_mtime=excluded.sidecar_mtime, sidecar_size=excluded.sidecar_size,
              rating=excluded.rating, rejected=excluded.rejected, label=excluded.label
            """) else { return }
        let key = Self.key(url)
        bind(stmt, 1, key); bind(stmt, 2, Self.folderKey(url))
        sqlite3_bind_int64(stmt, 3, stamp.mtime); sqlite3_bind_int64(stmt, 4, stamp.size)
        sqlite3_bind_int64(stmt, 5, Int64(marks.rating))
        sqlite3_bind_int64(stmt, 6, marks.rejected ? 1 : 0)
        if let label = marks.label { bind(stmt, 7, label) } else { sqlite3_bind_null(stmt, 7) }
        _ = step(stmt); sqlite3_finalize(stmt)
    }

    /// Fires between the read and the second stat. ⚠ It exists so the window
    /// in `refreshMarks` can be tested at all: the race is otherwise
    /// unobservable, and an unobservable guard is one nobody can tell has
    /// stopped working. Nothing in the product sets it.
    nonisolated(unsafe) static var marksReadWindow: (@Sendable () -> Void)?

    /// Reads a photograph's sidecar and records what **the file** says.
    ///
    /// The one route by which marks enter the index, so the invariant holds in
    /// one place: nothing is ever recorded that was not read back off disk.
    ///
    /// ⚠ Stat, read, stat again. Stamping before the read would pin the old
    /// contents to whatever identity the file happens to have afterwards — a row
    /// that reports a rating nobody set and passes every check. When the two
    /// stamps disagree the read is returned to the caller and recorded nowhere,
    /// so the next open misses and reads again.
    @discardableResult
    func refreshMarks(for url: URL) -> Marks {
        let sidecarURL = Sidecar.url(for: url)
        let before = Stamp.of(sidecarURL)
        let sidecar = Sidecar.read(for: url)
        Self.marksReadWindow?()
        let after = Stamp.of(sidecarURL)
        // A sidecar that exists and will not parse reads as no marks at all,
        // which is what the loader did before this file existed.
        let marks = Marks(rating: sidecar?.rating ?? 0,
                          rejected: sidecar?.rejected ?? false,
                          label: sidecar?.label)
        if before == after { record(marks: marks, for: url, stamp: after) }
        return marks
    }

    // MARK: Thumbnails

    /// The cached thumbnail for `url`, or nil when the raw has moved on.
    func thumbnail(for url: URL, stamp: Stamp) -> Data? {
        lock.lock(); defer { lock.unlock() }
        guard live, let stmt = prepare(
            "SELECT bytes, used FROM thumbnails WHERE path=? AND photo_mtime=? AND photo_size=?")
        else { counters.thumbMisses += 1; return nil }
        bind(stmt, 1, Self.key(url))
        sqlite3_bind_int64(stmt, 2, stamp.mtime)
        sqlite3_bind_int64(stmt, 3, stamp.size)
        var found: Data?
        var used: Int64 = 0
        if step(stmt) == SQLITE_ROW {
            if let p = sqlite3_column_blob(stmt, 0) {
                found = Data(bytes: p, count: Int(sqlite3_column_bytes(stmt, 0)))
            }
            used = sqlite3_column_int64(stmt, 1)
        }
        sqlite3_finalize(stmt)
        guard let found else { counters.thumbMisses += 1; return nil }
        counters.thumbHits += 1
        // LRU to the hour, so a folder open is reads and not 500 writes.
        let now = clock()
        if now - used > 3600, let touch = prepare("UPDATE thumbnails SET used=? WHERE path=?") {
            sqlite3_bind_int64(touch, 1, now)
            bind(touch, 2, Self.key(url))
            _ = step(touch); sqlite3_finalize(touch)
        }
        return found
    }

    func record(thumbnail bytes: Data, for url: URL, stamp: Stamp) {
        lock.lock(); defer { lock.unlock() }
        guard live, let stmt = prepare("""
            INSERT INTO thumbnails (path, dir, photo_mtime, photo_size, used, bytes)
            VALUES (?,?,?,?,?,?)
            ON CONFLICT(path) DO UPDATE SET
              photo_mtime=excluded.photo_mtime, photo_size=excluded.photo_size,
              used=excluded.used, bytes=excluded.bytes
            """) else { return }
        let key = Self.key(url)
        var previous: Int64 = 0
        if let old = prepare("SELECT LENGTH(bytes) FROM thumbnails WHERE path = ?") {
            bind(old, 1, key)
            if step(old) == SQLITE_ROW { previous = sqlite3_column_int64(old, 0) }
            sqlite3_finalize(old)
        }
        bind(stmt, 1, key); bind(stmt, 2, Self.folderKey(url))
        sqlite3_bind_int64(stmt, 3, stamp.mtime); sqlite3_bind_int64(stmt, 4, stamp.size)
        sqlite3_bind_int64(stmt, 5, clock())
        bytes.withUnsafeBytes { raw in
            _ = sqlite3_bind_blob(stmt, 6, raw.baseAddress, Int32(raw.count), Self.transient)
        }
        let ok = step(stmt) == SQLITE_DONE
        sqlite3_finalize(stmt)
        guard ok else { return }
        bytesHeld += Int64(bytes.count) - previous
        if bytesHeld > budget { evict(to: budget * 9 / 10) }
    }

    /// Least recently used first, in one transaction, down to the low mark.
    private func evict(to target: Int64) {
        guard let stmt = prepare("SELECT path, LENGTH(bytes) FROM thumbnails ORDER BY used ASC")
        else { return }
        var doomed: [String] = []
        var freed: Int64 = 0
        while bytesHeld - freed > target, step(stmt) == SQLITE_ROW {
            doomed.append(text(stmt, 0) ?? "")
            freed += sqlite3_column_int64(stmt, 1)
        }
        sqlite3_finalize(stmt)
        guard !doomed.isEmpty else { return }
        _ = run("BEGIN")
        for path in doomed {
            if let del = prepare("DELETE FROM thumbnails WHERE path = ?") {
                bind(del, 1, path); _ = step(del); sqlite3_finalize(del)
            }
        }
        _ = run("COMMIT")
        bytesHeld = scalar("SELECT COALESCE(SUM(LENGTH(bytes)), 0) FROM thumbnails") ?? 0
    }

    /// Bytes currently held by the thumbnail cache. For the tests and the probe.
    var heldBytes: Int64 { lock.lock(); defer { lock.unlock() }; return bytesHeld }

    // MARK: Scaling

    /// A camera's embedded preview, cut down to what a filmstrip cell shows.
    ///
    /// ⚠ `kCGImageSourceCreateThumbnailWithTransform` is load bearing. An
    /// embedded preview from a portrait frame is stored landscape with an EXIF
    /// orientation tag, and the re-encode below writes no tag at all. Without
    /// the transform the pixels come out unrotated and the tag that said to
    /// rotate them is gone — every portrait frame in the strip lying on its
    /// side, with nothing crashing.
    ///
    /// Pure and static, so the round trip is pinned in `orion-viewport-tests`,
    /// which has no database and no photograph.
    static func shrink(_ data: Data, longEdge: Int = PhotoIndex.thumbnailLongEdge) -> Data? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let scaled = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                  kCGImageSourceCreateThumbnailFromImageAlways: true,
                  kCGImageSourceCreateThumbnailWithTransform: true,
                  kCGImageSourceThumbnailMaxPixelSize: longEdge,
              ] as CFDictionary)
        else { return nil }
        let out = NSMutableData()
        guard let dest = CGImageDestinationCreateWithData(
            out, UTType.jpeg.identifier as CFString, 1, nil) else { return nil }
        CGImageDestinationAddImage(dest, scaled,
                                   [kCGImageDestinationLossyCompressionQuality: 0.8] as CFDictionary)
        guard CGImageDestinationFinalize(dest) else { return nil }
        return out as Data
    }

    /// Runs SQL against a database file directly, so a test can present this
    /// build with one it did not write — a foreign schema, an older version.
    /// Nothing in the product calls it.
    @discardableResult
    static func executeForTests(at url: URL, _ sql: String) -> Bool {
        var handle: OpaquePointer?
        let flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
        guard sqlite3_open_v2(url.path, &handle, flags, nil) == SQLITE_OK else {
            sqlite3_close_v2(handle); return false
        }
        let ok = sqlite3_exec(handle, sql, nil, nil, nil) == SQLITE_OK
        sqlite3_close_v2(handle)
        return ok
    }

    // MARK: SQLite plumbing

    static let transient = unsafeBitCast(-1, to: sqlite3_destructor_type.self)

    /// One key derivation, used for both the row and its folder, so the two
    /// cannot disagree about what a path is.
    static func key(_ url: URL) -> String {
        url.resolvingSymlinksInPath().standardizedFileURL.path
    }

    /// The `dir` column: the folder a photograph was **listed in**, not the
    /// folder its bytes turned out to live in.
    ///
    /// ⚠ **The two are different the moment a photograph is a symlink, and
    /// conflating them silently retired the whole index.** `dir` was the parent
    /// of the *resolved* file path, while `plan` queries `WHERE dir = key(the
    /// folder you opened)` — so for a folder of aliases every `SELECT` returned
    /// nothing, every field missed, and every open re-read every raw and every
    /// sidecar. Nothing looked wrong: the listing was correct, the pictures were
    /// correct, `agree` passed, and the only symptom was that opening a folder
    /// stayed slow forever. `--library-open` is the gate that shows it — **0 of
    /// 3 info hits, 0 of 3 mark hits, 3 of 3 thumbnail hits**, the thumbnails
    /// hitting because they are keyed by `path` alone and never consult `dir`.
    ///
    /// Resolving the *parent* rather than taking the parent of the resolved
    /// path is the whole fix, and it keeps `path` fully resolved so one file
    /// reached two ways is still one row.
    static func folderKey(_ url: URL) -> String {
        key(url.deletingLastPathComponent())
    }

    /// Collects every row whose **folder** no longer exists, once per launch.
    ///
    /// ⚠ `plan` prunes only what it was handed. It runs against the listing for
    /// the folder being opened, so it can only ever clean a folder you are
    /// looking at — and a folder you never open again is never cleaned at all.
    /// The rows are small (~200 B), so this is untidiness rather than a leak
    /// with teeth, but a cache that only grows is a cache that eventually has
    /// to be explained.
    ///
    /// Keyed on the folder, not the file. Checking every *path* would stat
    /// thousands of files at launch to save a kilobyte; checking each distinct
    /// `dir` is one stat per folder the photographer has ever opened.
    ///
    /// ⚠ **An unplugged drive looks exactly like a deleted folder, and this
    /// deliberately does not care.** Nothing lives only here — this is a cache
    /// beside the photographs, and the sidecars are the source of truth (#79).
    /// The cost of collecting a folder that comes back is one re-scan; the cost
    /// of keeping every folder forever is a database that never stops growing.
    /// A photographer with an archive drive pays a cold open the first time
    /// after each reconnect, which is what they paid before the index existed.
    private func collectMissingFolders() {
        guard live else { return }
        var dirs: [String] = []
        if let stmt = prepare("SELECT DISTINCT dir FROM photos") {
            while step(stmt) == SQLITE_ROW {
                if let c = sqlite3_column_text(stmt, 0) {
                    dirs.append(String(cString: c))
                }
            }
            sqlite3_finalize(stmt)
        }

        var isDir: ObjCBool = false
        let gone = dirs.filter {
            !(FileManager.default.fileExists(atPath: $0, isDirectory: &isDir)
              && isDir.boolValue)
        }
        guard !gone.isEmpty else { return }

        _ = run("BEGIN")
        for dir in gone {
            for table in ["photos", "thumbnails"] {
                if let stmt = prepare("DELETE FROM \(table) WHERE dir = ?") {
                    bind(stmt, 1, dir); _ = step(stmt); sqlite3_finalize(stmt)
                }
            }
        }
        _ = run("COMMIT")
        bytesHeld = scalar("SELECT COALESCE(SUM(LENGTH(bytes)), 0) FROM thumbnails") ?? bytesHeld
    }

    /// Takes the index out of service on the two codes that mean the file is
    /// not a database. Everything else — a busy lock, a full disk — is a
    /// transient failure of one statement and leaves the index up.
    private func note(_ code: Int32) {
        guard code == SQLITE_CORRUPT || code == SQLITE_NOTADB else { return }
        discardable = true
        live = false
    }

    private func prepare(_ sql: String) -> OpaquePointer? {
        var stmt: OpaquePointer?
        let rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nil)
        guard rc == SQLITE_OK else { note(rc); sqlite3_finalize(stmt); return nil }
        return stmt
    }

    private func step(_ stmt: OpaquePointer) -> Int32 {
        let rc = sqlite3_step(stmt)
        if rc != SQLITE_ROW && rc != SQLITE_DONE { note(rc) }
        return rc
    }

    @discardableResult
    private func run(_ sql: String) -> Bool {
        let rc = sqlite3_exec(db, sql, nil, nil, nil)
        if rc != SQLITE_OK { note(rc) }
        return rc == SQLITE_OK
    }

    private func scalar(_ sql: String) -> Int64? {
        guard let stmt = prepare(sql) else { return nil }
        defer { sqlite3_finalize(stmt) }
        return step(stmt) == SQLITE_ROW ? sqlite3_column_int64(stmt, 0) : nil
    }

    private func bind(_ stmt: OpaquePointer, _ i: Int32, _ value: String) {
        sqlite3_bind_text(stmt, i, value, -1, Self.transient)
    }

    private func text(_ stmt: OpaquePointer, _ i: Int32) -> String? {
        guard let c = sqlite3_column_text(stmt, i) else { return nil }
        return String(cString: c)
    }

    private func date(_ stmt: OpaquePointer, _ i: Int32) -> Date? {
        guard sqlite3_column_type(stmt, i) != SQLITE_NULL else { return nil }
        return Date(timeIntervalSince1970: Double(sqlite3_column_int64(stmt, i)))
    }

    /// A NULL column pair means that half was never cached, which must not
    /// compare equal to a real stamp — nor to `.absent`, which means the file
    /// was looked for and is not there.
    private func stamp(_ stmt: OpaquePointer, _ m: Int32, _ s: Int32) -> Stamp {
        guard sqlite3_column_type(stmt, m) != SQLITE_NULL,
              sqlite3_column_type(stmt, s) != SQLITE_NULL else {
            return Stamp(mtime: -1, size: -2)
        }
        return Stamp(mtime: sqlite3_column_int64(stmt, m),
                     size: sqlite3_column_int64(stmt, s))
    }
}

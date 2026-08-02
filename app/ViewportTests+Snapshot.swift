// Saved versions: the file, the sweep that must not take their mattes, and the
// working edit a restore has to keep.
//
// See app/Snapshots.swift and decision #99.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// A folder of its own per run, so two suites in flight cannot collide and
    /// nothing is left behind.
    private static func snapshotScratch(_ what: String) -> URL {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-versions-"
                + "\(ProcessInfo.processInfo.processIdentifier)-\(what)")
        try? FileManager.default.removeItem(at: dir)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    /// A component naming a matte file, and the file itself.
    private static func writeMatte(beside photo: URL) -> String {
        let alpha = [Float](repeating: 0.5, count: 4)
        return (try? MatteStore.write(alpha, width: 2, height: 2, photo: photo)) ?? ""
    }

    private static func matteComponent(_ id: String,
                                       _ source: String = "Subject") -> MaskComponentState {
        var c = MaskComponentState()
        c.kind = 4
        c.matteId = id
        c.matteSource = source
        return c
    }

    // MARK: The file

    /// Every field of a state survives being saved as a version and read back
    /// by a *different* store — which is what reopening the photograph is.
    ///
    /// ⚠ `busyState()` and not a hand-made one: it is the fixture that has to
    /// stay exhaustive, and the round trip is only as good as the state it
    /// round-trips. A version carries the whole `DevelopState`, including the
    /// crop, the dust and the masks a preset deliberately refuses to.
    ///
    /// Mutation that turns it red: drop `state` from `Snapshot`'s stored
    /// properties, or give the version file its own hand-written decoder that
    /// forgets a key.
    static func testSnapshotRoundTripsTheWholeState() {
        let dir = snapshotScratch("roundtrip")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")

        let store = SnapshotStore(photo: photo)
        let busy = busyState()
        do {
            try store.save(name: "everything", state: busy)
        } catch {
            report(false, "a version saves at all", error.localizedDescription)
            return
        }

        // A second store, as a reopen builds.
        let reopened = SnapshotStore(photo: photo)
        report(reopened.snapshots.count == 1, "the version survives a reopen",
               "\(reopened.snapshots.count)")
        guard let back = reopened.snapshots.first else { return }
        report(back.name == "everything", "with its name", back.name)
        report(back.state == busy, "and every field of the state")

        // ⚠ And the fields a preset never carries, named one at a time. `==`
        // above covers them, but it covers them in a way that says "something
        // differs" rather than what — and these four are the whole reason a
        // version is not a preset.
        report(back.state.cropW == busy.cropW && back.state.cropX == busy.cropX,
               "the crop travels with a version, unlike a preset")
        report(back.state.spots == busy.spots, "so does the dust")
        report(back.state.maskComponents == busy.maskComponents, "so do the masks")
        report(back.state.layers == busy.layers, "and every layer's local edit")
    }

    /// ⚠ The date strategies at the two ends have to agree.
    ///
    /// They are built in one place for that reason: an encoder writing ISO 8601
    /// against a decoder expecting seconds-since-2001 makes **every** version
    /// file unreadable, which hides the whole feature *and* pins every matte on
    /// the disk forever, because unreadable means "collect nothing".
    ///
    /// Mutation that turns it red: delete `dateEncodingStrategy = .iso8601`.
    static func testSnapshotDatesRoundTrip() {
        let dir = snapshotScratch("dates")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")

        // ⚠ Through the store both ways, never through a pair of encoders this
        // file builds for itself — that would pin ISO 8601 against ISO 8601 and
        // stay green with the store's two ends disagreeing, which is the only
        // failure worth checking here.
        let store = SnapshotStore(photo: photo)
        let before = Date()
        _ = try? store.save(name: "dated", state: DevelopState())
        guard let written = store.snapshots.first else {
            report(false, "a version saves at all")
            return
        }
        report(written.created >= before.addingTimeInterval(-1)
               && written.created <= Date().addingTimeInterval(1),
               "a version is stamped with the moment it was taken")

        let reopened = SnapshotStore(photo: photo)
        report(!reopened.unreadable,
               "the file the encoder wrote is one the decoder can read")
        guard let back = reopened.snapshots.first else {
            report(false, "and the version is still in it")
            return
        }
        near(CGFloat(back.created.timeIntervalSince1970),
             CGFloat(written.created.timeIntervalSince1970), 1.0,
             "with its date intact")

        // And the text is a date a person can read, which is the whole reason
        // ISO 8601 was chosen over a float and the file over base64 in XML.
        let text = (try? String(contentsOf: SnapshotStore.url(photo: photo),
                                encoding: .utf8)) ?? ""
        report(text.contains("T") && text.contains(":")
               && text.contains("\"created\""),
               "written as a date a person can read, not a number",
               String(text.prefix(90)))
    }

    /// The file sits beside the sidecar and the mattes, under the same
    /// basename, so a `PHOTO.*` copy catches all of it.
    ///
    /// Mutation that turns it red: put the file in Application Support.
    static func testSnapshotFileSitsBesideTheSidecar() {
        let photo = URL(fileURLWithPath: "/pictures/2026/_PIC8220.ARW")
        let versions = SnapshotStore.url(photo: photo)
        report(versions.deletingLastPathComponent()
                == photo.deletingLastPathComponent(),
               "the version file is beside the photograph", versions.path)
        report(versions.lastPathComponent == "_PIC8220.orion-snapshots.json",
               "under the photograph's own basename", versions.lastPathComponent)
        report(versions.lastPathComponent
                .hasPrefix(Sidecar.url(for: photo).deletingPathExtension()
                            .lastPathComponent),
               "which is the prefix the sidecar and the mattes share")
    }

    /// ⚠ Absent and unreadable are different, and the difference is whether
    /// anything may be written over it.
    ///
    /// #87's lesson in a second file. A version file Orion merely failed to
    /// parse — a newer build's format, a truncated write — must not be treated
    /// as "no versions" and overwritten with one.
    ///
    /// Mutation that turns it red: make `read` return `[]` when the decode
    /// fails.
    static func testAnUnreadableVersionFileIsNotOverwritten() {
        let dir = snapshotScratch("unreadable")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")
        let path = SnapshotStore.url(photo: photo)

        let garbage = Data("{ this is not the file you are looking for".utf8)
        try? garbage.write(to: path)

        let store = SnapshotStore(photo: photo)
        report(store.unreadable, "an unparseable version file is reported unreadable")
        report(store.snapshots.isEmpty, "and shows nothing rather than guessing")

        var refused: SnapshotStore.Refusal?
        do { try store.save(name: "new", state: DevelopState()) }
        catch let e as SnapshotStore.Refusal { refused = e }
        catch {}
        report(refused != nil, "saving over it is refused")
        report((refused?.errorDescription ?? "").contains("orion-snapshots.json"),
               "and the refusal names the file",
               refused?.errorDescription ?? "no reason given")
        report((try? Data(contentsOf: path)) == garbage,
               "the bytes on disk are untouched")

        // Absent is the other state, and it is writable.
        let fresh = dir.appendingPathComponent("b.ARW")
        let ok = SnapshotStore(photo: fresh)
        report(!ok.unreadable, "a photograph with no version file is not unreadable")
        report((try? ok.save(name: "first", state: DevelopState())) != nil,
               "and can be saved to")
    }

    // MARK: The sweep

    /// ⚠ **The one that matters.** A version names matte files, and the sweep
    /// deletes every matte the *sidecar* does not reference. Without the pin, a
    /// version saved with a Subject mask restores a mask covering nothing the
    /// first time the photograph is opened after the row is deleted — the
    /// picture changes and nothing says why.
    ///
    /// Mutation that turns it red: drop `.union(pinned)` from
    /// `MatteStore.sweepAfterLoad`.
    static func testTheSweepKeepsAMatteAVersionNames() {
        let dir = snapshotScratch("sweep")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")
        FileManager.default.createFile(atPath: Sidecar.url(for: photo).path,
                                       contents: Data("<x/>".utf8))

        let id = writeMatte(beside: photo)
        let file = MatteStore.url(photo: photo, id: id)
        report(FileManager.default.fileExists(atPath: file.path),
               "the matte was written in the first place")

        // Saved into a version, then dropped from the working edit — which is
        // exactly "I took the mask off, and I want yesterday's back".
        var kept = DevelopState()
        kept.maskComponents = [matteComponent(id)]
        let store = SnapshotStore(photo: photo)
        _ = try? store.save(name: "with the subject", state: kept)

        MatteStore.sweepAfterLoad(photo: photo, parsed: [])
        report(FileManager.default.fileExists(atPath: file.path),
               "a matte a saved version names survives the sweep")

        // And it is collected once no version wants it — a pin that never
        // releases is a leak, and this half fails if `sweep` is simply
        // disarmed rather than given a wider keep-set.
        _ = try? store.remove(store.snapshots[0].id)
        MatteStore.sweepAfterLoad(photo: photo, parsed: [])
        report(!FileManager.default.fileExists(atPath: file.path),
               "and is collected once the last version naming it is deleted")
    }

    /// The version file's third state reaches the sweep: unreadable means
    /// **collect nothing**, whatever the sidecar says.
    ///
    /// An empty keep-set there would delete the mattes of every version in a
    /// file Orion only failed to parse — permanent loss of work out of a
    /// recoverable parse failure, which is precisely #87.
    ///
    /// Mutation that turns it red: make `pinnedMattes` return `[]` instead of
    /// `nil` on an unreadable file.
    static func testAnUnreadableVersionFileCollectsNothing() {
        let dir = snapshotScratch("sweep-unreadable")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")
        FileManager.default.createFile(atPath: Sidecar.url(for: photo).path,
                                       contents: Data("<x/>".utf8))

        let id = writeMatte(beside: photo)
        let file = MatteStore.url(photo: photo, id: id)
        try? Data("not json".utf8).write(to: SnapshotStore.url(photo: photo))

        MatteStore.sweepAfterLoad(photo: photo, parsed: [])
        report(FileManager.default.fileExists(atPath: file.path),
               "an unreadable version file collects nothing at all")

        // The same, with no sidecar — the branch that used to collect
        // everything, because "a matte id lives only in a sidecar" stopped
        // being true the day versions existed.
        try? FileManager.default.removeItem(at: Sidecar.url(for: photo))
        MatteStore.sweepAfterLoad(photo: photo, parsed: nil)
        report(FileManager.default.fileExists(atPath: file.path),
               "and still collects nothing when the sidecar is gone too")
    }

    /// With no sidecar at all, a version file is the only thing on disk that
    /// can name a matte — and it is honored.
    ///
    /// Mutation that turns it red: leave the absent-sidecar branch sweeping
    /// with an empty keep-set.
    static func testAVersionPinsAMatteWithNoSidecar() {
        let dir = snapshotScratch("sweep-nosidecar")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")

        let pinned = writeMatte(beside: photo)
        let orphan = writeMatte(beside: photo)

        var kept = DevelopState()
        kept.maskComponents = [matteComponent(pinned)]
        let store = SnapshotStore(photo: photo)
        _ = try? store.save(name: "kept", state: kept)

        MatteStore.sweepAfterLoad(photo: photo, parsed: nil)
        report(FileManager.default.fileExists(
                atPath: MatteStore.url(photo: photo, id: pinned).path),
               "with no sidecar, a version still pins its matte")
        report(!FileManager.default.fileExists(
                atPath: MatteStore.url(photo: photo, id: orphan).path),
               "and the orphan beside it is still collected")
    }

    /// ⚠ A missing matte is reported, not swallowed.
    ///
    /// Pinning stops Orion's own sweep. It cannot stop a photograph being
    /// copied without its siblings, so the panel has to be able to say which
    /// selections a version can no longer find — **before** it is pressed.
    ///
    /// Mutation that turns it red: return `[]` from `missingMattes`.
    static func testAVersionSaysWhichMattesAreMissing() {
        let dir = snapshotScratch("missing")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")

        let present = writeMatte(beside: photo)
        var state = DevelopState()
        state.maskComponents = [matteComponent(present, "Subject"),
                                matteComponent("gone-forever", "Sky")]
        // A non-raster row with no file, which must not be counted.
        var linear = MaskComponentState()
        linear.kind = 1
        state.maskComponents.append(linear)

        let s = Snapshot(name: "v", state: state)
        let missing = SnapshotStore.missingMattes(s, photo: photo)
        report(missing == ["Sky"],
               "only the raster mask whose file is gone is reported",
               missing.joined(separator: ", "))

        // Delete the other one and both are named, in row order.
        try? FileManager.default.removeItem(
            at: MatteStore.url(photo: photo, id: present))
        report(SnapshotStore.missingMattes(s, photo: photo) == ["Subject", "Sky"],
               "and both, once both are gone")

        // A version with no raster mask is never flagged.
        report(SnapshotStore.missingMattes(Snapshot(name: "plain",
                                                    state: DevelopState()),
                                           photo: photo).isEmpty,
               "a version with no raster mask reports nothing")
    }

    // MARK: Restoring

    /// ⚠ Restoring keeps the working edit first, so a restore is reversible
    /// after a quit and not only under ⌘Z.
    ///
    /// Undo is fifty deep, coalesces, and dies with the process; the sidecar is
    /// overwritten 900 ms after the restore lands. Without this an hour's work
    /// is one click from being reachable for one session and gone after it.
    ///
    /// Mutation that turns it red: delete the `keepWorkingEdit` call from
    /// `SnapshotStore.restore`.
    static func testRestoringKeepsTheWorkingEdit() {
        let dir = snapshotScratch("restore")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")

        let store = SnapshotStore(photo: photo)
        var saved = DevelopState()
        saved.exposureEv = 1.5
        _ = try? store.save(name: "bright", state: saved)

        var working = DevelopState()
        working.exposureEv = -2.0
        working.clarity = 0.4

        var assigned: [String] = []
        store.restore(store.snapshots.first { $0.name == "bright" }!,
                      working: working) { assigned.append($0.name) }

        report(assigned == ["bright"], "the version asked for is the one assigned",
               assigned.joined(separator: ", "))
        guard let auto = store.snapshots.first(where: { $0.automatic }) else {
            report(false, "the working edit is kept")
            return
        }
        report(auto.state == working, "and it is kept exactly, not approximately")
        report(auto.name.contains("bright"),
               "named for what it was about to be replaced by", auto.name)

        // ⚠ On disk, not only in memory. A guard that lives in the panel's
        // list until the process ends is not a guard against the process
        // ending.
        let reopened = SnapshotStore(photo: photo)
        report(reopened.snapshots.contains { $0.automatic && $0.state == working },
               "and it is on disk before the restore is handed on")

        // One slot: restoring again replaces it rather than piling up.
        var later = DevelopState()
        later.exposureEv = 3.0
        store.restore(store.snapshots.first { $0.name == "bright" }!,
                      working: later) { _ in }
        report(store.snapshots.filter { $0.automatic }.count == 1,
               "restoring twice leaves one automatic version, not two",
               "\(store.snapshots.filter { $0.automatic }.count)")
        report(store.snapshots.first { $0.automatic }?.state == later,
               "and it holds the most recent working edit")
        report(store.snapshots.contains { $0.name == "bright" },
               "the named version is untouched by any of it")

        // Restoring the state you are already in keeps nothing: pressing a
        // version twice should not fill the list with copies of itself.
        let count = store.snapshots.count
        store.restore(store.snapshots.first { $0.name == "bright" }!,
                      working: saved) { _ in }
        report(store.snapshots.count == count,
               "restoring what you already have keeps no extra version",
               "\(store.snapshots.count) from \(count)")
    }

    /// Renaming the automatic version keeps it — one gesture for "actually, I
    /// want that one", instead of it being replaced by the next restore.
    ///
    /// Mutation that turns it red: leave `automatic` set in `rename`.
    static func testRenamingTheAutomaticVersionKeepsIt() {
        let dir = snapshotScratch("rename")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")

        let store = SnapshotStore(photo: photo)
        _ = try? store.save(name: "target", state: DevelopState())

        var working = DevelopState()
        working.exposureEv = 0.9
        store.restore(store.snapshots[0], working: working) { _ in }
        guard let auto = store.snapshots.first(where: { $0.automatic }) else {
            report(false, "there is an automatic version to rename")
            return
        }

        _ = try? store.rename(auto.id, to: "the good one")
        report(store.snapshots.contains { $0.name == "the good one" && !$0.automatic },
               "renaming the automatic version promotes it")

        var newer = DevelopState()
        newer.exposureEv = -0.4
        store.restore(store.snapshots.first { $0.name == "target" }!,
                      working: newer) { _ in }
        report(store.snapshots.contains { $0.name == "the good one" },
               "so the next restore does not take it back")
        report(store.snapshots.filter { $0.automatic }.count == 1,
               "and there is still exactly one automatic slot")

        // Renaming is persisted, not just displayed.
        report(SnapshotStore(photo: photo).snapshots
                .contains { $0.name == "the good one" },
               "a rename reaches the file")
    }

    /// Saving, replacing by name, deleting, and the ceiling.
    ///
    /// Mutations that turn it red: append instead of replacing in `save`;
    /// evict the oldest instead of throwing at the limit.
    static func testVersionsSaveReplaceAndDelete() {
        let dir = snapshotScratch("crud")
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("a.ARW")
        let store = SnapshotStore(photo: photo)

        var one = DevelopState(); one.exposureEv = 1
        var two = DevelopState(); two.exposureEv = 2
        _ = try? store.save(name: "v", state: one)
        _ = try? store.save(name: "v", state: two)
        report(store.snapshots.count == 1,
               "saving twice under one name leaves one version",
               "\(store.snapshots.count)")
        report(store.snapshots[0].state.exposureEv == 2, "and it is the newer one")

        // An empty name is refused rather than saved as a blank row.
        var refusedName = false
        do { try store.save(name: "   ", state: one) } catch { refusedName = true }
        report(refusedName, "a nameless version is refused")

        _ = try? store.save(name: "w", state: one)
        _ = try? store.remove(store.snapshots.first { $0.name == "v" }!.id)
        report(store.snapshots.map(\.name) == ["w"], "delete removes exactly one",
               store.snapshots.map(\.name).joined(separator: ", "))
        report(SnapshotStore(photo: photo).snapshots.map(\.name) == ["w"],
               "and reaches the file")

        // ⚠ Refused at the ceiling, not evicted. Dropping the oldest to make
        // room is deleting somebody's named work to satisfy a number they have
        // never heard of.
        for i in 0..<(SnapshotStore.limit - 1) {
            _ = try? store.save(name: "f\(i)", state: one)
        }
        report(store.snapshots.count == SnapshotStore.limit,
               "the ceiling is reachable", "\(store.snapshots.count)")
        var refusedFull: SnapshotStore.Refusal?
        do { try store.save(name: "one too many", state: one) }
        catch let e as SnapshotStore.Refusal { refusedFull = e }
        catch {}
        report(refusedFull == .tooMany, "and the next save is refused")
        report(store.snapshots.count == SnapshotStore.limit,
               "with nothing evicted to make room", "\(store.snapshots.count)")
        report((refusedFull?.errorDescription ?? "").contains("\(SnapshotStore.limit)"),
               "and the reason says the number",
               refusedFull?.errorDescription ?? "none")

        // The automatic slot is outside the ceiling, so keeping the working
        // edit can never be the thing that fails.
        store.restore(store.snapshots[0], working: two) { _ in }
        report(store.snapshots.contains { $0.automatic },
               "the automatic version fits even at the ceiling")
    }

    /// A store with no photograph refuses rather than writing somewhere, and
    /// every refusal says something a person can act on.
    ///
    /// Mutation that turns it red: give any `Refusal` case a nil description.
    static func testEveryVersionRefusalGivesAReason() {
        let store = SnapshotStore(photo: nil)
        var refused: SnapshotStore.Refusal?
        do { try store.save(name: "x", state: DevelopState()) }
        catch let e as SnapshotStore.Refusal { refused = e }
        catch {}
        report(refused == .noPhoto, "saving with no photograph open is refused")

        let all: [SnapshotStore.Refusal] = [.noPhoto, .noName, .noSuchSnapshot,
                                            .tooMany, .fileUnreadable("x.json")]
        for r in all {
            let text = r.errorDescription ?? ""
            report(!text.isEmpty && text.count > 12,
                   "\(r) gives a reason worth reading", text)
        }
    }
}

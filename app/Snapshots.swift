import Foundation

/// Named versions of one photograph's edit. Lightroom calls them snapshots.
///
/// ## What this is not
///
/// Not undo history (`EditHistory`): that is fifty entries deep, coalesces per
/// control, branches when you edit after undoing, and dies with the process.
/// Not a preset (`Presets`): that is a **patch** carried *between* photographs,
/// deliberately excluding the crop, the dust and the masks. A snapshot is the
/// whole `DevelopState` of **this** photograph, under a name, kept until it is
/// deleted.
///
/// The three answer three different questions — "take that back", "make this
/// one look like that one", "let me get back to how it was on Tuesday" — and
/// the third had no answer at all.
///
/// ## Where they live, and why not in the sidecar — decision #96
///
/// A sibling `PHOTO.orion-snapshots.json`, beside `PHOTO.xmp` and the
/// `PHOTO.orion-matte-*.png` files.
///
/// The sidecar is the source of truth for the working edit (#9) and is the
/// obvious place to put these too. They are small — a `DevelopState` is a few
/// kilobytes of JSON — so #79's argument against base64 in the XMP, that a
/// raster matte would be megabytes of text per autosave settle, does **not**
/// transfer. Two arguments that do:
///
/// - **Autosave rewrites the sidecar 900 ms after any slider moves**, and
///   `Sidecar.merge` is a read-modify-write through a hand-rolled string
///   matcher rather than an XML parser. Every version kept would be decoded,
///   re-encoded, re-escaped and rewritten on every settle, and the cost of a
///   slider drag would grow with how many versions the photograph has.
/// - **Blast radius.** One bad merge would take the working edit *and* every
///   version with it. A snapshot's whole job is to be the copy that survives
///   when the working state does not; keeping it in the bytes the working state
///   is rewritten into sixty times an hour is not that.
///
/// So: a file written when a version is saved, renamed or deleted, and at no
/// other time. What travels with a photograph is the raw, its sidecar, its
/// mattes and now its versions — one basename, all caught by a `PHOTO.*` copy.
/// Application Support was rejected for the reason #79 gives: a cache keyed by
/// a path dies the moment the photograph is moved, and this is storage.
///
/// Plain JSON rather than base64 inside XML, and dates in ISO 8601, because a
/// photographer who wants to know what is in the file should be able to open
/// it — the same reason a matte is a PNG rather than a bespoke blob.
///
/// ## ⚠ Mattes, and the silent failure this had to solve
///
/// A `DevelopState` is not the whole edit. A raster mask (kind 4) is a sibling
/// PNG named by id (#79), and `MatteStore.sweep` deletes every matte the
/// *sidecar* does not reference. A version naming a swept matte would restore a
/// mask row that covers nothing — the picture changes, the row looks fine, and
/// nothing says why. That is the exact defect class #79 was written against.
///
/// Two halves, and the first is what makes the second small:
///
/// 1. **Matte files are immutable.** Regenerating mints a fresh id and a fresh
///    file rather than overwriting, so an id held in a version always names the
///    same pixels it named when the version was saved. Nothing needs copying —
///    only pinning.
/// 2. **The sweep's keep-set is a union**: what the sidecar references, plus
///    what every version references. That lives in `MatteStore.sweepAfterLoad`,
///    the one function the delete policy is written in, so this cannot drift
///    from the app's loader and the scenario runner's `reopen` separately.
///
/// What pinning cannot cover is a file deleted from outside Orion, or a
/// photograph copied without its siblings. So `missingMattes` reports what a
/// version can no longer find, and the panel says so **before** it is pressed
/// rather than after it has silently emptied a mask.
///
/// ## ⚠ Three states, not two
///
/// The same rule `MatteStore` learned the hard way (#87) applies to this file:
/// **absent** and **present but unreadable** are different. `read` returns `[]`
/// for absent and `nil` for unreadable, and unreadable means both "collect no
/// mattes" and "write nothing over it". Conflating them would delete the mattes
/// of every version in a file Orion merely failed to parse, and then overwrite
/// the file that named them.
struct Snapshot: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    var created = Date()
    /// The whole state. Not a patch — see the class note above.
    var state: DevelopState
    /// Taken by Orion rather than asked for: the working edit, kept on the way
    /// into a restore. There is at most one, and renaming it makes it an
    /// ordinary version.
    var automatic = false
}

@Observable
final class SnapshotStore {

    /// The photograph these belong to. Nil between photographs, and while one
    /// is loading — a version list is per photograph and showing the previous
    /// one's would invite restoring it onto this one.
    private(set) var photo: URL?

    /// Newest first, which is the order a list wants and the order that puts
    /// the automatic version — always the most recent thing written — on top.
    private(set) var snapshots: [Snapshot] = []

    /// The file exists and could not be parsed. Nothing is shown and nothing is
    /// written; see the three-states note.
    private(set) var unreadable = false

    /// A ceiling, so one photograph's version file cannot grow without limit —
    /// a state carrying a long brush stroke is a few hundred kilobytes.
    ///
    /// ⚠ **Refused, not evicted.** Dropping the oldest version to make room for
    /// a new one is deleting somebody's named work to satisfy a number they
    /// have never heard of. A hundred is far past what anyone keeps, and being
    /// told is the only honest thing to do at the edge. The automatic slot does
    /// not count against it, so keeping the working edit can never be the thing
    /// that fails.
    static let limit = 100

    init(photo: URL? = nil) { open(photo: photo) }

    /// A store holding a list that did not come off a disk.
    ///
    /// ⚠ For the screenshot harness and the tests, and it is here rather than
    /// left out because a panel nobody renders is a panel that ships broken —
    /// this project has the receipts. Nothing in the app may use it: a store
    /// built this way has a photograph and a list that disagree with the file,
    /// so the first save would write the harness's fixture over somebody's
    /// versions.
    init(photo: URL?, showing list: [Snapshot]) {
        self.photo = photo
        self.snapshots = list
    }

    // MARK: The file

    /// `PHOTO.orion-snapshots.json`, beside `PHOTO.xmp`.
    static func url(photo: URL) -> URL {
        let base = photo.deletingPathExtension().lastPathComponent
        return photo.deletingLastPathComponent()
            .appendingPathComponent("\(base).orion-snapshots.json")
    }

    /// ⚠ `nil` means **present but unreadable**, which is not `[]`. See the
    /// three-states note on `Snapshot`.
    static func read(photo: URL) -> [Snapshot]? {
        let path = url(photo: photo)
        guard let data = try? Data(contentsOf: path) else {
            // Absent is empty; unopenable-but-there is not.
            return FileManager.default.fileExists(atPath: path.path) ? nil : []
        }
        return try? decoder().decode([Snapshot].self, from: data)
    }

    /// ⚠ The two strategies have to agree. They are built here, together, for
    /// that reason: an encoder writing ISO 8601 against a decoder expecting the
    /// default seconds-since-2001 makes **every** version file unreadable — and
    /// unreadable is the state that hides the whole feature and pins every
    /// matte forever. `testSnapshotDatesRoundTrip` fails if they part.
    private static func encoder() -> JSONEncoder {
        let e = JSONEncoder()
        e.dateEncodingStrategy = .iso8601
        return e
    }

    private static func decoder() -> JSONDecoder {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }

    // MARK: The photograph in hand

    func open(photo: URL?) {
        self.photo = photo
        guard let photo else {
            snapshots = []
            unreadable = false
            return
        }
        if let list = SnapshotStore.read(photo: photo) {
            snapshots = list
            unreadable = false
        } else {
            snapshots = []
            unreadable = true
        }
    }

    // MARK: Saving, renaming, deleting

    /// Saves the state under a name, replacing a version of the same name.
    ///
    /// Replacing rather than appending, exactly as `PresetStore.add` does:
    /// saving "before the crop" twice should leave one entry, which is what
    /// every other application does and what a photographer means. The
    /// automatic version is never the one replaced — a version a person
    /// deliberately named "Before restoring" is theirs, not Orion's slot.
    @discardableResult
    func save(name: String, state: DevelopState) throws -> Snapshot {
        let photo = try writable()
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { throw Refusal.noName }

        let kept = Snapshot(name: trimmed, state: state)
        var next = snapshots
        if let i = next.firstIndex(where: { $0.name == trimmed && !$0.automatic }) {
            next[i] = kept
        } else {
            guard next.filter({ !$0.automatic }).count < SnapshotStore.limit else {
                throw Refusal.tooMany
            }
            next.insert(kept, at: 0)
        }
        try commit(next, to: photo)
        return kept
    }

    /// Renaming an automatic version **keeps** it: the flag is cleared, so the
    /// next restore writes a new automatic slot instead of replacing this one.
    /// That makes "actually, keep that one" a single gesture rather than a
    /// save-then-delete, which is the only way the slot is any use as a safety
    /// net rather than a thing to notice and lose.
    func rename(_ id: UUID, to name: String) throws {
        let photo = try writable()
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { throw Refusal.noName }
        guard let i = snapshots.firstIndex(where: { $0.id == id }) else {
            throw Refusal.noSuchSnapshot
        }
        var next = snapshots
        next[i].name = trimmed
        next[i].automatic = false
        try commit(next, to: photo)
    }

    func remove(_ id: UUID) throws {
        let photo = try writable()
        guard snapshots.contains(where: { $0.id == id }) else {
            throw Refusal.noSuchSnapshot
        }
        try commit(snapshots.filter { $0.id != id }, to: photo)
    }

    // MARK: Restoring

    /// Restores a version, keeping the working edit first.
    ///
    /// ⚠ **The order is the point, and it lives here rather than at each call
    /// site.** Restoring overwrites the working edit, and autosave writes that
    /// over the sidecar 900 ms later — so without this, the only route back is
    /// ⌘Z, which is capped at fifty entries and does not survive a quit. An
    /// hour's work would be reachable for one session and gone after it, which
    /// is a trap rather than a feature. The engine still records a history entry
    /// too, so the same step is one ⌘Z away *within* the session.
    ///
    /// One automatic slot, replaced each time, so restoring six versions in a
    /// row leaves one "before" and not six. Renaming it promotes it.
    ///
    /// The engine half is handed in as a closure because this file compiles
    /// into `orion-viewport-tests`, which has no `Engine`, no facade and no
    /// GPU — the same seam `Autosave` and `BatchExport` use, and what lets the
    /// order above be pinned by a test rather than by reading it.
    func restore(_ snapshot: Snapshot, working: DevelopState,
                 assign: (Snapshot) -> Void) {
        keepWorkingEdit(working, before: snapshot.name)
        assign(snapshot)
    }

    /// The single automatic slot. Skipped when the working edit is already the
    /// version being restored — pressing a version twice should not fill the
    /// list with copies of itself.
    private func keepWorkingEdit(_ state: DevelopState, before name: String) {
        guard let photo, !unreadable else { return }
        guard !snapshots.contains(where: { $0.state == state && !$0.automatic })
        else { return }

        let kept = Snapshot(name: "Before restoring \(name)",
                            state: state, automatic: true)
        var next = snapshots.filter { !$0.automatic }
        next.insert(kept, at: 0)
        // Best effort, and it is the *guard* that is best effort rather than
        // the restore: a version list that cannot be written is reported by the
        // next save, and refusing to restore over it would be punishing the
        // photographer for a disk problem.
        do { try commit(next, to: photo) }
        catch { NSLog("Orion: could not keep the working edit — \(error)") }
    }

    // MARK: Mattes

    /// Every matte id any version of this photograph names — the set the sweep
    /// must add to the sidecar's own.
    ///
    /// ⚠ `nil` when the version file is present and unreadable, and the caller
    /// must then collect **nothing**. Returning an empty set there would delete
    /// the mattes of every version in a file Orion only failed to parse, which
    /// is #87's lesson in a second place.
    static func pinnedMattes(photo: URL) -> Set<String>? {
        guard let list = read(photo: photo) else { return nil }
        return Set(list.flatMap { $0.state.maskComponents }.compactMap { $0.matteId })
    }

    /// The mattes this version names that are no longer beside the photograph,
    /// by the label the panel shows for them.
    ///
    /// ⚠ **Reported, not swallowed.** Pinning stops Orion's own sweep from
    /// taking a version's mattes, but nothing stops a photograph being copied
    /// without its siblings or a file being deleted in the Finder. A version
    /// restored in that state comes back with the mask row present and covering
    /// nothing — a changed picture with nothing on screen saying why, which is
    /// the failure this project has paid for twice.
    static func missingMattes(_ snapshot: Snapshot, photo: URL) -> [String] {
        snapshot.state.maskComponents.compactMap { c -> String? in
            guard c.kind == 4, let id = c.matteId else { return nil }
            let path = MatteStore.url(photo: photo, id: id).path
            guard !FileManager.default.fileExists(atPath: path) else { return nil }
            return c.matteSource ?? "a selection"
        }
    }

    // MARK: Writing

    /// The photograph a write may go to, or the reason it may not.
    private func writable() throws -> URL {
        guard let photo else { throw Refusal.noPhoto }
        guard !unreadable else {
            throw Refusal.fileUnreadable(SnapshotStore.url(photo: photo).lastPathComponent)
        }
        return photo
    }

    /// ⚠ Writes first, publishes second. A list that reached the panel and not
    /// the disk is a version a photographer believes they have.
    private func commit(_ next: [Snapshot], to photo: URL) throws {
        let data = try SnapshotStore.encoder().encode(next)
        try data.write(to: SnapshotStore.url(photo: photo), options: .atomic)
        snapshots = next
    }

    enum Refusal: LocalizedError, Equatable {
        case noPhoto
        case noName
        case noSuchSnapshot
        case tooMany
        case fileUnreadable(String)

        var errorDescription: String? {
            switch self {
            case .noPhoto:
                return "Open a photo before saving a version of it."
            case .noName:
                return "Give the version a name."
            case .noSuchSnapshot:
                return "That version is no longer in the list."
            case .tooMany:
                return "This photo already has \(SnapshotStore.limit) versions. "
                     + "Delete one before saving another."
            case .fileUnreadable(let name):
                return "\(name) could not be read, so nothing will be written "
                     + "over it. Move it aside to start again."
            }
        }
    }
}

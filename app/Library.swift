import AppKit
import Foundation

/// A folder of raw files, with ratings and flags.
///
/// Folder-first by design: no catalog, no import step. Point Orion at a
/// directory and it reads what is there. Ratings and flags live in XMP sidecars
/// next to each file, so they survive Orion being deleted and are readable by
/// other software.
@MainActor
@Observable
final class Library {

    struct Photo: Identifiable, Equatable {
        let url: URL
        var id: URL { url }
        var name: String { url.lastPathComponent }

        var rating: Int = 0        // 0–5 stars
        var rejected: Bool = false
        var colorLabel: String?    // free text, matching XMP convention

        var width: UInt32 = 0
        var height: UInt32 = 0
        var camera: String = ""
        var iso: Float = 0
        var shutter: Float = 0
        var aperture: Float = 0
        /// When the shutter fired, as the file records it. Nil when it does not.
        var captured: Date?

        /// Loaded lazily off the main thread.
        var thumbnail: NSImage?

        mutating func apply(_ info: PhotoIndex.Info) {
            width = info.width; height = info.height; camera = info.camera
            iso = info.iso; shutter = info.shutter; aperture = info.aperture
            captured = info.captured
        }

        mutating func apply(_ marks: PhotoIndex.Marks) {
            rating = marks.rating; rejected = marks.rejected; colorLabel = marks.label
        }
    }

    enum Filter: String, CaseIterable, Identifiable {
        case all, picks, unrated, rejected
        var id: String { rawValue }

        var title: String {
            switch self {
            case .all:      "All"
            case .picks:    "Rated"
            case .unrated:  "Unrated"
            case .rejected: "Rejected"
            }
        }
    }

    private(set) var folder: URL?

    /// Why the last folder could not be listed, or `nil` if it could.
    ///
    /// ⚠ Distinct from `photos.isEmpty`. A refused listing and an empty folder
    /// used to be the same state, so "Open Folder" on an aliased card looked
    /// like a card with nothing on it.
    private(set) var lastFailure: String?
    private(set) var photos: [Photo] = []
    private(set) var loading = false

    /// The folder index — a cache of what a listing needs, and nothing more.
    /// A database that will not open changes how long an open takes and
    /// nothing else, which is the whole of decision #90.
    @ObservationIgnored let index: PhotoIndex

    /// The background pass that fills metadata and thumbnails in behind the
    /// listing. Exposed because `loading` reports the *listing*, and a
    /// measurement of a folder open has to wait for the folder to be whole.
    @ObservationIgnored private(set) var loadTask: Task<Void, Never>?

    init(index: PhotoIndex = PhotoIndex(at: PhotoIndex.defaultURL)) {
        self.index = index
    }

    var filter: Filter = .all {
        // ⚠ A filter change is the one moment a selection can come to contain
        // photographs nobody can see. Confining here rather than at every
        // reader is what keeps `targets` honest.
        didSet { if filter != oldValue { selection.confine(to: visibleURLs) } }
    }

    /// Which photographs a batch acts on. The rules are in `PhotoSelection`;
    /// this class only supplies the list and the filesystem.
    private(set) var selection = PhotoSelection()

    /// Photos passing the current filter, in the order they appear on disk.
    var visible: [Photo] {
        photos.filter { photo in
            switch filter {
            case .all:      true
            case .picks:    photo.rating >= 1 && !photo.rejected
            case .unrated:  photo.rating == 0 && !photo.rejected
            case .rejected: photo.rejected
            }
        }
    }

    var visibleURLs: [URL] { visible.map(\.url) }

    var summary: String {
        let total = photos.count
        let rejected = photos.filter(\.rejected).count
        guard total > 0 else { return "" }
        var parts = ["\(total) photos"]
        if rejected > 0 { parts.append("\(rejected) rejected") }
        // Only once it means something: `PhotoSelection` treats one photo as no
        // selection at all, and a readout saying "1 selected" beside a batch
        // that acts on forty would be worse than no readout.
        let chosen = selection.summary(in: visibleURLs)
        if !chosen.isEmpty { parts.append(chosen) }
        return parts.joined(separator: " · ")
    }

    // MARK: Selection

    /// The photographs a batch operation should act on, in strip order.
    var targets: [URL] { selection.targets(in: visibleURLs) }

    /// The photographs a batch **export** should write, in strip order.
    ///
    /// ⚠ **A rejection is a decision, and writing the file anyway overrides it.**
    /// `targets` follows the filter, so Rated and Unrated already exclude
    /// rejects — but the resting filter is **All**, which does not, so the
    /// ordinary case of "open a folder, cull, edit, export everything" wrote the
    /// rejects into the delivery folder alongside the keepers. Reported by the
    /// developer in exactly those words.
    ///
    /// ⚠ **Two ways to say you meant it, and both are honoured**: select the
    /// photographs by hand, or set the filter to Rejected. Either is an explicit
    /// request for those frames and neither can be arrived at by accident, so
    /// this drops rejects only when it was never asked. Silently exporting
    /// nothing at all when somebody filters to Rejected and presses the button
    /// would be the same defect pointing the other way.
    ///
    /// ⚠ **Export only, deliberately.** `syncSettings` still uses `targets`: a
    /// sidecar is a note about a photograph, not a deliverable, and a reject
    /// that later gets un-rejected should carry the grade its neighbours got.
    /// The two commands look alike from the panel and they are not alike here.
    /// The rule itself is `BatchExport.exportable`, where it can be asserted.
    var exportTargets: [URL] {
        BatchExport.exportable(targets, rejected: rejectedURLs,
                               asked: selection.isExplicit || filter == .rejected)
    }

    private var rejectedURLs: Set<URL> {
        Set(photos.filter(\.rejected).map(\.url))
    }

    /// How many photographs `exportTargets` left behind, so the interface can
    /// say so rather than quietly writing a smaller folder than the count on
    /// the button implied.
    var rejectedInView: Int { targets.count - exportTargets.count }

    /// True when the photographer has actually chosen a set, rather than simply
    /// having one photo open.
    var hasExplicitSelection: Bool { selection.isExplicit }

    /// The canvas moved. Every route into `OrionApp.load` goes through this.
    func focus(_ url: URL?) { selection.focus(url) }

    /// A filmstrip click. Returns the photo to open, or nil when the click only
    /// changed the selection.
    func click(_ url: URL, modifiers: PhotoSelection.Modifiers) -> URL? {
        selection.click(url, modifiers: modifiers, in: visibleURLs)
    }

    /// What a rating or a rejection aimed at `url` should cover: the selection
    /// when `url` is part of a real one, and that photo alone otherwise.
    func cullScope(_ url: URL) -> [URL] {
        hasExplicitSelection && isSelected(url) ? targets : [url]
    }

    func selectAll() { selection.selectAll(in: visibleURLs) }
    func collapseSelection() { selection.collapse() }
    func isSelected(_ url: URL) -> Bool { selection.contains(url) }

    /// What the open panel offers and what a folder scan picks up — one list,
    /// because two drifted: the panel took eight extensions and the scan ten,
    /// so a folder could show a file the Open dialog refused.
    ///
    /// Everything here decodes through LibRaw. Bayer sensors develop; X-Trans
    /// and Foveon files are recognized and refused by name in `decodeBayer`
    /// rather than rendering as garbage, which is why `raf` is on the list at
    /// all — a Fujifilm file should say what is wrong with it.
    nonisolated static let rawExtensions: [String] =
        ["arw", "srf", "sr2",            // Sony
         "dng",                          // Adobe and everything that writes it
         "nef", "nrw",                   // Nikon
         "cr2", "cr3", "crw",            // Canon
         "raf",                          // Fujifilm
         "orf",                          // Olympus / OM
         "rw2",                          // Panasonic
         "pef",                          // Pentax
         "srw",                          // Samsung
         "erf",                          // Epson
         "3fr", "fff",                   // Hasselblad
         "iiq",                          // Phase One
         "mrw",                          // Minolta
         "raw", "rwl",                   // Leica
         "x3f"]                          // Sigma — refused with a reason

    // MARK: Scanning

    /// Lists the folder, then streams metadata and thumbnails in behind it.
    ///
    /// Returns as soon as the *listing* has landed, which is the moment a
    /// caller can pick the first photo. Waiting for the metadata would mean
    /// waiting for every embedded JPEG in the folder to decode. The caller
    /// used to poll `loading` every 30 ms instead; this is the same wait,
    /// stated in the signature.
    ///
    /// The listing now carries whatever the index can vouch for, so a folder
    /// opened before appears rated and captioned rather than filling in behind
    /// itself. ⚠ The *listing itself* is still the filesystem's and only the
    /// filesystem's: `PhotoIndex.plan` is handed the directory contents and can
    /// only answer about files that are in it.
    func open(folder url: URL) async {
        folder = url
        photos = []
        // A selection is a set of URLs and nothing stops those URLs existing in
        // the next folder too. Cleared outright rather than confined, because
        // "the same filename in a different folder" is a photograph nobody
        // picked.
        selection = PhotoSelection()
        loading = true

        // Directory listing off the main thread; the folder may be on a
        // slow volume and the window must stay responsive.
        let index = self.index
        let (found, plans, failure) = await Task.detached(priority: .userInitiated) {
            Self.scan(url, index: index)
        }.value

        photos = found
        // ⚠ A folder that could not be listed is not a folder with no photos in
        // it, and the interface has to be able to tell them apart.
        lastFailure = failure
        loading = false

        loadTask = Task {
            // Metadata and thumbnails stream in afterwards, so a folder of 500
            // frames appears immediately rather than after every file is read.
            //
            // Several at a time, bounded. One at a time — which is what an
            // awaited loop does — trickles a 500-frame folder in at the speed of
            // one decode, on a machine with eight cores idle. Bounded rather
            // than unbounded because each task decodes an embedded JPEG, and
            // five hundred of those at once is a memory spike, not throughput.
            await withTaskGroup(of: (Int, Loaded).self) { group in
                var next = 0
                let width = min(6, plans.count)

                func spawn(_ i: Int) {
                    let plan = plans[i]
                    group.addTask(priority: .utility) { (i, Self.load(plan, index: index)) }
                }

                while next < width { spawn(next); next += 1 }

                while let (i, loaded) = await group.next() {
                    if next < plans.count { spawn(next); next += 1 }
                    apply(loaded, at: i, expecting: plans[i].url)
                }
            }
        }
    }

    /// What one file's background read produced. A nil half means the index
    /// already answered for it and nothing was read.
    private struct Loaded {
        let info: PhotoIndex.Info?
        let marks: PhotoIndex.Marks?
        let thumb: NSImage?
    }

    private func apply(_ loaded: Loaded, at index: Int, expecting url: URL) {
        // The folder may have changed under us while this was loading.
        guard photos.indices.contains(index), photos[index].url == url else { return }

        if let info = loaded.info { photos[index].apply(info) }
        if let marks = loaded.marks { photos[index].apply(marks) }
        photos[index].thumbnail = loaded.thumb
    }

    /// The listing, and the index's answer for it.
    /// ⚠ **The listing's failure comes back rather than reading as an empty
    /// folder.** It was `try? … else { return ([], []) }`, and the two are not
    /// the same thing at all: a folder Orion cannot read looked exactly like a
    /// folder with no raw files in it, and "Open Folder" simply did nothing.
    ///
    /// It is reachable and it was reached. `contentsOfDirectory(at:)` fails
    /// with POSIX 20 `ENOTDIR` when the URL is a **symlink to a directory** —
    /// an aliased card, a NAS mount, a `ln -s`'d shoot folder — while the
    /// `atPath:` spelling of the same call succeeds on the same path. The
    /// directory is resolved before the call for that reason; `MatteStore.sweep`
    /// carried the identical bug and swept nothing, ever, on the same folders.
    /// ⚠ Not `private`, so `orion-viewport-tests` can drive the listing without
    /// a window. The invariant it pins — a symlinked folder is listed rather
    /// than read as empty — has no other route to a check.
    nonisolated static func scan(_ url: URL, index: PhotoIndex)
        -> (photos: [Photo], plans: [PhotoIndex.Plan], failure: String?) {
        let keys: [URLResourceKey] = [.isRegularFileKey]
        let items: [URL]
        do {
            items = try FileManager.default.contentsOfDirectory(
                at: url.resolvingSymlinksInPath(), includingPropertiesForKeys: keys,
                options: [.skipsHiddenFiles, .skipsSubdirectoryDescendants])
        } catch {
            let why = "Orion could not read \(url.lastPathComponent) — "
                    + error.localizedDescription
            FileHandle.standardError.write(Data("orion: \(why)\n".utf8))
            return ([], [], why)
        }

        let files = items
            .filter { rawExtensions.contains($0.pathExtension.lowercased()) }
            .sorted { $0.lastPathComponent.localizedStandardCompare(
                        $1.lastPathComponent) == .orderedAscending }

        let plans = index.plan(folder: url, contents: files)
        let photos = plans.map { plan -> Photo in
            var photo = Photo(url: plan.url)
            if let info = plan.info { photo.apply(info) }
            if let marks = plan.marks { photo.apply(marks) }
            return photo
        }
        return (photos, plans, nil)
    }

    /// Reads only the halves the index could not vouch for, and records what it
    /// reads. ⚠ Everything recorded here is stamped with the identity taken by
    /// `plan` **before** the read: a file that changes mid-read therefore keys
    /// its row to a stamp that no longer matches, and the next open misses.
    /// Stamping afterwards would pin the new file's identity to the old file's
    /// contents, which is a row that is wrong and looks right.
    private nonisolated static func load(_ plan: PhotoIndex.Plan,
                                         index: PhotoIndex) -> Loaded {
        var info: PhotoIndex.Info?
        if plan.needsInfo, let read = readInfo(plan.url) {
            info = read
            index.record(info: read, for: plan.url, stamp: plan.photo)
        }
        let marks = plan.needsMarks ? index.refreshMarks(for: plan.url) : nil
        return Loaded(info: info, marks: marks, thumb: thumbnail(plan, index: index))
    }

    /// ⚠ The cached thumbnail and the freshly read one are the **same picture**:
    /// both go through `PhotoIndex.shrink`. A cache that hands back a different
    /// image from the one it stands in for is not a cache, and the reduction is
    /// wanted anyway — the strip used to hold five hundred full-size embedded
    /// previews in memory to draw them a hundred points wide.
    private nonisolated static func thumbnail(_ plan: PhotoIndex.Plan,
                                              index: PhotoIndex) -> NSImage? {
        if let cached = index.thumbnail(for: plan.url, stamp: plan.photo),
           let image = NSImage(data: cached) {
            return image
        }
        guard let embedded = readThumbnail(plan.url),
              let small = PhotoIndex.shrink(embedded) else { return nil }
        index.record(thumbnail: small, for: plan.url, stamp: plan.photo)
        return NSImage(data: small)
    }

    private nonisolated static func readInfo(_ url: URL) -> PhotoIndex.Info? {
        var info = OrionRawInfo()
        guard orion_read_info(url.path, &info) == ORION_OK else { return nil }
        let camera = withUnsafeBytes(of: info.camera) { raw in
            String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
        }
        return PhotoIndex.Info(
            width: info.width, height: info.height, camera: camera,
            iso: info.iso, shutter: info.shutter, aperture: info.aperture,
            captured: info.timestamp > 0
                ? Date(timeIntervalSince1970: Double(info.timestamp)) : nil)
    }

    /// Uses the camera's embedded JPEG preview. Decoding the mosaic for a
    /// thumbnail would take ~50ms per frame; this takes about two.
    private nonisolated static func readThumbnail(_ url: URL) -> Data? {
        var size: UInt32 = 0
        guard orion_read_thumbnail(url.path, nil, 0, &size) == ORION_OK, size > 0 else {
            return nil
        }
        var buffer = [UInt8](repeating: 0, count: Int(size))
        guard orion_read_thumbnail(url.path, &buffer, size, &size) == ORION_OK else {
            return nil
        }
        return Data(buffer)
    }

    // MARK: Ratings

    func setRating(_ rating: Int, for url: URL) {
        guard let i = photos.firstIndex(where: { $0.url == url }) else { return }
        photos[i].rating = max(0, min(5, rating))
        photos[i].rejected = false
        persist(photos[i])
    }

    func toggleRejected(_ url: URL) {
        guard let i = photos.firstIndex(where: { $0.url == url }) else { return }
        photos[i].rejected.toggle()
        if photos[i].rejected { photos[i].rating = 0 }
        persist(photos[i])
    }

    /// Rating and rejection over a set of photographs.
    ///
    /// ⚠ Rejection is *set*, not toggled, across a group. Toggling each one
    /// individually would flip a mixed selection into its own negative — half
    /// the frames rejected, the other half un-rejected — which is never what
    /// anybody means by pressing Reject on a group. The clicked photo decides
    /// the direction and the rest follow it.
    func setRating(_ rating: Int, for urls: [URL]) {
        for url in urls { setRating(rating, for: url) }
    }

    func setRejected(_ rejected: Bool, for urls: [URL]) {
        for url in urls {
            guard let i = photos.firstIndex(where: { $0.url == url }) else { continue }
            photos[i].rejected = rejected
            if rejected { photos[i].rating = 0 }
            persist(photos[i])
        }
    }

    private func persist(_ photo: Photo) {
        // Merged, not rebuilt: the develop settings live in the same file and
        // are none of this function's business.
        //
        // ⚠ **The answer is kept.** The row below is refreshed from the file,
        // so the *index* was already honest — but the in-memory `Photo` keeps
        // the star and the flag whatever happened, and that is what the
        // filmstrip draws. On a locked card an entire cull therefore read back
        // as done: two hundred frames rejected on screen, nothing in any
        // sidecar, and the only trace in a log. One sentence, not a dialog per
        // frame — a cull is a hundred keystrokes and a modal on each is its own
        // defect.
        if !Sidecar.merge(into: photo.url, {
            $0.rating = photo.rating
            $0.rejected = photo.rejected
            $0.label = photo.colorLabel
        }) {
            lastFailure = "Ratings could not be saved for "
                        + "\(photo.url.lastPathComponent). The card or folder "
                        + "may be read-only."
        }
        // ⚠ The index is refreshed by **reading the file back**, never from the
        // values above. A merge that failed — a read-only card, a full disk —
        // would otherwise leave the index holding a rating that is not in any
        // sidecar, which is precisely the stale row that looks correct. Without
        // this the row is merely stale and the next open re-reads it; with the
        // in-memory shortcut it would be wrong for as long as the file lived.
        index.refreshMarks(for: photo.url)
    }

    func index(of url: URL) -> Int? { visible.firstIndex { $0.url == url } }

    /// Next or previous photo passing the filter, for arrow-key navigation.
    func neighbor(of url: URL, offset: Int) -> URL? {
        let list = visible
        guard let i = list.firstIndex(where: { $0.url == url }) else {
            return list.first?.url
        }
        let next = i + offset
        guard list.indices.contains(next) else { return nil }
        return list[next].url
    }
}

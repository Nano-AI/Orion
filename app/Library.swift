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

        /// Loaded lazily off the main thread.
        var thumbnail: NSImage?
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
    private(set) var photos: [Photo] = []
    private(set) var loading = false

    var filter: Filter = .all

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

    var summary: String {
        let total = photos.count
        let rejected = photos.filter(\.rejected).count
        guard total > 0 else { return "" }
        return rejected > 0
            ? "\(total) photos · \(rejected) rejected"
            : "\(total) photos"
    }

    /// What the open panel offers and what a folder scan picks up — one list,
    /// because two drifted: the panel took eight extensions and the scan ten,
    /// so a folder could show a file the Open dialog refused.
    ///
    /// Everything here decodes through LibRaw. Bayer sensors develop; X-Trans
    /// and Foveon files are recognised and refused by name in `decodeBayer`
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
    func open(folder url: URL) async {
        folder = url
        photos = []
        loading = true

        // Directory listing off the main thread; the folder may be on a
        // slow volume and the window must stay responsive.
        let found = await Task.detached(priority: .userInitiated) {
            Self.scan(url)
        }.value

        photos = found
        loading = false

        Task {
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
                let width = min(6, found.count)

                func spawn(_ index: Int) {
                    let url = found[index].url
                    group.addTask(priority: .utility) {
                        (index, Loaded(info: Self.readInfo(url),
                                       thumb: Self.readThumbnail(url),
                                       sidecar: Sidecar.read(for: url)))
                    }
                }

                while next < width { spawn(next); next += 1 }

                while let (index, loaded) = await group.next() {
                    if next < found.count { spawn(next); next += 1 }
                    apply(loaded, at: index, expecting: found[index].url)
                }
            }
        }
    }

    /// What one file's background read produced.
    private struct Loaded {
        let info: RawSummary?
        let thumb: NSImage?
        let sidecar: Sidecar?
    }

    private func apply(_ loaded: Loaded, at index: Int, expecting url: URL) {
        // The folder may have changed under us while this was loading.
        guard photos.indices.contains(index), photos[index].url == url else { return }

        if let info = loaded.info {
            photos[index].width = info.width
            photos[index].height = info.height
            photos[index].camera = info.camera
            photos[index].iso = info.iso
            photos[index].shutter = info.shutter
            photos[index].aperture = info.aperture
        }
        photos[index].thumbnail = loaded.thumb
        if let sidecar = loaded.sidecar {
            photos[index].rating = sidecar.rating
            photos[index].rejected = sidecar.rejected
            photos[index].colorLabel = sidecar.label
        }
    }

    private nonisolated static func scan(_ url: URL) -> [Photo] {
        let keys: [URLResourceKey] = [.isRegularFileKey]
        guard let items = try? FileManager.default.contentsOfDirectory(
            at: url, includingPropertiesForKeys: keys,
            options: [.skipsHiddenFiles, .skipsSubdirectoryDescendants])
        else { return [] }

        return items
            .filter { rawExtensions.contains($0.pathExtension.lowercased()) }
            .sorted { $0.lastPathComponent.localizedStandardCompare(
                        $1.lastPathComponent) == .orderedAscending }
            .map { Photo(url: $0) }
    }

    typealias RawSummary = (width: UInt32, height: UInt32, camera: String,
                            iso: Float, shutter: Float, aperture: Float)

    private nonisolated static func readInfo(_ url: URL) -> RawSummary? {
        var info = OrionRawInfo()
        guard orion_read_info(url.path, &info) == ORION_OK else { return nil }
        let camera = withUnsafeBytes(of: info.camera) { raw in
            String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
        }
        return (info.width, info.height, camera, info.iso, info.shutter, info.aperture)
    }

    /// Uses the camera's embedded JPEG preview. Decoding the mosaic for a
    /// thumbnail would take ~50ms per frame; this takes about two.
    private nonisolated static func readThumbnail(_ url: URL) -> NSImage? {
        var size: UInt32 = 0
        guard orion_read_thumbnail(url.path, nil, 0, &size) == ORION_OK, size > 0 else {
            return nil
        }
        var buffer = [UInt8](repeating: 0, count: Int(size))
        guard orion_read_thumbnail(url.path, &buffer, size, &size) == ORION_OK else {
            return nil
        }
        return NSImage(data: Data(buffer))
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

    private func persist(_ photo: Photo) {
        // Merged, not rebuilt: the develop settings live in the same file and
        // are none of this function's business.
        Sidecar.merge(into: photo.url) {
            $0.rating = photo.rating
            $0.rejected = photo.rejected
            $0.label = photo.colorLabel
        }
    }

    func index(of url: URL) -> Int? { visible.firstIndex { $0.url == url } }

    /// Next or previous photo passing the filter, for arrow-key navigation.
    func neighbour(of url: URL, offset: Int) -> URL? {
        let list = visible
        guard let i = list.firstIndex(where: { $0.url == url }) else {
            return list.first?.url
        }
        let next = i + offset
        guard list.indices.contains(next) else { return nil }
        return list[next].url
    }
}

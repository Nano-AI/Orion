import AppKit
import Foundation

/// A folder of raw files, with ratings and flags.
///
/// Folder-first by design: no catalogue, no import step. Point Orion at a
/// directory and it reads what is there. Ratings and flags live in XMP sidecars
/// next to each file, so they survive Orion being deleted and are readable by
/// other software.
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
    var minimumRating = 0

    /// Photos passing the current filter, in the order they appear on disk.
    var visible: [Photo] {
        photos.filter { photo in
            switch filter {
            case .all:      !photo.rejected || minimumRating == 0
            case .picks:    photo.rating >= max(minimumRating, 1) && !photo.rejected
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

    private static let rawExtensions: Set<String> =
        ["arw", "dng", "nef", "cr2", "cr3", "raf", "orf", "rw2", "pef", "srw"]

    // MARK: Scanning

    func open(folder url: URL) {
        folder = url
        photos = []
        loading = true

        Task.detached(priority: .userInitiated) { [weak self] in
            let found = Self.scan(url)

            await MainActor.run {
                self?.photos = found
                self?.loading = false
            }

            // Metadata and thumbnails stream in afterwards, so a folder of 500
            // frames appears immediately rather than after every file is read.
            for (index, photo) in found.enumerated() {
                let info = Self.readInfo(photo.url)
                let thumb = Self.readThumbnail(photo.url)
                let sidecar = Sidecar.read(for: photo.url)

                await MainActor.run {
                    guard let self, self.photos.indices.contains(index),
                          self.photos[index].url == photo.url else { return }
                    if let info {
                        self.photos[index].width = info.width
                        self.photos[index].height = info.height
                        self.photos[index].camera = info.camera
                        self.photos[index].iso = info.iso
                        self.photos[index].shutter = info.shutter
                        self.photos[index].aperture = info.aperture
                    }
                    self.photos[index].thumbnail = thumb
                    if let sidecar {
                        self.photos[index].rating = sidecar.rating
                        self.photos[index].rejected = sidecar.rejected
                        self.photos[index].colorLabel = sidecar.label
                    }
                }
            }
        }
    }

    private static func scan(_ url: URL) -> [Photo] {
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

    private static func readInfo(_ url: URL)
        -> (width: UInt32, height: UInt32, camera: String,
            iso: Float, shutter: Float, aperture: Float)? {
        var info = OrionRawInfo()
        guard orion_read_info(url.path, &info) == ORION_OK else { return nil }
        let camera = withUnsafeBytes(of: info.camera) { raw in
            String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
        }
        return (info.width, info.height, camera, info.iso, info.shutter, info.aperture)
    }

    /// Uses the camera's embedded JPEG preview. Decoding the mosaic for a
    /// thumbnail would take ~50ms per frame; this takes about two.
    private static func readThumbnail(_ url: URL) -> NSImage? {
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
        Sidecar(rating: photo.rating, rejected: photo.rejected,
                label: photo.colorLabel).write(for: photo.url)
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

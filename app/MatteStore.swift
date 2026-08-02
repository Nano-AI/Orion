import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

/// Saving a raster matte with the photograph.
///
/// Every other mask kind is a handful of numbers and lives in the sidecar's
/// `orion:develop` blob. Kind 4 is a *raster* — 1024 × 683 alpha values, produced
/// by Vision or by the sky detector — and until this file existed it was never
/// written anywhere. Reopening a photograph left the Subject, Person or Sky row
/// present and empty, and the only route back was to run the model again.
///
/// ⚠ **A regenerated matte is not the same matte.** Vision's models move between
/// macOS releases, so "just re-run it on open" would silently change a finished
/// edit on an OS update. That is why this is storage and not a cache, and why
/// the files sit beside the photograph rather than in Application Support keyed
/// by a path that changes the moment the photograph is moved.
///
/// ## The file
///
/// One sibling PNG per matte, `PHOTO.orion-matte-<uuid>.png`, next to
/// `PHOTO.xmp`. So what travels with a photograph is the raw, its sidecar, and
/// its mattes — all sharing the basename, all caught by a `PHOTO.*` copy.
///
/// **16-bit grayscale, and the color space is stated rather than left to
/// ImageIO.** PNG carries grayscale samples big-endian (ISO/IEC 15948:2004 §7.1)
/// and CoreGraphics will happily color-manage them: write 0.5 through a
/// Gamma-2.2 gray space and read it back as linear and every feathered edge on
/// every reopened photograph shifts, with nothing to see and nothing to crash.
/// Both ends name `CGColorSpace.linearGray`, so the conversion is the identity,
/// and `MatteStore.roundTrip` in `orion-viewport-tests` fails if that ever stops
/// being true.
///
/// 16 bits rather than 8 because the argument for 8 is "the guided filter
/// probably averages the staircase away", and probably is not a thing to ship a
/// mask on. The cost is about 1.5× on data that compresses well regardless.
///
/// ## The lifecycle
///
/// A matte file is **immutable**: regenerating mints a new id and a new file, so
/// a half-written file can never overwrite the only good copy. The write happens
/// when the model returns, **before** the id reaches the component — the sidecar
/// can therefore never name a file that is not already on disk, and the only
/// reachable crash window leaves an orphan of a few hundred KB. Orphans are
/// swept when the photograph is opened, and only after its sidecar has parsed:
/// sweeping against a failed parse would read as "no components" and delete
/// every matte the photograph has.
///
/// ⚠ Nothing here knows about `Engine`. That is what lets `orion-viewport-tests`
/// — which has no GPU, no facade and no photograph — pin the round trip that
/// every other guarantee in this file depends on. The upload half lives on
/// `Engine.restoreMattes`.
enum MatteStore {

    /// PNG bytes for an alpha raster. Pure, so the round trip can be pinned
    /// without touching a disk.
    ///
    /// Values are clamped to 0…1 on the way in. The matte kernel saturates
    /// anyway, so a producer handing over 1.2 means "covered" — writing that as
    /// wraparound would be a hole in the mask.
    static func encode(_ alpha: [Float], width: Int, height: Int) -> Data? {
        guard width > 0, height > 0, alpha.count >= width * height else { return nil }

        var samples = [UInt16](repeating: 0, count: width * height)
        for i in 0..<(width * height) {
            let v = min(max(alpha[i], 0), 1)
            samples[i] = UInt16((v * 65535).rounded())
        }

        guard let space = CGColorSpace(name: CGColorSpace.linearGray),
              let provider = CGDataProvider(data: Data(bytes: samples,
                                                       count: samples.count * 2) as CFData),
              let image = CGImage(width: width, height: height,
                                  bitsPerComponent: 16, bitsPerPixel: 16,
                                  bytesPerRow: width * 2,
                                  space: space,
                                  // Native order in, big-endian in the file:
                                  // the PNG encoder does that swap itself.
                                  bitmapInfo: CGBitmapInfo(rawValue:
                                      CGImageAlphaInfo.none.rawValue
                                      | CGBitmapInfo.byteOrder16Little.rawValue),
                                  provider: provider, decode: nil,
                                  shouldInterpolate: false,
                                  intent: .defaultIntent)
        else { return nil }

        let out = NSMutableData()
        guard let dest = CGImageDestinationCreateWithData(
                out, UTType.png.identifier as CFString, 1, nil) else { return nil }
        CGImageDestinationAddImage(dest, image, nil)
        guard CGImageDestinationFinalize(dest) else { return nil }
        return out as Data
    }

    /// The inverse. Draws through a linear-gray context rather than reading the
    /// decoded image's own rows: the file states its space, the context states
    /// the same one, and CoreGraphics is then doing an identity conversion it
    /// can be trusted with — where reading raw rows would be trusting that
    /// nothing in the chain re-tagged them.
    static func decode(_ data: Data) -> (alpha: [Float], width: Int, height: Int)? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }

        let w = image.width, h = image.height
        guard w > 0, h > 0 else { return nil }

        var samples = [UInt16](repeating: 0, count: w * h)
        let ok: Bool = samples.withUnsafeMutableBytes { raw -> Bool in
            guard let space = CGColorSpace(name: CGColorSpace.linearGray),
                  let ctx = CGContext(data: raw.baseAddress,
                                      width: w, height: h,
                                      bitsPerComponent: 16, bytesPerRow: w * 2,
                                      space: space,
                                      bitmapInfo: CGImageAlphaInfo.none.rawValue
                                          | CGBitmapInfo.byteOrder16Little.rawValue)
            else { return false }
            ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
            return true
        }
        guard ok else { return nil }

        var alpha = [Float](repeating: 0, count: w * h)
        for i in 0..<(w * h) { alpha[i] = Float(samples[i]) / 65535 }
        return (alpha, w, h)
    }

    // MARK: Files

    /// `PHOTO.orion-matte-<id>.png`, beside `PHOTO.xmp`.
    static func url(photo: URL, id: String) -> URL {
        let base = photo.deletingPathExtension().lastPathComponent
        return photo.deletingLastPathComponent()
            .appendingPathComponent("\(base).orion-matte-\(id).png")
    }

    /// Writes a matte and returns the id it was written under. A fresh id every
    /// time, so nothing is ever overwritten in place.
    ///
    /// Throws rather than returning nil: a caller that cannot write the file
    /// must not go on to upload a matte the sidecar will never be able to name.
    @discardableResult
    static func write(_ alpha: [Float], width: Int, height: Int,
                      photo: URL) throws -> String {
        guard let data = encode(alpha, width: width, height: height) else {
            throw Failure.couldNotEncode
        }
        let id = UUID().uuidString.lowercased()
        try data.write(to: url(photo: photo, id: id), options: .atomic)
        return id
    }

    static func read(photo: URL, id: String) -> (alpha: [Float], width: Int, height: Int)? {
        guard let data = try? Data(contentsOf: url(photo: photo, id: id)) else { return nil }
        return decode(data)
    }

    /// Deletes matte files for this photograph that no component names.
    ///
    /// ⚠ **Only ever called after a sidecar has parsed.** An unreadable sidecar
    /// yields no components, and sweeping against that would delete every matte
    /// the photograph has — turning a recoverable parse failure into permanent
    /// loss of work.
    /// ⚠ **The enumeration is inside an autorelease pool, and that is load
    /// bearing rather than decoration.** `contentsOfDirectory` mints one
    /// autoreleased `NSURL` per entry in the folder, and each of those drags an
    /// `NSPathStore2`, a CoreServices `_FileCache` and a few `CFString`s along
    /// with it — about **0.8 KB per file in the folder, per call**. Nothing here
    /// releases them; they sit in whatever pool encloses the caller until it
    /// drains. A caller that loads photograph after photograph without ever
    /// returning to the run loop therefore grows without limit, and the growth
    /// is set by how many files are next to the photograph rather than by
    /// anything about the photograph.
    ///
    /// Measured on the reopen path with 300 loads: **+2.5 KB per load beside a
    /// folder of 2 files, +165 KB per load beside a folder of 200** — the same
    /// 0.8 KB per file, a straight line in the file count, and the reason a
    /// reopen loop leaked where an open loop was flat. With the pool it is
    /// +1.2 KB per load at either folder size, which is the open loop's own
    /// number. `testSweepDoesNotHoardTheDirectory` fails if this is removed.
    static func sweep(photo: URL, keeping ids: Set<String>) {
        let dir = photo.deletingLastPathComponent()
        let prefix = "\(photo.deletingPathExtension().lastPathComponent).orion-matte-"
        autoreleasepool {
            guard let entries = try? FileManager.default.contentsOfDirectory(
                    at: dir, includingPropertiesForKeys: nil) else { return }
            for entry in entries {
                let name = entry.lastPathComponent
                guard name.hasPrefix(prefix), name.hasSuffix(".png") else { continue }
                let id = String(name.dropFirst(prefix.count).dropLast(4))
                guard !ids.contains(id) else { continue }
                try? FileManager.default.removeItem(at: entry)
            }
        }
    }

    /// Every matte id the state names. The set `sweep` must keep.
    /// The sweep's whole policy, in one place, because there are **three**
    /// states of a sidecar and conflating two of them leaked files forever.
    ///
    /// | The sidecar | What may be collected |
    /// |---|---|
    /// | parsed | everything it does not reference |
    /// | **absent** | everything — a matte id lives only in a sidecar, so with no
    ///   sidecar nothing on disk can reference one |
    /// | present but unreadable | **nothing** |
    ///
    /// ⚠ That middle row is the fix. The load path used to sweep only inside
    /// the successful-parse branch, with a comment defending it against the
    /// third row — correctly. But a photograph that had simply never been saved
    /// fell into the same `else`, so its mattes could never be collected at
    /// all. Measured on `samples/_PIC8095.ARW`: **26 orphans, 512 KB, the
    /// oldest three days old**, because pressing Subject mints a fresh UUID
    /// every time and writes a new file.
    ///
    /// ⚠ And it is one function rather than a rule repeated at each call site.
    /// It was already written twice — the app's loader and the scenario
    /// runner's `reopen`, which claims in its own comment to take "the same
    /// steps `Editor.load` takes". Two copies of a delete policy is how one of
    /// them quietly stops matching.
    static func sweepAfterLoad(photo: URL, parsed: [MaskComponentState]?) {
        if let parsed {
            sweep(photo: photo, keeping: referenced(parsed))
        } else if !FileManager.default.fileExists(
                    atPath: Sidecar.url(for: photo).path) {
            sweep(photo: photo, keeping: [])
        }
    }

    static func referenced(_ components: [MaskComponentState]) -> Set<String> {
        Set(components.compactMap { $0.matteId })
    }

    enum Failure: LocalizedError {
        case couldNotEncode

        var errorDescription: String? {
            switch self {
            case .couldNotEncode: return "That selection could not be saved with the photo."
            }
        }
    }
}

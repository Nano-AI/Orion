import AppKit
import Foundation

/// Batch export against a real engine.
///
/// Separate from `BatchExport` because that file is compiled into
/// `orion-viewport-tests`, which has no `Engine` and no GPU — the naming, the
/// collision rules, the failure handling and the cancellation are all pure
/// logic and are tested there. This is the twenty lines that cannot be, and
/// keeping them apart is what lets the rest be checked without a device.
///
/// Same split as `MatteGeometry` and `SubjectMatte`.
extension BatchExport {

    /// `Orion --batch-export <folder> <photo> [photo...]`
    ///
    /// ⚠ Here so the batch is *exercised* rather than shipped on the strength
    /// of its unit tests. Everything around the export is pure logic and is
    /// checked in `orion-viewport-tests`; what only a real run can show is that
    /// one engine, reused across photographs of different shapes, still writes
    /// the right pixels for each — and that a folder of them does not grow
    /// without bound.
    @MainActor
    static func runCommandLine(_ arguments: [String]) -> Never {
        NSApplication.shared.setActivationPolicy(.accessory)

        guard let i = arguments.firstIndex(of: "--batch-export"),
              i + 2 < arguments.count else {
            FileHandle.standardError.write(Data(
                "usage: Orion --batch-export <folder> <photo> [photo...]\n".utf8))
            exit(2)
        }
        let folder = URL(fileURLWithPath: arguments[i + 1], isDirectory: true)
        let sources = arguments[(i + 2)...].map { URL(fileURLWithPath: $0) }

        try? FileManager.default.createDirectory(at: folder,
                                                 withIntermediateDirectories: true)
        guard let engine = try? Engine() else {
            FileHandle.standardError.write(Data("orion: no engine\n".utf8))
            exit(2)
        }

        let settings = ExportSettings()
        let jobs = plan(sources: sources, into: folder,
                        extension: settings.format.rawValue == "jpeg"
                                 ? "jpg" : settings.format.rawValue)

        let began = DispatchTime.now().uptimeNanoseconds
        let outcome = run(jobs: jobs, engine: engine, settings: settings)
        let ms = Double(DispatchTime.now().uptimeNanoseconds - began) / 1_000_000.0

        for url in outcome.written {
            let bytes = (try? FileManager.default
                .attributesOfItem(atPath: url.path)[.size] as? Int) ?? 0
            FileHandle.standardError.write(Data(String(
                format: "  %-24@ %7.1f KB\n",
                url.lastPathComponent as NSString,
                Double(bytes ?? 0) / 1024.0).utf8))
        }
        for (url, why) in outcome.failed {
            FileHandle.standardError.write(Data(
                "  FAILED \(url.lastPathComponent) — \(why)\n".utf8))
        }
        FileHandle.standardError.write(Data(String(
            format: "orion: %@ %.0f ms total, %.0f ms each\n",
            outcome.summary as NSString, ms,
            ms / Double(max(jobs.count, 1))).utf8))

        exit(outcome.failed.isEmpty ? 0 : 1)
    }

    /// The driver, against a real engine.
    ///
    /// ⚠ `open` then `restore`, in that order, for every photograph. `open`
    /// resets to as-shot and `restore` puts the saved edit back; a loop that
    /// called only the first would export every frame unedited, and one that
    /// called neither would export all of them with the *first* frame's grade.
    /// Both look plausible on a contact sheet, which is why the order is
    /// spelled out here rather than left to the caller.
    @MainActor
    static func run(jobs: [Job], engine: Engine, settings: ExportSettings,
                    progress: @escaping (Int, Int) -> Void = { _, _ in },
                    isCanceled: @escaping () -> Bool = { false }) -> Outcome {
        run(jobs,
            openAndRestore: { url in
                try engine.open(path: url.path)
                if let saved = Sidecar.read(for: url)?.develop {
                    engine.restore(encoded: saved)
                }
            },
            exportTo: { destination in
                try engine.export(
                    to: destination.path,
                    quality: Float(settings.quality),
                    maxDimension: settings.longestEdge(sourceWidth: engine.imageWidth,
                                                       sourceHeight: engine.imageHeight),
                    // ⚠ Every field the panel holds, not a subset. The color
                    // space was already being dropped here, so a batch wrote
                    // sRGB however the panel was set — the same bug the depth
                    // and the sharpening would have walked straight into.
                    space: settings.space.code,
                    metadata: settings.metadata.rawValue,
                    depth: settings.effectiveDepth.rawValue,
                    sharpen: settings.sharpening.rawValue)
            },
            progress: progress,
            isCanceled: isCanceled)
    }

}

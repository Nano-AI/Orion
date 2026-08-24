import AppKit
import Foundation

/// The HDR merge against a real engine: the Swift face of the three
/// `orion_engine_hdr_merge*` facade calls, and the `--hdr-merge` command-line
/// mode that keeps the whole path exercised rather than shipped on the
/// strength of its unit tests (the same reasoning as `BatchExportDriver`,
/// one file up).
///
/// The eligibility rules, the default reference and the output naming are
/// pure logic and live in `HdrMergeFlow` so `orion-viewport-tests` can check
/// them without a GPU. This file is the part that cannot be checked there.
extension Engine {

    /// Merges the bracket into a new DNG at `output`. Blocking — call it off
    /// the main thread and poll `hdrMergeProgress()` from the UI. The output
    /// must not exist; collision-free names are the caller's job
    /// (`HdrMergeFlow.outputURL`).
    func hdrMerge(paths: [String], referenceIndex: Int, output: String) throws {
        guard let handle else { throw Failure.merge("no engine") }
        guard paths.count >= 2 else { throw Failure.merge("a merge needs at least two frames") }

        let cStrings = paths.map { strdup($0) }
        defer { cStrings.forEach { free($0) } }
        var pointers = cStrings.map { UnsafePointer($0) }

        let status = pointers.withUnsafeMutableBufferPointer { buffer in
            orion_engine_hdr_merge(handle, buffer.baseAddress,
                                   Int32(paths.count), Int32(referenceIndex),
                                   output)
        }
        guard status == ORION_OK else { throw Failure.merge(errorText(status)) }
    }

    /// 0...1, safe from any thread while a merge runs on another.
    func hdrMergeProgress() -> Float {
        guard let handle else { return 0 }
        var value: Float = 0
        _ = orion_engine_hdr_merge_progress(handle, &value)
        return value
    }

    /// Asks the running merge to stop at its next stage. No partial file is
    /// ever left behind — the writer lands the DNG by rename.
    func hdrMergeCancel() {
        guard let handle else { return }
        _ = orion_engine_hdr_merge_cancel(handle)
    }
}

enum HdrMergeCLI {

    /// `Orion --hdr-merge <output.dng> <frame> <frame> [frame...]`
    ///
    /// The first listed frame is the reference and sets the framing. The mode
    /// asserts its own result — the merged file must decode and render
    /// through a fresh open — so `check-modes.py` can gate it by exit code
    /// the way it gates `--batch-export`.
    @MainActor
    static func runCommandLine(_ arguments: [String]) -> Never {
        NSApplication.shared.setActivationPolicy(.accessory)

        guard let i = arguments.firstIndex(of: "--hdr-merge"),
              i + 3 < arguments.count else {
            FileHandle.standardError.write(Data(
                "usage: Orion --hdr-merge <output.dng> <frame> <frame> [frame...]\n".utf8))
            exit(2)
        }
        let output = arguments[i + 1]
        let frames = Array(arguments[(i + 2)...])

        guard let engine = try? Engine() else {
            FileHandle.standardError.write(Data("orion: no engine\n".utf8))
            exit(2)
        }

        let began = DispatchTime.now().uptimeNanoseconds
        do {
            try engine.hdrMerge(paths: frames, referenceIndex: 0, output: output)
        } catch {
            FileHandle.standardError.write(Data(
                "orion: merge failed: \(error.localizedDescription)\n".utf8))
            exit(1)
        }
        let ms = Double(DispatchTime.now().uptimeNanoseconds - began) / 1_000_000.0

        // The mode's own oracle: the file it claims to have written must
        // open and render like any library photo. An exit 0 on the strength
        // of "the call returned" is the shape #121 catches.
        do {
            try engine.open(path: output)
            engine.render()
        } catch {
            FileHandle.standardError.write(Data(
                "orion: merged file does not open: \(error.localizedDescription)\n".utf8))
            exit(1)
        }

        let bytes = (try? FileManager.default
            .attributesOfItem(atPath: output)[.size] as? Int) ?? 0
        FileHandle.standardError.write(Data(String(
            format: "orion: merged %d frames into %@ (%.1f MB) in %.0f ms\n",
            frames.count, (output as NSString).lastPathComponent,
            Double(bytes ?? 0) / 1_048_576.0, ms).utf8))
        exit(0)
    }
}

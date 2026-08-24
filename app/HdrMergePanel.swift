import SwiftUI

/// The HDR merge sheet, and the Editor action behind it.
///
/// The sheet exists for exactly one decision the software cannot make: which
/// frame's framing wins. Everything else — eligibility, naming, the exposure
/// ladder — is `HdrMergeFlow`'s pure logic, and the engine work crosses the
/// facade on a background task while the sheet polls progress (the merge
/// facade is documented thread-safe for exactly this shape; contrast the
/// batch export, which shares the develop graph and must hold the main
/// actor).
extension Editor {

    /// The filmstrip's context-menu entry point.
    func askHdrMerge(_ urls: [URL]) {
        let candidates = urls.compactMap { url -> HdrMergeFlow.Candidate? in
            guard let photo = library.photos.first(where: { $0.url == url }) else {
                return nil
            }
            return HdrMergeFlow.Candidate(url: url, camera: photo.camera,
                                          iso: photo.iso, shutter: photo.shutter,
                                          aperture: photo.aperture)
        }
        if let reason = HdrMergeFlow.ineligibility(candidates) {
            message = reason
            return
        }
        mergeCandidates = candidates
        mergeReference = HdrMergeFlow.defaultReference(candidates)
    }

    /// The sheet body, attached in `OrionApp.swift`.
    @ViewBuilder
    var hdrMergeSheet: some View {
        if let candidates = mergeCandidates {
            VStack(alignment: .leading, spacing: 12) {
                Text("Merge to HDR")
                    .font(.system(size: 13, weight: .semibold))

                Text("The chosen frame sets the framing; the others are "
                   + "aligned to it and the result is written beside the "
                   + "originals as a new DNG.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.dim)
                    .fixedSize(horizontal: false, vertical: true)

                ForEach(Array(candidates.enumerated()), id: \.offset) { i, c in
                    Button {
                        if mergeProgress == nil { mergeReference = i }
                    } label: {
                        HStack(spacing: 8) {
                            Image(systemName: i == mergeReference
                                  ? "largecircle.fill.circle" : "circle")
                                .font(.system(size: 10))
                            Text(c.url.lastPathComponent)
                                .font(.system(size: 11, design: .monospaced))
                            Spacer()
                            Text(exposureLine(c))
                                .font(.system(size: 10))
                                .foregroundStyle(Palette.dim)
                        }
                    }
                    .buttonStyle(.plain)
                }

                if let p = mergeProgress {
                    HStack(spacing: 8) {
                        ProgressView(value: p)
                            .progressViewStyle(.linear)
                        Button("Stop") { engine.hdrMergeCancel() }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                    }
                } else {
                    HStack {
                        Spacer()
                        Button("Cancel") { mergeCandidates = nil }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                        Button("Merge") { runHdrMerge(candidates) }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.small)
                    }
                }
            }
            .padding(16)
            .frame(width: 440)
        }
    }

    private func exposureLine(_ c: HdrMergeFlow.Candidate) -> String {
        guard c.shutter > 0, c.iso > 0 else { return "no exposure data" }
        let shutter = c.shutter >= 1
            ? String(format: "%.0fs", c.shutter)
            : String(format: "1/%.0f", 1 / c.shutter)
        return String(format: "%@  f/%.1f  ISO %.0f", shutter, c.aperture, c.iso)
    }

    private func runHdrMerge(_ candidates: [HdrMergeFlow.Candidate]) {
        let reference = mergeReference
        let output = HdrMergeFlow.outputURL(
            reference: candidates[reference].url,
            exists: { FileManager.default.fileExists(atPath: $0.path) })
        let paths = candidates.map(\.url.path)

        // Anything owed to the open photo lands first — the merge reads the
        // files, not the session.
        autosave.flush()
        mergeProgress = 0

        // The blocking call runs off the main actor; the facade's progress
        // and cancel entries are the documented cross-thread surface.
        let engine = self.engine
        Task { @MainActor in
            let poller = Task { @MainActor in
                while mergeProgress != nil {
                    mergeProgress = Double(engine.hdrMergeProgress())
                    try? await Task.sleep(for: .milliseconds(150))
                }
            }
            let outcome: String? = await Task.detached(priority: .userInitiated) {
                do {
                    try engine.hdrMerge(paths: paths, referenceIndex: reference,
                                        output: output.path)
                    return nil
                } catch {
                    return error.localizedDescription
                }
            }.value
            poller.cancel()
            mergeProgress = nil
            mergeCandidates = nil

            if let outcome {
                message = outcome
                return
            }
            // The new DNG is a library photo like any other: rescan the
            // folder so it appears, then open it.
            if let folder = library.folder {
                await library.open(folder: folder)
            }
            load(output)
            message = "Merged \(paths.count) exposures into "
                    + output.lastPathComponent
        }
    }
}

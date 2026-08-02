import AppKit
import SwiftUI
import UniformTypeIdentifiers

// Everything that touches a file on disk.
//
// Opening one and a folder of them, the load that reads a sidecar and restores
// its mattes, the single export and the batch, and the sync that rewrites every
// sidecar in view. They are together because they share the one hazard: each
// writes to, or reads from, the photographer's own folder, and the ⚠ comments
// below are almost all about *which* photographs a command acts on — the
// selection, the filter, the open file that has unsaved state in memory. That
// question is answered the same way in four places and getting it wrong is how
// culling to Rated and pressing Export all wrote every reject.

extension Editor {
    /// Exports every photo in view to a folder the photographer chooses.
    ///
    /// ⚠ Runs on the main actor because the engine does, so the interface would
    /// freeze for the duration — a folder of three hundred is two and a half
    /// minutes. It yields between photographs instead, which lets the progress
    /// readout paint and the Stop button respond. Not the same as running it
    /// off the main thread, and honest about it: the panel says "working" and
    /// the rest of the interface is disabled while it does.
    func runBatchExport() {
        // ⚠ `targets`, not `photos`. This exported the whole folder regardless
        // of the filter, so culling to Rated and pressing Export all wrote every
        // reject alongside the keepers — into a folder the photographer had just
        // told it was for their picks. The selection subsumes that: with none
        // made it is everything *in view*, which is what the panel already
        // claimed to be doing.
        let targets = library.targets
        guard engine.isLoaded, !targets.isEmpty else { return }

        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Export Here"
        panel.message = library.hasExplicitSelection
            ? "Choose a folder for the \(targets.count) selected photos."
            : "Choose a folder for \(targets.count) exported photos."
        guard panel.runModal() == .OK, let folder = panel.url else { return }

        // Anything owed to the open photo is written first: batch reads
        // sidecars, and the one on screen may have edits that have not settled.
        autosave.flush()

        let ext = exportSettings.format == .jpeg ? "jpg"
                                                 : exportSettings.format.rawValue
        let jobs = BatchExport.plan(sources: targets,
                                    into: folder, extension: ext)
        batchCancelled = false
        batchProgress = (0, jobs.count)

        Task { @MainActor in
            let outcome = BatchExport.run(
                jobs: jobs, engine: engine, settings: exportSettings,
                progress: { done, total in batchProgress = (done, total) },
                isCanceled: { batchCancelled })

            batchProgress = nil
            message = outcome.summary
            // The engine is now sitting on the last photo of the batch. Put the
            // one the photographer was looking at back, or they return to a
            // different picture than they left.
            if let current { load(current) }
        }
    }

    /// What the sync confirmation says it will do.
    ///
    /// The groups are named rather than counted: "sync settings" is exactly the
    /// phrase that hides *which* settings, and a photographer about to rewrite
    /// forty sidecars should be able to read the list.
    var syncWarning: String {
        let names = PresetGroup.allCases
            .filter { presetGroups.contains($0) }
            .map(\.title)
            .joined(separator: ", ")
        // The count as well as the names, and *which* list it came from. "Every
        // photo in view" and "the eleven you picked" are different promises, and
        // the difference is invisible in a dialog that says neither.
        let n = library.targets.count
        let scope = library.hasExplicitSelection
            ? "the \(n) selected photos"
            : "all \(n) photos in view"
        return "Writes \(names) to \(scope). This cannot be undone "
             + "for photos other than the one open."
    }

    /// Applies the clipboard to every photo in view.
    ///
    /// ⚠ The photographs are not opened. Sync edits their sidecars, which are
    /// the source of truth, at the level of the JSON keys — see the note in
    /// `SyncSettings` for why decoding them into a `DevelopState` first would
    /// rewhite-balance every photo that has never been edited.
    ///
    /// The open photo is handled separately, through the ordinary edit path:
    /// it has unsaved state in memory, so writing its sidecar behind its back
    /// would be undone by the next autosave.
    func runSync() {
        guard let copied else { return }
        // Same correction as the batch export: this wrote the whole folder,
        // filter and selection alike ignored, under a warning that said "every
        // photo in view".
        let others = library.targets.filter { $0 != current }
        let outcome = SyncSettings.sync(source: copied, groups: presetGroups,
                                        to: others)

        if engine.isLoaded {
            engine.apply(preset: Preset(name: "Sync", groups: presetGroups,
                                        state: copied))
        }
        let n = outcome.written
        syncedCount = n
        let done = n == 1 ? "Synced 1 other photo."
                          : "Synced \(n) other photos."
        // ⚠ One sentence, not one dialog per photograph. The count used to be
        // the *attempt* count, so a locked card reported a full success.
        message = outcome.complaint.map { "\(done) \($0)" } ?? done
    }

    /// Opening one photo still scans its folder, so the filmstrip is populated
    /// without a separate import step.
    func openFile() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.prompt = "Open Photo"
        panel.allowedContentTypes = Library.rawExtensions
            .compactMap { UTType(filenameExtension: $0) }
        guard panel.runModal() == .OK, let url = panel.url else { return }

        Task { await library.open(folder: url.deletingLastPathComponent()) }
        load(url)
    }

    func openFolder() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.prompt = "Open Folder"
        guard panel.runModal() == .OK, let url = panel.url else { return }

        Task {
            await library.open(folder: url)
            if let first = library.visible.first?.url { load(first) }
        }
    }

    func load(_ url: URL) {
        // The photo being left keeps its edits. Without this, going to the next
        // frame and back threw the work away — which is the whole difference
        // between an editor and a viewer.
        //
        // `stop` writes what is owed *and* disarms: the engine renders several
        // times while a file opens, and until the sidecar has been restored
        // those renders still describe the photo being left.
        autosave.stop()
        // ⚠ Closed before the new photograph opens, not after. The list is per
        // photograph, and a panel still showing the previous one's versions is
        // one click from restoring that photograph's crop onto this one.
        snapshots.open(photo: nil)
        renamingSnapshot = nil

        current = url
        // ⚠ Every route to a new photograph goes through here — the filmstrip,
        // the arrow keys, opening a folder — so this is the one place the
        // selection has to follow the canvas. It collapses onto the new photo
        // rather than growing, or arrowing through a folder would quietly build
        // a selection of everything walked past.
        library.focus(url)

        // Crop and straighten belong to the photo that was open, not the one
        // arriving. Carrying them over composites the old geometry against the
        // new frame, which is what produced the doubled, offset picture.
        engine.resetCrop()

        Task { @MainActor in
            // One runloop turn, so the placeholder actually paints before the
            // synchronous decode begins.
            await Task.yield()
            do {
                try engine.open(path: url.path)
                viewport.reset()
                let saved = Sidecar.read(for: url)?.develop
                // ⚠ `restored` is the parse's *answer*, not "a blob was
                // present". They used to be conflated and the two lines below
                // both read the wrong one. See `Engine.restore`.
                var restored = false
                if let saved {
                    restored = engine.restore(encoded: saved)
                    // ⚠ Inside the successful-parse branch, because a sidecar
                    // that failed to parse yields no components and restoring
                    // from that would discard the photograph's edits.
                    if restored { engine.restoreMattes(photo: url) }
                }
                // ⚠ Outside it, and that is the fix. The sweep's policy has
                // three cases, not two, and it lives in `MatteStore` so the
                // scenario runner cannot drift from it.
                // ⚠ Before the sweep. `sweepAfterLoad` reads the version file
                // itself — the keep-set is the union of the sidecar's mattes
                // and every version's — so this is only the panel catching up,
                // but a reader who assumes the sweep depends on it should find
                // the order it expects rather than one that happens to work.
                snapshots.open(photo: url)
                // ⚠ The two facts, handed over as facts. The conclusion is
                // `MatteStore`'s, so this loader and the scenario runner's
                // cannot drift — see `MatteStore.SidecarState`.
                MatteStore.sweepAfterLoad(photo: url, blob: saved,
                                          restored: restored,
                                          components: engine.maskComponents)
                if MatteStore.SidecarState.of(blob: saved, restored: restored)
                    == .unreadable {
                    // Present but unreadable. Say so, and **do not arm
                    // autosave**: its baseline would be the default state, and
                    // the first slider tick would write that over edits that
                    // had only failed to parse. Leaving the sidecar alone is
                    // what keeps this recoverable by hand.
                    message = "Orion could not read the saved edits for "
                            + "\(url.lastPathComponent). They have been left on "
                            + "disk untouched; editing now would overwrite them."
                } else {
                    // Arm only once the photo is settled, with what its sidecar
                    // already holds — so opening a file does not write straight
                    // back what it just read.
                    autosave.begin(url: url, saved: engine.state)
                }
            } catch {
                message = error.localizedDescription
            }
        }
    }

    func exportFile() {
        let panel = NSSavePanel()
        // The photo's own name, not "export": a folder of files called
        // export-1.jpg is what happens when the dialog does not offer one.
        let base = current?.deletingPathExtension().lastPathComponent ?? "export"
        panel.nameFieldStringValue = "\(base).\(exportSettings.format.ext)"
        panel.allowedContentTypes = [UTType(filenameExtension: exportSettings.format.ext)
                                     ?? .jpeg]
        guard panel.runModal() == .OK, let url = panel.url else { return }

        do {
            try engine.export(to: url.path,
                              quality: Float(exportSettings.quality),
                              maxDimension: exportSettings.longestEdge(
                                  sourceWidth: engine.imageWidth,
                                  sourceHeight: engine.imageHeight),
                              space: exportSettings.space.code,
                              rating: Int32(library.photos.first { $0.url == current }?.rating ?? 0),
                              metadata: exportSettings.metadata.rawValue,
                              depth: exportSettings.effectiveDepth.rawValue,
                              sharpen: exportSettings.sharpening.rawValue)
        } catch {
            message = error.localizedDescription
        }
    }
}

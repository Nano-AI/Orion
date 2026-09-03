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
        // ⚠ `exportTargets`, not `photos` and not `targets`. Two rounds of the
        // same bug: it first exported the whole folder regardless of the filter,
        // so culling to Rated and pressing Export all wrote every reject
        // alongside the keepers — into a folder the photographer had just told
        // it was for their picks. Following the filter fixed that case and left
        // the common one, because the resting filter is **All** and All includes
        // rejects. `exportTargets` drops them unless they were asked for by hand
        // or by the Rejected filter; see the note on it.
        let targets = library.exportTargets
        let skipped = library.rejectedInView
        guard engine.isLoaded, !targets.isEmpty else { return }

        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Export Here"
        // ⚠ The skipped count is said out loud, on the panel that chooses the
        // folder — the last moment before files are written. A batch that
        // quietly delivers fewer photographs than the button offered is the
        // same class of surprise as one that delivers more, and this program
        // has already shipped the second.
        let rejects = skipped == 0 ? ""
            : skipped == 1 ? " 1 rejected photo is not included."
                           : " \(skipped) rejected photos are not included."
        panel.message = (library.hasExplicitSelection
            ? "Choose a folder for the \(targets.count) selected photos."
            : "Choose a folder for \(targets.count) exported photos.") + rejects
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
    /// `Orion --open <photo-or-folder>` — the real window, on a named
    /// photograph, without a panel.
    ///
    /// ⚠ **This exists because a reported bug lived in the one place nothing
    /// could reach.** `--scenario` drives the engine, `--screenshot` draws the
    /// real view hierarchy, and both were green on the file a photographer
    /// screenshotted as a flat brown rectangle: neither runs the `MTKView`,
    /// because AppKit cannot capture a Metal layer and a scenario has no
    /// window. So a fault between the engine's texture and the drawable was
    /// reproducible only by hand, by the one person who could see the screen.
    ///
    /// It takes the same two steps `openFile` does — scan the folder, load the
    /// photograph — so what it exercises is the product's own path and not a
    /// second one that happens to work.
    /// ⚠ Takes **every** path after the flag, and steps through them the way
    /// the arrow keys do. The report this was built for was not "this photo is
    /// broken" but "I opened three and the third one was". A flag that could
    /// only open one would have reproduced the wrong thing.
    static func wantedOpen(_ arguments: [String]) -> [URL] {
        guard let i = arguments.firstIndex(of: "--open") else { return [] }
        return arguments.dropFirst(i + 1)
            .prefix { !$0.hasPrefix("--") }
            .map { URL(fileURLWithPath: $0) }
    }

    /// `--dwell <milliseconds>` between the photographs of an `--open` list.
    static func wantedDwell(_ arguments: [String]) -> Int {
        guard let i = arguments.firstIndex(of: "--dwell"), i + 1 < arguments.count,
              let ms = Int(arguments[i + 1]), ms > 0 else { return 3000 }
        return ms
    }

    func openFromCommandLine(_ urls: [URL]) {
        guard let first = urls.first else { return }
        var isFolder: ObjCBool = false
        FileManager.default.fileExists(atPath: first.path, isDirectory: &isFolder)
        Task {
            if isFolder.boolValue {
                await library.open(folder: first)
                if let photo = library.visible.first?.url { load(photo) }
                return
            }
            await library.open(folder: first.deletingLastPathComponent())
            // `load` finishes its decode in a task of its own, so the dwell is
            // the settling time a hand on the arrow key would give it. Three
            // seconds is a browse; `--dwell 200` is a photographer holding the
            // key down, which is a different test — a decode still in flight
            // when the next one starts is exactly the shape of fault a
            // reproduction has to be able to ask for.
            let dwell = Editor.wantedDwell(CommandLine.arguments)
            for url in urls {
                load(url)
                try? await Task.sleep(for: .milliseconds(dwell))
            }
        }
    }

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

        // ⚠ **The photograph being opened, standing in for itself.** Decoding a
        // raw file is synchronous and took **210.9 ms** when it was measured
        // (#151), and for every one of those milliseconds the canvas went on
        // showing the *previous* photograph — `isLoaded` is set once and never
        // cleared, so nothing told it to stop. A window that asserts the wrong
        // picture for a fifth of a second is worse than one that admits it is
        // busy, and it is worst in the flat-frame case, where a correct
        // photograph lingers over a wrong one.
        //
        // ⚠⚠ **The mechanism for this was already built and connected to
        // nothing.** `Engine.placeholder` is drawn by `OrionApp+Canvas` over the
        // Metal view, under a comment saying it is "held while a new photo
        // decodes" — and the only caller of `showPlaceholder` in the tree was
        // the screenshot harness. `clearPlaceholder` had no caller at all. The
        // line below is the wiring those two comments already described.
        //
        // The library's thumbnail is the right still to use: it is the picture
        // being asked for, it is already decoded, and where there is none — a
        // file opened from outside a folder — the canvas simply keeps what it
        // had, which is the behaviour that was there before.
        engine.showPlaceholder(library.photos.first { $0.url == url }?.thumbnail)

        Task { @MainActor in
            // One runloop turn, so the placeholder actually paints before the
            // synchronous decode begins.
            await Task.yield()
            // ⚠ `defer`, not a line at the end of the `do`. A photograph that
            // fails to open must take the still down too — otherwise the canvas
            // keeps a thumbnail of a file it never managed to read, over the
            // previous photograph, with an error in the footer explaining
            // neither. `orion_engine_render` is synchronous, so by the time any
            // of these paths unwinds the frame behind this is the real one.
            //
            // ⚠⚠ **Only the most recent load takes the still down**, and that
            // guard is not hypothetical: `--open` steps a list with a dwell,
            // and `--dwell 200` against a 210 ms decode (#151) exists precisely
            // so a load begins while the one before it is in flight — its own
            // comment says so. Without the check, the older task's `defer`
            // fires after the newer one has put *its* thumbnail up, clearing
            // the picture being opened and leaving the canvas on the one being
            // left, which is the bug this whole mechanism exists to prevent.
            //
            // ⚠ Reasoned, not reproduced. The interleaving needs the main actor
            // to schedule the two tasks in a particular order and nothing in
            // this repository can drive the open path at all (#121, #181), so
            // this is recorded as a reading rather than dressed up as a
            // measurement — the shape #154 settled on for the same situation.
            defer { if current == url { engine.clearPlaceholder() } }
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

    // MARK: Trash

    /// Puts the confirmation up. Nothing moves until "Move to Trash" is
    /// pressed in the alert this arms - see `runTrash`.
    func confirmTrash(_ urls: [URL]) {
        guard !urls.isEmpty else { return }
        pendingTrash = urls
    }

    /// Delete Rejected, folder-wide: every rejected photograph, whatever the
    /// current filter shows. The confirmation's counted title is what keeps
    /// that honest - the number read is the number acted on.
    func confirmTrashRejected() {
        let rejected = library.photos.filter(\.rejected).map(\.url)
        guard !rejected.isEmpty else { return }
        pendingTrash = rejected
    }

    /// The confirmed move. Order matters twice here:
    ///
    /// ⚠ `autosave.stop()` runs *before* the files move - a coalesced write
    /// landing after the trash would resurrect a sidecar beside a raw that is
    /// gone, which is exactly the orphan `TrashPlan` exists to prevent.
    ///
    /// ⚠ The survivors are computed *before* the library mutates, from the
    /// list order the photographer was looking at - afterwards the trashed
    /// photographs are no longer in it to measure from.
    func runTrash() {
        guard let pending = pendingTrash, !pending.isEmpty else {
            pendingTrash = nil
            return
        }
        pendingTrash = nil
        let goes = Set(pending)

        // The nearest photograph that stays: forward first, then backward,
        // the way deleting from any list advances.
        let list = library.visibleURLs
        func survivor(of url: URL?) -> URL? {
            guard let url else { return nil }
            guard goes.contains(url) else { return url }
            guard let i = list.firstIndex(of: url) else { return nil }
            return list[(i + 1)...].first { !goes.contains($0) }
                ?? list[..<i].reversed().first { !goes.contains($0) }
        }
        let canvasSurvivor = survivor(of: current)
        let focusSurvivor = survivor(of: galleryFocus)

        let currentGoes = current.map(goes.contains) ?? false
        if currentGoes {
            autosave.stop()
            snapshots.open(photo: nil)
        }

        let outcome = library.trash(pending)
        if let complaint = outcome.complaint { message = complaint }
        let gone = Set(outcome.trashed)

        if let cur = current, gone.contains(cur) {
            current = nil
            if mode == .develop {
                if let canvasSurvivor {
                    load(canvasSurvivor)
                } else {
                    // Nothing left to put on the canvas. The empty gallery is
                    // the honest face for an emptied folder - the canvas would
                    // go on showing a photograph that is in the Trash.
                    mode = .cull
                }
            }
        } else if currentGoes, let cur = current {
            // The raw refused to move, so the photograph is still open - put
            // back what the failed attempt disarmed, or the next slider tick
            // silently stops reaching the sidecar.
            autosave.begin(url: cur, saved: engine.state)
            snapshots.open(photo: cur)
        }

        if mode == .cull {
            galleryFocus = focusSurvivor ?? library.visible.first?.url
            if let galleryFocus { library.focus(galleryFocus) }
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

import Foundation

extension BatchExport {
    /// The GUI borrows its document's engine; CLI exports have no live document.
    @MainActor
    static func runInteractive(jobs: [Job], current: URL, engine: Engine,
                               autosave: Autosave, settings: ExportSettings,
                               watermark: Watermark? = nil,
                               progress: @escaping (Int, Int) -> Void = { _, _ in },
                               isCanceled: @escaping () -> Bool = { false }) async throws -> Outcome {
        guard engine.isLoaded, !engine.isOpening, !engine.documentEditsLocked,
              !engine.batchExporting, !engine.interacting, engine.liveIndex < 0,
              !ProposalWatcher.shared.isLive, autosave.isActive(for: current) else {
            throw Engine.Failure.export("Finish opening, editing or reviewing the photo before exporting a batch.")
        }
        // ponytail: LUT identity/pixels are not in DevelopState. Add immutable
        // LUT capture plus per-photo identity before borrowing a LUT-edited engine.
        guard engine.lutName.isEmpty else {
            throw Engine.Failure.export("Batch export cannot preserve a loaded creative LUT yet. Export this photo individually, or clear the LUT before exporting a batch.")
        }
        let state = engine.state
        for component in state.maskComponents where component.kind == 4 {
            guard let id = component.matteId, MatteStore.read(photo: current, id: id) != nil else {
                throw Engine.Failure.export("The open photo has a mask whose saved matte is missing or unreadable. Restore that matte before exporting a batch.")
            }
        }
        let history = engine.history.checkpoint
        let selectedMask = engine.selectedMask
        let split = engine.compareSplit
        let vertical = engine.compareVertical
        let cropPreview = engine.cropPreview
        let lensProfileEnabled = engine.lensProfileEnabled
        // A failed outgoing save keeps its pending job and the live document.
        autosave.note(state)
        autosave.flush()
        guard !autosave.isDirty else {
            throw Engine.Failure.export(autosave.lastFailure ?? "The open photo could not be saved.")
        }
        ProposalWatcher.shared.attach(photo: nil, engine: engine, autosave: autosave)
        autosave.stop()
        let onEdit = engine.onEdit
        engine.onEdit = nil
        engine.batchExporting = true
        engine.documentEditsLocked = true
        engine.isOpening = true // Never display B underneath A's title.
        engine.clearCompare()
        engine.suspended = true
        engine.cropPreview = false
        engine.lensProfileEnabled = true
        engine.suspended = false
        defer {
            engine.onEdit = onEdit
            engine.batchExporting = false
            engine.documentEditsLocked = ProposalWatcher.shared.isLive
            engine.isOpening = false
        }

        var outcome = Outcome()
        for (i, job) in jobs.enumerated() {
            progress(i, jobs.count)
            // ponytail: one photo still blocks main. A serial worker-owned
            // engine is needed for cancellation within decode/render/export.
            // A future deadline lets the run loop service UI events/timers;
            // an immediate main-queue chain can keep draining without them.
            await withCheckedContinuation { continuation in
                DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(1)) {
                    continuation.resume()
                }
            }
            if isCanceled() || Task.isCancelled { outcome.canceled = true; break }
            // The current photo carries this live, non-persisted lens switch;
            // other photos use the normal open default, never A's switch.
            engine.suspended = true
            engine.lensProfileEnabled = job.source.standardizedFileURL == current.standardizedFileURL
                ? lensProfileEnabled : true
            engine.suspended = false
            let result = run(jobs: [job], engine: engine, settings: settings, watermark: watermark)
            outcome.written += result.written
            outcome.failed += result.failed
        }
        progress(outcome.written.count + outcome.failed.count, jobs.count)

        do {
            try engine.open(path: current.path, restoring: true)
            engine.suspended = true
            engine.cropPreview = cropPreview
            engine.lensProfileEnabled = lensProfileEnabled
            engine.apply(state)
            try restoreRequiredMattes(photo: current, engine: engine)
            engine.history.restore(history)
            engine.compareVertical = vertical
            if split < 1 { engine.setCompare(split: split) }
            engine.selectedMask = selectedMask
            if let why = engine.lastFailure { throw Engine.Failure.open(why) }
        } catch {
            // Keep the captured edit/history inspectable, but no drawable or
            // save target may pretend the last batch photo belongs to A.
            engine.isLoaded = false
            engine.suspended = true
            engine.cropPreview = cropPreview
            engine.lensProfileEnabled = lensProfileEnabled
            engine.assign(state)
            engine.suspended = false
            engine.history.restore(history)
            engine.clearCompare()
            throw Engine.Failure.open("Could not restore \(current.lastPathComponent) after batch export. Its edits are saved; reopen the photo. \(error.localizedDescription)")
        }
        autosave.begin(url: current, saved: state)
        engine.onEdit = onEdit
        engine.documentEditsLocked = false
        // Reattach only after the real document is valid again. A proposal may
        // have arrived during the batch, and must get its ordinary review lock.
        engine.batchExporting = false
        ProposalWatcher.shared.attach(photo: current, engine: engine, autosave: autosave)
        return outcome
    }
}

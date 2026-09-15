import AppKit

extension Scenario {
    /// Exercise the actual document callbacks, disk writes and GPU comparison.
    /// A private RAW copy keeps this runnable against anybody's photograph.
    static func checkDesktopWorkflow(photo source: URL) throws {
        let folder = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-workflow-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        let photo = folder.appendingPathComponent("photo.ARW")
        try FileManager.default.copyItem(at: source, to: photo)
        let engine = try Engine()
        try engine.open(path: photo.path)

        func check(_ ok: Bool, _ text: String) {
            checks += 1
            if !ok { failures += 1 }
            say("  \(ok ? "ok  " : "FAIL")  \(text)\n")
        }
        func saved() throws -> DevelopState {
            guard let blob = Sidecar.read(for: photo)?.develop else {
                throw Bad(what: "workflow sidecar missing")
            }
            return try JSONDecoder().decode(DevelopState.self, from: blob)
        }
        // Stay inside the reference half, clear of the shader's divider line.
        let region = CGRect(x: 0, y: 0, width: 0.49, height: 1)
        func picture(_ surface: Screenshot.Surface) throws -> Reading {
            try read(engine, region, through: surface)
        }
        func agrees(_ a: Reading, _ b: Reading) -> Bool {
            abs(a.luma - b.luma) < 0.001 && abs(a.saturation - b.saturation) < 0.001
        }

        engine.beginInteraction()
        check(engine.interacting, "cold first gesture enters preview")
        engine.edit("Exposure") { engine.exposureEv = 1 }
        check(engine.previewTexture != nil, "first drag produces a preview frame")
        let preview = try picture(.preview)
        engine.edit("Exposure") { engine.exposureEv = 3 }
        check(!agrees(preview, try picture(.preview)), "preview follows the moving control")
        engine.endInteraction()
        let settled = try picture(.output)
        check(!engine.interacting, "release returns to full output")
        engine.render()
        check(agrees(settled, try picture(.output)), "release already rendered the full answer")

        engine.resetEdits()
        engine.edit("Exposure") { engine.exposureEv = 1 }
        let committed = engine.state
        check(Autosave.toSidecar(photo, committed), "saved edit written")
        let autosave = Autosave(deferral: { _ in })
        autosave.begin(url: photo, saved: committed)
        var notifications = 0
        engine.onEdit = { state in notifications += 1; autosave.note(state) }
        defer {
            ProposalWatcher.shared.attach(photo: nil, engine: engine, autosave: autosave)
            engine.onEdit = nil
            autosave.stop()
        }
        engine.setCompare(split: 0.5)
        check(notifications == 0, "Compare publishes no temporary document edits")
        autosave.flush()
        check(try saved() == committed, "Compare and flush preserve the saved edit on disk")
        engine.clearCompare()
        engine.edit("Exposure") { engine.exposureEv = 2 }
        engine.edit("Exposure") { engine.exposureEv = 1 }
        autosave.flush()
        check(try saved() == committed, "returning to saved state cancels an obsolete write")

        // Crop-panel and toolbar rotation now share this transaction. A crop
        // and an exposure precede it so an over-broad Undo cannot pass.
        engine.edit("Crop") { engine.setCrop(x: 0.1, y: 0.1, w: 0.7, h: 0.7) }
        let beforeRotation = engine.state
        engine.rotate(1)
        check(engine.rotateQuarters == 1, "rotation changes the photograph")
        engine.undo()
        check(engine.state == beforeRotation, "one Undo restores crop and keeps prior exposure")
        engine.redo()
        check(engine.rotateQuarters == 1 && engine.exposureEv == 1, "Redo restores only rotation")
        engine.undo()
        engine.resetEdits()
        engine.edit("Exposure") { engine.exposureEv = 1 }
        autosave.flush()

        let baseline = engine.state
        let baselinePicture = try picture(.output)
        var proposed = baseline
        proposed.exposureEv = 3
        let proposalURL = Proposal.proposedURL(for: photo)
        try JSONEncoder().encode(proposed).write(to: proposalURL, options: .atomic)
        ProposalWatcher.shared.attach(photo: photo, engine: engine, autosave: autosave)
        check(engine.state == proposed && engine.documentEditsLocked,
              "proposal loads and locks document editing")
        engine.setCompare(split: 0.5)
        check(agrees(baselinePicture, try picture(.canvas)),
              "proposal reference is the committed edit, not as-shot")
        check(!agrees(baselinePicture, try picture(.output)), "proposal visibly differs from baseline")
        proposed.exposureEv = 2
        try JSONEncoder().encode(proposed).write(to: proposalURL, options: .atomic)
        // Exercise the real directory watcher, including its debounce, rather
        // than passing an invented .changed event straight to the policy.
        for _ in 0..<50 {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
            if engine.state == proposed { break }
        }
        check(engine.state == proposed, "a revised proposal reaches the live preview")
        check(agrees(baselinePicture, try picture(.canvas)),
              "a revised proposal keeps the original committed reference")
        engine.rotate(1)
        engine.undo()
        engine.redo()
        engine.resetEdits()
        engine.edit("Exposure") { engine.exposureEv = -2 }
        check(engine.state == proposed, "rotation, undo, redo, reset and edits cannot mutate a live proposal")
        autosave.flush()
        check(try saved() == baseline, "unapproved preview never writes its adjustments")
        ProposalWatcher.shared.approve()
        check(try saved() == proposed, "Approve writes the displayed proposal")
        check(!engine.documentEditsLocked && !engine.comparing, "Approve restores editing")
        engine.undo()
        check(engine.state == baseline, "one Undo after approval returns to the committed edit")
        autosave.flush()

        try JSONEncoder().encode(proposed).write(to: proposalURL, options: .atomic)
        ProposalWatcher.shared.attach(photo: photo, engine: engine, autosave: autosave)
        ProposalWatcher.shared.reject()
        check(engine.state == baseline && !engine.documentEditsLocked,
              "Reject restores the committed edit and editing access")
        check(try saved() == baseline, "Reject leaves the saved edit intact")
        try JSONEncoder().encode(proposed).write(to: proposalURL, options: .atomic)
        ProposalWatcher.shared.attach(photo: photo, engine: engine, autosave: autosave)
        ProposalWatcher.shared.attach(photo: nil, engine: engine, autosave: autosave)
        check(engine.state == baseline && !engine.documentEditsLocked,
              "leaving a proposal restores the outgoing photo before another opens")
        check(FileManager.default.fileExists(atPath: proposalURL.path),
              "leaving review keeps the proposal available for later")
        engine.setCompare(split: 0.5)
        let asShot = try picture(.canvas)
        check(!agrees(baselinePicture, asShot), "ordinary Compare returns to as-shot after review")
        engine.clearCompare()
    }
}

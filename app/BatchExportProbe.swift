import AppKit
import SwiftUI

/// Real GUI-used orchestration, with real autosave/render callbacks and tiny RAW copies.
enum BatchExportProbe {
    @MainActor
    static func runCommandLine() -> Never {
        // Mounting the real Editor later must not honor any --open argument.
        guard CommandLine.arguments.count == 2 else { exit(2) }
        NSApplication.shared.setActivationPolicy(.accessory)
        Task { @MainActor in
            do { exit(try await check()) }
            catch { fputs("FAIL batch safety: \(error)\n", stderr); exit(1) }
        }
        NSApplication.shared.run()
        exit(2)
    }

    @MainActor
    static func check() async throws -> Int32 {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("orion-batch-\(UUID())")
        try fm.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: root) }
        let fixture = URL(fileURLWithPath: "tools/fixtures/mask-invert.dng")
        let a = root.appendingPathComponent("A.dng")
        let b = root.appendingPathComponent("B.dng")
        try fm.copyItem(at: fixture, to: a)
        try fm.copyItem(at: fixture, to: b)
        let engine = try Engine()
        let autosave = Autosave()
        engine.onEdit = { autosave.note($0) }
        try engine.open(path: a.path)
        var seeded = engine.state
        var brush = MaskComponentState()
        brush.kind = 3; brush.brushStroke = [0.25, 0.25, 0.35, 0.35]
        var matte = MaskComponentState()
        matte.kind = 4
        matte.matteId = try MatteStore.write(Array(repeating: 0.8, count: 64 * 64),
                                            width: 64, height: 64, photo: a)
        seeded.maskComponents = [brush, matte]
        seeded.layers[0].exposureEv = -1
        engine.apply(seeded)
        engine.restoreMattes(photo: a)
        engine.selectedMask = 1
        engine.history.reset(to: engine.state)
        autosave.begin(url: a, saved: engine.state)
        engine.edit("Exposure") { engine.exposureEv = 0.7 }
        engine.edit("Contrast") { engine.contrast = 1.2 }
        engine.undo()
        autosave.flush()
        engine.lensProfileEnabled = false
        engine.compareVertical = false
        engine.setCompare(split: 0.35)
        engine.selectedMask = 1
        let before = try Data(contentsOf: Sidecar.url(for: a))
        let state = engine.state
        let history = engine.history.checkpoint
        let reference = root.appendingPathComponent("reference.jpg")
        try engine.export(to: reference.path)
        var other = state
        other.exposureEv = -1.3; other.maskComponents = []
        _ = Sidecar(develop: try JSONEncoder().encode(other)).write(for: b)
        let settings = ExportSettings()
        func job(_ name: String, source: URL? = nil) -> BatchExport.Job {
            .init(source: source ?? b, destination: root.appendingPathComponent(name + ".jpg"))
        }
        var failures = 0
        func expect(_ value: Bool, _ label: String) {
            print("  \(value ? "ok" : "FAIL") \(label)")
            if !value { failures += 1 }
        }
        func historyMatches() -> Bool {
            engine.history.position == history.position && engine.history.entries.count == history.entries.count
            && zip(engine.history.entries, history.entries).allSatisfy {
                $0.label == $1.label && $0.state == $1.state && $0.time == $1.time
            }
        }
        let success = try await BatchExport.runInteractive(jobs: [job("success")], current: a,
            engine: engine, autosave: autosave, settings: settings)
        expect(success.written.count == 1 && success.failed.isEmpty, "GUI batch exports B")
        expect(try Data(contentsOf: Sidecar.url(for: a)) == before, "A sidecar unchanged")
        expect(engine.state == state, "A live state restored")
        expect(historyMatches(), "exact undo and redo preserved")
        expect(engine.selectedMask == 1 && engine.compareSplit == 0.35 && !engine.compareVertical
               && !engine.lensProfileEnabled, "selection compare and lens switch restored")
        let restored = root.appendingPathComponent("restored.jpg")
        try engine.export(to: restored.path)
        expect(try Data(contentsOf: restored) == Data(contentsOf: reference), "A rendered brush and matte restored")
        engine.redo(); expect(engine.contrast == 1.2, "redo still reaches future edit")
        engine.undo(); autosave.flush()

        var heartbeat = false
        var heartbeatScheduled = false
        var heartbeatMS = 0.0
        var cancel = false
        let second = job("never")
        let canceled = try await BatchExport.runInteractive(jobs: [job("first"), second], current: a,
            engine: engine, autosave: autosave, settings: settings, progress: { done, _ in
                if done == 1 && !heartbeatScheduled {
                    heartbeatScheduled = true
                    let heartbeatScheduledAt = DispatchTime.now().uptimeNanoseconds
                    Timer.scheduledTimer(withTimeInterval: 0, repeats: false) { _ in
                        Task { @MainActor in
                            heartbeatMS = Double(DispatchTime.now().uptimeNanoseconds - heartbeatScheduledAt) / 1e6
                            heartbeat = true; cancel = true
                            let borrowed = engine.state
                            engine.edit("Blocked") { engine.exposureEv = 4 }
                            engine.undo()
                            expect(engine.state == borrowed && engine.documentEditsLocked && engine.isOpening,
                                   "UI heartbeat cannot edit the borrowed engine")
                        }
                    }
                }
            }, isCanceled: { cancel })
        expect(heartbeat && canceled.canceled && canceled.written.count == 1
               && !fm.fileExists(atPath: second.destination.path), "main heartbeat and Stop run before job two")
        print("post-job-one timer scheduling to main-actor heartbeat: \(heartbeatMS) ms (64x64 fixture)")
        expect(engine.state == state && historyMatches(), "cancel restores A and history")
        expect(!autosave.isDirty && !engine.batchExporting && !engine.isOpening,
               "cancel releases engine and rearms saving")

        let failed = try await BatchExport.runInteractive(jobs: [job("missing", source: root.appendingPathComponent("gone.dng"))],
            current: a, engine: engine, autosave: autosave, settings: settings)
        expect(failed.failed.count == 1 && engine.state == state && historyMatches(),
               "export failure restores A and history")

        let sidecarURL = Sidecar.url(for: a)
        let sidecarBeforeFailure = try Data(contentsOf: sidecarURL)
        try fm.removeItem(at: sidecarURL)
        try fm.createDirectory(at: sidecarURL, withIntermediateDirectories: false)
        let failingSave = Autosave(deferral: { _ in })
        failingSave.begin(url: a, saved: state)
        engine.onEdit = { failingSave.note($0) }
        engine.edit("Unsaved") { engine.exposureEv = 0.9 }
        let unsaved = engine.state
        var refused = false
        do {
            _ = try await BatchExport.runInteractive(jobs: [job("refused")], current: a,
                engine: engine, autosave: failingSave, settings: settings)
        } catch { refused = true }
        expect(refused && failingSave.isDirty && engine.state == unsaved
               && !fm.fileExists(atPath: job("refused").destination.path), "failed outgoing save refuses borrow and retains pending edit")
        try fm.removeItem(at: sidecarURL)
        try sidecarBeforeFailure.write(to: sidecarURL)
        failingSave.flush()
        let retried = try JSONDecoder().decode(DevelopState.self, from: Sidecar.read(for: a)!.develop!)
        expect(!failingSave.isDirty && retried == unsaved, "owed save retries after refusal")
        failingSave.stop()
        engine.onEdit = { autosave.note($0) }
        engine.apply(state); engine.history.restore(history)
        autosave.begin(url: a, saved: unsaved); autosave.note(state); autosave.flush()

        for blocked in ["opening", "locked", "LUT"] {
            engine.isOpening = blocked == "opening"
            engine.documentEditsLocked = blocked == "locked"
            engine.lutName = blocked == "LUT" ? "live LUT" : ""
            var rejected = false
            do {
                _ = try await BatchExport.runInteractive(jobs: [job("blocked-" + blocked)], current: a,
                    engine: engine, autosave: autosave, settings: settings)
            } catch { rejected = true }
            expect(rejected && engine.state == state, "refuses \(blocked) document")
            engine.isOpening = false; engine.documentEditsLocked = false; engine.lutName = ""
        }

        let matteURL = MatteStore.url(photo: a, id: matte.matteId!)
        let matteBytes = try Data(contentsOf: matteURL)
        try fm.removeItem(at: matteURL)
        var missingOwnMatteRefused = false
        do {
            _ = try await BatchExport.runInteractive(jobs: [job("own-matte-missing")], current: a,
                engine: engine, autosave: autosave, settings: settings)
        } catch { missingOwnMatteRefused = true }
        expect(missingOwnMatteRefused && engine.isLoaded && engine.state == state,
               "missing A matte refuses borrow without losing live pixels")
        try matteBytes.write(to: matteURL)

        var proposed = state; proposed.exposureEv = 1.4
        let proposal = Proposal.proposedURL(for: a)
        try JSONEncoder().encode(proposed).write(to: proposal)
        ProposalWatcher.shared.attach(photo: a, engine: engine, autosave: autosave)
        var liveRefused = false
        do {
            _ = try await BatchExport.runInteractive(jobs: [job("live-proposal")], current: a,
                engine: engine, autosave: autosave, settings: settings)
        } catch { liveRefused = true }
        expect(liveRefused && ProposalWatcher.shared.isLive && engine.state == proposed,
               "live proposal refuses batch without losing preview")
        ProposalWatcher.shared.reject()
        var proposalWritten = false
        _ = try await BatchExport.runInteractive(jobs: [job("proposal-arrival")], current: a,
            engine: engine, autosave: autosave, settings: settings, progress: { done, _ in
                if done == 1 && !proposalWritten {
                    proposalWritten = true
                    try? JSONEncoder().encode(proposed).write(to: proposal)
                    expect(!ProposalWatcher.shared.isLive, "proposal observation detached while borrowed")
                }
            })
        expect(ProposalWatcher.shared.isLive && engine.documentEditsLocked && engine.state == proposed
               && !autosave.isActive(for: a), "proposal arriving during batch rearms review only on restored A")
        ProposalWatcher.shared.reject()
        expect(engine.state == state && historyMatches(), "reject after batch preserves A and history")

        engine.lensProfileEnabled = false
        var sawCurrentLensSwitch = false
        var sawOtherLensDefault = false
        _ = try await BatchExport.runInteractive(jobs: [job("A-lens", source: a), job("B-lens")],
            current: a, engine: engine, autosave: autosave, settings: settings, progress: { done, _ in
                if done == 1 { sawCurrentLensSwitch = !engine.lensProfileEnabled }
                if done == 2 { sawOtherLensDefault = engine.lensProfileEnabled }
            })
        expect(sawCurrentLensSwitch && sawOtherLensDefault && !engine.lensProfileEnabled,
               "A exports with its live lens switch while B uses its own default")

        // Shared driver: a failed restore never writes a plausible default export.
        autosave.stop(); engine.onEdit = nil; engine.clearCompare()
        ProposalWatcher.shared.attach(photo: nil, engine: engine, autosave: autosave)
        _ = Sidecar(develop: Data("{bad json".utf8)).write(for: b)
        let malformed = BatchExport.run(jobs: [job("bad-state")], engine: engine, settings: settings)
        expect(malformed.failed.count == 1 && malformed.written.isEmpty, "driver refuses malformed saved state")
        for (name, xml, shouldWrite) in [
            ("broken-xml", "<x><broken>", false),
            ("bad-base64", "<x orion:Develop=\"!!\"/>", false),
            ("rating-only", "<x xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmp:Rating=\"3\"/>", true),
            ("foreign", "<x xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\" crs:Exposure2012=\"1.0\"/>", true)
        ] {
            try Data(xml.utf8).write(to: Sidecar.url(for: b))
            let result = BatchExport.run(jobs: [job(name)], engine: engine, settings: settings)
            expect(shouldWrite ? result.written.count == 1 : result.failed.count == 1,
                   "strict reader handles \(name)")
        }
        for (name, json) in [
            ("bad-adjustment", "{\"exposureEv\":\"broken\"}"),
            ("bad-mask-list", "{\"maskComponents\":\"broken\"}"),
            ("bad-mask-field", "{\"maskComponents\":[{\"kind\":2,\"radiusX\":\"broken\"}]}"),
            ("bad-mask-object", "{\"maskComponents\":[42]}"),
            ("bad-layer-field", "{\"layers\":[{\"exposureEv\":\"broken\"}]}"),
            ("bad-grade-length", "{\"gradeShadow\":[1]}")
        ] {
            _ = Sidecar(develop: Data(json.utf8)).write(for: b)
            let bad = BatchExport.run(jobs: [job(name)], engine: engine, settings: settings)
            expect(bad.failed.count == 1 && bad.written.isEmpty
                   && !fm.fileExists(atPath: job(name).destination.path), "driver refuses \(name)")
        }
        _ = Sidecar(develop: Data("{\"exposureEv\":0.4,\"maskComponents\":[{\"kind\":2}]}".utf8)).write(for: b)
        let legacy = BatchExport.run(jobs: [job("legacy-defaults")], engine: engine, settings: settings)
        expect(legacy.written.count == 1 && engine.exposureEv == 0.4
               && engine.maskComponents.first?.radiusX == MaskComponentState().radiusX,
               "strict export retains missing legacy field defaults")
        let encoded = try JSONEncoder().encode(other).base64EncodedString()
        _ = Sidecar(develop: try JSONEncoder().encode(other)).write(for: b)
        let canonical = BatchExport.run(jobs: [job("canonical-xml")], engine: engine, settings: settings)
        expect(canonical.written.count == 1, "canonical XML reference exports")
        for (name, xml) in [
            ("single-quote", "<x xmlns:orion='http://orion.photo/ns/1.0/' orion:Develop='\(encoded)'/>"),
            ("alias-spaced", "<x xmlns:edit='http://orion.photo/ns/1.0/' edit:Develop = '\(encoded)'/>"),
            ("alias-element", "<x xmlns:edit='http://orion.photo/ns/1.0/'><edit:Develop>\(encoded)</edit:Develop></x>")
        ] {
            try Data(xml.utf8).write(to: Sidecar.url(for: b))
            let parsed = BatchExport.run(jobs: [job(name)], engine: engine, settings: settings)
            expect(parsed.written.count == 1 && engine.exposureEv == other.exposureEv,
                   "driver restores \(name) Develop")
            if parsed.written.count == 1 {
                expect(try Data(contentsOf: job(name).destination)
                       == Data(contentsOf: job("canonical-xml").destination),
                       "\(name) export matches intended edit pixels")
            }
        }
        var masked = other; masked.maskComponents = [matte]
        _ = Sidecar(develop: try JSONEncoder().encode(masked)).write(for: b)
        let missingMatte = BatchExport.run(jobs: [job("missing-matte")], engine: engine, settings: settings)
        expect(missingMatte.failed.count == 1 && missingMatte.written.isEmpty, "driver refuses missing required matte")
        try fm.copyItem(at: MatteStore.url(photo: a, id: matte.matteId!),
                        to: MatteStore.url(photo: b, id: matte.matteId!))
        let withMatte = BatchExport.run(jobs: [job("with-matte")], engine: engine, settings: settings)
        expect(withMatte.written.count == 1 && engine.missingMattes.isEmpty, "driver restores required matte")
        let expectedMatte = root.appendingPathComponent("expected-matte.jpg")
        engine.restoreMattes(photo: b)
        try engine.export(to: expectedMatte.path, quality: Float(settings.quality),
                          metadata: settings.metadata.rawValue, depth: settings.effectiveDepth.rawValue)
        expect(try Data(contentsOf: expectedMatte) == Data(contentsOf: job("with-matte").destination),
               "driver matte export agrees with explicit restore")
        let generation = engine.generation
        _ = Sidecar(develop: try JSONEncoder().encode(other)).write(for: b)
        _ = BatchExport.run(jobs: [job("one-render")], engine: engine, settings: settings)
        expect(engine.generation == generation + 1, "edited batch open skips redundant default render")

        // Put A back, then remove only this probe's RAW between export and restore.
        try engine.open(path: a.path, restoring: true)
        engine.apply(state); engine.restoreMattes(photo: a); engine.history.restore(history)
        engine.onEdit = { autosave.note($0) }; autosave.begin(url: a, saved: state)
        let savedBytes = try Data(contentsOf: Sidecar.url(for: a))
        var restoreRefused = false
        do {
            _ = try await BatchExport.runInteractive(jobs: [job("restore-failure")], current: a,
                engine: engine, autosave: autosave, settings: settings, progress: { done, _ in
                    if done == 1 { try? fm.removeItem(at: a) }
                })
        } catch { restoreRefused = true }
        engine.exposureEv = 2; autosave.flush()
        expect(restoreRefused && !engine.isLoaded && !engine.isOpening && !engine.batchExporting
               && !autosave.isActive(for: a), "restore failure blanks and disarms document without permanent busy lock")
        expect(try Data(contentsOf: Sidecar.url(for: a)) == savedBytes, "restore failure cannot save B as A")
        try engine.open(path: b.path)
        expect(engine.isLoaded, "another photo opens after restore failure")
        expect(try await checkStopKey(engine: engine), "real Editor key monitor lets Escape activate Stop")
        print("batch safety: \(failures) failures; 64x64 fixtures, one engine")
        return failures == 0 ? 0 : 1
    }
    /// Runs through NSApplication event dispatch and the real Editor's installed
    /// local monitor. Calling the Stop closure directly would miss this regression.
    @MainActor
    static func checkStopKey(engine: Engine) async throws -> Bool {
        let oldOnEdit = engine.onEdit
        let oldOpening = engine.isOpening
        let oldLocked = engine.documentEditsLocked
        engine.batchExporting = true
        engine.documentEditsLocked = true
        engine.isOpening = true
        var startupCallbackSurvived = false
        engine.onEdit = { _ in startupCallbackSurvived = true }
        var stopped = false
        let content = VStack {
            Editor(engine: engine, startLibrary: Library(index: PhotoIndex(at: nil)),
                   startPresets: PresetStore(url: nil), startWatermark: Watermark(url: nil))
                .frame(width: 1100, height: 700)
            BatchExportProgress(done: 1, total: 2) { stopped = true }
        }
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1100, height: 750),
                              styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        window.contentView = NSHostingView(rootView: content)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        // Wait for the actual view lifecycle to install its monitor/autosave.
        try await Task.sleep(for: .milliseconds(30))
        engine.onEdit?(engine.state)
        let installed = !startupCallbackSurvived && window.isKeyWindow
        if let event = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [],
            timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
            context: nil, characters: "\u{1b}", charactersIgnoringModifiers: "\u{1b}",
            isARepeat: false, keyCode: 53) {
            NSApp.sendEvent(event)
        }
        try await Task.sleep(for: .milliseconds(30))
        window.contentView = nil
        window.close()
        // onDisappear removes the real local monitor before other checks can run.
        try await Task.sleep(for: .milliseconds(30))
        engine.onEdit = oldOnEdit
        engine.isOpening = oldOpening
        engine.documentEditsLocked = oldLocked
        engine.batchExporting = false
        return installed && stopped
    }

}

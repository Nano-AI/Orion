import AppKit
import SwiftUI

/// Drives the mounted Editor's real actions with an owned 64×64 RAW folder.
enum SaveDepartureProbe {
    @MainActor private final class Ready { var editor: Editor? }
    nonisolated(unsafe) private static var finished = false
    nonisolated(unsafe) private static var failures = 0

    static func run() -> Never {
        Task { @MainActor in
            await exercise()
            finished = true
        }
        while !finished {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.01))
        }
        exit(failures == 0 ? 0 : 1)
    }

    @MainActor private static func check(_ yes: Bool, _ name: String) {
        print("  \(yes ? "ok" : "FAIL") \(name)")
        if !yes { failures += 1 }
    }

    @MainActor private static func until(_ condition: () -> Bool) async -> Bool {
        let deadline = Date().addingTimeInterval(5)
        while !condition() && Date() < deadline {
            try? await Task.sleep(for: .milliseconds(10))
        }
        return condition()
    }

    @MainActor private static func exercise() async {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("orion-save-safety-\(UUID())")
        let folder = root.appendingPathComponent("photos")
        let archive = root.appendingPathComponent("archive")
        do {
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            try fm.createDirectory(at: archive, withIntermediateDirectories: true)
        } catch { check(false, "temporary folder: \(error)"); return }
        defer { try? fm.removeItem(at: root) }

        let source = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
            .appendingPathComponent("tools/fixtures/mask-invert.dng")
        let a = folder.appendingPathComponent("A.dng")
        let b = folder.appendingPathComponent("B.dng")
        let c = folder.appendingPathComponent("C.dng")
        do { for url in [a, b, c] { try fm.copyItem(at: source, to: url) } }
        catch { check(false, "copy tiny fixture: \(error)"); return }
        check(Sidecar().write(for: a), "seed A sidecar")
        let originalSidecar = try? Data(contentsOf: Sidecar.url(for: a))
        var accepting = false
        var writes: [(URL, Float)] = []
        let save = Autosave(deferral: { _ in }, write: { url, state in
            guard accepting else { return false }
            writes.append((url, state.exposureEv))
            return Autosave.toSidecar(url, state)
        })
        let library = Library(index: PhotoIndex(at: nil), moveToTrash: { url in
            try fm.moveItem(at: url, to: archive.appendingPathComponent(url.lastPathComponent))
        })
        await library.open(folder: folder)
        check(library.photos.count == 3, "fixture listing")
        guard let engine = try? Engine() else { check(false, "engine starts"); return }
        let ready = Ready()
        let editor = Editor(engine: engine, startLibrary: library, startAutosave: save,
                            startPresets: PresetStore(url: nil),
                            startWatermark: Watermark(url: nil),
                            currentFile: root.appendingPathComponent("current.json"),
                            onReady: { ready.editor = $0 })
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1200, height: 800),
                              styleMask: [.borderless], backing: .buffered, defer: false)
        window.contentView = NSHostingView(rootView: editor)
        window.orderFront(nil)
        guard await until({ ready.editor != nil }), let mounted = ready.editor else {
            check(false, "mounted Editor ready without a key window"); return
        }
        defer { window.orderOut(nil); window.contentView = nil }

        func openA() async -> Bool {
            mounted.load(a)
            return await until { mounted.current == a && !engine.isOpening && engine.isLoaded }
        }
        guard await openA() else { check(false, "A ready"); return }

        func editA(_ ev: Float) {
            engine.edit("Exposure") { engine.exposureEv = ev }
        }
        func retained(_ name: String, _ ev: Float) {
            check(mounted.current == a && engine.state.exposureEv == ev
                  && engine.history.position >= 1
                  && engine.history.entries.last?.state.exposureEv == ev
                  && library.selection.current == a && save.isDirty,
                  name + " keeps A, history, selection and pending save")
            check(engine.onEdit != nil, name + " keeps render callback")
            check((try? Data(contentsOf: Sidecar.url(for: a))) == originalSidecar,
                  name + " keeps A sidecar bytes")
        }

        editA(1)
        mounted.load(b)
        retained("direct load", 1)
        if mounted.current != a { guard await openA() else { return } }

        mounted.openPhoto(b)
        retained("Open Photo", 1)
        check(mounted.message == save.lastFailure, "refused Open Photo reports save failure")
        if mounted.current != a { guard await openA() else { return } }

        editA(2)
        Filmstrip(library: library, selected: a, onSelect: mounted.load,
                  onMerge: { _ in }).activate(b, modifiers: [])
        retained("plain filmstrip", 2)
        if mounted.current != a { guard await openA() else { return } }

        editA(3)
        mounted.mode = .cull
        mounted.openFromGallery(b)
        retained("gallery open", 3)
        check(mounted.mode == .cull, "refused gallery open keeps gallery")
        if mounted.current != a { guard await openA() else { return } }

        editA(4)
        mounted.confirmTrash([a, c])
        mounted.runTrash()
        retained("current Trash", 4)
        check([a, c, Sidecar.url(for: a)].allSatisfy { fm.fileExists(atPath: $0.path) }
              && (try? fm.contentsOfDirectory(atPath: archive.path))?.isEmpty == true,
              "refused current Trash moves no requested file or sibling")

        // The scan began before this edit; the final decision must see it.
        let oldPhotos = library.photos.map(\.url)
        let oldSelection = library.selection
        let oldFolder = library.folder
        let oldFailure = library.lastFailure
        var replaced = false
        let refused = await library.open(folder: folder, beforeReplacing: {
            editA(5)
            return mounted.canLeavePhoto()
        }, didReplace: { replaced = true })
        check(!refused && !replaced && library.folder == oldFolder
              && library.photos.map(\.url) == oldPhotos
              && library.selection == oldSelection
              && library.lastFailure == oldFailure && !library.loading,
              "scan refusal keeps listing, selection and callback untouched")
        retained("scan decision", 5)

        accepting = true
        check(save.flushBeforeLeaving(), "recovered storage saves A")
        check(writes.last?.0 == a && writes.last?.1 == 5, "retry lands latest A edit")
        replaced = false
        let committed = await library.open(folder: folder,
                                           beforeReplacing: mounted.canLeavePhoto,
                                           didReplace: { replaced = true; mounted.load(b) })
        check(committed && replaced, "recovered scan commits and calls loader")
        check(await until { mounted.current == b && !engine.isOpening },
              "recovered scan opens B")
        engine.edit("Exposure") { engine.exposureEv = 6 }
        save.flush()
        check(writes.last?.0 == b && writes.last?.1 == 6
              && (try? JSONDecoder().decode(DevelopState.self,
                   from: Sidecar.read(for: a)?.develop ?? Data()))?.exposureEv == 5,
              "B edit saves to B after A retry")

        mounted.confirmTrash([c])
        mounted.runTrash()
        check(!fm.fileExists(atPath: c.path)
              && fm.fileExists(atPath: archive.appendingPathComponent(c.lastPathComponent).path)
              && mounted.current == b, "noncurrent Trash uses private archive")

        let d = folder.appendingPathComponent("D.dng")
        do { try fm.copyItem(at: source, to: d) }
        catch { check(false, "copy late listing fixture: \(error)"); return }
        check(Library.scan(folder, index: library.index).photos.contains(where: { $0.name == d.lastPathComponent }),
              "late D is visible to the existing scanner")
        func opensOfA() -> Int {
            let log = (try? String(contentsOf: InteractionLog.url, encoding: .utf8)) ?? ""
            return log.split(separator: "\n").filter { $0 == "open \(a.path)" }.count
        }
        let opensBefore = opensOfA()
        var editBeforeScan = false
        mounted.openPhoto(a, beforeListingScan: {
            let decoded = await until {
                mounted.current == a && !engine.isOpening
                && engine.history.position == 0
            }
            check(decoded, "chosen A finishes its first decode before scan")
            guard decoded else { return }
            engine.edit("Exposure") { engine.exposureEv = 7 }
            editBeforeScan = save.isDirty && engine.state.exposureEv == 7
        })
        check(mounted.current == a, "Open Photo changes current immediately after recovery")
        check(await until { library.photos.contains(where: { $0.name == d.lastPathComponent }) },
              "Open Photo commits the later listing")
        check(await until { !engine.isOpening },
              "Open Photo is settled after the listing")
        check(editBeforeScan && save.isDirty && engine.state.exposureEv == 7
              && engine.history.entries.last?.state.exposureEv == 7
              && library.selection.current == a,
              "edit made after A decode survives its listing commit")
        check((try? JSONDecoder().decode(DevelopState.self,
               from: Sidecar.read(for: a)?.develop ?? Data()))?.exposureEv == 5,
              "listing leaves A's pending edit off disk")
        check(opensOfA() == opensBefore + 1,
              "Open Photo decodes A exactly once across the listing")
        print("save-safety: \(failures) failure(s)")
    }
}

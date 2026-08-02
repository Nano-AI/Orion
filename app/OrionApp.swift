// The entry, and what the shell holds.
//
// ⚠ **A command-line mode is added here and nowhere else.** `OrionApp.init`
// below is the whole list of them, and it runs before the scene is built, which
// is the only reason `--screenshot` can render without a window ever appearing.
// The order inside it is load-bearing — see the comments there.
//
// Everything else in this file is here because Swift put it here rather than
// because it belongs together: `@State` are *stored* properties, and stored
// properties are banned from extensions outright, so `Editor`'s three dozen of
// them and the `init` that seeds four cannot move to a `+` file the way its
// views did. What is left is therefore the values, the `body` that arranges
// them, and the lifecycle pair `body` installs and takes back.
//
// The rest of `Editor` is by region, one file each, and the region is what a
// change is *about*:
//
//   OrionApp+Commands.swift   what a key press does — the menus and the monitor
//   OrionApp+Chrome.swift     the frame: toolbar, tab strip, footer
//   OrionApp+Canvas.swift     the picture and the overlays on it
//   OrionApp+Tools.swift      the panel column
//   OrionApp+Files.swift      everything that touches a file on disk
//   DevelopPanels+<Tab>.swift the tool panels themselves, one file per tab
//
// ⚠ The cost is the same one `Engine.swift` paid in #117 — Swift's `private` is
// file-scoped, so seventeen declarations that only this file had any business
// touching are now internal to the app target and rest on convention: five
// stored properties (marked below), eleven views and methods, and
// `PhotoCommands`, which was a private type and is now merely an internal one.
// There is no way to split a Swift type across files without paying this.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

@main
struct OrionApp: App {
    /// `--screenshot` renders the interface to a PNG and exits without ever
    /// showing a window. Checked here because App.init runs before the scene.
    init() {
        // A scripted interaction, checked into repro/ — see app/Scenario.swift.
        // Ahead of --screenshot because a scenario writes its own stills.
        if let script = Scenario.path(CommandLine.arguments) {
            Scenario.run(script)
        }
        if let options = Screenshot.options(CommandLine.arguments) {
            Screenshot.run(options)
        }
        // A real batch, so the feature is run rather than only unit-tested.
        if CommandLine.arguments.contains("--batch-export") {
            BatchExport.runCommandLine(CommandLine.arguments)
        }
        // A folder open, cold then warm then indexless, through the product's
        // own Library — the only thing that can see whether the index is wired
        // in at all.
        if let folder = LibraryProbe.wanted(CommandLine.arguments) {
            LibraryProbe.run(folder)
        }
    }

    var body: some Scene {
        WindowGroup("Orion") {
            RootView().frame(minWidth: 1100, minHeight: 700)
        }
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
            PhotoCommands()
        }
    }
}

// The palette is `design/tokens.json`, generated into
// `Sources/OrionUI/DesignTokens.swift`. This is an alias, not a copy: the file
// that used to sit here was a hand-kept mirror, and it had already drifted —
// different names, a token dropped, two colors invented, and every value built
// in sRGB against a generator that emitted Display P3.
//
// Two names differ from the token file and are mapped rather than renamed in
// either direction: the tokens describe what the color *is* (`reject`,
// `star`), the app reads better saying what the thing *does*.
typealias Palette = Orion.Palette

extension Orion.Palette {
    static let rejected = reject
    static let rated    = star
}

struct RootView: View {
    @State private var engine: Engine?
    @State private var startupError: String?

    var body: some View {
        Group {
            if let engine {
                Editor(engine: engine)
            } else {
                VStack(spacing: 8) {
                    Text("Orion could not start").font(.headline)
                    Text(startupError ?? "Unknown error")
                        .font(.callout).foregroundStyle(Palette.dim)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Palette.ground)
            }
        }
        .task {
            do { engine = try Engine() }
            catch { startupError = error.localizedDescription }
        }
        .preferredColorScheme(.dark)
    }
}

struct Editor: View {
    @Bindable var engine: Engine
    /// ⚠ Both widened from `private` by the split. `viewport` is read by the
    /// canvas, the toolbar's zoom readout and every key that changes the zoom;
    /// `tab` decides which overlay is live, which panel is drawn and what
    /// Return and Escape mean. They are the two values every region consults.
    @State var viewport = Viewport()
    @State var tab: ToolTab

    /// `startLibrary` exists for the screenshot harness. The filmstrip has been
    /// changed twice without any capture showing it, because the harness opens
    /// one photo and never scans a folder — so the strip appeared in no
    /// screenshot at all and was checked by reading the code. Handing in a
    /// pre-scanned library is the smallest seam that fixes that.
    init(engine: Engine, startTab: ToolTab = .light, startLibrary: Library? = nil,
         startSnapshots: SnapshotStore? = nil) {
        self.engine = engine
        _tab = State(initialValue: startTab)
        _library = State(initialValue: startLibrary ?? Library())
        // Same seam and same reason as `startLibrary`: the harness never calls
        // `load`, so a per-photograph list would be empty in every capture and
        // the rows would be reviewed by reading them.
        _snapshots = State(initialValue: startSnapshots ?? SnapshotStore())
    }

    @State var band: HueBand = .blue

    /// Saved looks. research is not needed for these — see Presets.swift.
    @State var presets = PresetStore()
    @State var presetName = ""
    @State var presetGroups: Set<PresetGroup> = PresetGroup.defaultSelection

    /// Saved versions of *this* photograph's edit — see Snapshots.swift. Held
    /// beside `presets` and retargeted in `load`, because a version list
    /// belongs to one photograph the way a look belongs to none.
    @State var snapshots: SnapshotStore
    @State var snapshotName = ""
    /// The version being renamed in place, and the text being typed into it.
    @State var renamingSnapshot: UUID?
    @State var renameText = ""

    /// The settings on the clipboard, if any. In memory only — a copied look is
    /// not worth persisting across a launch, and pretending otherwise would
    /// mean deciding what happens when the photograph it came from is gone.
    @State var copied: DevelopState?
    @State var confirmingSync = false
    /// How many sidecars the last sync wrote, for the footer message.
    @State var syncedCount = 0

    /// Batch export progress: how many done of how many, and a cancel flag the
    /// running loop polls. Nil when nothing is running.
    @State var batchProgress: (done: Int, total: Int)?
    @State var batchCancelled = false

    /// Set while a segmentation model is running, so the two buttons can say so
    /// and cannot be pressed twice. research/masking.md §5.
    @State var matteRunning = false
    @State var targeted = TargetedAdjust()
    @State var message: String?
    /// ⚠ Widened from `private` by the split: the export panel is opened from
    /// the toolbar (`+Chrome`) and the Photo menu (`+Commands`), and the
    /// settings are read by both `exportFile` and `runBatchExport` (`+Files`).
    @State var exportSettings = ExportSettings()
    @State var showingExport = false
    @State var library = Library()
    /// Not `private`: `findMatte` in `DevelopPanels+Mask.swift` needs it, because a
    /// matte is saved beside the photograph and so cannot be written without
    /// knowing which photograph is open.
    @State var current: URL?
    /// ⚠ Widened from `private` by the split: installed in `+Commands`, taken
    /// back by `teardown` below.
    @State var keyMonitor: Any?

    /// Edits reach the sidecar on their own — see `Autosave`. Held here rather
    /// than in `Engine` because it is the shell that knows which file is open.
    @State var autosave = Autosave()
    @State private var lifecycleObservers: [NSObjectProtocol] = []

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Rectangle().fill(Palette.line).frame(height: 1)

            HStack(spacing: 0) {
                canvas
                Rectangle().fill(Palette.line).frame(width: 1)
                tools.frame(width: 364)
            }

            if !library.photos.isEmpty {
                Filmstrip(library: library, selected: current, onSelect: load)
            }
        }
        .background(Palette.ground)
        .focusable()
        .onKeyPress(.init("z"), phases: .down) { press in
            guard press.modifiers.contains(.command) else { return .ignored }
            press.modifiers.contains(.shift) ? engine.redo() : engine.undo()
            return .handled
        }
        // Culling, rating, stepping and zoom live in the Photo and View menus.
        // Their shortcuts go through the responder chain, so they keep working
        // once the Metal canvas has taken first responder — which is what a
        // bare onKeyPress here could not do.
        .focusedSceneValue(\.cull, cullActions)
        .onAppear { installKeyMonitor(); installAutosave() }
        .onDisappear { teardown() }
        .sheet(isPresented: $showingExport) {
            ExportPanel(settings: exportSettings,
                        sourceWidth: engine.imageWidth,
                        sourceHeight: engine.imageHeight,
                        measure: {
                            engine.exportedSize(
                                format: exportSettings.format.code,
                                quality: Float(exportSettings.quality),
                                maxDimension: exportSettings.longestEdge(
                                    sourceWidth: engine.imageWidth,
                                    sourceHeight: engine.imageHeight),
                                space: exportSettings.space.code,
                                depth: exportSettings.effectiveDepth.rawValue,
                                sharpen: exportSettings.sharpening.rawValue)
                        },
                        onExport: { showingExport = false; exportFile() },
                        onCancel: { showingExport = false })
        }
        .alert("Something went wrong",
               isPresented: Binding(get: { message != nil },
                                    set: { if !$0 { message = nil } })) {
            Button("OK") { message = nil }
        } message: {
            Text(message ?? "")
        }
        // ⚠ Confirmed, and the count is in the question. Sync writes a sidecar
        // for every photograph in view — dozens of files, on disk, without
        // opening any of them — and there is no undo across photographs. The
        // groups are named too, because "sync settings" is exactly the phrase
        // that hides which settings.
        .alert("Sync to \(library.photos.count) photos?",
               isPresented: $confirmingSync) {
            Button("Cancel", role: .cancel) { }
            Button("Sync", role: .destructive) { runSync() }
        } message: {
            Text(syncWarning)
        }
    }

    /// Edits reach disk on their own from here on.
    ///
    /// Three triggers, because they fail differently. Coalesced writes cover
    /// the crash and the logout; `willTerminate` covers the ⌘Q that arrives
    /// inside the coalescing window; resigning active shortens the window
    /// before a force-quit. Only the first is a policy — the other two are just
    /// "write now", which is why `Autosave` owns no AppKit.
    private func installAutosave() {
        guard lifecycleObservers.isEmpty else { return }

        engine.onEdit = { [autosave] state in autosave.note(state) }

        let center = NotificationCenter.default
        lifecycleObservers = [
            NSApplication.willTerminateNotification,
            NSApplication.willResignActiveNotification,
        ].map { name in
            center.addObserver(forName: name, object: nil, queue: .main) { [autosave] _ in
                autosave.flush()
            }
        }
    }

    /// The key monitor and the lifecycle observers both outlive the view unless
    /// they are taken back. Flushing here as well covers the window being
    /// closed without the app quitting.
    private func teardown() {
        autosave.flush()
        engine.onEdit = nil

        lifecycleObservers.forEach(NotificationCenter.default.removeObserver)
        lifecycleObservers = []

        if let keyMonitor {
            NSEvent.removeMonitor(keyMonitor)
            self.keyMonitor = nil
        }
    }
}

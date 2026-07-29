import SwiftUI
import UniformTypeIdentifiers

@main
struct OrionApp: App {
    /// `--screenshot` renders the interface to a PNG and exits without ever
    /// showing a window. Checked here because App.init runs before the scene.
    init() {
        if let options = Screenshot.options(CommandLine.arguments) {
            Screenshot.run(options)
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

/// Culling, as menu commands.
///
/// These were bare `onKeyPress` handlers on the editor's root view, which only
/// fire while SwiftUI holds focus — and the Metal canvas takes first responder
/// on any click, so rejecting stopped working the moment you touched the photo.
/// A menu command routes through the responder chain regardless, and has the
/// side benefit of being findable: the shortcut is written next to its name
/// instead of having to be known in advance.
private struct PhotoCommands: Commands {
    @FocusedValue(\.cull) private var cull

    /// Nothing open means nothing to cull.
    private var idle: Bool { cull?.url == nil }

    var body: some Commands {
        CommandGroup(after: .sidebar) {
            ForEach(Array(ToolTab.allCases.enumerated()), id: \.element) { index, t in
                Button(t.title) { cull?.selectTab(t) }
                    .keyboardShortcut(KeyEquivalent(Character("\(index + 1)")),
                                      modifiers: [.command])
                    .disabled(idle)
            }

            Divider()

            Button(cull?.comparing == true ? "Hide Original  (\\)" : "Compare Original  (\\)") {
                cull?.toggleCompare()
            }
            .disabled(idle)

            Divider()

            Button("Fit in Window  (0)") { cull?.fit() }
                .keyboardShortcut("0", modifiers: [.command])
                .disabled(idle)
            Button("Actual Size  (9)") { cull?.actualSize() }
                .keyboardShortcut("9", modifiers: [.command])
                .disabled(idle)
        }

        CommandGroup(after: .importExport) {
            Button("Export…") { cull?.export() }
                .keyboardShortcut("e", modifiers: [.command])
                .disabled(idle)
        }

        CommandGroup(after: .undoRedo) {
            Button("Reset Adjustments") { cull?.resetEdits() }
                .keyboardShortcut("r", modifiers: [.command])
                .disabled(idle)
        }

        CommandMenu("Photo") {
            // The bare keys are handled by the canvas, which only has them
            // while it is first responder. As menu key equivalents they fired
            // in every window, including the Open panel — where "r" and the
            // digits ate the type-ahead and Return and Escape took the
            // buttons, so a raw file could not be opened at all.
            Button(cull?.isRejected == true ? "Keep  (R)" : "Reject  (R)") {
                cull?.toggleReject()
            }
            .disabled(idle)

            Divider()

            ForEach(1...5, id: \.self) { n in
                Button("\(n) Star\(n == 1 ? "" : "s")  (\(n))") { cull?.rate(n) }
                    .disabled(idle)
            }
            Button("No Rating  (`)") { cull?.rate(0) }
                .disabled(idle)

            Divider()

            Button("Next Photo  (→)") { cull?.step(1) }
                .keyboardShortcut(.rightArrow, modifiers: [.command])
                .disabled(idle)
            Button("Previous Photo  (←)") { cull?.step(-1) }
                .keyboardShortcut(.leftArrow, modifiers: [.command])
                .disabled(idle)

            Divider()

            Button("Apply Crop  (⏎)") { cull?.applyCrop() }
                .disabled(cull?.cropping != true)
            Button("Cancel Crop  (⎋)") { cull?.cancelCrop() }
                .disabled(cull?.cropping != true)
        }
    }
}

/// What the Photo menu acts on. Equatable by state rather than by closure, so
/// SwiftUI can tell when the menu's titles need to change.
struct CullActions: Equatable {
    /// nil when nothing is open, which is what grays the menu out.
    let url: URL?
    let isRejected: Bool
    let rating: Int
    let toggleReject: () -> Void
    let rate: (Int) -> Void
    let step: (Int) -> Void
    let fit: () -> Void
    let actualSize: () -> Void
    let resetEdits: () -> Void

    /// True while the crop tool is open, which is when Apply and Cancel mean
    /// anything.
    let cropping: Bool
    let applyCrop: () -> Void
    let cancelCrop: () -> Void

    /// Which tool panel is showing, and how to change it. On the menu so the
    /// four panels have shortcuts, and so a keyboard user can reach the tools
    /// without knowing that the tab strip exists.
    let tab: ToolTab
    let selectTab: (ToolTab) -> Void

    let comparing: Bool
    let toggleCompare: () -> Void
    let export: () -> Void

    static func == (a: CullActions, b: CullActions) -> Bool {
        a.url == b.url && a.isRejected == b.isRejected && a.rating == b.rating
            && a.cropping == b.cropping && a.tab == b.tab && a.comparing == b.comparing
    }
}

private struct CullActionsKey: FocusedValueKey {
    typealias Value = CullActions
}

extension FocusedValues {
    var cull: CullActions? {
        get { self[CullActionsKey.self] }
        set { self[CullActionsKey.self] = newValue }
    }
}

// The palette is `design/tokens.json`, generated into
// `Sources/OrionUI/DesignTokens.swift`. This is an alias, not a copy: the file
// that used to sit here was a hand-kept mirror, and it had already drifted —
// different names, a token dropped, two colours invented, and every value built
// in sRGB against a generator that emitted Display P3.
//
// Two names differ from the token file and are mapped rather than renamed in
// either direction: the tokens describe what the colour *is* (`reject`,
// `star`), the app reads better saying what the thing *does*.
typealias Palette = Orion.Palette

extension Orion.Palette {
    static let rejected = reject
    static let rated    = star
}

enum ToolTab: String, CaseIterable, Identifiable {
    case light, color, detail, crop
    var id: String { rawValue }

    var title: String {
        switch self {
        case .light:  "Light"
        case .color: "Color"
        case .detail: "Detail"
        case .crop:   "Crop"
        }
    }

    var symbol: String {
        switch self {
        case .light:  "sun.max"
        case .color: "circle.lefthalf.filled"
        case .detail: "magnifyingglass"
        case .crop:   "crop"
        }
    }
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
    @State private var viewport = Viewport()
    @State private var tab: ToolTab

    /// `startLibrary` exists for the screenshot harness. The filmstrip has been
    /// changed twice without any capture showing it, because the harness opens
    /// one photo and never scans a folder — so the strip appeared in no
    /// screenshot at all and was checked by reading the code. Handing in a
    /// pre-scanned library is the smallest seam that fixes that.
    init(engine: Engine, startTab: ToolTab = .light, startLibrary: Library? = nil) {
        self.engine = engine
        _tab = State(initialValue: startTab)
        _library = State(initialValue: startLibrary ?? Library())
    }

    @State var band: HueBand = .blue
    @State var targeted = TargetedAdjust()
    @State private var message: String?
    @State private var exportSettings = ExportSettings()
    @State private var showingExport = false
    @State private var library = Library()
    @State private var current: URL?
    @State private var keyMonitor: Any?

    /// Edits reach the sidecar on their own — see `Autosave`. Held here rather
    /// than in `Engine` because it is the shell that knows which file is open.
    @State private var autosave = Autosave()
    @State private var lifecycleObservers: [NSObjectProtocol] = []

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Rectangle().fill(Palette.line).frame(height: 1)

            HStack(spacing: 0) {
                canvas
                Rectangle().fill(Palette.line).frame(width: 1)
                tools.frame(width: 322)
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
                                space: exportSettings.space.code)
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
    }

    // MARK: Toolbar

    /// One height for every control in the toolbar.
    ///
    /// They used to size themselves: the icon chips carried an explicit 22,
    /// the text chips grew out of their padding, and the Open menu added
    /// whatever `.borderlessButton` felt like on top of that. Three answers,
    /// three heights, and Open visibly taller than the buttons beside it.
    private static let chipHeight: CGFloat = 23

    /// The toolbar reads left to right in the order the work happens: what you
    /// open, what you are looking at, what you do to it, and what comes out.
    /// Open used to sit between the rotate buttons and Reset, which put the
    /// start of the workflow in the middle of the middle of it.
    private var toolbar: some View {
        HStack(spacing: 12) {
            // Serif wordmark against a sans interface. The instrument is
            // sans because it is read at a glance; the name is serif because
            // it is read once. Both faces ship with macOS.
            Text("Orion")
                .font(.system(size: 17, weight: .regular, design: .serif))
                .tracking(0.5)
                .foregroundStyle(Palette.text)

            Menu {
                Button("Open Photo…") { openFile() }
                Button("Open Folder…") { openFolder() }
            } label: {
                HStack(spacing: 5) {
                    Image(systemName: "folder").font(.system(size: 11))
                    Text("Open").font(.system(size: 11))
                    Image(systemName: "chevron.down").font(.system(size: 8, weight: .semibold))
                }
                .padding(.horizontal, 9)
                .frame(height: Self.chipHeight)
                .contentShape(Rectangle())
            }
            .menuStyle(.borderlessButton)
            .menuIndicator(.hidden)
            .fixedSize()
            .foregroundStyle(Palette.text)
            .background(Palette.raised, in: RoundedRectangle(cornerRadius: 5))
            .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.line, lineWidth: 1))
            .help("Open a photo or a folder")

            Text(engine.isLoaded ? engine.camera : "")
                .font(.system(size: 12))
                .foregroundStyle(Palette.dim)
                .frame(maxWidth: .infinity)

            // ── Looking ──────────────────────────────────────────────────
            HStack(spacing: 7) {
                if engine.isLoaded {
                    Text("\(viewport.percent)%")
                        .font(.system(size: 11)).monospacedDigit()
                        .foregroundStyle(Palette.faint)
                        .frame(width: 42, alignment: .trailing)
                }
                Button {
                    engine.comparing ? engine.clearCompare()
                                     : engine.setCompare(split: 0.5)
                } label: {
                    Text("Compare")
                        .font(.system(size: 11))
                        .padding(.horizontal, 9)
                        .frame(height: Self.chipHeight)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .foregroundStyle(engine.comparing ? Palette.accent : Palette.dim)
                .overlay(RoundedRectangle(cornerRadius: 5)
                    .stroke(engine.comparing ? Palette.accent : Palette.line, lineWidth: 1))
                .disabled(!engine.isLoaded)
                .help("Split the view against the original")

                if engine.comparing {
                    iconChip(engine.compareVertical
                             ? "rectangle.split.2x1" : "rectangle.split.1x2",
                             enabled: true) {
                        engine.compareVertical.toggle()
                        engine.generationBump()
                    }
                    .help("Swap between a vertical and horizontal split")
                }
            }

            divider

            // ── Editing ──────────────────────────────────────────────────
            HStack(spacing: 7) {
                iconChip("arrow.uturn.backward", enabled: engine.history.canUndo) {
                    engine.undo()
                }
                .help(engine.history.undoLabel.map { "Undo \($0)" } ?? "Undo")

                iconChip("arrow.uturn.forward", enabled: engine.history.canRedo) {
                    engine.redo()
                }
                .help(engine.history.redoLabel.map { "Redo \($0)" } ?? "Redo")

                iconChip("rotate.left", enabled: engine.isLoaded) {
                    engine.edit("Rotate") { engine.rotate(-1) }; viewport.reset()
                }
                .help("Rotate left")
                iconChip("rotate.right", enabled: engine.isLoaded) {
                    engine.edit("Rotate") { engine.rotate(1) }; viewport.reset()
                }
                .help("Rotate right")

                chip("Reset", enabled: engine.isLoaded) { engine.resetEdits() }
                    .help("Put every adjustment back")
            }

            divider

            // ── Out ──────────────────────────────────────────────────────
            Button {
                showingExport = true
            } label: {
                Text("Export…")
                    .font(.system(size: 11, weight: .medium))
                    .padding(.horizontal, 11)
                    .frame(height: Self.chipHeight)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .foregroundStyle(engine.isLoaded ? Palette.text : Palette.faint)
            .background(engine.isLoaded ? Palette.accent.opacity(0.18) : .clear,
                        in: RoundedRectangle(cornerRadius: 5))
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(engine.isLoaded ? Palette.accent.opacity(0.55) : Palette.line,
                        lineWidth: 1))
            .disabled(!engine.isLoaded)
        }
        .padding(.horizontal, 14)
        .frame(height: 44)
        .background(Palette.panel)
    }

    /// Separates the toolbar's three groups. A hairline rather than a gap:
    /// spacing alone reads as a rhythm, a rule reads as a boundary.
    private var divider: some View {
        Rectangle()
            .fill(Palette.line)
            .frame(width: 1, height: 18)
    }

    private func iconChip(_ symbol: String, enabled: Bool,
                          action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 12))
                .frame(width: 26, height: Self.chipHeight)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .foregroundStyle(enabled ? Palette.dim : Palette.faint)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.line, lineWidth: 1))
        .disabled(!enabled)
    }

    private func chip(_ title: String, enabled: Bool,
                      action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11))
                .padding(.horizontal, 9)
                .frame(height: Self.chipHeight)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .foregroundStyle(enabled ? Palette.dim : Palette.faint)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.line, lineWidth: 1))
        .disabled(!enabled)
    }

    // MARK: Canvas

    private var canvas: some View {
        GeometryReader { geo in
            ZStack(alignment: .bottomLeading) {
                Palette.surround

                if engine.isLoaded {
                    ImageCanvas(engine: engine, viewport: viewport,
                                targeted: targeted, generation: engine.generation)
                        // Held while a new photo decodes, and what the
                        // screenshot harness draws into — AppKit cannot capture
                        // a Metal layer, so the canvas has to be a still there.
                        // First in the chain, so every overlay below is drawn
                        // over it rather than hidden by it.
                        .overlay {
                            if let still = engine.placeholder {
                                Image(nsImage: still)
                                    .resizable()
                                    .interpolation(.high)
                                    .aspectRatio(contentMode: .fit)
                                    .allowsHitTesting(false)
                            }
                        }
                        // The crop overlay goes on before the padding, so its
                        // coordinate space is the Metal view's exactly. Applied
                        // after, it measured the padded box and every handle
                        // sat a few points off the pixels.
                        // The divider and its labels. Written weeks ago and
                        // never placed in the view, which is why compare had no
                        // handle to drag and nothing naming the two sides — the
                        // split itself was happening in the shader all along.
                        .overlay {
                            if engine.comparing {
                                GeometryReader { canvasGeo in
                                    CompareOverlay(engine: engine,
                                                   frame: drawnFrame(in: canvasGeo.size))
                                }
                            }
                        }
                        .overlay {
                            if tab == .crop {
                                GeometryReader { canvasGeo in
                                    CropOverlay(engine: engine,
                                                frame: photoFrame(in: canvasGeo.size),
                                                bounds: canvasGeo.size)
                                }
                                .clipped()
                            }
                        }
                        .padding(20)
                        .onChange(of: tab) { _, t in
                            engine.cropPreview = (t == .crop)
                            viewport.locked = (t == .crop)

                            // Anything armed and waiting for a click on the
                            // photo belongs to the panel that armed it. Leaving
                            // that panel with the color picker still live means
                            // the next click on the picture does something the
                            // visible controls no longer explain.
                            targeted.isActive = false
                            targeted.clearHover()

                            // Compare holds a copy of the unedited render at the
                            // current geometry. Cropping changes that geometry
                            // under it, so the two halves stop being the same
                            // picture.
                            if t == .crop { engine.clearCompare() }
                        }
                        .onAppear {
                            engine.cropPreview = (tab == .crop)
                            viewport.locked = (tab == .crop)
                        }
                        .overlay { ColorLoupe(targeted: targeted) }
                        .onChange(of: targeted.lastPicked) { _, picked in
                            // Follow the pick, so the sliders below act on the
                            // band you just clicked rather than a stale one.
                            if let picked { band = picked }
                        }

                    HStack(alignment: .bottom, spacing: 10) {
                        if !viewport.isFit {
                            Navigator(imageWidth: engine.imageWidth,
                                      imageHeight: engine.imageHeight,
                                      viewport: viewport,
                                      viewAspect: geo.size.width / max(geo.size.height, 1))
                        }
                        Text(hint)
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
                            .padding(.horizontal, 8).padding(.vertical, 4)
                            .background(Color.black.opacity(0.45),
                                        in: RoundedRectangle(cornerRadius: 4))

                        Spacer(minLength: 10)

                        // Culling happens while looking at the picture, so the
                        // mark you are setting belongs over it — not only as
                        // four-pixel dots on a thumbnail you are not looking at.
                        if let current,
                           let photo = library.photos.first(where: { $0.url == current }) {
                            RatingBar(rating: photo.rating,
                                      rejected: photo.rejected,
                                      rate: { library.setRating($0, for: current) },
                                      toggleReject: { library.toggleRejected(current) })
                        }
                    }
                    .padding(14)
                } else {
                    VStack(spacing: 0) {
                        Text("Orion")
                            .font(.system(size: 52, weight: .regular, design: .serif))
                            .foregroundStyle(Palette.text)

                        Text("A darkroom for raw files.")
                            .font(.system(size: 13, design: .serif))
                            .italic()
                            .foregroundStyle(Palette.dim)
                            .padding(.top, 6)

                        Button("Open a raw file") { openFile() }
                            .buttonStyle(.plain)
                            .font(.system(size: 12))
                            .foregroundStyle(Palette.accent)
                            .padding(.horizontal, 16)
                            .padding(.vertical, 7)
                            .overlay(RoundedRectangle(cornerRadius: 5)
                                .stroke(Palette.accent.opacity(0.5), lineWidth: 1))
                            .padding(.top, 28)

                        Text("ARW, DNG, NEF, CR2, CR3, RAF, ORF, RW2 — Sony is the tested one.")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
                            .padding(.top, 14)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    /// The photo's rectangle within the canvas, from the same function the
    /// renderer uses, so the overlay lands on the pixels rather than near them.
    private func photoFrame(in size: CGSize) -> CGRect {
        guard engine.imageWidth > 0, engine.imageHeight > 0 else { return .zero }
        return CanvasLayout.frameRect(
            imageAspect: CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight),
            in: size)
    }

    /// Where the picture is on screen right now, zoom included.
    ///
    /// The compare divider lives in view space — it stays put while the image
    /// pans under it — so it has to be placed against the rectangle the picture
    /// actually covers, not the one it covers at fit. Zoomed in, those are not
    /// the same rectangle, and the divider drawn on one while the split happens
    /// on the other is the whole of the bug.
    private func drawnFrame(in size: CGSize) -> CGRect {
        guard engine.imageWidth > 0, engine.imageHeight > 0, size.height > 0 else {
            return .zero
        }
        let quad = viewport.quadScale(
            imageAspect: CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight),
            viewAspect: size.width / size.height)
        return CanvasLayout.drawnRect(quadScale: quad, in: size)
    }

    private var hint: String {
        if tab == .crop { return "drag the rectangle or its corners" }
        if !library.photos.isEmpty {
            return viewport.isFit
                ? "← → to browse · 1–5 rates · R rejects"
                : "drag to pan · right-click to fit"
        }
        return viewport.isFit
            ? "scroll or pinch to zoom · right-click to fit"
            : "drag to pan · right-click to fit"
    }

    // MARK: Tools

    private var tools: some View {
        VStack(spacing: 0) {
            if engine.isLoaded && !engine.histogramBins.isEmpty {
                Histogram(bins: engine.histogramBins, height: 84)
                    .padding(.horizontal, 14)
                    .padding(.top, 12)
                    .padding(.bottom, 6)

                HStack {
                    Text("shadows").tracking(0.4)
                    Spacer()
                    Text("midtones").tracking(0.4)
                    Spacer()
                    Text("highlights").tracking(0.4)
                }
                .font(.system(size: 9))
                .foregroundStyle(Palette.faint)
                .padding(.horizontal, 14)
                .padding(.bottom, 10)
            }

            tabBar

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    switch tab {
                    case .light:  lightPanel
                    case .color: colorPanel
                    case .detail: detailPanel
                    case .crop:
                        section("Aspect") {
                            // Ratios photographers actually shoot and print to.
                            let ratios: [(String, Float?)] = [
                                ("Original", nil), ("1:1", 1), ("4:5", 0.8),
                                ("3:2", 1.5), ("16:9", 16.0 / 9.0),
                            ]
                            LazyVGrid(columns: Array(repeating: GridItem(.flexible(),
                                                                        spacing: 4),
                                                     count: 3),
                                      spacing: 4) {
                                ForEach(ratios, id: \.0) { name, ratio in
                                    Button {
                                        engine.edit("Aspect") {
                                            if ratio == nil { engine.resetCrop() }
                                            else { engine.setAspect(ratio) }
                                        }
                                    } label: {
                                        // Every modifier here is inside the
                                        // label. A plain button's hit region is
                                        // its label's shape, so padding applied
                                        // outside the label stays inert — which
                                        // is why only the text responded.
                                        Text(name)
                                            .font(.system(size: 10))
                                            .foregroundStyle(Palette.dim)
                                            .frame(maxWidth: .infinity)
                                            .padding(.vertical, 7)
                                            .background(Palette.raised,
                                                        in: RoundedRectangle(cornerRadius: 4))
                                            .contentShape(RoundedRectangle(cornerRadius: 4))
                                    }
                                    .buttonStyle(.plain)
                                }
                            }
                        }

                        section("Straighten") {
                            slider("Angle", $engine.straightenDeg, -90...90, "°", 1, resetsTo: engine.defaults.straightenDeg)
                            Text("Turns the picture about the frame's center. "
                                 + "The rectangle shrinks to stay inside the "
                                 + "turned frame, so a corner is never empty.")
                                .font(.system(size: 10))
                                .foregroundStyle(Palette.faint)
                                .fixedSize(horizontal: false, vertical: true)
                        }

                        section("Rotate") {
                            HStack(spacing: 8) {
                                Button {
                                    engine.rotate(-1); viewport.reset()
                                } label: {
                                    Label("Left", systemImage: "rotate.left")
                                        .font(.system(size: 11))
                                }
                                Button {
                                    engine.rotate(1); viewport.reset()
                                } label: {
                                    Label("Right", systemImage: "rotate.right")
                                        .font(.system(size: 11))
                                }
                            }
                            .buttonStyle(.bordered)
                            .controlSize(.small)

                            Text("The camera's own orientation is applied automatically; "
                                 + "this rotates on top of it.")
                                .font(.system(size: 10))
                                .foregroundStyle(Palette.faint)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                        Button("Reset crop") { engine.edit("Crop") { engine.resetCrop() } }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                    }
                }
                .padding(14)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            footer
        }
        .background(Palette.panel)
        .disabled(!engine.isLoaded)
    }

    /// File-folder tabs: the selected one is filled with the panel's own color
    /// and covers the strip's rule, so tab and panel read as one surface.
    /// Tabs, and they stay tabs.
    ///
    /// A camera-style mode dial was built here and thrown away: the knurled rim
    /// and the 3D turn looked like a costume on a control that has one job, and
    /// the perspective could not be checked by the screenshot suite at all
    /// (`cacheDisplay` drops 3D transforms). A tab that reads instantly beats a
    /// tab with a story.
    private var tabBar: some View {
        HStack(alignment: .bottom, spacing: 2) {
            ForEach(ToolTab.allCases) { t in
                let selected = t == tab
                Button { withAnimation(.easeOut(duration: 0.16)) { tab = t } } label: {
                    HStack(spacing: 6) {
                        Image(systemName: t.symbol).font(.system(size: 12))
                        if selected {
                            Text(t.title).font(.system(size: 11)).tracking(0.6)
                        }
                    }
                    .frame(maxWidth: selected ? .infinity : 40)
                    .frame(height: selected ? 32 : 27)
                    .background(selected ? Palette.panel : Palette.raised)
                    .foregroundStyle(selected ? Palette.accent : Palette.faint)
                    .clipShape(UnevenRoundedRectangle(topLeadingRadius: 5,
                                                      topTrailingRadius: 5))
                    .overlay(
                        UnevenRoundedRectangle(topLeadingRadius: 5, topTrailingRadius: 5)
                            .stroke(Palette.line, lineWidth: 1)
                    )
                }
                .buttonStyle(.plain)
                .help(t.title)
            }
        }
        .padding(.horizontal, 14)
        .padding(.top, 6)
        .background(Palette.ground)
    }


    private var footer: some View {
        HStack {
            if engine.isLoaded {
                Text("\(engine.imageWidth) × \(engine.imageHeight)")
                Spacer()
                Text(String(format: "%.1f ms", engine.lastRenderMs))
                    .monospacedDigit()
                    .foregroundStyle(engine.lastRenderMs < 16 ? Palette.accent : .orange)
            }
        }
        .font(.system(size: 10))
        .foregroundStyle(Palette.dim)
        .padding(.horizontal, 14).padding(.vertical, 8)
        .overlay(alignment: .top) { Rectangle().fill(Palette.line).frame(height: 1) }
    }

    // MARK: Pieces

    func bandBinding(_ key: ReferenceWritableKeyPath<Engine, [Float]>)
        -> Binding<Float> {
        Binding(get: { engine[keyPath: key][band.rawValue] },
                set: { engine[keyPath: key][band.rawValue] = $0 })
    }

    func section<Content: View>(_ title: String,
                                        @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 11) {
            Text(title.uppercased())
                .font(.system(size: 11, weight: .semibold))
                .tracking(0.8)
                .foregroundStyle(Palette.text)
            content()
        }
    }

    /// `resetsTo` is the value the control returns to for *this* photo, which
    /// for white balance is the camera's own reading rather than a constant.
    /// It is spelled out at every call site on purpose: a control whose reset
    /// silently disagreed with its binding would put the wrong number back, and
    /// nothing else in the app would notice.
    func slider(_ name: String, _ value: Binding<Float>,
                        _ range: ClosedRange<Float>, _ unit: String,
                        _ decimals: Int, resetsTo base: Float) -> some View {
        AdjustmentSlider(name: name, value: value, range: range, unit: unit,
                         decimals: decimals, base: base, engine: engine)
    }

    // MARK: Actions

    /// Opening one photo still scans its folder, so the filmstrip is populated
    /// without a separate import step.
    private func openFile() {
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

    private func openFolder() {
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

    private func load(_ url: URL) {
        // The photo being left keeps its edits. Without this, going to the next
        // frame and back threw the work away — which is the whole difference
        // between an editor and a viewer.
        //
        // `stop` writes what is owed *and* disarms: the engine renders several
        // times while a file opens, and until the sidecar has been restored
        // those renders still describe the photo being left.
        autosave.stop()

        current = url

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
                if let saved = Sidecar.read(for: url)?.develop {
                    engine.restore(encoded: saved)
                }
                // Arm only once the photo is settled, with what its sidecar
                // already holds — so opening a file does not write straight
                // back what it just read.
                autosave.begin(url: url, saved: engine.state)
            } catch {
                message = error.localizedDescription
            }
        }
    }

    /// Published to the scene so the Photo and View menus can act on whatever
    /// is open, from wherever focus happens to be.
    private var cullActions: CullActions {
        let photo = current.flatMap { url in library.photos.first { $0.url == url } }
        return CullActions(
            url: current,
            isRejected: photo?.rejected ?? false,
            rating: photo?.rating ?? 0,
            toggleReject: { if let current { library.toggleRejected(current) } },
            rate: { stars in if let current { library.setRating(stars, for: current) } },
            step: { offset in step(offset) },
            fit: { viewport.reset() },
            actualSize: { viewport.toggleFitAndActual() },
            resetEdits: { engine.resetEdits() },
            cropping: tab == .crop,
            // Applying is just leaving the tool: the preview canvas goes away
            // and the engine renders the crop itself.
            applyCrop: { tab = .light },
            cancelCrop: { engine.edit("Crop") { engine.resetCrop() }; tab = .light },
            tab: tab,
            selectTab: { tab = $0 },
            comparing: engine.comparing,
            toggleCompare: {
                engine.comparing ? engine.clearCompare() : engine.setCompare(split: 0.5)
            },
            export: { showingExport = true })
    }

    /// Single keys, seen before AppKit dispatches them.
    ///
    /// Not menu key equivalents: a bare letter or digit on a menu item fires in
    /// any key window, which took `r` and the digits from the Open panel's
    /// type-ahead and Return and Escape from its buttons. Not the canvas's
    /// keyDown either: that only arrives while the canvas is first responder,
    /// and SwiftUI's own focus machinery holds it most of the time, so the keys
    /// simply did nothing.
    ///
    /// A local monitor sees every key event and hands back the ones that belong
    /// to somebody else — a sheet, a panel, a text field, or anything with a
    /// modifier.
    private func installKeyMonitor() {
        guard keyMonitor == nil else { return }
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            guard let window = NSApp.keyWindow, window.sheets.isEmpty,
                  !(window is NSPanel),
                  !(window.firstResponder is NSTextView)
            else { return event }

            return handleKey(event) ? nil : event
        }
    }

    private func handleKey(_ event: NSEvent) -> Bool {
        guard !event.modifierFlags.contains(.command) else { return false }
        let press = (characters: event.charactersIgnoringModifiers ?? "",
                     keyCode: event.keyCode)

        // Escape and Return, by key code: their characters are control
        // sequences that do not compare well.
        switch press.keyCode {
        case 53:   // escape
            guard tab == .crop else { return false }
            engine.edit("Crop") { engine.resetCrop() }
            tab = .light
            return true
        case 36, 76:   // return, enter
            guard tab == .crop else { return false }
            tab = .light
            return true
        case 123:  // left arrow
            step(-1); return true
        case 124:  // right arrow
            step(1); return true
        default:
            break
        }

        guard engine.isLoaded else { return false }

        switch press.characters.lowercased() {
        case "r":
            guard let current else { return false }
            library.toggleRejected(current); return true
        case "1", "2", "3", "4", "5":
            guard let n = Int(press.characters) else { return false }
            rate(n); return true
        case "`":
            rate(0); return true
        case "0":
            viewport.reset(); return true
        case "9":
            viewport.toggleFitAndActual(); return true
        case "\\":
            engine.comparing ? engine.clearCompare() : engine.setCompare(split: 0.5)
            return true
        case "[":
            engine.edit("Rotate") { engine.rotate(-1) }; viewport.reset(); return true
        case "]":
            engine.edit("Rotate") { engine.rotate(1) }; viewport.reset(); return true
        default:
            return false
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

    private func rate(_ stars: Int) {
        guard let current else { return }
        library.setRating(stars, for: current)
    }

    private func step(_ offset: Int) {
        guard let current, let next = library.neighbour(of: current, offset: offset) else {
            return
        }
        load(next)
    }

    private func exportFile() {
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
                              metadata: exportSettings.metadata.rawValue)
        } catch {
            message = error.localizedDescription
        }
    }
}

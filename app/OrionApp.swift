import SwiftUI
import UniformTypeIdentifiers

@main
struct OrionApp: App {
    var body: some Scene {
        WindowGroup("Orion") {
            RootView().frame(minWidth: 1100, minHeight: 700)
        }
        .windowStyle(.hiddenTitleBar)
        .commands { CommandGroup(replacing: .newItem) {} }
    }
}

// Mirrors design/tokens.json. Neutrals are deliberately near-neutral: a tinted
// interface reads as a colour cast and corrupts the judgement the app exists
// to support.
enum Palette {
    static let ground   = Color(red: 0.078, green: 0.078, blue: 0.086)
    static let panel    = Color(red: 0.106, green: 0.106, blue: 0.114)
    static let raised   = Color(red: 0.137, green: 0.137, blue: 0.149)
    static let surround = Color(red: 0.165, green: 0.165, blue: 0.173)
    static let line     = Color(red: 0.192, green: 0.192, blue: 0.208)
    static let text     = Color(red: 0.910, green: 0.910, blue: 0.918)
    static let dim      = Color(red: 0.541, green: 0.541, blue: 0.565)
    static let faint    = Color(red: 0.353, green: 0.353, blue: 0.376)
    static let accent   = Color(red: 0.302, green: 0.714, blue: 0.769)
}

enum ToolTab: String, CaseIterable, Identifiable {
    case light, colour, detail, crop
    var id: String { rawValue }

    var title: String {
        switch self {
        case .light:  "Light"
        case .colour: "Colour"
        case .detail: "Detail"
        case .crop:   "Crop"
        }
    }

    var symbol: String {
        switch self {
        case .light:  "sun.max"
        case .colour: "circle.lefthalf.filled"
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

private struct Editor: View {
    @Bindable var engine: Engine
    @State private var viewport = Viewport()
    @State private var tab: ToolTab = .light
    @State private var band: HueBand = .blue
    @State private var targeted = TargetedAdjust()
    @State private var message: String?
    @State private var exportSettings = ExportSettings()
    @State private var showingExport = false
    @State private var library = Library()
    @State private var current: URL?

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
        // Hold backslash for the original — Lightroom's muscle memory.
        .onKeyPress(.init("\\")) {
            engine.comparing ? engine.clearCompare() : engine.setCompare(split: 0.5)
            return .handled
        }
        .onKeyPress(.leftArrow) { step(-1); return .handled }
        .onKeyPress(.rightArrow) { step(1); return .handled }
        .onKeyPress(.init("x")) {
            guard let current else { return .ignored }
            library.toggleRejected(current); return .handled
        }
        .onKeyPress(.init("0")) { viewport.reset(); return .handled }
        .onKeyPress(.init("1")) {
            if let current, !library.photos.isEmpty { library.setRating(1, for: current) }
            else { viewport.toggleFitAndActual() }
            return .handled
        }
        .onKeyPress(.init("2")) { rate(2); return .handled }
        .onKeyPress(.init("3")) { rate(3); return .handled }
        .onKeyPress(.init("4")) { rate(4); return .handled }
        .onKeyPress(.init("5")) { rate(5); return .handled }
        .onKeyPress(.init("r")) {
            guard engine.isLoaded else { return .ignored }
            engine.resetEdits(); return .handled
        }
        .onKeyPress(.init("[")) {
            guard engine.isLoaded else { return .ignored }
            engine.edit("Rotate") { engine.rotate(-1) }; viewport.reset(); return .handled
        }
        .onKeyPress(.init("]")) {
            guard engine.isLoaded else { return .ignored }
            engine.edit("Rotate") { engine.rotate(1) }; viewport.reset(); return .handled
        }
        .sheet(isPresented: $showingExport) {
            ExportPanel(settings: exportSettings,
                        sourceWidth: engine.imageWidth,
                        sourceHeight: engine.imageHeight,
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

    private var toolbar: some View {
        HStack(spacing: 18) {
            // Serif wordmark against a sans interface. The instrument is
            // sans because it is read at a glance; the name is serif because
            // it is read once. Both faces ship with macOS.
            Text("Orion")
                .font(.system(size: 17, weight: .regular, design: .serif))
                .tracking(0.5)
                .foregroundStyle(Palette.text)

            Text(engine.isLoaded ? engine.camera : "")
                .font(.system(size: 12))
                .foregroundStyle(Palette.dim)
                .frame(maxWidth: .infinity)

            HStack(spacing: 10) {
                if engine.isLoaded {
                    Text("\(viewport.percent)%")
                        .font(.system(size: 11)).monospacedDigit()
                        .foregroundStyle(Palette.faint)
                        .frame(width: 46, alignment: .trailing)
                }
                Button {
                    engine.comparing ? engine.clearCompare()
                                     : engine.setCompare(split: 0.5)
                } label: {
                    Text("Compare")
                        .font(.system(size: 11))
                        .padding(.horizontal, 10).padding(.vertical, 4)
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
                iconChip("rotate.right", enabled: engine.isLoaded) {
                    engine.edit("Rotate") { engine.rotate(1) }; viewport.reset()
                }
                Menu {
                    Button("Open Photo…") { openFile() }
                    Button("Open Folder…") { openFolder() }
                } label: {
                    Text("Open")
                        .font(.system(size: 11))
                        .padding(.horizontal, 10).padding(.vertical, 4)
                        .contentShape(Rectangle())
                }
                .menuStyle(.borderlessButton)
                .menuIndicator(.hidden)
                .fixedSize()
                .foregroundStyle(Palette.dim)
                .overlay(RoundedRectangle(cornerRadius: 5)
                    .stroke(Palette.line, lineWidth: 1))
                chip("Reset", enabled: engine.isLoaded) { engine.resetEdits() }
                chip("Export…", enabled: engine.isLoaded) { showingExport = true }
            }
        }
        .padding(.horizontal, 14)
        .frame(height: 44)
        .background(Palette.panel)
    }

    private func iconChip(_ symbol: String, enabled: Bool,
                          action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 12))
                .frame(width: 26, height: 22)
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
                .padding(.horizontal, 10)
                .padding(.vertical, 4)
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
                        .padding(20)
                        .overlay {
                            if tab == .crop {
                                GeometryReader { canvasGeo in
                                    CropOverlay(engine: engine,
                                                frame: photoFrame(in: canvasGeo.size))
                                }
                            }
                        }
                        .onChange(of: tab) { _, t in
                            engine.cropPreview = (t == .crop)
                            viewport.locked = (t == .crop)
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

                        Text("Sony ARW today. More cameras as they are tested.")
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

    /// The photo's rectangle within the canvas, matching the renderer's
    /// letterbox exactly so the overlay lands on the pixels rather than near
    /// them. Measured inside the padded canvas, which is the same geometry the
    /// Metal view is given.
    private func photoFrame(in size: CGSize) -> CGRect {
        guard engine.imageWidth > 0, engine.imageHeight > 0,
              size.width > 0, size.height > 0 else { return .zero }

        let imageAspect = CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight)
        let viewAspect = size.width / size.height

        var w = size.width, h = size.height
        if imageAspect > viewAspect { h = w / imageAspect } else { w = h * imageAspect }

        // Matches the renderer's crop inset, so the rectangle lands on the
        // pixels rather than near them.
        if engine.cropPreview {
            w *= 0.86
            h *= 0.86
        }

        return CGRect(x: (size.width - w) / 2, y: (size.height - h) / 2,
                      width: w, height: h)
    }

    private var hint: String {
        if tab == .crop { return "drag the rectangle or its corners" }
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
                    case .colour: colourPanel
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
                            slider("Angle", $engine.straightenDeg, -15...15, "°", 1)
                            Text("Rotates about the centre of the crop, so the "
                                 + "composition stays put.")
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

    /// File-folder tabs: the selected one is filled with the panel's own colour
    /// and covers the strip's rule, so tab and panel read as one surface.
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

    private var lightPanel: some View {
        Group {
            section("White Balance") {
                slider("Temperature", $engine.temperatureK, 2000...12000, " K", 0)
                slider("Tint", $engine.tint, -1...1, "", 2)
            }
            section("Light") {
                slider("Exposure", $engine.exposureEv, -5...5, " EV", 2)
                slider("Contrast", $engine.contrast, 0.5...2, "", 2)
                slider("Highlights", $engine.highlights, -1...1, "", 2)
                slider("Shadows", $engine.shadows, -1...1, "", 2)
                slider("Whites", $engine.whites, -1...1, "", 2)
                slider("Blacks", $engine.blacks, -1...1, "", 2)
            }
        }
    }

    private var colourPanel: some View {
        Group {
            section("Presence") {
                slider("Vibrance", $engine.vibrance, -1...1, "", 2)
                slider("Saturation", $engine.saturation, -1...1, "", 2)
            }
            section("Colour Mixer") {
                // Targeted adjustment: click a colour in the photo and drag.
                // Beats guessing which of eight swatches the sky falls into.
                HStack(spacing: 6) {
                    Button {
                        targeted.isActive.toggle()
                        if !targeted.isActive { targeted.clearHover() }
                    } label: {
                        Image(systemName: targeted.isActive
                              ? "scope" : "eyedropper")
                            .font(.system(size: 12))
                            .frame(width: 26, height: 22)
                    }
                    .buttonStyle(.plain)
                    .foregroundStyle(targeted.isActive ? Palette.accent : Palette.dim)
                    .overlay(RoundedRectangle(cornerRadius: 5)
                        .stroke(targeted.isActive ? Palette.accent : Palette.line, lineWidth: 1))
                    .help("Targeted adjustment — drag on the photo")

                    if targeted.isActive {
                        Picker("", selection: $targeted.mode) {
                            ForEach(TargetedAdjust.Mode.allCases) { m in
                                Text(m.title).tag(m)
                            }
                        }
                        .pickerStyle(.segmented)
                        .controlSize(.small)
                        .labelsHidden()
                    } else {
                        Text("Drag on the photo to adjust its colour")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
                    }
                }

                HStack(spacing: 4) {
                    ForEach(HueBand.allCases) { b in
                        Circle()
                            .fill(b.swatch)
                            .frame(width: 19, height: 19)
                            .overlay(Circle().strokeBorder(
                                band == b ? Palette.text : .clear, lineWidth: 1.5))
                            .onTapGesture { band = b }
                            .help(b.name)
                    }
                }
                HStack {
                    Text(band.name).font(.system(size: 11)).foregroundStyle(Palette.dim)
                    if let active = targeted.activeBand {
                        Spacer()
                        Text("adjusting \(active.name)")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.accent)
                    }
                }
                slider("Hue", bandBinding(\.hueShift), -1...1, "", 2)
                slider("Saturation", bandBinding(\.satShift), -1...1, "", 2)
                slider("Luminance", bandBinding(\.lumShift), -1...1, "", 2)
            }
        }
    }

    private var detailPanel: some View {
        section("Sharpening") {
            slider("Amount", $engine.sharpenAmount, 0...2, "", 2)
            slider("Radius", $engine.sharpenRadius, 0.5...3, " px", 1)
            slider("Masking", $engine.sharpenMasking, 0...1, "", 2)
            Text("Masking protects flat areas, where noise lives and detail does not.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
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

    private func bandBinding(_ key: ReferenceWritableKeyPath<Engine, [Float]>)
        -> Binding<Float> {
        Binding(get: { engine[keyPath: key][band.rawValue] },
                set: { engine[keyPath: key][band.rawValue] = $0 })
    }

    private func section<Content: View>(_ title: String,
                                        @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 11) {
            Text(title.uppercased())
                .font(.system(size: 11, weight: .semibold))
                .tracking(0.8)
                .foregroundStyle(Palette.text)
            content()
        }
    }

    private func slider(_ name: String, _ value: Binding<Float>,
                        _ range: ClosedRange<Float>, _ unit: String,
                        _ decimals: Int) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(name).font(.system(size: 12)).foregroundStyle(Palette.dim)
                Spacer()
                Text(String(format: "%.\(decimals)f%@", value.wrappedValue, unit))
                    .font(.system(size: 11)).monospacedDigit()
                    .foregroundStyle(Palette.text)
            }
            Slider(value: Binding(
                get: { value.wrappedValue },
                // Route through history so each control's drags coalesce into
                // one undo step rather than a hundred.
                set: { v in engine.edit(name) { value.wrappedValue = v } }
            ), in: range)
                .controlSize(.small)
                .tint(Palette.accent)
        }
    }

    // MARK: Actions

    /// Opening one photo still scans its folder, so the filmstrip is populated
    /// without a separate import step.
    private func openFile() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.prompt = "Open Photo"
        panel.allowedContentTypes = ["arw", "dng", "nef", "cr2", "cr3", "raf", "orf", "rw2"]
            .compactMap { UTType(filenameExtension: $0) }
        guard panel.runModal() == .OK, let url = panel.url else { return }

        library.open(folder: url.deletingLastPathComponent())
        load(url)
    }

    private func openFolder() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.prompt = "Open Folder"
        guard panel.runModal() == .OK, let url = panel.url else { return }

        library.open(folder: url)
        Task {
            while library.loading { try? await Task.sleep(for: .milliseconds(30)) }
            if let first = library.visible.first?.url { load(first) }
        }
    }

    private func load(_ url: URL) {
        current = url

        // Crop and straighten belong to the photo that was open, not the one
        // arriving. Carrying them over composites the old geometry against the
        // new frame, which is what produced the doubled, offset picture.
        engine.resetCrop()

        // Show the camera's embedded preview straight away, then decode. The
        // decode is ~50ms plus a full-resolution render; without this the view
        // holds the previous photo for the whole of it, which reads as lag even
        // though the work is unavoidable.
        engine.showPlaceholder(library.photos.first { $0.url == url }?.thumbnail)

        Task { @MainActor in
            // One runloop turn, so the placeholder actually paints before the
            // synchronous decode begins.
            await Task.yield()
            do {
                try engine.open(path: url.path)
                viewport.reset()
            } catch {
                message = error.localizedDescription
            }
            engine.clearPlaceholder()
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
        panel.nameFieldStringValue = "export.\(exportSettings.format.ext)"
        panel.allowedContentTypes = [UTType(filenameExtension: exportSettings.format.ext)
                                     ?? .jpeg]
        guard panel.runModal() == .OK, let url = panel.url else { return }

        do {
            try engine.export(to: url.path,
                              quality: Float(exportSettings.quality),
                              maxDimension: exportSettings.size.longestEdge)
        } catch {
            message = error.localizedDescription
        }
    }
}

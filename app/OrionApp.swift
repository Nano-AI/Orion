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

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Rectangle().fill(Palette.line).frame(height: 1)

            HStack(spacing: 0) {
                canvas
                Rectangle().fill(Palette.line).frame(width: 1)
                tools.frame(width: 322)
            }
        }
        .background(Palette.ground)
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
            Text("ORION")
                .font(.system(size: 13, weight: .semibold))
                .tracking(2.2)
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
                iconChip("rotate.left", enabled: engine.isLoaded) {
                    engine.rotate(-1); viewport.reset()
                }
                iconChip("rotate.right", enabled: engine.isLoaded) {
                    engine.rotate(1); viewport.reset()
                }
                chip("Open…", enabled: true) { openFile() }
                chip("Reset", enabled: engine.isLoaded) { engine.resetEdits() }
                chip("Export…", enabled: engine.isLoaded) { exportFile() }
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
                    VStack(spacing: 12) {
                        Text("No photo open").foregroundStyle(Palette.dim)
                        Button("Open a RAW file…") { openFile() }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var hint: String {
        viewport.isFit
            ? "scroll or pinch to zoom · right-click to fit"
            : "drag to pan · right-click to fit"
    }

    // MARK: Tools

    private var tools: some View {
        VStack(spacing: 0) {
            tabBar

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    switch tab {
                    case .light:  lightPanel
                    case .colour: colourPanel
                    case .detail: detailPanel
                    case .crop:
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
                        Text("Crop and straighten still to come — M1")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
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
            Slider(value: value, in: range)
                .controlSize(.small)
                .tint(Palette.accent)
        }
    }

    // MARK: Actions

    private func openFile() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = ["arw", "dng", "nef", "cr2", "cr3", "raf", "orf", "rw2"]
            .compactMap { UTType(filenameExtension: $0) }
        guard panel.runModal() == .OK, let url = panel.url else { return }

        do {
            try engine.open(path: url.path)
            viewport.reset()
        } catch {
            message = error.localizedDescription
        }
    }

    private func exportFile() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "export.jpg"
        panel.allowedContentTypes = [.jpeg, .png, .tiff]
        panel.message = "Format follows the file extension."
        guard panel.runModal() == .OK, let url = panel.url else { return }

        do { try engine.export(to: url.path) }
        catch { message = error.localizedDescription }
    }
}

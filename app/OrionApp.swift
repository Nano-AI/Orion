import SwiftUI
import UniformTypeIdentifiers

@main
struct OrionApp: App {
    var body: some Scene {
        WindowGroup("Orion") {
            RootView()
                .frame(minWidth: 900, minHeight: 600)
        }
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}

// Palette mirrors design/tokens.json. Once the app has a real target this
// should come from the generated Sources/OrionUI/DesignTokens.swift instead.
private enum Palette {
    static let ground   = Color(red: 0.078, green: 0.078, blue: 0.086)
    static let panel    = Color(red: 0.106, green: 0.106, blue: 0.114)
    static let surround = Color(red: 0.165, green: 0.165, blue: 0.173)
    static let line     = Color(red: 0.192, green: 0.192, blue: 0.208)
    static let text     = Color(red: 0.910, green: 0.910, blue: 0.918)
    static let dim      = Color(red: 0.541, green: 0.541, blue: 0.565)
    static let accent   = Color(red: 0.302, green: 0.714, blue: 0.769)
}

struct RootView: View {
    @State private var engine: Engine?
    @State private var startupError: String?
    @State private var openError: String?

    var body: some View {
        Group {
            if let engine {
                Editor(engine: engine, openError: $openError)
            } else {
                VStack(spacing: 8) {
                    Text("Orion could not start")
                        .font(.headline)
                    Text(startupError ?? "Unknown error")
                        .font(.callout)
                        .foregroundStyle(Palette.dim)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Palette.ground)
            }
        }
        .task {
            do {
                let e = try Engine()
                engine = e
                // Convenience for development: `Orion.app/Contents/MacOS/Orion foo.ARW`
                // opens straight into the file instead of the panel.
                if let path = CommandLine.arguments.dropFirst().first,
                   FileManager.default.fileExists(atPath: path) {
                    try e.open(path: path)
                }
            } catch {
                startupError = error.localizedDescription
            }
        }
        .preferredColorScheme(.dark)
    }
}

private struct Editor: View {
    @Bindable var engine: Engine
    @Binding var openError: String?

    var body: some View {
        HStack(spacing: 0) {
            canvas
            Divider().background(Palette.line)
            tools.frame(width: 322)
        }
        .background(Palette.ground)
        .toolbar { toolbarContent }
    }

    private var canvas: some View {
        ZStack {
            Palette.surround
            if engine.isLoaded {
                ImageCanvas(engine: engine, generation: engine.generation)
                    .padding(26)
            } else {
                VStack(spacing: 10) {
                    Text("No photo open")
                        .foregroundStyle(Palette.dim)
                    Button("Open a RAW file…") { openFile() }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var tools: some View {
        VStack(alignment: .leading, spacing: 0) {
            header

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    section("Light") {
                        slider("Exposure", value: $engine.exposureEv,
                               range: -5...5, unit: " EV", decimals: 2)
                        slider("Black", value: $engine.black,
                               range: -0.1...0.1, unit: "", decimals: 3)
                    }
                    section("Tone") {
                        slider("Contrast", value: $engine.contrast,
                               range: 0.5...2, unit: "", decimals: 2)
                        slider("Saturation", value: $engine.saturation,
                               range: 0...2, unit: "", decimals: 2)
                    }
                }
                .padding(14)
            }

            Spacer()
            footer
        }
        .background(Palette.panel)
        .disabled(!engine.isLoaded)
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("ORION")
                .font(.system(size: 13, weight: .semibold))
                .tracking(2)
                .foregroundStyle(Palette.text)
            Text(engine.isLoaded ? engine.camera : "no photo")
                .font(.system(size: 11))
                .foregroundStyle(Palette.dim)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(Palette.panel)
        .overlay(alignment: .bottom) { Rectangle().fill(Palette.line).frame(height: 1) }
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
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .overlay(alignment: .top) { Rectangle().fill(Palette.line).frame(height: 1) }
    }

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItem(placement: .primaryAction) {
            Button("Open…") { openFile() }
        }
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

    private func slider(_ name: String, value: Binding<Float>,
                        range: ClosedRange<Float>, unit: String, decimals: Int) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(name)
                    .font(.system(size: 12))
                    .foregroundStyle(Palette.dim)
                Spacer()
                Text(String(format: "%.\(decimals)f%@", value.wrappedValue, unit))
                    .font(.system(size: 11))
                    .monospacedDigit()
                    .foregroundStyle(Palette.text)
            }
            Slider(value: value, in: range)
                .controlSize(.small)
                .tint(Palette.accent)
        }
    }

    private func openFile() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [
            UTType(filenameExtension: "arw") ?? .data,
            UTType(filenameExtension: "dng") ?? .data,
            UTType(filenameExtension: "nef") ?? .data,
            UTType(filenameExtension: "cr2") ?? .data,
            UTType(filenameExtension: "cr3") ?? .data,
        ]
        guard panel.runModal() == .OK, let url = panel.url else { return }

        do { try engine.open(path: url.path) }
        catch { openError = error.localizedDescription }
    }
}

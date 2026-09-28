// The watermark editor, a sheet over the Export panel. The model it edits is
// in Watermark.swift, which deliberately has no SwiftUI in it.
//
// ⚠ The preview is the export itself: a real `Engine.export` of the open photo
// at 1024 px with the mask, read back. There is no second compositing path to
// drift from what gets written - what you see is the file. DECISIONS #284.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct WatermarkPanel: View {
    @Bindable var mark: Watermark
    /// Renders the open photo with this mark. Real work, so it is debounced.
    let preview: () async -> NSImage?
    let onDone: () -> Void

    @State private var image: NSImage?
    @State private var rendering = false
    @State private var token = 0
    /// The kind not showing, kept so flipping Text/SVG and back loses nothing.
    @State private var otherContent: Watermark.Content?

    init(mark: Watermark, preview: @escaping () async -> NSImage?,
         initial: NSImage? = nil, onDone: @escaping () -> Void) {
        self.mark = mark
        self.preview = preview
        self.onDone = onDone
        _image = State(initialValue: initial)
    }

    private var anchored: Watermark.Anchor? {
        if case .anchor(let a) = mark.placement { a } else { nil }
    }

    private var options: some View {
        VStack(alignment: .leading, spacing: 16) {
            row("Mark") {
                Picker("", selection: Binding(get: { mark.content.isText },
                                              set: { switchKind(toText: $0) })) {
                    Text("Text").tag(true)
                    Text("SVG").tag(false)
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                content
            }

            row("Placement") {
                HStack(spacing: 14) {
                    anchorGrid
                    placementChip("Diagonal", on: mark.placement == .diagonal) {
                        mark.placement = .diagonal
                    }
                    Spacer()
                }
            }

            if anchored != nil {
                slider("Size", $mark.size, 0.01...0.15)
                slider("Margin", $mark.margin, 0...0.1)
            } else {
                slider("Size", $mark.span, 0.2...1)
            }
            slider("Opacity", $mark.opacity, 0.05...1)

            Text("One neutral gray, always. Opacity is how present it is.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.dim)
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Watermark")
                .font(.system(size: 19, weight: .regular, design: .serif))
                .foregroundStyle(Palette.text)

            previewBox
            // Not scrolled: the controls are a fixed set, and a scroll fold
            // put Opacity - the one control every mark needs - out of sight.
            options

            HStack {
                if let why = mark.lastFailure {
                    Text(why).font(.system(size: 10)).foregroundStyle(Palette.dim)
                }
                Spacer()
                Button("Done") { mark.save(); onDone() }
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(22)
        .frame(width: 400)
        .background(Palette.panel)
        .onAppear { if image == nil { rerender() } }
        .onChange(of: key) { _, _ in rerender() }
    }

    // ── Pieces ──────────────────────────────────────────────────────────────

    private var previewBox: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 5).fill(Palette.raised)
            if let image {
                Image(nsImage: image).resizable().aspectRatio(contentMode: .fit)
            }
            if rendering {
                ProgressView().controlSize(.small)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                    .padding(6)
            }
        }
        .frame(height: 200)
    }

    @ViewBuilder private var content: some View {
        switch mark.content {
        case .text(let words, let family, let bold):
            TextField("Your name", text: Binding(
                get: { words },
                set: { mark.content = .text($0, fontFamily: family, bold: bold) }))
                .textFieldStyle(.roundedBorder)
                .controlSize(.small)
            HStack(spacing: 10) {
                Picker("", selection: Binding(
                    get: { family },
                    set: { mark.content = .text(words, fontFamily: $0, bold: bold) })) {
                    ForEach(NSFontManager.shared.availableFontFamilies, id: \.self) {
                        Text($0).tag($0)
                    }
                }
                .pickerStyle(.menu)
                .labelsHidden()
                Toggle("Bold", isOn: Binding(
                    get: { bold },
                    set: { mark.content = .text(words, fontFamily: family, bold: $0) }))
                    .toggleStyle(.checkbox)
            }
        case .svg(_, let name):
            HStack(spacing: 10) {
                Button("Choose SVG…", action: chooseSVG).controlSize(.small)
                Text(name.isEmpty ? "No file chosen" : name)
                    .lineLimit(1).truncationMode(.middle)
                    .foregroundStyle(name.isEmpty ? Palette.dim : Palette.text)
            }
            .font(.system(size: 11))
            Text("Only the shape is used. Its colors become the one gray, and an "
                 + "opaque background becomes a gray box.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.dim)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    /// The 3x3 grid, drawn in the order `Anchor` declares its cases.
    private var anchorGrid: some View {
        let cases = Watermark.Anchor.allCases
        return Grid(horizontalSpacing: 3, verticalSpacing: 3) {
            ForEach(0..<3, id: \.self) { r in
                GridRow {
                    ForEach(0..<3, id: \.self) { c in
                        let a = cases[r * 3 + c]
                        Button { mark.placement = .anchor(a) } label: {
                            RoundedRectangle(cornerRadius: 2)
                                .fill(anchored == a ? Palette.accent : Palette.raised)
                                .frame(width: 18, height: 14)
                        }
                        .buttonStyle(.plain)
                        .help(a.title.capitalized)
                    }
                }
            }
        }
    }

    private func placementChip(_ title: String, on: Bool,
                               action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11))
                .padding(.horizontal, 10).padding(.vertical, 4)
                .background(on ? Palette.accent.opacity(0.25) : Palette.raised,
                            in: RoundedRectangle(cornerRadius: 4))
                .foregroundStyle(on ? Palette.text : Palette.dim)
        }
        .buttonStyle(.plain)
    }

    private func slider(_ label: String, _ value: Binding<Double>,
                        _ range: ClosedRange<Double>) -> some View {
        row(label) {
            HStack(spacing: 8) {
                Slider(value: value, in: range).controlSize(.small).tint(Palette.accent)
                Text("\(Int((value.wrappedValue * 100).rounded()))%")
                    .font(.system(size: 10)).monospacedDigit()
                    .foregroundStyle(Palette.dim)
                    .frame(width: 34, alignment: .trailing)
            }
        }
    }

    private func row<Content: View>(_ label: String,
                                    @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(label.uppercased())
                .font(.system(size: 10, weight: .semibold))
                .tracking(0.8)
                .foregroundStyle(Palette.dim)
            content()
        }
    }

    // ── Behavior ────────────────────────────────────────────────────────────

    /// Everything the picture depends on, so one `onChange` covers it.
    private var key: String {
        "\(mark.content)|\(mark.placement)|\(mark.size)|\(mark.span)|\(mark.margin)|\(mark.opacity)"
    }

    private func switchKind(toText: Bool) {
        guard toText != mark.content.isText else { return }
        let was = mark.content
        mark.content = otherContent
            ?? (toText ? .text("", fontFamily: "Helvetica Neue", bold: false)
                       : .svg(Data(), name: ""))
        otherContent = was
    }

    private func chooseSVG() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.svg]
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url,
              let data = try? Data(contentsOf: url) else { return }
        mark.content = .svg(data, name: url.lastPathComponent)
    }

    /// Debounced as `ExportPanel.remeasure()` is: a full render and a 1024 px
    /// encode per slider tick would make the slider unusable.
    private func rerender() {
        token += 1
        let mine = token
        rendering = true
        Task {
            try? await Task.sleep(for: .milliseconds(260))
            guard mine == token else { return }
            let picture = await preview()
            guard mine == token else { return }
            image = picture
            rendering = false
        }
    }
}

extension Engine {

    /// The open photo exported at 1024 px with this mark and read back - the
    /// editor's preview, through the same writer every export uses.
    func watermarkPreview(_ mark: Watermark) -> NSImage? {
        guard isLoaded, imageWidth > 0, imageHeight > 0 else { return nil }
        let settings = ExportSettings()
        settings.size = .px1024
        let (w, h) = settings.dimensions(sourceWidth: imageWidth, sourceHeight: imageHeight)
        let path = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-watermark-preview-\(UUID().uuidString).jpg").path
        defer { try? FileManager.default.removeItem(atPath: path) }
        do {
            try export(to: path, quality: 0.9,
                       maxDimension: settings.longestEdge(sourceWidth: imageWidth,
                                                          sourceHeight: imageHeight),
                       depth: 8,
                       watermark: WatermarkRaster.mask(for: mark, width: Int(w), height: Int(h)))
        } catch {
            return nil
        }
        return FileManager.default.contents(atPath: path).flatMap(NSImage.init(data:))
    }
}

import SwiftUI
import UniformTypeIdentifiers

/// Export settings, modeled on macOS Preview's export sheet.
///
/// Preview is the right reference because every Mac user already knows it. The
/// part that matters most is the **live size estimate**: a quality slider with
/// no size beside it is unreadable, which is exactly why Preview shows one.
@Observable
final class ExportSettings {

    enum Format: String, CaseIterable, Identifiable {
        case jpeg, png, tiff
        var id: String { rawValue }

        var title: String {
            switch self {
            case .jpeg: "JPEG"
            case .png:  "PNG"
            case .tiff: "TIFF"
            }
        }

        var ext: String { self == .jpeg ? "jpg" : rawValue }

        /// Matches OrionImageFormat in orion.h.
        var code: Int32 {
            switch self {
            case .png:  0
            case .jpeg: 1
            case .tiff: 2
            }
        }
        var isLossy: Bool { self == .jpeg }

        /// Rough bytes per pixel at a given quality, for the size estimate.
        /// Empirical rather than derived — a real encode is the only exact
        /// answer, and running one per slider tick would be absurd.
        func bytesPerPixel(quality: Double) -> Double {
            switch self {
            case .jpeg: 0.08 + 0.72 * pow(quality, 2.4)
            case .png:  2.1
            case .tiff: 3.0
            }
        }
    }

    enum Size: String, CaseIterable, Identifiable {
        case full, px4096, px2048, px1024, custom
        var id: String { rawValue }

        var title: String {
            switch self {
            case .full:   "Full size"
            case .px4096: "4096 px"
            case .px2048: "2048 px"
            case .px1024: "1024 px"
            case .custom: "Custom…"
            }
        }

        var longestEdge: UInt32 {
            switch self {
            case .full, .custom: 0
            case .px4096: 4096
            case .px2048: 2048
            case .px1024: 1024
            }
        }
    }

    var format: Format = .jpeg
    var quality: Double = 0.9
    var size: Size = .full

    /// Typed dimensions, used when `size` is `.custom`. The aspect is held, so
    /// entering either one sets the other — a free pair would let you squash
    /// the picture by accident, and nobody exporting a photo wants that.
    var customWidth: UInt32 = 0
    var customHeight: UInt32 = 0

    /// Measured by a real encode, not estimated. nil until the first one lands.
    var measuredBytes: Int?

    /// Pixel dimensions after resizing, given the source.
    func dimensions(sourceWidth: UInt32, sourceHeight: UInt32) -> (UInt32, UInt32) {
        if size == .custom {
            let w = max(1, customWidth), h = max(1, customHeight)
            return (w, h)
        }

        let longest = max(sourceWidth, sourceHeight)
        let limit = size.longestEdge
        guard limit > 0, longest > limit else { return (sourceWidth, sourceHeight) }

        let scale = Double(limit) / Double(longest)
        return (max(1, UInt32((Double(sourceWidth) * scale).rounded())),
                max(1, UInt32((Double(sourceHeight) * scale).rounded())))
    }

    /// What the engine is asked to cap the longest edge at. Custom dimensions
    /// are expressed the same way, because the export path resizes by longest
    /// edge and holding the aspect means the two are equivalent.
    func longestEdge(sourceWidth: UInt32, sourceHeight: UInt32) -> UInt32 {
        let (w, h) = dimensions(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        let longest = max(w, h)
        return longest >= max(sourceWidth, sourceHeight) ? 0 : longest
    }

    /// Sets one dimension and derives the other from the source's aspect.
    func setCustom(width: UInt32, sourceWidth: UInt32, sourceHeight: UInt32) {
        guard sourceWidth > 0, sourceHeight > 0 else { return }
        customWidth = max(1, width)
        customHeight = max(1, UInt32((Double(customWidth)
            * Double(sourceHeight) / Double(sourceWidth)).rounded()))
    }

    func setCustom(height: UInt32, sourceWidth: UInt32, sourceHeight: UInt32) {
        guard sourceWidth > 0, sourceHeight > 0 else { return }
        customHeight = max(1, height)
        customWidth = max(1, UInt32((Double(customHeight)
            * Double(sourceWidth) / Double(sourceHeight)).rounded()))
    }

    func estimatedBytes(sourceWidth: UInt32, sourceHeight: UInt32) -> Int {
        let (w, h) = dimensions(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        return Int(Double(w) * Double(h) * format.bytesPerPixel(quality: quality))
    }

    /// The measured size if one has come back, otherwise the estimate. The
    /// estimate exists only to fill the moment before the first encode lands;
    /// showing nothing there makes the panel look broken.
    func sizeText(sourceWidth: UInt32, sourceHeight: UInt32) -> String {
        let bytes = measuredBytes
            ?? estimatedBytes(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        let f = ByteCountFormatter()
        f.countStyle = .file
        f.allowedUnits = [.useMB, .useKB]
        return f.string(fromByteCount: Int64(bytes))
    }
}

struct ExportPanel: View {
    @Bindable var settings: ExportSettings
    let sourceWidth: UInt32
    let sourceHeight: UInt32
    /// Encodes with the current settings and returns the byte count. Real work,
    /// so the caller debounces it.
    let measure: () async -> Int?
    let onExport: () -> Void
    let onCancel: () -> Void

    @State private var widthText = ""
    @State private var heightText = ""
    @State private var measuring = false
    @State private var measureToken = 0

    private var dims: (UInt32, UInt32) {
        settings.dimensions(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("Export")
                .font(.system(size: 19, weight: .regular, design: .serif))
                .foregroundStyle(Palette.text)

            row("Format") {
                Picker("", selection: $settings.format) {
                    ForEach(ExportSettings.Format.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
            }

            if settings.format.isLossy {
                row("Quality") {
                    VStack(alignment: .leading, spacing: 3) {
                        Slider(value: $settings.quality, in: 0.3...1)
                            .controlSize(.small)
                            .tint(Palette.accent)
                        HStack {
                            Text("\(Int(settings.quality * 100))")
                                .monospacedDigit()
                            Spacer()
                            Text("smaller file")
                            Spacer()
                            Text("better detail")
                        }
                        .font(.system(size: 9))
                        .foregroundStyle(Palette.faint)
                    }
                }
            }

            row("Size") {
                VStack(alignment: .leading, spacing: 8) {
                    Picker("", selection: $settings.size) {
                        ForEach(ExportSettings.Size.allCases) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.menu)
                    .labelsHidden()

                    if settings.size == .custom {
                        HStack(spacing: 6) {
                            dimensionField("Width", text: $widthText) { v in
                                settings.setCustom(width: v, sourceWidth: sourceWidth,
                                                   sourceHeight: sourceHeight)
                                syncFields()
                            }
                            Text("×").foregroundStyle(Palette.faint)
                            dimensionField("Height", text: $heightText) { v in
                                settings.setCustom(height: v, sourceWidth: sourceWidth,
                                                   sourceHeight: sourceHeight)
                                syncFields()
                            }
                            Text("px")
                                .font(.system(size: 10))
                                .foregroundStyle(Palette.faint)
                        }
                    }
                }
            }

            // The two numbers that make every control above legible.
            HStack(spacing: 8) {
                Text("\(dims.0) × \(dims.1)")
                Spacer()
                if measuring {
                    ProgressView().controlSize(.small).scaleEffect(0.6)
                }
                Text(settings.sizeText(sourceWidth: sourceWidth,
                                       sourceHeight: sourceHeight))
                    .monospacedDigit()
                    .foregroundStyle(settings.measuredBytes == nil
                                     ? Palette.dim : Palette.text)
            }
            .font(.system(size: 11))
            .foregroundStyle(Palette.dim)
            .padding(.vertical, 9)
            .padding(.horizontal, 11)
            .background(Palette.raised, in: RoundedRectangle(cornerRadius: 5))

            Text(settings.measuredBytes == nil
                 ? "Measuring the encoded size…"
                 : "Encoded size, measured. Color space is sRGB.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)

            HStack(spacing: 8) {
                Spacer()
                Button("Cancel", action: onCancel)
                Button("Export…", action: onExport)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(22)
        .frame(width: 380)
        .background(Palette.panel)
        .onAppear {
            if settings.customWidth == 0 {
                settings.setCustom(width: sourceWidth, sourceWidth: sourceWidth,
                                   sourceHeight: sourceHeight)
            }
            syncFields()
            remeasure()
        }
        .onChange(of: settings.format) { _, _ in remeasure() }
        .onChange(of: settings.quality) { _, _ in remeasure() }
        .onChange(of: settings.size) { _, _ in syncFields(); remeasure() }
        .onChange(of: settings.customWidth) { _, _ in remeasure() }
    }

    /// Re-encodes after a pause. A full JPEG encode of a 24 MP frame is about a
    /// sixth of a second, so running one per slider tick would make the slider
    /// unusable — and running none is how the old estimate came to be wrong.
    private func remeasure() {
        measureToken += 1
        let token = measureToken
        settings.measuredBytes = nil
        measuring = true

        Task {
            try? await Task.sleep(for: .milliseconds(260))
            guard token == measureToken else { return }

            let bytes = await measure()
            guard token == measureToken else { return }
            settings.measuredBytes = bytes
            measuring = false
        }
    }

    private func syncFields() {
        widthText = String(dims.0)
        heightText = String(dims.1)
    }

    /// A numeric field that commits on Return or on losing focus, not on every
    /// keystroke — re-encoding while you are halfway through typing "2048" is
    /// three wasted encodes and a jumping number.
    private func dimensionField(_ label: String, text: Binding<String>,
                                commit: @escaping (UInt32) -> Void) -> some View {
        TextField(label, text: text)
            .textFieldStyle(.roundedBorder)
            .controlSize(.small)
            .frame(width: 74)
            .monospacedDigit()
            .onSubmit { if let v = UInt32(text.wrappedValue), v > 0 { commit(v) } }
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
}

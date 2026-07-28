import SwiftUI
import UniformTypeIdentifiers

/// Export settings, modelled on macOS Preview's export sheet.
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
        case full, px4096, px2048, px1024
        var id: String { rawValue }

        var title: String {
            switch self {
            case .full:   "Full size"
            case .px4096: "4096 px"
            case .px2048: "2048 px"
            case .px1024: "1024 px"
            }
        }

        var longestEdge: UInt32 {
            switch self {
            case .full:   0
            case .px4096: 4096
            case .px2048: 2048
            case .px1024: 1024
            }
        }
    }

    var format: Format = .jpeg
    var quality: Double = 0.9
    var size: Size = .full

    /// Pixel dimensions after resizing, given the source.
    func dimensions(sourceWidth: UInt32, sourceHeight: UInt32) -> (UInt32, UInt32) {
        let longest = max(sourceWidth, sourceHeight)
        let limit = size.longestEdge
        guard limit > 0, longest > limit else { return (sourceWidth, sourceHeight) }

        let scale = Double(limit) / Double(longest)
        return (max(1, UInt32((Double(sourceWidth) * scale).rounded())),
                max(1, UInt32((Double(sourceHeight) * scale).rounded())))
    }

    func estimatedBytes(sourceWidth: UInt32, sourceHeight: UInt32) -> Int {
        let (w, h) = dimensions(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
        return Int(Double(w) * Double(h) * format.bytesPerPixel(quality: quality))
    }

    func estimatedSize(sourceWidth: UInt32, sourceHeight: UInt32) -> String {
        let bytes = estimatedBytes(sourceWidth: sourceWidth, sourceHeight: sourceHeight)
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
    let onExport: () -> Void
    let onCancel: () -> Void

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
                Picker("", selection: $settings.size) {
                    ForEach(ExportSettings.Size.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.menu)
                .labelsHidden()
            }

            // The two numbers that make every control above legible.
            HStack {
                Text("\(dims.0) × \(dims.1)")
                Spacer()
                Text(settings.estimatedSize(sourceWidth: sourceWidth,
                                            sourceHeight: sourceHeight))
                    .monospacedDigit()
                    .foregroundStyle(Palette.text)
            }
            .font(.system(size: 11))
            .foregroundStyle(Palette.dim)
            .padding(.vertical, 9)
            .padding(.horizontal, 11)
            .background(Palette.raised, in: RoundedRectangle(cornerRadius: 5))

            Text("File size is an estimate. Colour space is sRGB; 16-bit output "
                 + "is not available yet.")
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

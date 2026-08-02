// The export sheet itself. The settings it edits are in ExportSettings.swift,
// which deliberately has no SwiftUI in it.

import SwiftUI
import UniformTypeIdentifiers

struct ExportPanel: View {
    @Bindable var settings: ExportSettings
    let sourceWidth: UInt32
    let sourceHeight: UInt32
    /// Encodes with the current settings and returns the byte count. Real work,
    /// so the caller debounces it.
    let measure: () async -> Int?
    let onExport: () -> Void
    let onCancel: () -> Void

    /// Which dimension field holds focus, so leaving one commits it.
    enum Field { case width, height }
    @FocusState private var focusedField: Field?

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

            row("Color") {
                VStack(alignment: .leading, spacing: 5) {
                    Picker("", selection: $settings.space) {
                        ForEach(ExportSettings.Space.allCases) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.menu)
                    .labelsHidden()

                    Text(settings.space.note)
                        .font(.system(size: 9))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            row("Depth") {
                VStack(alignment: .leading, spacing: 5) {
                    Picker("", selection: $settings.depth) {
                        ForEach(ExportSettings.Depth.allCases) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    // Greyed out rather than hidden: a control that vanishes
                    // reads as a bug, and the reason it is unavailable is the
                    // useful part.
                    .disabled(!settings.format.carriesDepth)

                    Text(settings.format.carriesDepth
                         ? settings.depth.note
                         : "JPEG holds eight bits. Choose PNG or TIFF for more.")
                        .font(.system(size: 9))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            row("Metadata") {
                VStack(alignment: .leading, spacing: 5) {
                    Picker("", selection: $settings.metadata) {
                        ForEach(ExportSettings.Metadata.allCases) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.menu)
                    .labelsHidden()

                    Text(settings.metadata.note)
                        .font(.system(size: 9))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
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
                            dimensionField("Width", field: .width, text: $widthText) { v in
                                settings.setCustom(width: v, sourceWidth: sourceWidth,
                                                   sourceHeight: sourceHeight)
                                syncFields()
                            }
                            Text("×").foregroundStyle(Palette.faint)
                            dimensionField("Height", field: .height, text: $heightText) { v in
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

            // Directly under Size, because that is what it corrects: a resize
            // resamples, resampling softens, and this is the standard answer.
            row("Sharpening") {
                VStack(alignment: .leading, spacing: 5) {
                    Picker("", selection: $settings.sharpening) {
                        ForEach(ExportSettings.Sharpening.allCases) {
                            Text($0.title).tag($0)
                        }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()

                    Text(settings.sharpening.note)
                        .font(.system(size: 9))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
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
                 : "Encoded size, measured. Color space is \(settings.space.title).")
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
        .onChange(of: settings.space) { _, _ in remeasure() }
        // Both move the byte count — eight bits is about half the PNG, and
        // sharpening gives the JPEG encoder more to encode. A size that did not
        // follow them would be the measurement of a different file.
        .onChange(of: settings.depth) { _, _ in remeasure() }
        .onChange(of: settings.sharpening) { _, _ in remeasure() }
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
    private func dimensionField(_ label: String, field: Field, text: Binding<String>,
                                commit: @escaping (UInt32) -> Void) -> some View {
        TextField(label, text: text)
            .textFieldStyle(.roundedBorder)
            .controlSize(.small)
            .frame(width: 74)
            .monospacedDigit()
            // On Return *and* on losing focus. Typing a width and clicking
            // Export used to export the previous dimensions, because the field
            // had never been submitted.
            .onSubmit { if let v = UInt32(text.wrappedValue), v > 0 { commit(v) } }
            .focused($focusedField, equals: field)
            .onChange(of: focusedField) { was, _ in
                guard was == field else { return }
                if let v = UInt32(text.wrappedValue), v > 0 { commit(v) }
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
}

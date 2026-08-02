import SwiftUI

// The panel column: the switch that picks a panel, and the two builders every
// panel is written in.
//
// The panels themselves are `DevelopPanels.swift`. The Crop panel is the one
// exception and is inline below, where it has always been — it is the only tab
// whose controls are the geometry rather than the develop state.
//
// `section` and `slider` live here rather than beside the panels because they
// are what a panel is *made of*: adding a control means one `slider` line in
// `DevelopPanels.swift`, and the reason that line is one line is here.

extension Editor {
    var tools: some View {
        VStack(spacing: 0) {
            if engine.isLoaded && !engine.histogramBins.isEmpty {
                // The axis labels used to live out here, in a second row under
                // the plate. They belong to the instrument: the marks and the
                // words they name have to be one scale, or they drift apart the
                // first time either moves. `Histogram` draws its own rail now.
                Histogram(bins: engine.histogramBins, height: 84)
                    .padding(.horizontal, 14)
                    .padding(.top, 12)
                    .padding(.bottom, 10)
            }

            tabBar

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    switch tab {
                    case .light:  lightPanel
                    case .color: colorPanel
                    case .detail: detailPanel
                    case .optics: opticsPanel
                    case .mask:   maskPanel
                    case .presets: presetsPanel
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
                        section("Perspective") {
                            slider("Vertical", $engine.perspectiveVertical, -1...1, "", 2,
                                   resetsTo: engine.defaults.perspectiveVertical)
                            slider("Horizontal", $engine.perspectiveHorizontal, -1...1, "", 2,
                                   resetsTo: engine.defaults.perspectiveHorizontal)
                            slider("Aspect", $engine.perspectiveAspect, -1...1, "", 2,
                                   resetsTo: engine.defaults.perspectiveAspect)
                            Text("Straightens converging lines — a building "
                                 + "shot looking up, a wall shot from one side. "
                                 + "The frame zooms to stay full, as the lens "
                                 + "corrections do. Aspect undoes the squeeze a "
                                 + "strong correction leaves behind.")
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

    func bandBinding(_ key: ReferenceWritableKeyPath<Engine, [Float]>)
        -> Binding<Float> {
        Binding(get: { engine[keyPath: key][band.rawValue] },
                set: { engine[keyPath: key][band.rawValue] = $0 })
    }

    func section<Content: View>(_ title: String,
                                        @ViewBuilder content: @escaping () -> Content) -> some View {
        SectionPlate(title: title, content: content)
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
}

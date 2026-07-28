import SwiftUI

/// One adjustment: a name, a value that can be put back, and a slider.
///
/// The readout doubles as the reset. A control that has been moved shows its
/// number in the accent colour, so the panel can be scanned for what has been
/// touched, and clicking that number returns it to where the photo started.
/// Hovering adds the revert glyph to the left of it.
///
/// The glyph grows leftwards into the gap the spacer already holds, so the two
/// things the eye is tracking — the label, and the number's right edge — do not
/// move when it appears. A control that shifts under the pointer on hover is
/// one you miss on the click.
///
/// One affordance rather than two: the colour says which controls are modified
/// *and* marks what is clickable, which a separate change dot in the margin
/// would have said again in a smaller, harder-to-hit way.
struct AdjustmentSlider: View {
    let name: String
    @Binding var value: Float
    let range: ClosedRange<Float>
    let unit: String
    let decimals: Int

    /// Where this control started, for this photo. White balance is the
    /// camera's own reading, so the base cannot be a constant — "reset
    /// temperature" has to mean the camera's number, not the type's default.
    let base: Float

    let engine: Engine

    @State private var pointerInside = false

    /// Forces the hover state on for the screenshot harness. A pointer cannot
    /// be staged in an offscreen render, and the thing worth looking at is
    /// whether the revert glyph appears without pushing anything sideways —
    /// which is precisely what a screenshot answers and reasoning does not.
    nonisolated(unsafe) static var previewHover = false

    private var hovering: Bool { pointerInside || Self.previewHover }

    private var modified: Bool {
        AdjustmentMath.isModified(value, from: base, decimals: decimals)
    }

    private func text(_ v: Float) -> String {
        String(format: "%.\(decimals)f%@", v, unit)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 0) {
                Text(name)
                    .font(.system(size: 12))
                    .foregroundStyle(Palette.dim)
                Spacer(minLength: 8)
                readout
            }
            // The readout carries 4pt of its own padding so the hover
            // background is not tight against the glyph. Pulling the row back
            // by the same amount keeps the number itself aligned with the end
            // of the track below it, and lets the background hang into the
            // panel's margin instead of indenting the column.
            .padding(.trailing, -4)

            Slider(value: Binding(
                get: { value },
                // Route through history so each control's drags coalesce into
                // one undo step rather than a hundred.
                set: { v in engine.edit(name) { value = v } }
            ), in: range)
                .controlSize(.small)
                .tint(Palette.accent)
        }
    }

    @ViewBuilder private var readout: some View {
        if modified {
            Button {
                engine.edit("Reset \(name)") { value = base }
            } label: {
                HStack(spacing: 3) {
                    if hovering {
                        Image(systemName: "arrow.uturn.backward")
                            .font(.system(size: 9, weight: .semibold))
                    }
                    Text(text(value))
                        .font(.system(size: 11))
                        .monospacedDigit()
                }
                .foregroundStyle(Palette.accent)
                .padding(.horizontal, 4)
                .padding(.vertical, 1)
                .background(hovering ? Palette.raised : .clear,
                            in: RoundedRectangle(cornerRadius: 3))
                .contentShape(RoundedRectangle(cornerRadius: 3))
            }
            .buttonStyle(.plain)
            .onHover { pointerInside = $0 }
            .help("Reset \(name) to \(text(base))")
        } else {
            // Same padding as the button, so the number does not jump sideways
            // the moment a control stops being at its default.
            Text(text(value))
                .font(.system(size: 11))
                .monospacedDigit()
                .foregroundStyle(Palette.text)
                .padding(.horizontal, 4)
                .padding(.vertical, 1)
        }
    }
}

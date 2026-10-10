import SwiftUI

/// One adjustment: a name, a value that can be put back, and a slider.
///
/// The readout doubles as the reset. A control that has been moved shows its
/// number in the accent color, so the panel can be scanned for what has been
/// touched, and clicking that number returns it to where the photo started.
/// Hovering adds the revert glyph to the left of it.
///
/// The glyph grows leftwards into the gap the spacer already holds, so the two
/// things the eye is tracking — the label, and the number's right edge — do not
/// move when it appears. A control that shifts under the pointer on hover is
/// one you miss on the click.
///
/// One affordance rather than two: the color says which controls are modified
/// *and* marks what is clickable, which a separate change dot in the margin
/// would have said again in a smaller, harder-to-hit way.
///
/// Double-clicking the readout types a value instead, on any control, moved or
/// not. A drag resolves a hundredth of the throw at best, and an exact angle
/// or a round number of stops is something a photographer already knows
/// rather than something to hunt for.
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

    /// The track's label gradient, passed straight through to `AnalogTrack`.
    /// Nil is the plain engraved groove.
    var tint: Gradient?

    @State private var pointerInside = false

    /// Forces the hover state on for the screenshot harness. A pointer cannot
    /// be staged in an offscreen render, and the thing worth looking at is
    /// whether the revert glyph appears without pushing anything sideways —
    /// which is precisely what a screenshot answers and reasoning does not.
    nonisolated(unsafe) static var previewHover = false

    private var hovering: Bool { pointerInside || Self.previewHover }

    /// The value being typed, while the readout is an editor.
    @State private var editing = false
    @State private var draft = ""
    @FocusState private var fieldFocused: Bool

    /// Opens the editor on the control of this name, for the screenshot
    /// harness - the same reason as `previewHover`: a double-click cannot be
    /// staged offscreen, and whether the field sits exactly where the number
    /// did is a question only a picture answers.
    nonisolated(unsafe) static var previewEditing: String?

    private var isEditing: Bool { editing || Self.previewEditing == name }

    private var modified: Bool {
        AdjustmentMath.isModified(value, from: base, decimals: decimals)
    }

    private func text(_ v: Float) -> String {
        String(format: "%.\(decimals)f%@", v, unit)
    }

    /// The number alone, as the editor shows it.
    private func number(_ v: Float) -> String {
        String(format: "%.\(decimals)f", v)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 0) {
                // Engraved caps, so the control's name sits a level below its
                // section's nameplate and a level above the value it carries.
                // The three used to be near-identical gray text, which is why a
                // panel of forty rows read as a wall.
                Engraved.Label(text: name)
                Spacer(minLength: 8)
                readout
            }
            // The readout carries 4pt of its own padding so the hover
            // background is not tight against the glyph. Pulling the row back
            // by the same amount keeps the number itself aligned with the end
            // of the track below it, and lets the background hang into the
            // panel's margin instead of indenting the column.
            .padding(.trailing, -5)

            AnalogTrack(value: value, range: range, base: base, set: { v in
                // Route through history so each control's drags coalesce into
                // one undo step rather than a hundred.
                engine.edit(name) { value = v }
            }, interacting: { on in
                // ⚠ Every slider arms it, not only the slow ones. Which
                // controls are expensive is a property of the graph and it
                // changes — dehaze and clarity today, whatever is added next
                // tomorrow — and a hand-kept list of "the slow ones" is exactly
                // the shape of thing this codebase has been bitten by before.
                // A cheap control on the preview path costs nothing it was not
                // already spending.
                if on { engine.beginInteraction() } else { engine.endInteraction() }
            }, tint: tint)
            .accessibilityLabel(Text(name))
        }
        // The section's nameplate marks itself when anything under it has moved.
        // Reported from here rather than listed there, so a slider added later
        // cannot be left out of its own section's state.
        .preference(key: SectionModifiedKey.self, value: modified)
    }

    /// The value, in a recessed window — the distance scale in a lens barrel
    /// rather than a caption floating on the panel.
    ///
    /// It is also the reset. A control that has moved shows its number in the
    /// accent and the window becomes a button; hovering adds the revert glyph
    /// to its left, growing into the gap the spacer already holds so nothing
    /// the eye is tracking moves.
    ///
    /// Double-click types a value. ⚠ The two taps are one exclusive gesture so
    /// a double-click never resets first, and the price is that the single
    /// click's reset waits out the system double-click interval before it
    /// fires. A reset that lands a fraction of a second late is a far smaller
    /// cost than a double-click that wipes the value it was about to edit.
    @ViewBuilder private var readout: some View {
        let accent = modified && !isEditing
        window(accent: accent)
            .onHover { pointerInside = $0 }
            .gesture(
                TapGesture(count: 2).onEnded { beginEditing() }
                    .exclusively(before: TapGesture(count: 1).onEnded {
                        guard modified, !isEditing else { return }
                        engine.edit("Reset \(name)") { value = base }
                    }),
                // While the field is open its clicks belong to it: placing the
                // caret must not be read as a reset or a second editor.
                including: isEditing ? .subviews : .all
            )
            .help(modified
                  ? "Reset \(name) to \(text(base)). Double-click to type a value"
                  : "Double-click to type a value")
            .accessibilityElement(children: .combine)
            .accessibilityAddTraits(modified ? .isButton : [])
            .accessibilityAction(named: Text("Type a value")) { beginEditing() }
            .accessibilityAction(named: Text("Reset")) {
                guard modified else { return }
                engine.edit("Reset \(name)") { value = base }
            }
            // A drag on the track while the field is open makes what it holds
            // stale; the track wins and the editor closes without committing.
            .onChange(of: value) { _, _ in
                if editing { closeEditor() }
            }
    }

    /// The number, or a field holding it while it is being typed.
    ///
    /// ⚠ The field is laid over a hidden copy of the readout's own text, so the
    /// window keeps exactly the width and height the number gave it: the label,
    /// the track and the window's right edge cannot move when it opens. Typing
    /// more than fits grows the window leftwards into the spacer, the same way
    /// the revert glyph does.
    @ViewBuilder private var content: some View {
        if isEditing {
            ZStack(alignment: .trailing) {
                Text(text(value)).hidden()
                Text(draft).hidden()
            }
            .font(.system(size: 11))
            .monospacedDigit()
            .overlay(alignment: .trailing) {
                TextField("", text: $draft)
                    .textFieldStyle(.plain)
                    .font(.system(size: 11))
                    .monospacedDigit()
                    .multilineTextAlignment(.trailing)
                    // Measured, not guessed: at 11pt AppKit's field cell sets
                    // its baseline one device pixel (half a point at 2x) below
                    // where SwiftUI's Text puts it, so without this the number
                    // visibly drops when the editor opens (`reset-edit`
                    // against `reset`).
                    .offset(y: -0.5)
                    .focused($fieldFocused)
                    // The harness opens the editor without `beginEditing`.
                    .onAppear { if draft.isEmpty { draft = number(value) } }
                    .onSubmit { commitEdit() }
                    .onExitCommand { closeEditor() }
                    .onKeyPress(keys: [.upArrow, .downArrow],
                                phases: [.down, .repeat]) { press in
                        let direction = press.key == .upArrow ? 1 : -1
                        let steps = direction * (press.modifiers.contains(.shift) ? 10 : 1)
                        if let v = AdjustmentMath.nudged(draft, steps: steps, unit: unit,
                                                         decimals: decimals, range: range) {
                            draft = number(v)
                        }
                        return .handled
                    }
                    .onChange(of: fieldFocused) { _, focused in
                        // Clicking away commits, the way every inspector field
                        // on this platform does.
                        if !focused && editing { commitEdit() }
                    }
            }
        } else {
            Text(text(value))
                .font(.system(size: 11))
                .monospacedDigit()
        }
    }

    private func beginEditing() {
        guard !editing else { return }
        draft = number(value)
        editing = true
        // Focused on the next turn, once the field exists. A text field given
        // focus this way selects its whole contents, so the first keystroke
        // replaces the number rather than appending to it.
        DispatchQueue.main.async { fieldFocused = true }
    }

    /// One undo step for the typed value, through the same history as a drag.
    /// Anything that is not a number leaves the old value where it was.
    private func commitEdit() {
        guard editing else { return }
        let v = AdjustmentMath.parsed(draft, unit: unit, range: range)
        closeEditor()
        if let v, v != value {
            engine.edit(name) { value = v }
        }
    }

    private func closeEditor() {
        editing = false
        fieldFocused = false
    }

    private func window(accent: Bool) -> some View {
        HStack(spacing: 3) {
            if accent && hovering {
                Image(systemName: "arrow.uturn.backward")
                    .font(.system(size: 9, weight: .semibold))
            }
            content
        }
        .foregroundStyle(accent ? Palette.accent : Palette.text)
        .padding(.horizontal, 5)
        .padding(.vertical, 1.5)
        .background(
            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .fill(hovering && accent ? Palette.raised : Palette.ground)
        )
        .overlay(
            // Hairline on the upper edge only, which is where a recess catches
            // the light. All four sides would read as a raised chip instead.
            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .strokeBorder(Palette.line, lineWidth: 0.5)
                .mask(LinearGradient(colors: [.black, .black.opacity(0.15)],
                                     startPoint: .top, endPoint: .bottom))
        )
        .contentShape(RoundedRectangle(cornerRadius: 3, style: .continuous))
    }
}

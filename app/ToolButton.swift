import SwiftUI

/// Which tool owns the next click on the photograph.
///
/// One value, owned by `Engine`, instead of one Bool per panel. The Bools were
/// how a click could do something the visible controls no longer explained:
/// each panel armed its own flag, each flag needed its own disarm rule, and the
/// tab-switch hygiene cleared two of the three — the mask color picker survived
/// a switch to Light and quietly ate the next click. An enum cannot hold two
/// armed tools at once, so that class of state is unrepresentable rather than
/// tested for.
///
/// The rules, all of them:
/// - a panel arms a tool with a `ToolButton` — one line, like `slider(...)`
/// - switching tabs disarms (`OrionApp+Canvas`), Escape disarms
///   (`OrionApp+Commands`), and re-clicking the button disarms
/// - the canvas consults this one value to decide what a press means
/// - the footer names the armed tool (`OrionApp+Chrome`), so "what does a
///   click do right now" always has a visible answer
enum CanvasTool: Equatable {
    case none
    /// The color mixer's targeted adjustment: a drag on the photo adjusts the
    /// hue band under the cursor.
    case targeted
    /// The color-range mask's picker: the next click sets the mask's target
    /// color and disarms.
    case maskColor
    /// Spot removal placement: a press-and-drag on `SpotOverlay` places a spot
    /// and chooses its source.
    case spot

    /// The pickers sample a pixel, so the loupe follows the cursor for them.
    /// Spot placement drags out its own circle and needs no swatch.
    var wantsLoupe: Bool { self == .targeted || self == .maskColor }
}

/// The one way a panel arms a canvas tool.
///
/// Every armed-and-waiting-for-a-click control is this button, so they all
/// read the same: accent text and stroke while armed, the label swapped for
/// what the photo now expects. Adding a tool is one `CanvasTool` case, one
/// branch in the canvas, and one `ToolButton` line in the panel that owns it —
/// the same shape adding a slider already has.
struct ToolButton: View {
    let tool: CanvasTool
    let icon: String
    /// Shown while armed, when the armed state changes what the cursor does
    /// (the targeted tool trades the eyedropper for a scope). Defaults to
    /// `icon`.
    var armedIcon: String? = nil
    let label: String
    let armedLabel: String
    var help: String = ""
    let engine: Engine

    private var armed: Bool { engine.tool == tool }

    var body: some View {
        Button {
            engine.tool = armed ? .none : tool
        } label: {
            HStack(spacing: 5) {
                Image(systemName: armed ? (armedIcon ?? icon) : icon)
                    .font(.system(size: 11))
                Text(armed ? armedLabel : label)
                    .font(.system(size: 11))
            }
            .padding(.horizontal, 8)
            .frame(height: 22)
            .contentShape(RoundedRectangle(cornerRadius: 5))
        }
        .buttonStyle(.plain)
        .foregroundStyle(armed ? Palette.accent : Palette.text)
        .overlay(RoundedRectangle(cornerRadius: 5)
            .stroke(armed ? Palette.accent : Palette.line, lineWidth: 1))
        .help(help)
    }
}

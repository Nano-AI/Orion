import SwiftUI

/// The instrument vocabulary: engraved labels, and how a section says it has
/// been touched.
///
/// Everything a camera prints on itself is set the same way — wide-tracked
/// uppercase, small, low contrast, cut into the body rather than drawn on it.
/// That is the register this app's chrome is in, and having it in one file is
/// what stops the third panel from inventing a fourth label style.
///
/// **In the Apple face on purpose.** A camera's engraving is not a typeface a
/// photographer recognizes; the widths and the tracking are what read as
/// engraved. San Francisco carries expanded widths and tabular figures, so the
/// look costs no bundled font, no license file, and no fallback to worry about
/// on a machine that failed to load one.
enum Engraved {

    /// A label cut into the panel. Names of things, never values.
    struct Label: View {
        let text: String
        var color: Color = Palette.dim
        /// Axis marks and captions sit a tier below control names, so they are
        /// set smaller rather than only dimmer — two signals for one rank, which
        /// is what keeps the panel legible at a glance.
        var size: CGFloat = 10
        /// ⚠ Letter-spacing is what makes these read as engraved, but it is
        /// also pure horizontal cost — and the tab bar is the one place in the
        /// program where seven labels compete for a fixed width. Overridable
        /// there and nowhere else.
        var tracking: CGFloat = 1.0

        var body: some View {
            Text(text.uppercased())
                // Tracking, not an expanded width. Both read as engraving, and
                // the expanded face was too wide in practice: a panel of forty
                // labels at that width is a wall of letters, and long names like
                // ROUNDNESS and HIGHLIGHTS ran to the readout. Letter-spacing
                // carries the same signal at a fraction of the horizontal cost.
                .font(.system(size: size, weight: .medium))
                .tracking(tracking)
                .foregroundStyle(color)
        }
    }

    /// A number the instrument is reporting. Tabular so a digit changing width
    /// mid-drag cannot make the row twitch under the pointer.
    struct Readout: View {
        let text: String
        var color: Color = Palette.text
        var size: CGFloat = 10

        var body: some View {
            Text(text)
                .font(.system(size: size, weight: .medium, design: .monospaced))
                .monospacedDigit()
                .foregroundStyle(color)
        }
    }

    /// A section's engraved nameplate: the name, a hairline running out to the
    /// panel edge, and a mark when anything inside has been moved.
    ///
    /// The hairline is the hierarchy. Section names and control names were both
    /// small grey text before, so a panel of forty rows had no level to it and
    /// nothing read first — the developer's own first complaint.
    struct Plate: View {
        let title: String
        let modified: Bool

        var body: some View {
            HStack(spacing: 8) {
                Label(text: title, color: Palette.text)
                // Film-rebate amber, the same ink the site numbers frames in.
                // Structural only, and this is the far end of the window from
                // the photograph — the neutral surround the canvas is judged
                // against stays neutral. planning/DECISIONS.md #63.
                Circle()
                    .fill(Palette.star)
                    .frame(width: 3, height: 3)
                    .opacity(modified ? 1 : 0)
                Rectangle()
                    .fill(Palette.line)
                    .frame(height: 1)
            }
        }
    }
}

/// A section: its engraved nameplate, and its controls.
///
/// A view rather than a function because the plate has to know whether anything
/// below it has been moved, and that arrives as a preference from the controls
/// themselves — which needs somewhere to hold state.
struct SectionPlate<Content: View>: View {
    let title: String
    @ViewBuilder let content: () -> Content

    @State private var modified = false

    var body: some View {
        VStack(alignment: .leading, spacing: 11) {
            Engraved.Plate(title: title, modified: modified)
            content()
        }
        .onPreferenceChange(SectionModifiedKey.self) { modified = $0 }
    }
}

/// Whether any control inside a section has been moved off its base.
///
/// A preference rather than a flag passed in at each `section(…)` call, and that
/// is the whole point: a hand-written list of "which controls does this section
/// contain" goes stale the moment a slider is added, silently, with the section
/// then reporting untouched forever. This is how `lutStrength` once shipped a
/// slider that did nothing — the change-detection list did not know about it.
/// Here the controls report upward and the section cannot be wrong.
struct SectionModifiedKey: PreferenceKey {
    static let defaultValue = false
    static func reduce(value: inout Bool, nextValue: () -> Bool) {
        value = value || nextValue()
    }
}

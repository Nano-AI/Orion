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

    /// What a control does, on hover, instead of as a paragraph under it.
    ///
    /// ⚠ **The text it carries used to be set in the panel**, three lines of
    /// 10-point gray under the slider it explained. Thirty-seven of them, and
    /// the developer's objection was that stuffing prose into a control column
    /// is bad — which it is: the explanation is read once and the slider is used
    /// forever, so the panel paid for the first every time it drew the second.
    /// The Detail panel alone gave up about a third of its height to text
    /// nobody was reading any more.
    ///
    /// `.help` rather than a popover, and that is a deliberate narrowing: it is
    /// the idiom this codebase already uses in ten places, it needs no `@State`
    /// per row, it cannot get stuck open, and **VoiceOver reads it for free** —
    /// a popover would need all four built and kept working. The cost is that a
    /// tooltip is plain text with a system-set delay, so this is not the place
    /// for anything a photographer needs *while* dragging.
    /// What a control does, shown on hover.
    ///
    /// ⚠ **This shipped broken in v0.4.0-alpha.4 and the way it broke is the
    /// lesson.** It was a bare `Image` with `.help(…)` on it. The ⓘ drew
    /// perfectly and explained nothing — SwiftUI's `.help` needs a view that
    /// takes part in hit testing, and an `Image` does not. Every other `.help`
    /// in this app happens to sit on a `Button`, so the pattern had never been
    /// tried anywhere else and looked obviously fine.
    ///
    /// ⚠ **A screenshot cannot catch this and the suite still cannot.** A
    /// capture of a working ⓘ and a dead one are the same pixels, and walking
    /// AppKit offscreen finds nothing to inspect: SwiftUI collapses this panel
    /// into **134 views with 2 `NSButton`s in them**, and `NSView.toolTip` is
    /// empty whether or not `.help` works. The verification for this control is
    /// a person putting a pointer on it. Said plainly so the next person does
    /// not spend the afternoon proving it a different way.
    struct Info: View {
        let text: String

        @State private var inside = false

        var body: some View {
            Image(systemName: "info.circle")
                // Matched to `Label`'s own size: it sits on the nameplate row,
                // and an icon larger than the name beside it reads as a button
                // to press rather than a mark to hover.
                .font(.system(size: 9.5))
                .foregroundStyle(inside ? Palette.text : Palette.faint)
                // A 9.5-point circle is a few points across and genuinely hard
                // to hit, so the target is padded to a comfortable square
                // without the glyph growing.
                .frame(width: 14, height: 14)
                // ⚠ These two lines are the fix. `contentShape` makes the
                // padded square the hit region rather than the glyph, and
                // `onHover` is what installs the tracking area `.help` needs.
                // The highlight it drives is deliberate as well: it is the only
                // feedback that says the icon is live before the tooltip's
                // delay has elapsed.
                .contentShape(Rectangle())
                .onHover { inside = $0 }
                .help(text)
                .accessibilityLabel(Text(text))
        }
    }

    /// A section's engraved nameplate: the name, a hairline running out to the
    /// panel edge, a mark when anything inside has been moved, and — when the
    /// section has something to explain — an ⓘ at the far end.
    ///
    /// The hairline is the hierarchy. Section names and control names were both
    /// small gray text before, so a panel of forty rows had no level to it and
    /// nothing read first — the developer's own first complaint.
    struct Plate: View {
        let title: String
        let modified: Bool
        /// Nil when the section has nothing to say, so no icon is drawn at all
        /// — an ⓘ that explains nothing is worse than no ⓘ, because it teaches
        /// the photographer the icon is not worth hovering.
        var info: String? = nil

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
                // ⚠ After the hairline, so it lands at the panel's right edge
                // and every section's icon is in the same column. Before it,
                // each icon would sit at the end of its own name and the
                // column would ripple down the panel.
                if let info { Info(text: info) }
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
    /// What the section does, shown on hover from the nameplate's ⓘ rather than
    /// set as a paragraph under the controls. See `Engraved.Info`.
    var info: String? = nil
    @ViewBuilder let content: () -> Content

    @State private var modified = false

    var body: some View {
        VStack(alignment: .leading, spacing: 11) {
            Engraved.Plate(title: title, modified: modified, info: info)
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

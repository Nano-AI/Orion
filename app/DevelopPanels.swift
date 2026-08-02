import SwiftUI

// Furniture the develop panels share.
//
// The panels are one file per tool tab — `DevelopPanels+Light`, `+Color`,
// `+Detail`, `+Optics`, `+Mask`, `+Presets` — because that is the unit a
// change arrives in: a new slider belongs to exactly one tab, so naming the
// file after the tab makes *which file* answerable without opening any of
// them. This file holds what more than one tab needs, which today is one
// button.
//
// They are extensions on `Editor` rather than views of their own because
// every control here binds straight to the engine and takes its label, its
// unit and its reset value from the same three helpers; wrapping each panel
// in a struct would mean threading all of that through an initializer to
// gain nothing.
//
// ⚠ `PanelButton` is internal rather than private, and that is the price of
// the split rather than an invitation. Swift's `private` is file-scoped, so a
// control used by Light *and* Detail cannot stay private to either. It is the
// only widening the split needed; every other private member kept its callers
// in its own file. `DECISIONS.md` #122, and #117 which paid the same price.
//
// The crop panel stays with the editor: it is the only one that reads the
// canvas geometry. The tab bar, the panel it dispatches to, and the `section`
// and `slider` helpers every panel here calls are all in `OrionApp.swift`.

/// A panel button that looks like one.
///
/// `.buttonStyle(.plain)` in this palette renders as ordinary text: no border,
/// no fill, nothing that says it can be pressed. Both panel buttons shipped
/// that way, and the screenshot harness is what caught it — the code compiled,
/// the control worked, and it read as a label sitting next to its own caption.
struct PanelButton: View {
    let title: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(Palette.text)
                .lineLimit(1)
                .truncationMode(.middle)
                .padding(.horizontal, 9)
                .padding(.vertical, 4)
                .background(
                    RoundedRectangle(cornerRadius: 5, style: .continuous)
                        .fill(Palette.raised)
                        .overlay(
                            RoundedRectangle(cornerRadius: 5, style: .continuous)
                                .stroke(Palette.line, lineWidth: 1)))
        }
        .buttonStyle(.plain)
    }
}

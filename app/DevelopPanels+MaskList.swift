import SwiftUI

/// The mask stack as cards — one card per mask, its shapes as rows beneath.
///
/// The flat numbered list this replaces made the central question of the panel
/// — *how many masks do I have, and what is each one doing* — a parsing
/// exercise: layers were runs you inferred from link icons, and combining two
/// masks meant finding a button whose meaning depended on a default (#197)
/// that had just changed under it. A card is a mask; its rows are the shapes
/// it is built from; the op each row folds in with is written on the row.
///
/// Its own view struct rather than an `Editor` extension because renaming
/// needs local state (the draft, and which card is editing), and stored
/// properties cannot live in extensions.
struct MaskList: View {
    let engine: Engine

    /// Which layer's name is being edited, and the text as typed. Draft state,
    /// deliberately not the model's: the name lands in the edit only on
    /// submit, so escape or clicking away abandons the rename instead of
    /// committing every keystroke to history.
    @State private var renamingLayer: Int?
    @State private var draft = ""
    @FocusState private var renameFocused: Bool

    var body: some View {
        let runs = MaskLayers.group(engine.maskComponents)

        VStack(spacing: 4) {
            ForEach(runs.indices, id: \.self) { layer in
                card(layer: layer, run: runs[layer])
            }

            // The count the whole redesign exists to make legible, and the
            // cap said out loud instead of an Add button that quietly stops
            // working. Reads the constant, so it follows the cap wherever it
            // goes.
            HStack {
                Text(runs.count == 1 ? "1 mask" : "\(runs.count) masks")
                Spacer(minLength: 0)
                Text("\(engine.maskComponents.count) of \(Engine.maxMaskComponents) shapes")
            }
            .font(.system(size: 10))
            .foregroundStyle(Palette.faint)
            .padding(.horizontal, 2)
        }
    }

    // MARK: One mask

    private func card(layer: Int, run: [Int]) -> some View {
        let start = run[0]
        let holdsSelection = run.contains(engine.selectedMask)

        return VStack(spacing: 0) {
            header(layer: layer, run: run)
            ForEach(run, id: \.self) { i in
                shapeRow(i, position: i - start)
            }
        }
        .padding(3)
        .background(RoundedRectangle(cornerRadius: 5)
            .fill(holdsSelection ? Palette.faint.opacity(0.10) : .clear))
        .overlay(RoundedRectangle(cornerRadius: 5)
            .strokeBorder(holdsSelection ? Palette.accent.opacity(0.6) : Palette.line,
                          lineWidth: 1))
        .opacity(engine.layerHidden(layer) ? 0.45 : 1)
    }

    private func header(layer: Int, run: [Int]) -> some View {
        let start = run[0]
        return HStack(spacing: 6) {
            // The card's eye: the whole mask, one undo step. Outside the
            // select gesture for the same reason the row eyes are — hiding a
            // mask must not move the selection to it.
            Button { engine.toggleLayerHidden(layer) } label: {
                Image(systemName: engine.layerHidden(layer) ? "eye.slash" : "eye")
                    .font(.system(size: 10))
                    .frame(width: 16)
                    .foregroundStyle(engine.layerHidden(layer) ? Palette.faint : Palette.dim)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help(engine.layerHidden(layer) ? "Show this mask" : "Hide this mask")

            if renamingLayer == layer {
                TextField("", text: $draft)
                    .textFieldStyle(.plain)
                    .font(.system(size: 11, weight: .semibold))
                    .focused($renameFocused)
                    .onSubmit {
                        engine.renameMask(layerStartingAt: start, to: draft)
                        renamingLayer = nil
                    }
                    .onExitCommand { renamingLayer = nil }
            } else {
                Text(MaskLayers.displayName(ofLayer: layer, in: engine.maskComponents))
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(Palette.text)
                    .lineLimit(1)
                    // Select on click, rename on double click — the order
                    // matters: a double-click that also moved the selection
                    // would be fine, so the single-click gesture goes second.
                    .gesture(TapGesture(count: 2).onEnded { beginRename(layer: layer, start: start) })
                    .onTapGesture { engine.selectedMask = start }
                    .help("Double-click to rename")
            }

            Spacer(minLength: 0)

            if run.count > 1 {
                Text("\(run.count) shapes")
                    .font(.system(size: 9))
                    .foregroundStyle(Palette.faint)
            }
        }
        .padding(.horizontal, 4)
        .padding(.vertical, 3)
        .contentShape(Rectangle())
        .contextMenu {
            Button("Rename…") { beginRename(layer: layer, start: start) }
            if layer > 0 {
                Divider()
                // The merge the revamp exists for: one act, with a direction,
                // instead of a link icon plus a compose picker found later.
                Button("Subtract from mask above") {
                    engine.mergeIntoLayerAbove(at: start, compose: 1)
                }
                Button("Intersect with mask above") {
                    engine.mergeIntoLayerAbove(at: start, compose: 2)
                }
                Button("Add to mask above") {
                    engine.mergeIntoLayerAbove(at: start, compose: 0)
                }
            }
            Divider()
            Button(engine.layerHidden(layer) ? "Show mask" : "Hide mask") {
                engine.toggleLayerHidden(layer)
            }
        }
    }

    // MARK: One shape

    // ⚠ @ViewBuilder, not a plain `-> some View`: ForEach can re-run this
    // closure with an index from the *previous* photo after `Engine.open`
    // has already replaced `maskComponents` (SwiftUI re-entrancy, #110.3
    // class of bug — a real crash, see the confirmed-crash report). The
    // bounds check below drops the row instead of indexing out of range.
    @ViewBuilder
    private func shapeRow(_ i: Int, position: Int) -> some View {
        if engine.maskComponents.indices.contains(i) {
        let m = engine.maskComponents[i]
        HStack(spacing: 4) {
            Button { engine.toggleMaskHidden(i) } label: {
                Image(systemName: m.hidden ? "eye.slash" : "eye")
                    .font(.system(size: 9))
                    .frame(width: 14)
                    .foregroundStyle(m.hidden ? Palette.faint : Palette.dim)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help(m.hidden ? "Show this shape" : "Hide this shape")

            Button { engine.selectedMask = i } label: {
                HStack(spacing: 6) {
                    // The op the row folds in with, written on the row. The
                    // first shape has none: the fold starts from zero, where
                    // add is the identity and the other two empty the mask —
                    // the engine forces add there, and a glyph it ignores
                    // would be a label that lies.
                    Image(systemName: position == 0 ? "circle.dotted"
                          : m.compose == 1 ? "minus.circle"
                          : m.compose == 2 ? "circle.lefthalf.filled" : "plus.circle")
                        .font(.system(size: 9))
                        .frame(width: 13)
                        .foregroundStyle(position == 0 ? Palette.faint : Palette.dim)
                        .help(position == 0 ? "The mask's base shape"
                              : Editor.composeName(m.compose))
                    Text(MaskLayers.kindName(m.kind))
                    Spacer(minLength: 0)
                    if m.kind == 3 && !m.brushStroke.isEmpty {
                        Text("\(m.brushStroke.count / 2) dabs")
                            .foregroundStyle(Palette.faint)
                    }
                }
                .font(.system(size: 11))
                .padding(.horizontal, 5)
                .padding(.vertical, 2.5)
                .frame(maxWidth: .infinity)
                .background(i == engine.selectedMask
                            ? Palette.faint.opacity(0.18) : Color.clear)
                .clipShape(RoundedRectangle(cornerRadius: 3))
            }
            .buttonStyle(.plain)
            .opacity(m.hidden ? 0.45 : 1)
            .contextMenu {
                if position > 0 {
                    Button("Make its own mask") { engine.setLayerBreak(true, at: i) }
                }
                Button("Remove shape") {
                    engine.removeMaskComponent(at: i)
                    engine.commitMaskGroupEdit("Remove mask")
                }
            }
        }
        .padding(.leading, 14)
        }
    }

    private func beginRename(layer: Int, start: Int) {
        draft = engine.maskComponents[start].name ?? ""
        renamingLayer = layer
        renameFocused = true
    }
}

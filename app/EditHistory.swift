import Foundation

/// Undo and redo for the develop settings.
///
/// Snapshots the whole adjustment set rather than recording deltas. That is the
/// right trade here: the settings are a few dozen floats, so a snapshot costs
/// nothing, and it means a new control cannot silently break undo by forgetting
/// to record itself.
///
/// Slider drags are coalesced. Without that, dragging exposure across the panel
/// would push a hundred entries and undo would crawl back one frame at a time
/// instead of returning you to where you started.
@Observable
final class EditHistory {

    struct Entry {
        let label: String
        let state: DevelopState
        let time: Date
    }

    private(set) var entries: [Entry] = []
    private(set) var position = -1

    /// Drags within this window collapse into the previous entry.
    private let coalesceWindow: TimeInterval = 0.6

    var canUndo: Bool { position > 0 }
    var canRedo: Bool { position >= 0 && position < entries.count - 1 }

    var undoLabel: String? { canUndo ? entries[position].label : nil }
    var redoLabel: String? { canRedo ? entries[position + 1].label : nil }

    /// Most recent first, for a history panel.
    var recent: [Entry] { entries.reversed() }

    func reset(to state: DevelopState) {
        entries = [Entry(label: "Opened", state: state, time: Date())]
        position = 0
    }

    /// Records a change. `label` names the control, and is what coalescing
    /// keys on — consecutive edits to the same control merge.
    func record(_ state: DevelopState, label: String) {
        guard position >= 0 else {
            reset(to: state)
            return
        }

        // A new branch discards anything that was redoable.
        if position < entries.count - 1 {
            entries.removeSubrange((position + 1)...)
        }

        let now = Date()
        if let last = entries.last,
           last.label == label,
           now.timeIntervalSince(last.time) < coalesceWindow {
            entries[entries.count - 1] = Entry(label: label, state: state, time: now)
            return
        }

        entries.append(Entry(label: label, state: state, time: now))
        position = entries.count - 1

        // Bound the depth. Fifty steps is well past what anyone walks back
        // through, and it keeps a long session from growing without limit.
        if entries.count > 50 {
            entries.removeFirst(entries.count - 50)
            position = entries.count - 1
        }
    }

    func undo() -> DevelopState? {
        guard canUndo else { return nil }
        position -= 1
        return entries[position].state
    }

    func redo() -> DevelopState? {
        guard canRedo else { return nil }
        position += 1
        return entries[position].state
    }

    /// Jumps to an absolute point, for clicking an entry in a history panel.
    func jump(to index: Int) -> DevelopState? {
        guard entries.indices.contains(index) else { return nil }
        position = index
        return entries[index].state
    }
}

/// Every develop setting, as a value. Snapshotting this is what makes undo work.
struct DevelopState: Equatable {
    var temperatureK: Float = 5500
    var tint: Float = 0
    var exposureEv: Float = 0
    var highlights: Float = 0
    var shadows: Float = 0
    var whites: Float = 0
    var blacks: Float = 0
    var vibrance: Float = 0
    var saturation: Float = 0
    /// Orion's base rendering, not neutral. See Engine.contrast.
    var contrast: Float = 1.15
    var rotateQuarters: Int32 = 0
    var straightenDeg: Float = 0
    var cropX: Float = 0
    var cropY: Float = 0
    var cropW: Float = 1
    var cropH: Float = 1
    var lensDistortion: Float = 0
    var lensVignette: Float = 0
    var lensCaRed: Float = 0
    var lensCaBlue: Float = 0
    var highlightRecovery: Float = 1
    var denoiseLuma: Float = 0
    var denoiseColour: Float = 0
    var sharpenAmount: Float = 0
    var sharpenRadius: Float = 1
    var sharpenMasking: Float = 0
    var curve = ToneCurve()
    var hueShift = [Float](repeating: 0, count: 8)
    var satShift = [Float](repeating: 0, count: 8)
    var lumShift = [Float](repeating: 0, count: 8)
}

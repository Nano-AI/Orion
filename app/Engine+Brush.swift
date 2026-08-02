import Foundation
import Metal
import MetalKit
import AppKit

/// The brush — mask kind 3, and the one mask primitive with no closed form.
///
/// The live-stroke buffers this drives (`liveStroke`, `liveErase`, `liveIndex`)
/// are `@ObservationIgnored` **stored** properties in `Engine.swift`, along
/// with the comment explaining why. Decision #102's predicate depends on them
/// not invalidating the panel; do not tidy them into observable storage.

extension Engine {

    // ── The brush (mask kind 3) ───────────────────────────────────────────
    var brushRadius: Float {
        get { selected?.brushRadius ?? 0.08 }
        set { editSelected { $0.brushRadius = newValue } }
    }
    var brushFlow: Float {
        get { selected?.brushFlow ?? 0.5 }
        set { editSelected { $0.brushFlow = newValue } }
    }
    var brushHardness: Float {
        get { selected?.brushHardness ?? 0.5 }
        set { editSelected { $0.brushHardness = newValue } }
    }

    /// The selected component's stroke, as normalized points on the displayed
    /// picture.
    var brushStroke: [CGPoint] {
        guard let m = selected else { return [] }
        return Self.points(m.brushStroke)
    }

    /// Pairs a flattened stroke back into points. An odd count is a truncated
    /// sidecar; drop the stray rather than reading past the end.
    private static func points(_ xy: [Float]) -> [CGPoint] {
        var pts: [CGPoint] = []
        pts.reserveCapacity(xy.count / 2)
        for i in stride(from: 0, to: xy.count - 1, by: 2) {
            pts.append(CGPoint(x: CGFloat(xy[i]), y: CGFloat(xy[i + 1])))
        }
        return pts
    }

    /// The selected component's per-dab polarity, as booleans.
    var brushErasePolarity: [Bool] {
        (selected?.brushErase ?? []).map { $0 != 0 }
    }

    /// The press. Seeds the buffers from whatever paint is already on the
    /// component, so a second pass builds on the first.
    func beginBrushStroke() {
        guard selectedMask >= 0 && selectedMask < maskComponents.count else { return }
        liveIndex = selectedMask
        liveStroke = maskComponents[liveIndex].brushStroke
        liveErase = maskComponents[liveIndex].brushErase
        // ⚠ Padded here rather than trusted, for the same reason `pushStroke`
        // pads: the two arrays reach the sidecar separately and a file that
        // predates erasing has a stroke and no polarity.
        let dabs = liveStroke.count / 2
        if liveErase.count < dabs {
            liveErase += [Float](repeating: 0, count: dabs - liveErase.count)
        }
    }

    /// One pointer event's worth of dabs. Appends; never rebuilds.
    func appendBrushDabs(_ points: [CGPoint], erasing: Bool) {
        guard liveIndex >= 0, !points.isEmpty else { return }
        liveStroke.reserveCapacity(liveStroke.count + points.count * 2)
        for p in points {
            liveStroke.append(Float(p.x))
            liveStroke.append(Float(p.y))
        }
        liveErase.append(contentsOf:
            repeatElement(erasing ? 1 : 0, count: points.count))
        pushLiveStroke()
    }

    /// The release. One observable write, and the record is correct again.
    ///
    /// ⚠ Returns whether anything was painted, so the caller can skip the
    /// history entry for a press that laid nothing — recording that would put a
    /// step in the history that undoes to itself.
    @discardableResult
    func endBrushStroke() -> Bool {
        guard liveIndex >= 0 else { return false }
        let index = liveIndex
        liveIndex = -1
        let changed = maskComponents[index].brushStroke != liveStroke
        if changed {
            maskComponents[index].brushStroke = liveStroke
            maskComponents[index].brushErase = liveErase
        }
        liveStroke = []; liveErase = []
        return changed
    }

    /// Sends the live buffers across the facade and renders. `pushAndRender`
    /// minus `onEdit`, and the omission is the whole optimisation.
    ///
    /// ⚠ **The adjustment block still has to go, and the first draft of this
    /// skipped it.** A stroke's dabs travel by their own call, so skipping the
    /// block looked free — but `brush_revision` rides *in* the block, and it is
    /// the only thing the engine compares to decide the mask node is stale (it
    /// never walks a stroke to find out). Uploading dabs without it left the
    /// node clean: the kernel never re-ran, `render()` returned in microseconds
    /// having done nothing, and the paint simply did not appear. It measured
    /// **0.0 ms an event at 45,000 fps**, which is what "did no work" looks
    /// like when you are hoping for "fast".
    ///
    /// `repro/gesture-preview-agrees.txt` caught it, on the check that the
    /// settled picture is the same armed or not — written the day before for a
    /// different reason entirely.
    ///
    /// What *is* safe to skip is `onEdit?(state)`, and that is where the cost
    /// was: it builds a whole `DevelopState` — copying the stroke it was just
    /// handed — and gives it to `Autosave.note`, whose first act is
    /// `state != saved`, a full structural compare of the same arrays. The
    /// autosave's business is the finished stroke, and `endBrushStroke`
    /// delivers one through the normal observable path.
    private func pushLiveStroke() {
        guard isLoaded, !suspended, let handle, liveIndex >= 0 else { return }
        let dabs = liveStroke.count / 2
        guard dabs > 0 else { return }
        let ok: OrionStatus = liveStroke.withUnsafeBufferPointer { p in
            liveErase.withUnsafeBufferPointer { e in
                orion_engine_set_brush_stroke(handle, Int32(liveIndex),
                                              p.baseAddress, e.baseAddress,
                                              Int32(dabs))
            }
        }
        // ⚠ Reported rather than dropped. A refused stroke is paint that does
        // not appear under a hand that is still moving, which reads as a dead
        // trackpad rather than as an error — and the next dab tries again, so
        // without a message there is nothing anywhere to say it happened.
        guard ok == ORION_OK else {
            noteBrushRefusal(ok)
            return
        }
        brushRevisions[liveIndex] &+= 1

        var adj = cAdjustments()
        let pushed = orion_engine_set_adjustments(handle, &adj)
        guard pushed == ORION_OK else {
            noteBrushRefusal(pushed)
            return
        }
        if interacting { renderPreview() } else { render() }
    }

    /// One place for the two brush pushes to complain from.
    ///
    /// ⚠ The stderr line is rate-limited to the *first* refusal of a run of
    /// them. A pointer event fires dozens of times a second, so a line per dab
    /// would be a megabyte of log for one bad stroke and would bury the first
    /// one, which is the only one that carries information. `lastFailure` is
    /// set every time — it is a single published value, and the footer showing
    /// the newest reason is right.
    private func noteBrushRefusal(_ status: OrionStatus) {
        let why = errorText(status)
        if lastFailure == nil {
            FileHandle.standardError.write(
                Data("orion: the brush stroke was refused — \(why)\n".utf8))
        }
        lastFailure = why
    }

    func setBrushStroke(_ points: [CGPoint], erasing: [Bool]? = nil) {
        guard selectedMask >= 0 && selectedMask < maskComponents.count else { return }
        maskComponents[selectedMask].brushStroke =
            points.flatMap { [Float($0.x), Float($0.y)] }
        if let erasing {
            maskComponents[selectedMask].brushErase = erasing.map { $0 ? 1 : 0 }
        } else {
            maskComponents[selectedMask].brushErase =
                [Float](repeating: 0, count: points.count)
        }
        guard pushStroke(selectedMask) else { return }
        pushAndRender()
    }

    /// Sends one component's stroke across the facade and bumps its revision.
    @discardableResult
    private func pushStroke(_ index: Int) -> Bool {
        guard isLoaded, let handle,
              index >= 0 && index < maskComponents.count else { return false }
        let xy = maskComponents[index].brushStroke

        // An empty stroke is a real state — it is what clearing the brush
        // means — so pass the null the facade documents rather than a dangling
        // pointer into an empty array.
        // ⚠ Padded to the dab count rather than trusted. The two arrays are
        // written to the sidecar separately, so a file edited by hand — or by a
        // build between these two changes — can arrive with a stroke and no
        // polarity for the tail of it. Short means "paints", which is the same
        // thing an absent array means.
        var erase = maskComponents[index].brushErase
        let dabs = xy.count / 2
        if erase.count < dabs { erase += [Float](repeating: 0, count: dabs - erase.count) }

        let status: OrionStatus = xy.isEmpty
            ? orion_engine_set_brush_stroke(handle, Int32(index), nil, nil, 0)
            : xy.withUnsafeBufferPointer { p in
                  erase.withUnsafeBufferPointer { e in
                      orion_engine_set_brush_stroke(handle, Int32(index),
                                                    p.baseAddress, e.baseAddress,
                                                    Int32(dabs))
                  }
              }
        guard status == ORION_OK else {
            noteBrushRefusal(status)
            return false
        }
        brushRevisions[index] &+= 1
        return true
    }

    /// Re-sends every component's stroke. Needed whenever the *indexing*
    /// changes rather than the paint — a restore, or a removal that shifts the
    /// rows after it down.
    func pushStrokes() {
        for i in 0..<Self.maxMaskComponents {
            if i < maskComponents.count {
                pushStroke(i)
            } else if isLoaded, let handle {
                // Clear the tail, or a component removed from a group of three
                // leaves its paint in the engine for the next one added to
                // inherit.
                if orion_engine_set_brush_stroke(handle, Int32(i), nil, nil, 0) == ORION_OK {
                    brushRevisions[i] &+= 1
                }
            }
        }
    }

    /// One history entry when a brush stroke finishes, rather than one per dab.
    func commitBrushEdit() { history.record(state, label: "Brush"); log.committed(state, label: "Brush") }

    /// Wipes the selected component's stroke. Undoable, because painting for a
    /// minute and losing it to a misclick is not a thing a photographer should
    /// have to fear.
    func clearBrushStroke() {
        guard !brushStroke.isEmpty else { return }
        setBrushStroke([])
        history.record(state, label: "Clear brush"); log.committed(state, label: "Clear brush")
    }
}

import Foundation

// The arithmetic behind an adjustment row. Kept apart from the row that draws
// it, so the tests can link it without dragging SwiftUI and the engine handle
// in behind it — the same split as CurveMath and CurveEditor.

enum AdjustmentMath {

    /// Whether a control has moved off its base, judged at the precision its
    /// readout actually shows.
    ///
    /// The tolerance is a tenth of the smallest visible step. Comparing exactly
    /// would leave a control marked as modified after it had been dragged back
    /// to where it started — a slider lands on 1e-8, not on zero — and the
    /// panel would go on claiming an edit the picture does not have, offering a
    /// reset that appears to do nothing when pressed.
    ///
    /// Scaling with the readout rather than using a fixed epsilon is what makes
    /// one rule serve both temperature, which prints whole kelvin, and tint,
    /// which prints two decimals over a range of two.
    static func isModified(_ value: Float, from base: Float, decimals: Int) -> Bool {
        abs(value - base) > 0.1 * pow(10, -Float(decimals))
    }

    /// Clamps a dragged value into the range and applies the detent at the base.
    ///
    /// The detent window is 1.5% of the throw either side of the base, and it
    /// snaps *exactly* - so a control dragged back to neutral stops reporting
    /// itself as modified (see `isModified`).
    ///
    /// ⚠ **It only catches an arrival, never a departure.** A drag that began on
    /// the base passes `departing: true` and is never snapped for the rest of
    /// that gesture. On Straighten's -90...90 the window is 2.7° wide, and a
    /// photographer who picked the thumb up at 0 could not produce 0.5° or 2°:
    /// every value under 2.7° collapsed back to 0 until the thumb jumped past
    /// it, which reads as a control that refuses small values. The detent exists
    /// to find neutral on the way back, and a drag that starts on neutral has
    /// already found it.
    ///
    /// `snapping: false` is Option's fine drag and the keyboard nudge, which
    /// never detent at all.
    static func detented(_ raw: Float, base: Float, range: ClosedRange<Float>,
                         departing: Bool, snapping: Bool) -> Float {
        let v = min(max(raw, range.lowerBound), range.upperBound)
        guard snapping, !departing else { return v }
        let span = max(range.upperBound - range.lowerBound, 1e-6)
        return abs(v - base) < span * 0.015 ? base : v
    }

    /// The number a photographer typed into a readout, or nil if it is not one.
    ///
    /// Forgiving about what a person actually types: surrounding whitespace, the
    /// control's own unit or any of the units the panel prints ("°", "rad",
    /// "EV", "K") left on the end, and a comma for the decimal point. A value
    /// outside the range is clamped rather than refused, so "200" on a -90...90
    /// control means "as far as it goes". Anything else is nil and the caller
    /// keeps the old value.
    static func parsed(_ text: String, unit: String, range: ClosedRange<Float>) -> Float? {
        var t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let suffixes = ([unit, "°", "rad", "EV", "K"]).filter { !$0.isEmpty }
        for suffix in suffixes {
            let trimmedUnit = suffix.trimmingCharacters(in: .whitespaces)
            if !trimmedUnit.isEmpty,
               t.range(of: trimmedUnit, options: [.anchored, .backwards, .caseInsensitive]) != nil {
                t = String(t.dropLast(trimmedUnit.count))
                    .trimmingCharacters(in: .whitespaces)
                break
            }
        }
        t = t.replacingOccurrences(of: ",", with: ".")
        guard !t.isEmpty, let v = Float(t), v.isFinite else { return nil }
        return min(max(v, range.lowerBound), range.upperBound)
    }

    /// The typed value nudged by `steps` readout steps (`10^-decimals` each),
    /// clamped to the range. Nil when the text is not a number, so an arrow key
    /// over a half-typed entry leaves it alone.
    static func nudged(_ text: String, steps: Int, unit: String, decimals: Int,
                       range: ClosedRange<Float>) -> Float? {
        guard let v = parsed(text, unit: unit, range: range) else { return nil }
        // Counted in whole printed steps and divided back out, so 2.8 nudged
        // up at one decimal is the float nearest 2.9 rather than 2.8 plus an
        // inexact tenth.
        let scale = pow(10, Float(decimals))
        let next = ((v * scale).rounded() + Float(steps)) / scale
        return min(max(next, range.lowerBound), range.upperBound)
    }
}

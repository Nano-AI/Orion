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
}

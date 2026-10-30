using System.Globalization;

namespace OrionShell;

/// A port of the Swift app's AdjustmentMath.swift: the slider rules every panel shares.
public static class AdjustmentMath {
    /// Changed by more than a tenth of the last shown digit.
    public static bool IsModified(float value, float baseValue, int decimals) =>
        MathF.Abs(value - baseValue) > 0.1f * MathF.Pow(10, -decimals);

    /// Clamped, and snapped to the detent within 1.5% of the span unless the
    /// drag began on it (decision #294).
    public static float Detented(float raw, float baseValue, float min, float max, bool departing, bool snapping = true) {
        float v = Math.Clamp(raw, min, max);
        if (!snapping || departing) return v;
        float span = MathF.Max(max - min, 1e-6f);
        return MathF.Abs(v - baseValue) < span * 0.015f ? baseValue : v;
    }

    /// What a person typed, forgiving the unit, a comma decimal, a true minus
    /// (the readout prints one) and spaces; clamped.
    public static float? Parsed(string text, string unit, float min, float max) {
        string t = text.Trim().Replace('−', '-');
        foreach (var suffix in new[] { unit, "°", "rad", "EV", "K" }.Select(s => s.Trim()).Where(s => s.Length > 0)) {
            if (t.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) {
                t = t[..^suffix.Length].Trim();
                break;
            }
        }
        t = t.Replace(',', '.');
        if (t.Length == 0 || !float.TryParse(t, NumberStyles.Float, CultureInfo.InvariantCulture, out float v) || !float.IsFinite(v))
            return null;
        return Math.Clamp(v, min, max);
    }

    /// Up/Down in the typed field: one step of the last shown digit (Shift: ten).
    public static float? Nudged(string text, int steps, string unit, int decimals, float min, float max) {
        if (Parsed(text, unit, min, max) is not { } v) return null;
        float scale = MathF.Pow(10, decimals);
        return Math.Clamp((MathF.Round(v * scale) + steps) / scale, min, max);
    }
}

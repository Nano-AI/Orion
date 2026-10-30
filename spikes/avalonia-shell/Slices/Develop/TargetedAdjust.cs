using Avalonia.Media;

namespace OrionShell.Develop;

/// A port of app/TargetedAdjust.swift: the colour mixer's eight bands, in the
/// order the engine's hue_shift/sat_shift/lum_shift arrays expect.
public enum HueBand { Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta }

/// The targeted adjustment tool: click a colour in the photo and drag, and the
/// band that pixel belongs to moves. Arming is EngineModel.Tool; this holds
/// only what the tool reads and shows once armed. The canvas slice drives
/// Begin/End from its drag.
public static class TargetedAdjust {
    public enum Mode { Hue, Saturation, Luminance }

    /// Band centres in degrees, mirroring hsl_ops.slang: if these drift apart
    /// the tool adjusts the wrong band.
    public static readonly double[] Centers = [0, 30, 60, 120, 180, 240, 285, 320];

    /// The swatch chroma: moderated (decision #63), just above TrackTint's labels.
    const double SwatchSaturation = 0.60, SwatchBrightness = 0.74;

    public static Mode Current = Mode.Saturation;

    /// The band under the cursor when the drag started.
    public static HueBand? ActiveBand { get; private set; }

    /// A pick landed, or the drag ended: the panel follows the selection.
    public static event Action? Changed;

    public static string Name(HueBand band) => band.ToString();

    /// The OrionAdjustments array a drag in `mode` moves, one slot per band.
    public static string Field(Mode mode) => mode switch {
        Mode.Hue => "hue_shift", Mode.Luminance => "lum_shift", _ => "sat_shift",
    };

    public static IBrush Swatch(HueBand band) =>
        new SolidColorBrush(HsvColor.ToRgb(Centers[(int)band], SwatchSaturation, SwatchBrightness));

    public static HueBand Band(double hue) {
        int best = 0;
        double bestDistance = double.MaxValue;
        for (int i = 0; i < Centers.Length; i++) {
            double raw = Math.Abs(hue - Centers[i]), d = Math.Min(raw, 360 - raw);
            if (d < bestDistance) (bestDistance, best) = (d, i);
        }
        return (HueBand)best;
    }

    /// Hue in degrees from display RGB, or null for a near-grey with no meaningful hue.
    public static double? Hue(double r, double g, double b) {
        double max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b)), delta = max - min;
        if (delta <= 0.02) return null;
        double h = max == r ? (g - b) / delta + (g < b ? 6 : 0)
                 : max == g ? (b - r) / delta + 2
                 : (r - g) / delta + 4;
        h *= 60;
        return h < 0 ? h + 360 : h;
    }

    public static void Begin(HueBand band) { ActiveBand = band; Changed?.Invoke(); }
    public static void End() { ActiveBand = null; Changed?.Invoke(); }
}

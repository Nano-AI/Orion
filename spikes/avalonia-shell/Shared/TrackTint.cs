using Avalonia;
using Avalonia.Media;

namespace OrionShell;

/// A port of app/TrackTint.swift: what each end of a slider would do, painted
/// into its groove. Same hues and the same HSB swatch maths.
public static class TrackTint {
    const double HueTravel = 30, LabelSaturation = 0.55, LabelBrightness = 0.65;

    public static readonly IBrush Temperature = Span(210, 45);   // blue → amber
    public static readonly IBrush Tint = Span(130, 315);         // green → magenta
    public static readonly IBrush Warmth = Temperature;
    public static readonly IBrush LocalTint = Tint;
    public static readonly IBrush FringeRC = Span(5, 185);       // red → cyan
    public static readonly IBrush FringeBY = Span(225, 55);      // blue → yellow
    public static readonly IBrush WheelLuma = Ramp((0, Swatch(0, 0, 0.10)), (1, Swatch(0, 0, 0.90)));

    /// Grey, then the six primaries and secondaries at rising saturation.
    public static readonly IBrush Presence = Ramp(
        new[] { (0.0, Swatch(0, 0)) }.Concat(
            new[] { 0.0, 60, 120, 180, 240, 300 }.Select((h, i) => {
                double t = (i + 1) / 6.0;
                return (0.2 + 0.8 * t, Swatch(h, LabelSaturation * t));
            })).ToArray());

    /// The targeted-colour band centred on `centre` degrees (TargetedAdjust.centers).
    public static IBrush Hue(double centre) =>
        Ramp((0, Swatch(centre - HueTravel)), (0.5, Swatch(centre)), (1, Swatch(centre + HueTravel)));
    public static IBrush Saturation(double centre) => Ramp((0, Swatch(centre, 0)), (1, Swatch(centre, 0.75)));
    public static IBrush Luminance(double centre) =>
        Ramp((0, Swatch(centre, LabelSaturation, 0.18)), (1, Swatch(centre, LabelSaturation, 0.92)));

    static Color Swatch(double degrees, double saturation = LabelSaturation, double brightness = LabelBrightness) =>
        HsvColor.ToRgb(((degrees % 360) + 360) % 360, saturation, brightness);

    static IBrush Span(double from, double to) => Ramp((0, Swatch(from)), (1, Swatch(to)));

    static IBrush Ramp(params (double At, Color Color)[] stops) {
        var brush = new LinearGradientBrush {
            StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
            EndPoint = new RelativePoint(1, 0, RelativeUnit.Relative),
        };
        foreach (var (at, color) in stops) brush.GradientStops.Add(new GradientStop(color, at));
        return brush;
    }
}

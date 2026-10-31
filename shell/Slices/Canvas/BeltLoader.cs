using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace OrionShell.CanvasSlice;

/// What the canvas shows while a photo decodes: Orion's Belt, the app's mark,
/// with a light passing along its three stars. It waits a moment before it
/// appears, so a photo that opens fast never flashes it.
sealed class BeltLoader : Control {
    // Alnitak, Alnilam, Mintaka: rising left to right, as the mark draws them.
    static readonly (double X, double Y, double R)[] Stars = [(6, 18, 4.5), (24, 12, 3.2), (42, 5, 4)];
    static readonly Color Lit = ((ISolidColorBrush)Palette.Accent).Color;
    double _t;

    public BeltLoader() { Width = 48; Height = 24; IsHitTestVisible = false; Opacity = 0; }

    public void Reset() { _t = 0; Opacity = 0; }

    public void Advance(double dt) {
        _t += dt;
        Opacity = Math.Clamp((_t - 0.15) / 0.25, 0, 1);   // nothing for 150 ms, then in over 250
        InvalidateVisual();
    }

    public override void Render(DrawingContext c) {
        double phase = _t * 2.2 % 4.4 - 0.7;   // the light runs past the last star, rests, starts again
        for (int i = 0; i < Stars.Length; i++) {
            var (x, y, r) = Stars[i];
            double g = Math.Exp(-(phase - i) * (phase - i) / 0.35);
            c.DrawEllipse(new ImmutableSolidColorBrush(Lit, 0.22 * g), null, new Point(x, y), r * 2.6, r * 2.6);
            c.DrawEllipse(new ImmutableSolidColorBrush(Lit, 0.3 + 0.7 * g), null, new Point(x, y), r, r);
        }
    }
}

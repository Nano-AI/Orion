using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace OrionShell.CanvasSlice;

/// A port of app/ColorLoupe.swift: the colour under the cursor while a picking
/// tool is armed, so a pick is legible before it is made. Above and left of the
/// pointer, never under it, and never animated (the reported eyedropper lag).
[Panel("canvas-loupe", Region.Canvas, 60)]
public sealed class ColorLoupe : Panel {
    const double Diameter = 46;
    readonly Border _label = new() {
        Background = Ink.Black(0.72), CornerRadius = new CornerRadius(4), Padding = new Thickness(7, 3),
        Child = new TextBlock { FontSize = Faces.Mark, FontWeight = FontWeight.Medium, Foreground = Ink.White() },
    };
    readonly Drawn _swatchLayer;
    Point _swatch;

    public ColorLoupe() {
        Margin = new Thickness(CanvasState.Inset);
        IsHitTestVisible = false;
        _swatchLayer = new Drawn(Draw);
        Children.AddRange([_swatchLayer, _label]);
        CanvasState.Changed += Place;
        if (CanvasState.Alone("canvas-loupe")) CanvasScenes.Loupe(this);
        Place();
    }

    static bool Showing => CanvasState.WantsLoupe && CanvasState.HoverPoint != null;

    void Place() {
        _label.IsVisible = Showing;
        _swatchLayer.InvalidateVisual();
        if (CanvasState.HoverPoint is not { } p || !Showing) return;
        ((TextBlock)_label.Child!).Text = CanvasState.HoverCaption;
        var label = Drawn.Measured(_label);
        // A VStack of swatch, 5, caption, centered on (x, y − diameter).
        double top = p.Y - Diameter - (Diameter + 5 + label.Height) / 2;
        _swatch = new Point(p.X, top + Diameter / 2);
        Drawn.At(_label, p.X - label.Width / 2, top + Diameter + 5);
    }

    void Draw(DrawingContext c) {
        if (!Showing) return;
        var color = CanvasState.HoverColor;
        const double r = Diameter / 2;
        c.DrawEllipse(new SolidColorBrush(color), null, _swatch, r, r);
        // Two rings: dark reads on a pale photo, light on a dark one.
        c.DrawEllipse(null, new Pen(Ink.Black(0.55), 3), _swatch, r - 1.5, r - 1.5);
        c.DrawEllipse(null, new Pen(Ink.White(0.9), 1.5), _swatch, r - 0.75, r - 0.75);
        // The exact sampled pixel. Swift draws it in difference blend; this picks
        // the contrasting ink instead, since a DrawingContext has no blend mode.
        double luma = (0.2126 * color.R + 0.7152 * color.G + 0.0722 * color.B) / 255;
        var cross = new Pen(luma > 0.6 ? Ink.Black(0.85) : Ink.White(0.85), 1);
        c.DrawLine(cross, new Point(_swatch.X - 7, _swatch.Y), new Point(_swatch.X + 7, _swatch.Y));
        c.DrawLine(cross, new Point(_swatch.X, _swatch.Y - 7), new Point(_swatch.X, _swatch.Y + 7));
    }

    static readonly string[] Bands = ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"];
    /// TargetedAdjust.centers, mirroring hsl_ops.slang's band centers.
    static readonly double[] Centers = [0, 30, 60, 120, 180, 240, 285, 320];

    /// `TargetedAdjust.hue`: degrees, or null for a near-grey with no meaningful hue.
    internal static double? Hue(float[] rgb) {
        double r = rgb[0], g = rgb[1], b = rgb[2];
        double max = Math.Max(r, Math.Max(g, b)), delta = max - Math.Min(r, Math.Min(g, b));
        if (delta <= 0.02) return null;
        double h = max == r ? (g - b) / delta + (g < b ? 6 : 0) : max == g ? (b - r) / delta + 2 : (r - g) / delta + 4;
        h *= 60;
        return h < 0 ? h + 360 : h;
    }

    /// `TargetedAdjust.band(forHue:)`: the nearest center, around the wheel.
    internal static int Band(double hue) {
        int best = 0;
        for (int i = 1; i < Centers.Length; i++)
            if (Wrap(hue, Centers[i]) < Wrap(hue, Centers[best])) best = i;
        return best;
        static double Wrap(double a, double b) { double d = Math.Abs(a - b); return Math.Min(d, 360 - d); }
    }

    /// ImageCanvas `hover`: publishes the colour under `p` while a picker is armed.
    /// The swatch is the rendered colour; the band comes from the scene colour,
    /// which does not move as you edit.
    internal static void Track(Point p, Size view) {
        if (!CanvasState.WantsLoupe) { CanvasState.ClearHover(); return; }
        var u = CanvasState.Map(view).ToUnit(p);
        if (u.X is < 0 or > 1 || u.Y is < 0 or > 1 || CanvasNative.Sample(u) is not { } s) { CanvasState.ClearHover(); return; }
        static byte Byte(float v) => (byte)Math.Round(Math.Clamp(v, 0, 1) * 255);
        var hue = Hue(s.Scene);
        string caption = EngineModel.Shared.Tool == CanvasTool.MaskColor ? "click sets the mask color"
                       : hue is { } h ? Bands[Band(h)] : "neutral — no hue";
        CanvasState.SetHover(p, Color.FromRgb(Byte(s.Display[0]), Byte(s.Display[1]), Byte(s.Display[2])), caption);
    }
}

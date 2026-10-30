using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.CanvasSlice;

/// The canvas's lower-left corner (Main.dc.html): the zoom as a translucent
/// pill over the photo, Fit or 100%, and above it, past fit, a port of
/// the Swift app's Navigator.swift: the frame's outline with the visible region inside it.
[Panel("canvas-navigator", Region.Canvas, 50)]
public sealed class Navigator : StackPanel {
    readonly Map _map = new();
    readonly StateButton _fit, _actual;
    readonly Border _pill;

    public Navigator() {
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Bottom;
        Margin = new Thickness(Space.Lg);
        Spacing = Space.Md;
        // A click springs about the view's centre; the keys (0, 9) jump, as keys should.
        var vp = CanvasState.Viewport;
        _fit = Pill.Button("Fit", () => { if (Commands.CanRun("fit")) vp.AnimateZoom(1, new Point(0.5, 0.5)); }, "Fit in window (0)");
        // Actual size from wherever the zoom is.
        _actual = Pill.Button("100%", () => {
            if (Commands.CanRun("actual-size")) vp.AnimateZoom(1 / Math.Max(vp.FitScale, 0.0001), new Point(0.5, 0.5));
        }, "Actual size (9)", numeral: true);
        _pill = Pill.Of(_fit, _actual);
        _pill.HorizontalAlignment = HorizontalAlignment.Left;
        Children.AddRange([_map, _pill]);
        CanvasState.Changed += Refresh;
        if (CanvasState.Alone("canvas-navigator")) CanvasScenes.Navigator();
        Refresh();
    }

    void Refresh() {
        _map.InvalidateMeasure();
        _map.InvalidateVisual();
        var vp = CanvasState.Viewport;
        _pill.IsVisible = CanvasState.Now == CanvasState.Scene.Picture;
        _fit.On = vp.IsFit;
        _actual.On = vp.Percent == 100 && !vp.IsFit;
        _fit.IsEnabled = _actual.IsEnabled = Commands.CanRun("fit") && !vp.Locked;
    }

    sealed class Map : Control {
        const double BoxWidth = 92;

        public Map() => IsHitTestVisible = false;

        static bool Showing => CanvasState.Viewport.PastFit && CanvasState.ImageAspect > 0;

        protected override Size MeasureOverride(Size available) =>
            Showing ? new Size(BoxWidth, BoxWidth / Math.Max(CanvasState.ImageAspect, 0.01)) : default;

        public override void Render(DrawingContext c) {
            if (!Showing) return;
            var vp = CanvasState.Viewport;
            var box = new Rect(Bounds.Size);
            // The canvas's own aspect, inside its 20-point inset.
            var host = (Parent?.Parent as Control)?.Bounds.Size ?? Bounds.Size;
            double viewAspect = (host.Width - 2 * CanvasState.Inset) / Math.Max(host.Height - 2 * CanvasState.Inset, 1);
            var visible = vp.VisibleFraction(CanvasState.ImageAspect, viewAspect);

            c.DrawRectangle(Ink.Black(0.45), null, box, 2, 2);
            c.DrawRectangle(null, new Pen(Ink.White(0.28), 1), box.Deflate(0.5), 2, 2);
            var region = new Rect(box.Width * Math.Max(0, vp.Center.X - visible.Width / 2),
                                  box.Height * Math.Max(0, vp.Center.Y - visible.Height / 2),
                                  box.Width * Math.Min(visible.Width, 1), box.Height * Math.Min(visible.Height, 1));
            c.DrawRectangle(null, new Pen(Palette.Accent, 1.5), region.Deflate(0.75));
        }
    }
}

/// Controls that float over the photo (Main.dc.html's `.float`): the ground
/// at 72% in a faint white hairline, so the picture shows through.
/// ponytail: no backdrop blur and no reduced-transparency check; Avalonia 12 exposes neither.
static class Pill {
    public static readonly IBrush Ground = new SolidColorBrush(((ISolidColorBrush)Palette.Ground).Color, 0.72);

    public static Border Of(params Control[] items) {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Xs };
        row.Children.AddRange(items);
        return new Border { Background = Ground, BorderBrush = Ink.White(0.08), BorderThickness = new Thickness(1),
                            CornerRadius = new CornerRadius(Radius.Lg), Padding = new Thickness(Space.Xs), Child = row };   // concentric: Md buttons, Xs in
    }

    public static StateButton Button(string title, Action click, string help, bool numeral = false) {
        var b = new StateButton(null, title, click, ButtonKind.Segment, help) { Height = 24, MinWidth = 0, CornerRadius = new CornerRadius(Radius.Md) };
        if (numeral && b.Content is TextBlock t) (t.FontFamily, t.FontWeight, t.FontFeatures) = (Faces.Numeral, FontWeight.Medium, Faces.Tabular);
        return b;
    }
}

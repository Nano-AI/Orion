using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.CanvasSlice;

/// A port of app/Navigator.swift: past fit, the frame's outline with the
/// visible region inside it, in the canvas's lower-left corner.
[Panel("canvas-navigator", Region.Canvas, 50)]
public sealed class Navigator : Control {
    const double BoxWidth = 92;

    public Navigator() {
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Bottom;
        Margin = new Thickness(Space.Pad);
        IsHitTestVisible = false;
        CanvasState.Changed += () => { InvalidateMeasure(); InvalidateVisual(); };
        if (CanvasState.Alone("canvas-navigator")) CanvasScenes.Navigator();
    }

    static bool Showing => !CanvasState.Viewport.IsFit && CanvasState.ImageAspect > 0;

    protected override Size MeasureOverride(Size available) =>
        Showing ? new Size(BoxWidth, BoxWidth / Math.Max(CanvasState.ImageAspect, 0.01)) : default;

    public override void Render(DrawingContext c) {
        if (!Showing) return;
        var vp = CanvasState.Viewport;
        var box = new Rect(Bounds.Size);
        // The canvas's own aspect, inside its 20-point inset.
        var host = (Parent as Control)?.Bounds.Size ?? Bounds.Size;
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

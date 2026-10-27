using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Rendering;
using Handle = OrionShell.CanvasSlice.CanvasLayout.CropHandle;

namespace OrionShell.CanvasSlice;

/// A port of app/CropOverlay.swift: on the Crop tab, everything outside the
/// crop is darkened rather than hidden, thirds always on, an outline, and four
/// L-shaped corners. Corners resize (Shift holds the ratio), the interior pans.
[Panel("canvas-crop", Region.Canvas, 10)]
public sealed class CropOverlay : Control, ICustomHitTest {
    const double ArmLength = 26, HandleBox = ArmLength + 18;
    Handle? _dragging;
    Rect _start;
    Point _press;

    public CropOverlay() {
        Margin = new Thickness(CanvasState.Inset);
        ClipToBounds = true;
        CanvasState.Changed += InvalidateVisual;
        if (CanvasState.Alone("canvas-crop")) CanvasScenes.Crop();
    }

    bool Showing => CanvasState.Tab == "Crop" && Frame.Width > 0;

    /// The rendered canvas's rectangle: while the tool is open, the preview canvas.
    Rect Frame => CanvasState.PhotoFrame(Bounds.Size);

    /// The crop in view coordinates: frame-normalized, placed on the preview canvas, placed in the view.
    Rect CropRect {
        get {
            var (origin, size) = CropEdit.Canvas;
            return CanvasLayout.InView(CanvasLayout.OnCanvas(CropEdit.Crop, origin, size), Frame);
        }
    }

    static IEnumerable<(Handle, Point)> Corners(Rect r) => [
        (Handle.TopLeft, r.TopLeft), (Handle.TopRight, r.TopRight),
        (Handle.BottomLeft, r.BottomLeft), (Handle.BottomRight, r.BottomRight)];

    public override void Render(DrawingContext c) {
        if (!Showing) return;
        var r = CropRect;

        // Dimmed to the view's own edges, so a turned picture's overhang is dimmed too.
        var dim = new PathGeometry { FillRule = FillRule.EvenOdd };
        dim.Figures!.Add(Figure(new Rect(Bounds.Size)));
        dim.Figures.Add(Figure(r));
        c.DrawGeometry(Ink.Black(0.55), null, dim);

        var thirds = new Pen(Ink.White(_dragging != null ? 0.5 : 0.3), 0.5);
        for (int i = 1; i < 3; i++) {
            double x = r.X + r.Width * i / 3, y = r.Y + r.Height * i / 3;
            c.DrawLine(thirds, new Point(x, r.Top), new Point(x, r.Bottom));
            c.DrawLine(thirds, new Point(r.Left, y), new Point(r.Right, y));
        }
        c.DrawRectangle(null, new Pen(Ink.White(0.9), 1), r.Deflate(0.5));

        // An L of two arms per corner: it reads as a corner and shows which edges move.
        var shadow = new Pen(Ink.Black(0.35), 5, lineCap: PenLineCap.Round);
        var arm = new Pen(Ink.White(), 3, lineCap: PenLineCap.Round);
        foreach (var (h, p) in Corners(r)) {
            double hx = h is Handle.TopLeft or Handle.BottomLeft ? 1 : -1, vy = h is Handle.TopLeft or Handle.TopRight ? 1 : -1;
            var l = new PolylineGeometry([new Point(p.X + ArmLength * hx, p.Y), p, new Point(p.X, p.Y + ArmLength * vy)], false);
            c.DrawGeometry(null, shadow, l);
            c.DrawGeometry(null, arm, l);
        }
    }

    static PathFigure Figure(Rect r) => new() {
        StartPoint = r.TopLeft, IsClosed = true,
        Segments = [new LineSegment { Point = r.TopRight }, new LineSegment { Point = r.BottomRight }, new LineSegment { Point = r.BottomLeft }],
    };

    /// Corner boxes win over the interior, which is the rectangle less half a box each side.
    Handle? HandleAt(Point p) {
        if (!Showing) return null;
        var r = CropRect;
        foreach (var (h, corner) in Corners(r))
            if (Math.Abs(p.X - corner.X) <= HandleBox / 2 && Math.Abs(p.Y - corner.Y) <= HandleBox / 2) return h;
        var inner = new Rect(r.X + HandleBox / 2, r.Y + HandleBox / 2, Math.Max(r.Width - HandleBox, 1), Math.Max(r.Height - HandleBox, 1));
        return inner.Contains(p) ? Handle.Move : null;
    }

    public bool HitTest(Point point) => HandleAt(point) != null;

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        if (HandleAt(e.GetPosition(this)) is not { } h) return;
        (_dragging, _start, _press) = (h, CropEdit.Crop, e.GetPosition(this));
        // A drag on the picture arms the preview graph, as every slider does (decision #84).
        EngineModel.Shared.BeginInteraction();
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        var frame = Frame;
        if (_dragging is not { } h || frame.Width <= 0 || frame.Height <= 0) return;
        // View points to crop coordinates: the canvas is wider than the frame.
        double size = CropEdit.Canvas.Size;
        var p = e.GetPosition(this);
        double dx = (p.X - _press.X) / frame.Width * size, dy = (p.Y - _press.Y) / frame.Height * size;
        // Polled every tick, so Shift pressed mid-drag takes effect on the next move.
        bool lockAspect = h != Handle.Move && e.KeyModifiers.HasFlag(KeyModifiers.Shift);
        CropEdit.SetCrop(CanvasLayout.CropDrag(h, _start, dx, dy, lockAspect));
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (_dragging == null) return;
        _dragging = null;
        e.Pointer.Capture(null);
        CropEdit.Commit("Crop");
        EngineModel.Shared.EndInteraction();   // after the commit: one full render
    }
}

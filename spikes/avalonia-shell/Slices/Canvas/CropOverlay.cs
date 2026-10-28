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
/// Beyond Swift, the developer's ask: each edge moves its one side, marked by a
/// short bar at its middle, and the pointer shows what a press there will move.
[Panel("canvas-crop", Region.Canvas, 10)]
public sealed class CropOverlay : Control, ICustomHitTest {
    const double ArmLength = 26, HandleBox = ArmLength + 18, EdgeBar = 16;

    /// What a press holds: a corner, an edge (a corner that moves along one axis only), or the interior.
    internal readonly record struct Grip(Handle Handle, StandardCursorType Cursor, bool X = true, bool Y = true);

    static readonly Grip[] Edges = [
        new(Handle.TopLeft, StandardCursorType.TopSide, X: false), new(Handle.BottomRight, StandardCursorType.BottomSide, X: false),
        new(Handle.TopLeft, StandardCursorType.LeftSide, Y: false), new(Handle.BottomRight, StandardCursorType.RightSide, Y: false)];
    static readonly Dictionary<StandardCursorType, Cursor> Cursors = new();

    Grip? _dragging;
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
    internal Rect CropRect {
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
        // A short bar at the middle of each edge: an edge moves too.
        var (mx, my, b) = (r.Center.X, r.Center.Y, EdgeBar / 2);
        foreach (var (p0, p1) in new[] {
                     (new Point(mx - b, r.Top), new Point(mx + b, r.Top)), (new Point(mx - b, r.Bottom), new Point(mx + b, r.Bottom)),
                     (new Point(r.Left, my - b), new Point(r.Left, my + b)), (new Point(r.Right, my - b), new Point(r.Right, my + b)) }) {
            c.DrawLine(shadow, p0, p1);
            c.DrawLine(arm, p0, p1);
        }
    }

    static PathFigure Figure(Rect r) => new() {
        StartPoint = r.TopLeft, IsClosed = true,
        Segments = [new LineSegment { Point = r.TopRight }, new LineSegment { Point = r.BottomRight }, new LineSegment { Point = r.BottomLeft }],
    };

    /// Corners win over edges, edges over the interior: each a band half a box either side of its line.
    internal Grip? GripAt(Point p) {
        if (!Showing) return null;
        var r = CropRect;
        const double t = HandleBox / 2;
        foreach (var (h, corner) in Corners(r))
            if (Math.Abs(p.X - corner.X) <= t && Math.Abs(p.Y - corner.Y) <= t)
                return new Grip(h, h switch {
                    Handle.TopLeft => StandardCursorType.TopLeftCorner, Handle.TopRight => StandardCursorType.TopRightCorner,
                    Handle.BottomLeft => StandardCursorType.BottomLeftCorner, _ => StandardCursorType.BottomRightCorner,
                });
        bool acrossX = p.X > r.Left && p.X < r.Right, acrossY = p.Y > r.Top && p.Y < r.Bottom;
        if (acrossX && Math.Abs(p.Y - r.Top) <= t) return Edges[0];
        if (acrossX && Math.Abs(p.Y - r.Bottom) <= t) return Edges[1];
        if (acrossY && Math.Abs(p.X - r.Left) <= t) return Edges[2];
        if (acrossY && Math.Abs(p.X - r.Right) <= t) return Edges[3];
        return r.Contains(p) ? new Grip(Handle.Move, StandardCursorType.SizeAll) : null;
    }

    public bool HitTest(Point point) => GripAt(point) != null;

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        if (GripAt(e.GetPosition(this)) is not { } g) return;
        (_dragging, _start, _press) = (g, CropEdit.Crop, e.GetPosition(this));
        // A drag on the picture arms the preview graph, as every slider does (decision #84).
        EngineModel.Shared.BeginInteraction();
        e.Pointer.Capture(this);
        e.Handled = true;
        InvalidateVisual();   // the thirds brighten on the press, not on the first move
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        if (_dragging is not { } g) {
            // Hovering: the pointer names what a press here would move.
            Cursor = GripAt(e.GetPosition(this))?.Cursor is { } type
                ? Cursors.TryGetValue(type, out var made) ? made : Cursors[type] = new Cursor(type) : null;
            return;
        }
        var frame = Frame;
        if (frame.Width <= 0 || frame.Height <= 0) return;
        // View points to crop coordinates: the canvas is wider than the frame.
        double size = CropEdit.Canvas.Size;
        var p = e.GetPosition(this);
        double dx = g.X ? (p.X - _press.X) / frame.Width * size : 0, dy = g.Y ? (p.Y - _press.Y) / frame.Height * size : 0;
        // Polled every tick, so Shift pressed mid-drag takes effect on the next move. Corners only:
        // holding the ratio from an edge would move a second side the pointer is not on.
        bool lockAspect = g.Handle != Handle.Move && g.X && g.Y && e.KeyModifiers.HasFlag(KeyModifiers.Shift);
        CropEdit.SetCrop(CanvasLayout.CropDrag(g.Handle, _start, dx, dy, lockAspect));
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (_dragging != null) e.Pointer.Capture(null);   // ends the drag, below
    }

    /// Every drag ends here: a release, or capture taken mid-drag (the window lost
    /// focus, a dialog opened). One history step and one full render either way;
    /// a drag that ended without them left the engine on the preview graph.
    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
        base.OnPointerCaptureLost(e);
        if (_dragging == null) return;
        _dragging = null;
        CropEdit.Commit("Crop");
        EngineModel.Shared.EndInteraction();   // after the commit: one full render
        InvalidateVisual();
    }
}

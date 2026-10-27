using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Rendering;

namespace OrionShell.CanvasSlice;

/// The selected mask component as the overlay places it, in frame coordinates.
// stub(#315): Engine+Mask.swift owns this until the logic moves below the facade
// (mask_components is a struct array no EngineModel field reaches, so a placement never renders)
static class MaskModel {
    public static MaskPlacement Placement = new();
    public static double BrushRadius = 0.08;
    public static int Dabs;

    static MaskModel() => EngineModel.Shared.Opened += () => { Placement = new MaskPlacement(); Dabs = 0; };
}

/// A port of app/MaskOverlay.swift: the gradient drawn on the picture and
/// dragged there, or the brush's round nib. No geometry here: MaskLayout says
/// where the handles are and what a drag means; this draws and forwards presses.
[Panel("canvas-mask", Region.Canvas, 30)]
public sealed class MaskOverlay : Panel, ICustomHitTest {
    readonly Control _rotateIcon = Icons.Create("arrow.clockwise", 7, Ink.Black(0.75));
    readonly Drawn _layer;
    MaskHandle? _dragging;
    Point _grab;
    MaskPlacement? _start;
    bool _painting;
    double _carry;
    Point _last;
    Point? _cursor;

    public MaskOverlay() {
        Margin = new Thickness(CanvasState.Inset);
        ClipToBounds = true;
        _rotateIcon.IsHitTestVisible = false;
        _layer = new Drawn(Draw);
        Children.AddRange([_layer, _rotateIcon]);
        CanvasState.Changed += Refresh;
        if (CanvasState.Alone("canvas-mask")) CanvasScenes.Mask();
        Refresh();
    }

    static MaskPlacement Mask => MaskModel.Placement;
    static bool Showing => CanvasState.Tab == "Mask" && Mask.Kind != 0;
    static bool IsBrush => Mask.Kind == 3;
    PictureMap Map => CanvasState.Map(Bounds.Size);

    void Refresh() {
        bool rotate = Showing && Mask.Kind == 2;
        _rotateIcon.IsVisible = rotate;
        if (rotate) {
            var at = MaskLayout.HandlePoint(MaskHandle.Rotate, Mask, Map);
            Drawn.At(_rotateIcon, at.X - 3.5, at.Y - 3.5);
        }
        _layer.InvalidateVisual();
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e) { base.OnSizeChanged(e); Refresh(); }

    void Draw(DrawingContext c) {
        if (!Showing) return;
        var map = Map;
        if (IsBrush) {
            if (_cursor is not { } at) return;
            var nib = Polygon(MaskLayout.BrushCursor(map.FrameUnit(at), MaskModel.BrushRadius, map));
            using (c.PushClip(map.Rect)) Stroke(c, nib, 3, 1.25, 1, null);
            return;
        }
        // Clipped to the picture: a mask exists on the photograph, not the letterbox.
        using (c.PushClip(map.Rect)) {
            if (Mask.Kind == 1)
                foreach (var (t, dashed) in new[] { (0.0, true), (1.0, true), (0.5, false) }) {
                    var (a, b) = MaskLayout.IsoLine(Mask, t);
                    Stroke(c, new LineGeometry(map.FramePoint(a), map.FramePoint(b)), 3.5, 1.5, 1, dashed ? [5, 5] : null);
                }
            if (Mask.Kind == 2) {
                Stroke(c, Polygon(MaskLayout.Outline(Mask, 1).Select(map.FramePoint)), 3.5, 1.5, 1, null);
                if (Mask.Feather > 0.01)
                    Stroke(c, Polygon(MaskLayout.Outline(Mask, 1 - Mask.Feather).Select(map.FramePoint)), 3.5, 1.5, 0.75, [4, 5]);
            }
        }
        // Handles stay unclipped: one dragged to the edge must stay grabbable.
        foreach (var h in MaskLayout.Handles(Mask)) {
            var p = MaskLayout.HandlePoint(h, Mask, map);
            var (outer, inner) = h switch { MaskHandle.Center => (15.0, 11.0), MaskHandle.Rotate => (14, 10), _ => (12, 8) };
            c.DrawEllipse(Ink.Halo, null, p, outer / 2, outer / 2);
            c.DrawEllipse(Ink.White(), null, p, inner / 2, inner / 2);
            if (h == MaskHandle.Center) c.DrawEllipse(Ink.Black(0.7), null, p, 2, 2);
        }
    }

    /// A thin white line over a dark halo; dashes are in points, as Swift gives them.
    static void Stroke(DrawingContext c, Geometry g, double halo, double line, double opacity, double[]? dash) {
        c.DrawGeometry(null, new Pen(Ink.Halo, halo, dash == null ? null : new DashStyle(dash.Select(d => d / halo), 0)), g);
        c.DrawGeometry(null, new Pen(Ink.White(opacity), line, dash == null ? null : new DashStyle(dash.Select(d => d / line), 0)), g);
    }

    /// Closed; "filled" only closes the figure, since nothing here passes a fill brush.
    static Geometry Polygon(IEnumerable<Point> points) => new PolylineGeometry(points.ToList(), true);

    public bool HitTest(Point p) => Showing && (IsBrush ? Map.Rect.Contains(p) : MaskLayout.Hit(p, Mask, Map) != null);

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        var map = Map;
        var p = e.GetPosition(this);
        if (IsBrush) {
            (_painting, _carry, _last, MaskModel.Dabs) = (true, 0, map.FrameUnit(p), 1);
            // stub(#315): Engine+Brush.swift owns this until the logic moves below the facade (beginBrushStroke)
        } else if (MaskLayout.Hit(p, Mask, map) is { } h) {
            // Armed only once a handle is grabbed: a miss is a pan, not an edit.
            (_dragging, _grab, _start) = (h, p, Mask);
        } else return;
        EngineModel.Shared.BeginInteraction();
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        var p = e.GetPosition(this);
        var map = Map;
        if (IsBrush) { _cursor = p; _layer.InvalidateVisual(); }
        if (_painting) {
            var here = map.FrameUnit(p);
            // stub(#315): Engine+Brush.swift owns this until the logic moves below the facade (appendBrushDabs)
            MaskModel.Dabs += MaskLayout.BrushDabs(_last, here, MaskModel.BrushRadius, ref _carry).Count;
            _last = here;
        } else if (_dragging is { } h && _start != null) {
            MaskModel.Placement = MaskLayout.Drag(h, _grab, p, _start, map);
            Refresh();
        }
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (!_painting && _dragging == null) return;
        // stub(#315): EditHistory.swift owns this until the logic moves below the facade (commitMaskEdit, commitBrushEdit)
        (_painting, _dragging, _start, _carry) = (false, null, null, 0);
        e.Pointer.Capture(null);
        EngineModel.Shared.EndInteraction();
    }

    protected override void OnPointerExited(PointerEventArgs e) { _cursor = null; _layer.InvalidateVisual(); }
}

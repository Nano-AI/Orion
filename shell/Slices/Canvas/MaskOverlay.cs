using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Rendering;
using Avalonia.Threading;
using OrionShell.Develop;
using OrionShell.Masks;

namespace OrionShell.CanvasSlice;

/// CanvasScenes poses its radial through this; a placement written here is a real row now.
static class MaskModel {
    public static MaskPlacement Placement {
        get => MaskGroup.Shared.Current is { } m ? MaskPlacement.Of(m) : new MaskPlacement();
        set {
            var group = MaskGroup.Shared;
            if (group.Current is { } m && m.Kind == value.Kind) { value.WriteTo(m); group.Push("Place mask"); }
            else group.Add(value.Kind, value.WriteTo);
        }
    }
}

/// A port of the Swift app's MaskOverlay.swift, joined to the rows the engine renders: the
/// selected shape's outline and handles, a pin for every other mask, the brush
/// ring, and the draw tools' press-and-drag. No geometry here: MaskLayout says
/// where things are and what a drag means; this draws, forwards presses and
/// writes the result to MaskGroup, which pushes it to the engine.
[Panel("canvas-mask", Region.Canvas, 30)]
public sealed class MaskOverlay : Panel, ICustomHitTest {
    enum Gesture { None, Handle, Draw, Paint }

    static bool ReduceMotion => OrionShell.Motion.Reduced;
    static readonly Cursor Hand = new(StandardCursorType.Hand), Cross = new(StandardCursorType.Cross), NoCursor = new(StandardCursorType.None);
    readonly Control _rotateIcon = Icons.Create("arrow.clockwise", 7, Ink.Black(0.75));
    readonly Drawn _layer;
    readonly Border _pill;
    readonly TextBlock _pillText = new() { FontSize = TypeSize.Base, Foreground = Palette.Text, VerticalAlignment = VerticalAlignment.Center };
    readonly DispatcherTimer _tick = new() { Interval = TimeSpan.FromMilliseconds(16) };
    readonly List<(DateTime At, double Over)> _trail = [];
    Gesture _gesture;
    MaskHandle _handle;
    MaskHandle? _hot;                      // pressed: accent from pointer-down, before any move
    Point _grab, _drawFrom, _last;
    MaskPlacement? _start, _shown;         // _shown: the outline when it is not the model (banded, springing)
    MaskPlacement? _springFrom;
    SpringBack _spring;
    DateTime _springAt;
    bool _erasing, _pillShown, _hovering;
    double _carry;
    Point? _cursor;
    (int, int)? _fadedFor;

    public MaskOverlay() {
        Margin = new Thickness(CanvasState.Inset);
        ClipToBounds = true;
        _rotateIcon.IsHitTestVisible = false;
        _layer = new Drawn(Draw);
        _pill = Pill();
        Children.AddRange([_layer, _rotateIcon, _pill]);
        _tick.Tick += (_, _) => Step();
        CanvasState.Changed += Refresh;
        G.Changed += Refresh;
        if (CanvasState.Alone("canvas-mask")) CanvasScenes.Mask();
        Refresh();
    }

    static MaskGroup G => MaskGroup.Shared;
    static bool Showing => CanvasState.Tab == "Mask" && !Chrome.ChromeStubs.DocumentEditsLocked;
    static bool IsBrush => G.Drawing == 3 || (G.Drawing == 0 && G.Current?.Kind == 3);
    PictureMap Map => CanvasState.Map(Bounds.Size);
    MaskPlacement? Shape => _shown ?? (G.Current is { Kind: 1 or 2 } m ? MaskPlacement.Of(m) : null);

    void Refresh() {
        G.Showing = Showing;
        var shape = Showing && G.Drawing == 0 ? Shape : null;
        _rotateIcon.IsVisible = shape?.Kind == 2;
        if (shape?.Kind == 2) {
            var at = MaskLayout.HandlePoint(MaskHandle.Rotate, shape, Map);
            Drawn.At(_rotateIcon, at.X - 3.5, at.Y - 3.5);
        }
        ShowPill(Showing && G.Drawing != 0);
        // A newly selected mask's outline fades in (150 ms, ease-out) rather than blinking on.
        var key = (G.Selected, G.Kind);
        if (Showing && G.Current != null && _fadedFor != key) {
            _fadedFor = key;
            _layer.Transitions = null;
            _layer.Opacity = 0;
            Fade(_layer, 1, 150, null);
        }
        _layer.InvalidateVisual();
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e) { base.OnSizeChanged(e); Refresh(); }

    // MARK: Drawing

    void Draw(DrawingContext c) {
        if (!Showing) return;
        var map = Map;
        foreach (var (row, at) in Pins(map)) Pin(c, at, G.Hover == G.LayerOf(row));
        if (G.Current is not { } m) return;
        if (IsBrush) { BrushRing(c, map, m); return; }
        if (G.Drawing != 0 && _gesture != Gesture.Draw) return;   // armed: the next press draws, the old handles step back

        using (c.PushClip(map.Rect)) {
            // The selected mask's other shapes, faint, so "radial minus brush" reads as one thing.
            foreach (int i in G.Runs()[G.SelectedLayer])
                if (i != G.Selected && G.Shapes[i].Kind is 1 or 2) Lines(c, map, MaskPlacement.Of(G.Shapes[i]), 0.4);
            if (Shape is { } shape) Lines(c, map, shape, 1);
        }
        // Handles stay unclipped: one dragged to the edge must stay grabbable.
        if (Shape is not { } s) return;
        foreach (var h in MaskLayout.Handles(s)) {
            var p = MaskLayout.HandlePoint(h, s, map);
            bool hot = h == _hot;
            if (h == MaskHandle.Feather) {   // hollow: it shapes the edge, it does not move anything
                c.DrawEllipse(null, new Pen(Ink.Halo, 3.5), p, 5, 5);
                c.DrawEllipse(null, new Pen(hot ? Palette.Accent : Ink.White(), 1.5), p, 5, 5);
                continue;
            }
            var (outer, inner) = h switch { MaskHandle.Center => (15.0, 11.0), MaskHandle.Rotate => (14, 10), _ => (12, 8) };
            if (hot) (outer, inner) = (outer + 4, inner + 4);
            c.DrawEllipse(Ink.Halo, null, p, outer / 2, outer / 2);
            c.DrawEllipse(hot ? Palette.Accent : Ink.White(), null, p, inner / 2, inner / 2);
            if (h == MaskHandle.Center) c.DrawEllipse(Ink.Black(0.7), null, p, 2, 2);
        }
    }

    static void Lines(DrawingContext c, PictureMap map, MaskPlacement m, double opacity) {
        if (m.Kind == 1)
            foreach (var (t, dashed) in new[] { (0.0, true), (1.0, true), (0.5, false) }) {
                var (a, b) = MaskLayout.IsoLine(m, t);
                Stroke(c, new LineGeometry(map.FramePoint(a), map.FramePoint(b)), opacity, dashed ? [5, 5] : null);
            }
        if (m.Kind == 2) {
            Stroke(c, Polygon(MaskLayout.Outline(m, 1).Select(map.FramePoint)), opacity, null);
            if (m.Feather > 0.01) Stroke(c, Polygon(MaskLayout.Outline(m, 1 - m.Feather).Select(map.FramePoint)), opacity * 0.75, [4, 5]);
        }
    }

    /// The nib: the outer ring is the size, the dashed inner one the hard core
    /// (1 − feather), and a bar across the middle while erasing. Where the
    /// pointer is, or mid-photo while a size slider moves.
    void BrushRing(DrawingContext c, PictureMap map, MaskShape m) {
        var at = _cursor ?? (G.BrushPreview ? map.Rect.Center : (Point?)null);
        if (at is not { } p) return;
        var u = map.FrameUnit(p);
        using var clip = c.PushClip(map.Rect);
        Stroke(c, Polygon(MaskLayout.BrushCursor(u, m.BrushRadius, map)), 1, null);
        if (m.BrushHardness is > 0.05f and < 0.95f)
            Stroke(c, Polygon(MaskLayout.BrushCursor(u, m.BrushRadius * m.BrushHardness, map)), 0.6, [3, 4]);
        if (G.BrushErasing || _erasing) Stroke(c, new LineGeometry(new Point(p.X - 5, p.Y), new Point(p.X + 5, p.Y)), 1, null);
    }

    static void Pin(DrawingContext c, Point at, bool hot) {
        c.DrawEllipse(Ink.Black(0.35), new Pen(Ink.Halo, 3.5), at, 5.5, 5.5);
        c.DrawEllipse(null, new Pen(hot ? Palette.Accent : Ink.White(0.9), 1.5), at, 5.5, 5.5);
    }

    /// A white line over a dark halo; dashes are in points, as Swift gives them.
    static void Stroke(DrawingContext c, Geometry g, double opacity, double[]? dash) {
        c.DrawGeometry(null, new Pen(Ink.Black(0.55 * opacity), 3.5, dash == null ? null : new DashStyle(dash.Select(d => d / 3.5), 0)), g);
        c.DrawGeometry(null, new Pen(Ink.White(opacity), 1.5, dash == null ? null : new DashStyle(dash.Select(d => d / 1.5), 0)), g);
    }

    static Geometry Polygon(IEnumerable<Point> points) => new PolylineGeometry(points.ToList(), true);

    /// One pin per shape outside the selected mask: a gradient's centre, a stroke's first dab.
    static IEnumerable<(int Row, Point At)> Pins(PictureMap map) {
        int layer = G.SelectedLayer;
        for (int i = 0; i < G.Shapes.Count; i++) {
            var m = G.Shapes[i];
            if (G.LayerOf(i) == layer || m.Hidden) continue;
            if (m.Kind is 1 or 2) yield return (i, map.FramePoint(new Point(m.CenterX, m.CenterY)));
            else if (m.Kind == 3 && m.BrushStroke.Count >= 2) yield return (i, map.FramePoint(new Point(m.BrushStroke[0], m.BrushStroke[1])));
        }
    }

    static int? PinAt(Point p, PictureMap map) =>
        Pins(map).Where(pin => PictureMap.Dist(p, pin.At) <= 11).Select(pin => (int?)pin.Row).FirstOrDefault();

    // MARK: The draw tools' pill: in from the bottom edge, out the same way.

    Border Pill() {
        var cancel = new StateButton(null, "Cancel", () => G.Drawing = 0) { Kind = ButtonKind.Quiet, Height = 22, Focusable = false };
        return new Border {
            Background = new SolidColorBrush(((ISolidColorBrush)Palette.Ground).Color, 0.9), BorderBrush = Palette.Line,
            BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(15), Padding = new Thickness(Space.Pad, 4, 4, 4),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Bottom,
            Margin = new Thickness(0, 0, 0, Space.Lg), Opacity = 0, IsHitTestVisible = false,
            RenderTransform = TransformOperations.Parse("translateY(16px)"),
            Child = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Md, Children = { _pillText, cancel } },
        };
    }

    void ShowPill(bool show) {
        if (show) _pillText.Text = G.Drawing switch {
            1 => "Drag from where the effect is full to where it fades out.",
            2 => "Drag on the photo to draw a radial mask. A click drops one there.",
            _ => $"Paint on the photo. Hold {(OperatingSystem.IsMacOS() ? "Option" : "Alt")} to erase.",
        };
        if (_pillShown == show) return;
        _pillShown = show;
        _pill.IsHitTestVisible = show;
        int ms = show ? 180 : 140;   // exits a little quicker than it enters
        Fade(_pill, show ? 1 : 0, ms, show ? "translateY(0px)" : "translateY(16px)");
    }

    /// Opacity (and a transform) to a value through a transition, which Avalonia
    /// starts from the presented value: interrupting it midway turns it round there.
    static void Fade(Control control, double opacity, int ms, string? transform) {
        bool animate = !ReduceMotion && ms > 0;
        control.Transitions = animate ? [
            new DoubleTransition { Property = OpacityProperty, Duration = TimeSpan.FromMilliseconds(ms), Easing = new CubicEaseOut() },
            new TransformOperationsTransition { Property = RenderTransformProperty, Duration = TimeSpan.FromMilliseconds(ms), Easing = new CubicEaseOut() },
        ] : null;
        control.Opacity = opacity;
        if (transform != null) control.RenderTransform = TransformOperations.Parse(transform);
    }

    // MARK: Pointer

    public bool HitTest(Point p) {
        if (!Showing) return false;
        var map = Map;
        if (G.Drawing != 0 || IsBrush) return map.Rect.Contains(p);
        return (Shape is { } s && MaskLayout.Hit(p, s, map) != null) || PinAt(p, map) != null;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
        var map = Map;
        var p = e.GetPosition(this);
        var q = map.FrameUnit(p);
        StopSpring();   // interrupted: the next drag starts from what is on screen
        var engine = EngineModel.Shared;
        if (G.Drawing is 1 or 2) {
            int kind = G.Drawing;
            engine.BeginInteraction();
            if (!G.Create(kind, MaskLayout.Drawn(kind, q, q, click: true).WriteTo)) { engine.EndInteraction(); return; }
            G.Drawing = 0;
            (_gesture, _grab, _drawFrom) = (Gesture.Draw, p, q);
        } else if (IsBrush) {
            engine.BeginInteraction();
            if (G.Drawing == 3) {
                if (!G.Create(3, null)) { engine.EndInteraction(); return; }
                G.Drawing = 0;
            }
            _erasing = G.BrushErasing || e.KeyModifiers.HasFlag(KeyModifiers.Alt);
            (_gesture, _carry, _last, _cursor) = (Gesture.Paint, 0, q, p);
            G.Paint([(q.X, q.Y)], _erasing);   // the press lays the first dab
        } else if (Shape is { } s && MaskLayout.Hit(p, s, map) is { } h) {
            (_gesture, _handle, _hot, _grab, _start) = (Gesture.Handle, h, h, p, s);
            _trail.Clear();
            engine.BeginInteraction();
        } else if (PinAt(p, map) is { } row) {
            G.Selected = row;   // a pin is a selection, not a drag
            e.Handled = true;
            return;
        } else return;
        e.Pointer.Capture(this);
        e.Handled = true;
        _layer.InvalidateVisual();
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        var p = e.GetPosition(this);
        var map = Map;
        if (IsBrush) { _cursor = p; G.BrushPreview = false; _layer.InvalidateVisual(); }
        if (_gesture == Gesture.None) { Hover(p, map); return; }
        if (G.Current is not { } m) return;
        var q = map.FrameUnit(p);
        switch (_gesture) {
            case Gesture.Paint:
                var dabs = MaskLayout.BrushDabs(_last, q, m.BrushRadius, ref _carry);
                _last = q;
                if (dabs.Count > 0) G.Paint(dabs.Select(d => (d.X, d.Y)), _erasing);
                return;
            case Gesture.Draw:
                Place(m, MaskLayout.Drawn(m.Kind, _drawFrom, q, click: PictureMap.Dist(p, _grab) < 4));
                return;
            case Gesture.Handle when _start != null:
                Place(m, MaskLayout.Drag(_handle, _grab, p, _start, map));
                return;
        }
    }

    /// The engine gets the limited shape; the outline shows the rubber band past it.
    void Place(MaskShape m, MaskPlacement raw) {
        var model = raw.Limited();
        model.WriteTo(m);
        var banded = raw.Banded();
        double over = banded.Distance(model);
        _shown = over > 1e-9 ? banded : null;
        _trail.Add((DateTime.Now, over));
        _trail.RemoveAll(t => (DateTime.Now - t.At).TotalMilliseconds > 100);
        G.Push();
        Refresh();
    }

    /// A pin under the pointer previews its mask's coverage until the pointer leaves it.
    void Hover(Point p, PictureMap map) {
        int? row = IsBrush || G.Drawing != 0 ? null : PinAt(p, map);
        Cursor = G.Drawing is 1 or 2 ? Cross : IsBrush ? NoCursor
               : row != null || (Shape is { } s && MaskLayout.Hit(p, s, map) != null) ? Hand : null;
        if (row is { } r) { _hovering = true; G.Hover = G.LayerOf(r); }
        else if (_hovering) { _hovering = false; G.Hover = null; }
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (_gesture == Gesture.None) return;
        e.Pointer.Capture(null);
        End();
    }

    /// Harden: a capture taken away mid-drag (a window switch, a second pointer)
    /// ends the gesture as a release would, so the next press works.
    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
        base.OnPointerCaptureLost(e);
        End();
    }

    void End() {
        if (_gesture == Gesture.None) return;
        var gesture = _gesture;
        _gesture = Gesture.None;
        _hot = null;
        _carry = 0;
        string kind = MaskGroup.KindName(G.Kind).ToLowerInvariant();
        EditHistory.Label(gesture switch {
            Gesture.Paint => _erasing ? "Erase" : "Paint",
            Gesture.Draw => $"Draw {kind}",
            _ => _handle switch {
                MaskHandle.Center or MaskHandle.Body => $"Move {kind}",
                MaskHandle.Rotate => $"Rotate {kind}",
                MaskHandle.Feather => $"Feather {kind}",
                _ => $"Resize {kind}",
            },
        });
        EngineModel.Shared.EndInteraction();
        if (_shown != null) StartSpring();
        G.Settle();
    }

    protected override void OnPointerExited(PointerEventArgs e) {
        _cursor = null;
        if (_hovering) { _hovering = false; G.Hover = null; }
        _layer.InvalidateVisual();
    }

    // MARK: The spring back from a rubber band, carrying the release velocity.

    void StartSpring() {
        if (ReduceMotion || _shown == null || G.Current == null) { _shown = null; Refresh(); return; }
        double over = _shown.Distance(MaskPlacement.Of(G.Current)), v = 0;
        if (_trail.Count >= 2 && over > 1e-9) {
            double dt = (_trail[^1].At - _trail[0].At).TotalSeconds;
            if (dt > 0) v = (_trail[^1].Over - _trail[0].Over) / dt / over;
        }
        (_springFrom, _spring, _springAt) = (_shown, new SpringBack(v), DateTime.Now);
        _tick.Start();
    }

    void Step() {
        double t = (DateTime.Now - _springAt).TotalSeconds;
        if (G.Current is not { } m || _springFrom == null || _spring.Settled(t)) { StopSpring(); _shown = null; }
        else _shown = MaskPlacement.Of(m).Toward(_springFrom, 1 - _spring.Value(t));
        Refresh();
    }

    void StopSpring() {
        _tick.Stop();
        _springFrom = null;
    }
}

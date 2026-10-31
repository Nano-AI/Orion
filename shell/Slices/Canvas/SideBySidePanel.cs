using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace OrionShell.CanvasSlice;

/// The side-by-side compare's two panes over the canvas (decision #400): the
/// select on the left, the candidate on the right, each with its name and marks
/// beneath it, and one floating bar for the link, swap and done. Hidden, and
/// letting every press through, while the compare is off. Order 70: over every
/// overlay, which belong to the single photo the canvas shows.
[Panel("canvas-side-by-side", Region.Canvas, 70)]
public sealed class SideBySidePanel : Grid {
    readonly StateButton _link;

    public SideBySidePanel() {
        Background = Palette.Surround;   // the canvas and its overlays stay under it, out of sight
        ColumnDefinitions = new ColumnDefinitions("*,*");
        RowDefinitions = new RowDefinitions("Auto,*");   // the bar above the pictures, never over them
        // Each picture inset from the sides as the canvas insets its one, with one inset between them.
        const double i = CanvasState.Inset;
        var left = new ComparePane(SideBySide.Left) { Margin = new Thickness(i, Space.Md, i / 2, Space.Md) };
        var right = new ComparePane(SideBySide.Right) { Margin = new Thickness(i / 2, Space.Md, i, Space.Md) };
        SetRow(left, 1);
        SetRow(right, 1);
        SetColumn(right, 1);
        _link = Pill.Button("Linked", SideBySide.ToggleLink, "Zoom and pan both photos together (L)");
        var bar = Pill.Of(_link,
                          Pill.Button("Swap", () => Commands.Run("compare-swap"), "Swap the two photos (Down)"),
                          Pill.Button("Done", () => Commands.Run("compare-side-by-side"), "Back to one photo (C)"));
        bar.HorizontalAlignment = HorizontalAlignment.Center;
        bar.VerticalAlignment = VerticalAlignment.Top;
        bar.Margin = new Thickness(0, Space.Md, 0, 0);
        SetColumnSpan(bar, 2);
        Children.AddRange([left, right, bar]);
        // A wheel or pinch in the inset between the panes stops here: the canvas under it hears its region's too.
        AddHandler(PointerWheelChangedEvent, (_, e) => e.Handled = true);
        AddHandler(PointerTouchPadGestureMagnifyEvent, (_, e) => e.Handled = true);
        SideBySide.Changed += Refresh;
        if (CanvasState.Alone("canvas-side-by-side")) CanvasScenes.SideBySide();
        Refresh();
    }

    void Refresh() {
        IsVisible = SideBySide.On;
        _link.On = SideBySide.Linked;
        if (_link.Content is TextBlock t) t.Text = SideBySide.Linked ? "Linked" : "Unlinked";
    }
}

/// One side: its picture at its viewport's zoom and pan, the hand's gestures on
/// it (the canvas's, same constants), and beneath it its caption.
sealed class ComparePane : Grid {
    static readonly IPen ActiveRing = new Pen(Palette.Accent, 2);
    readonly SideBySide.Side _side;
    readonly Drawn _picture;
    readonly TextBlock _note = new() { FontSize = Faces.Label, Foreground = Palette.Dim, TextAlignment = TextAlignment.Center,
                                       TextWrapping = TextWrapping.Wrap, MaxWidth = 300, HorizontalAlignment = HorizontalAlignment.Center,
                                       VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false };
    readonly TextBlock _role = new() { FontSize = Faces.Mark, Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center };
    readonly TextBlock _name = new() { FontSize = Faces.Mark, Foreground = Palette.Text, VerticalAlignment = VerticalAlignment.Center };
    readonly StackPanel _marks = new() { Orientation = Orientation.Horizontal, Spacing = 1, VerticalAlignment = VerticalAlignment.Center };
    Point? _anchor;
    readonly List<(ulong T, double V)> _x = [], _y = [];

    public ComparePane(SideBySide.Side side) {
        _side = side;
        Background = Brushes.Transparent;   // the whole pane takes a press; the picture clips itself, the ring may not
        _picture = new Drawn(Draw);
        var caption = new Border {
            Background = Pill.Ground, BorderBrush = Ink.White(0.08), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Radius.Lg), Padding = new Thickness(Space.Sm, Space.Gap),
            HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, Space.Md, 0, 0), IsHitTestVisible = false,
            Child = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, Children = { _role, _name, _marks } },
        };
        RowDefinitions = new RowDefinitions("*,Auto");
        SetRow(caption, 1);
        Children.AddRange([_picture, _note, caption]);
        SideBySide.Changed += Refresh;
        SideBySide.Viewed += () => { Place(); _picture.InvalidateVisual(); };   // a gesture's frame: no caption rebuilt
        _picture.SizeChanged += (_, _) => Place();
        AddHandler(PointerTouchPadGestureMagnifyEvent, Magnify);
        Refresh();
    }

    void Refresh() {
        if (!SideBySide.On) return;
        var s = _side;
        _role.Text = s.Select ? "Select" : "Candidate";
        _name.Text = s.Name;
        _marks.Children.Clear();
        if (s.Photo is { } p) {
            if (p.Rejected) _marks.Children.Add(new TextBlock { Text = "Rejected", FontSize = Faces.Mark, Foreground = Palette.Reject });
            else for (int i = 0; i < p.Rating; i++) _marks.Children.Add(Icons.Create("star.fill", IconSize.Sm, Palette.Star));
        }
        bool picture = s.Preview != null && s.Session != null;
        _note.Text = s.Problem ?? (picture ? "" : s.Index < 0 ? "" : $"Decoding {s.Name}");
        _note.Foreground = s.Problem != null ? Palette.Text : Palette.Dim;
        _note.IsVisible = _note.Text.Length > 0;
        Place();
        _picture.InvalidateVisual();
    }

    /// The picture's map in this pane, with the viewport told this pane's shape.
    PictureMap Map(out double aspect) {
        aspect = _side.Aspect;
        var size = Area;
        if (aspect <= 0 || size.Width <= 0 || size.Height <= 0) return new PictureMap(default, new Size(1, 1), default, FrameDisplayMap.Identity);
        var vp = _side.View;
        double va = size.Width / size.Height;
        vp.Measure(aspect, va);
        if (_side.Session?.ImageSize is { Width: > 0 } image)
            vp.FitScale = CanvasLayout.FrameRect(aspect, size).Width * Scaling / image.Width;
        return CanvasLayout.Map(vp.QuadScale(aspect, va), vp.VisibleFraction(aspect, va), vp.Center, size, FrameDisplayMap.Identity);
    }

    /// The picture's room: the pane above its caption, with the same origin, so a pointer's position needs no shift.
    Size Area => _picture.Bounds.Size;

    double Scaling => TopLevel.GetTopLevel(this)?.RenderScaling ?? 1;

    Rect Whole(PictureMap map) => new(map.ToView(new Point(0, 0)), map.ToView(new Point(1, 1)));

    /// Past fit, the part of the picture this pane shows: what the side renders once at rest.
    void Place() {
        var map = Map(out double a);
        if (a <= 0) return;
        var whole = Whole(map);
        var seen = whole.Intersect(new Rect(Area));
        if (seen.Width <= 0 || seen.Height <= 0) return;
        SideBySide.Want(_side, new Rect((seen.X - whole.X) / whole.Width, (seen.Y - whole.Y) / whole.Height,
                                        seen.Width / whole.Width, seen.Height / whole.Height),
                        new PixelSize((int)Math.Round(seen.Width * Scaling), (int)Math.Round(seen.Height * Scaling)));
    }

    void Draw(DrawingContext c) {
        var map = Map(out double a);
        if (a <= 0 || _side.Preview is not { } preview || _side.Session == null) {
            // Decoding or failed, the active side is still marked: the keys go to it either way.
            if (SideBySide.Active == _side) c.DrawRectangle(null, ActiveRing, new Rect(Area).Deflate(1), Radius.Md, Radius.Md);
            return;
        }
        var whole = Whole(map);
        var vp = _side.View;
        var mode = vp.FitScale * vp.Zoom > 4 ? BitmapInterpolationMode.None : BitmapInterpolationMode.HighQuality;
        using (c.PushGeometryClip(new RectangleGeometry(new Rect(Area))))
        using (c.PushRenderOptions(new RenderOptions { BitmapInterpolationMode = mode })) {
            c.DrawImage(preview, new Rect(preview.Size), whole);
            if (vp.PastFit && _side.Region is { } region)
                c.DrawImage(region, new Rect(region.Size), CanvasLayout.InView(_side.RegionUv, whole));
        }
        // The active side, the one the rating keys mark: an accent ring just outside its picture, in the inset.
        if (SideBySide.Active == _side)
            c.DrawRectangle(null, ActiveRing, whole.Intersect(new Rect(Area)).Inflate(4), Radius.Md, Radius.Md);
    }

    Point Anchor(Point p) => new(p.X / Math.Max(Area.Width, 1), p.Y / Math.Max(Area.Height, 1));

    Viewport Hold(KeyModifiers keys) {
        SideBySide.Unhooked = keys.HasFlag(KeyModifiers.Shift);   // Shift moves this side alone, for this gesture
        Motion.Stop(this);
        var vp = _side.View;
        vp.Begin();
        return vp;
    }

    /// The canvas's wheel: a mouse wheel or ⌘ (Ctrl) zooms about the pointer, a trackpad's two fingers pan.
    protected override void OnPointerWheelChanged(PointerWheelEventArgs e) {
        base.OnPointerWheelChanged(e);
        var d = e.Delta;
        bool precise = CanvasNative.PreciseScroll() ?? (d.X != 0 || d.Y != Math.Round(d.Y));
        double points = precise ? 50 : OperatingSystem.IsMacOS() ? 5 : 10;
        var vp = Hold(e.KeyModifiers);
        if (!precise || e.KeyModifiers.HasFlag(KeyModifiers.Meta) || e.KeyModifiers.HasFlag(KeyModifiers.Control))
            vp.ZoomBy(Math.Exp(0.01 * d.Y * points), Anchor(e.GetPosition(this)));
        else
            vp.Pan(new Vector(d.X * points / Math.Max(Area.Width, 1), d.Y * points / Math.Max(Area.Height, 1)));
        Rest();
        e.Handled = true;   // never on to the canvas under it, whose region also hears the wheel
    }

    void Magnify(object? sender, PointerDeltaEventArgs e) {
        Hold(e.KeyModifiers).ZoomBy(1 + Math.Max(e.Delta.X, -0.9), Anchor(e.GetPosition(this)));
        Rest();
        e.Handled = true;
    }

    /// 80 ms after the last wheel or pinch event, as the canvas waits: then it settles, and renders.
    void Rest() {
        if (CanvasNative.GestureEnded() == true) { Settle(default); return; }
        double waited = 0;
        Motion.Run(this, dt => {
            if ((waited += dt) < 0.08) return true;
            Settle(default);
            return false;
        });
    }

    void Settle(Vector velocity) => _side.View.Settle(velocity);

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        base.OnPointerPressed(e);
        SideBySide.Activate(_side);
        var p = e.GetPosition(this);
        e.Handled = true;
        if (e.GetCurrentPoint(this).Properties.IsRightButtonPressed) { _side.View.Reset(); return; }
        if (e.ClickCount == 2) {
            SideBySide.Unhooked = false;
            var vp = _side.View;   // 1:1 about the point clicked, or back to fit
            vp.AnimateZoom(vp.PastFit ? 1 : 1 / Math.Max(vp.FitScale, 0.0001), Anchor(p));
            return;
        }
        Hold(e.KeyModifiers);
        _x.Clear();
        _y.Clear();
        _anchor = p;
        e.Pointer.Capture(this);
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        base.OnPointerMoved(e);
        if (_anchor is not { } anchor) return;
        var p = e.GetPosition(this);
        _anchor = p;
        var vp = _side.View;
        vp.Pan(new Vector((p.X - anchor.X) / Math.Max(Area.Width, 1), (p.Y - anchor.Y) / Math.Max(Area.Height, 1)));
        _x.Add((e.Timestamp, vp.Raw.X));
        _y.Add((e.Timestamp, vp.Raw.Y));
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) { base.OnPointerReleased(e); Let(e.Timestamp, e.Pointer); }

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) { base.OnPointerCaptureLost(e); Let(0, null); }

    /// A flick carries on at its speed; past an edge it springs back, as on the canvas.
    void Let(ulong now, IPointer? pointer) {
        if (_anchor == null) return;
        _anchor = null;
        pointer?.Capture(null);
        Settle(now == 0 ? default : new Vector(Motion.Velocity(_x, now), Motion.Velocity(_y, now)));
    }
}

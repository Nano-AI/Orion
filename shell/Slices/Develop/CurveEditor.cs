using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Develop;

/// A port of the Swift app's CurveEditor.swift: the tone curve as a graph you draw on.
/// Click the line to add a point, drag one, drag it well clear to remove it,
/// double-click to reset the channel. Left/Right walk the points, Up/Down move
/// the selected one, Delete removes it.
sealed class CurveEditor : StackPanel {
    static readonly string[] Titles = ["RGB", "R", "G", "B"];

    /// RGB, R, G, B: the channels Session.Curve/SetCurve number 0-3. Edits are
    /// written through Session.SetCurve, which stage 1b builds; until then the
    /// status line says so and the drawn curve stays here.
    internal readonly List<(float X, float Y)>[] Curves = Titles.Select(_ => CurveMath.Identity.ToList()).ToArray();
    internal int Channel;

    readonly StackPanel _channels = new() { Orientation = Orientation.Horizontal, Spacing = 4 };
    readonly Button _reset;
    readonly CurveGraph _graph;
    readonly TextBlock _footnote = Develop.Note("");

    public CurveEditor() {
        Spacing = 9;
        _graph = new CurveGraph(this);
        _reset = Develop.TextButton("Reset", Palette.Dim, () => {
            foreach (var c in Curves) { c.Clear(); c.AddRange(CurveMath.Identity); }
            Edited(all: true);
        });
        var header = new DockPanel();
        DockPanel.SetDock(_reset, Dock.Right);
        header.Children.Add(_reset);
        header.Children.Add(_channels);
        Children.Add(header);
        Children.Add(_graph);
        Children.Add(_footnote);
        EngineModel.Shared.Opened += Reload;
        EngineModel.Shared.ValuesChanged += Reload;   // undo, reset, a preset: the graph follows the engine
        Levels.Updated += _graph.InvalidateVisual;
        Changed();
    }

    /// A photo opened: its curves, or identity while Session.Curve is not built.
    /// Quietly: an open is not something the photographer asked of the curve.
    void Reload() {
        for (int i = 0; i < Curves.Length; i++) {
            Curves[i].Clear();
            try { Curves[i].AddRange(EngineModel.Shared.Session?.Curve(i) ?? CurveMath.Identity); }
            catch (NotBuiltException) { Curves[i].AddRange(CurveMath.Identity); }
        }
        Changed();
    }

    /// After an edit: the channel (or all four) to the engine, then a repaint.
    internal void Edited(bool all = false) {
        EngineModel.Shared.Apply(s => {
            for (int i = all ? 0 : Channel; i <= (all ? 3 : Channel); i++) s.SetCurve(i, Curves[i]);
        });
        Changed();
    }

    public static IBrush Tint(int channel) => channel switch {
        1 => Palette.ChanR, 2 => Palette.ChanG, 3 => Palette.ChanB, _ => Palette.Accent,
    };

    /// Repaint: after an edit (through Edited), a channel switch or an open.
    internal void Changed() {
        _channels.Children.Clear();
        for (int i = 0; i < Titles.Length; i++) {
            int c = i;
            bool on = c == Channel;
            // The chosen channel changes colour (its own), never weight.
            var button = new StateButton(null, Titles[c], () => { Channel = c; Changed(); }, ButtonKind.Segment,
                                         c == 0 ? "All three channels" : $"The {Titles[c]} channel alone") {
                Width = 34, Height = 24, Padding = default, On = on, Ink = on ? Tint(c) : null,
            };
            _channels.Children.Add(button);
        }
        _reset.IsVisible = Curves.Any(c => !c.SequenceEqual(CurveMath.Identity));
        _footnote.Text = Curves[Channel].Count > 2
            ? "Drag a point. Double-click the graph to reset this channel."
            : "Click the line to add a point.";
        _graph.InvalidateVisual();
    }
}

/// The square graph: histogram behind, quarter grid, the identity diagonal,
/// the curve and its points.
sealed class CurveGraph : Control {
    const double GrabRadius = 14;
    const float Nudge = 0.01f;   // one arrow press: the resolution readouts print to

    readonly CurveEditor _editor;
    int? _dragging;
    int _selected;

    public CurveGraph(CurveEditor editor) {
        _editor = editor;
        Focusable = true;
        DoubleTapped += (_, _) => { Points.Clear(); Points.AddRange(CurveMath.Identity); _editor.Edited(); };
    }

    List<(float X, float Y)> Points => _editor.Curves[_editor.Channel];
    double Size => Bounds.Width;

    /// Square, as wide as the panel.
    protected override Size MeasureOverride(Size available) =>
        double.IsFinite(available.Width) ? new Size(available.Width, available.Width) : new Size(232, 232);

    Point View((float X, float Y) p) => new(p.X * Size, Size - p.Y * Size);

    public override void Render(DrawingContext c) {
        double s = Size;
        c.FillRectangle(Palette.Raised, new Rect(0, 0, s, s));

        // Faint, behind everything: orientation, not data you read off.
        var bins = Levels.Bins;
        if (bins.Length >= 3 * 128) {
            int n = bins.Length / 3;
            Func<int, uint> top = i => Math.Max(bins[i], Math.Max(bins[n + i], bins[2 * n + i]));
            double peak = Math.Max(1, Enumerable.Range(0, n).Max(top));
            var hist = new StreamGeometry();
            using (var g = hist.Open()) {
                g.BeginFigure(new Point(0, s), true);
                for (int i = 0; i < n; i++)
                    g.LineTo(new Point(s * i / (n - 1), s - Math.Sqrt(top(i) / peak) * s * 0.82));
                g.LineTo(new Point(s, s));
                g.EndFigure(true);
            }
            c.DrawGeometry(new SolidColorBrush(Colors.White, 0.055), null, hist);
        }

        var grid = new Pen(Palette.Line, 0.5);
        using (c.PushOpacity(0.7))
            for (int i = 1; i < 4; i++) {
                double t = s * i / 4;
                c.DrawLine(grid, new Point(t, 0), new Point(t, s));
                c.DrawLine(grid, new Point(0, t), new Point(s, t));
            }
        using (c.PushOpacity(0.35))
            c.DrawLine(new Pen(Palette.Dim, 0.5, new DashStyle([6, 6], 0)), new Point(0, s), new Point(s, 0));

        var tint = CurveEditor.Tint(_editor.Channel);
        var curve = new StreamGeometry();
        using (var g = curve.Open()) {
            for (int i = 0; i <= 96; i++) {
                float x = i / 96f;
                var v = new Point(x * s, s - CurveMath.Evaluate(Points, x) * s);
                if (i == 0) g.BeginFigure(v, false); else g.LineTo(v);
            }
            g.EndFigure(false);
        }
        c.DrawGeometry(null, new Pen(tint, 1.6, lineJoin: PenLineJoin.Round), curve);

        for (int i = 0; i < Points.Count; i++)
            c.DrawEllipse(_dragging == i ? tint : Palette.Text, new Pen(Palette.Raised, 1.5), View(Points[i]), 3.5, 3.5);
        c.DrawRectangle(null, new Pen(Palette.Line, 1), new Rect(0.5, 0.5, s - 1, s - 1));
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        Focus();
        var at = e.GetPosition(this);
        // Grab the nearest point, or make one where the line was clicked.
        int? hit = null;
        double best = GrabRadius;
        for (int i = 0; i < Points.Count; i++) {
            var v = View(Points[i]);
            double d = Math.Sqrt(Math.Pow(v.X - at.X, 2) + Math.Pow(v.Y - at.Y, 2));
            if (d <= best) (hit, best) = (i, d);
        }
        if (hit == null) {
            if (Points.Count >= 16 || CurveMath.Insertion((float)(at.X / Size), Points) is not { } placed) return;
            Points.Insert(placed.Index, (placed.X, CurveMath.Evaluate(Points, placed.X)));
            hit = placed.Index;
        }
        _dragging = _selected = hit.Value;
        e.Pointer.Capture(this);
        // Armed only once a point is held (decision #84).
        EngineModel.Shared.BeginInteraction();
        Move(at);
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        if (_dragging != null) Move(e.GetPosition(this));
    }

    void Move(Point at) {
        int i = _dragging!.Value;
        // The ends stay on their edges: they are the black and white points.
        bool end = i == 0 || i == Points.Count - 1;
        float x = end ? Points[i].X
                      : Math.Clamp((float)(at.X / Size), Points[i - 1].X + 0.005f, Points[i + 1].X - 0.005f);
        Points[i] = (x, Math.Clamp((float)(1 - at.Y / Size), 0, 1));
        _editor.Edited();
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (_dragging is not { } i) return;
        // Dragged well clear of the graph: that is how a point is deleted.
        double y = e.GetPosition(this).Y;
        bool removed = i > 0 && i < Points.Count - 1 && Points.Count > 2 && (y < -GrabRadius || y > Size + GrabRadius);
        if (removed) Points.RemoveAt(i);
        _dragging = null;
        e.Pointer.Capture(null);
        if (removed) _editor.Edited();   // before the release's full render, so it is the one that shows it
        EngineModel.Shared.EndInteraction();
        _editor.Changed();
    }

    /// Capture taken mid-drag (focus lost, a dialog): end the drag, or the engine stayed
    /// on the preview graph until the next drag anywhere. A release has already ended it.
    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
        base.OnPointerCaptureLost(e);
        if (_dragging == null) return;
        _dragging = null;
        EngineModel.Shared.EndInteraction();
        _editor.Changed();
    }

    protected override void OnKeyDown(KeyEventArgs e) {
        int i = Math.Min(_selected, Math.Max(0, Points.Count - 1));
        switch (e.Key) {
            case Key.Left: _selected = Math.Max(0, i - 1); break;
            case Key.Right: _selected = Math.Min(Points.Count - 1, i + 1); break;
            case Key.Up or Key.Down:
                Points[i] = (Points[i].X, Math.Clamp(Points[i].Y + (e.Key == Key.Up ? Nudge : -Nudge), 0, 1));
                break;
            case Key.Delete or Key.Back when Points.Count > 2 && i > 0 && i < Points.Count - 1:
                Points.RemoveAt(i);
                _selected = Math.Min(i, Points.Count - 1);
                break;
            default: return;
        }
        e.Handled = true;
        if (e.Key is Key.Left or Key.Right) _editor.Changed(); else _editor.Edited();
    }
}

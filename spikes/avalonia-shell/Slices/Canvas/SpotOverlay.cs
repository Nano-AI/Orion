using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Rendering;

namespace OrionShell.CanvasSlice;

/// The spots the overlay edits, in frame coordinates (dust is on the sensor).
// stub(#315): Engine+Spots.swift owns this until the logic moves below the facade
// (OrionAdjustments.spots is a struct array no EngineModel field reaches, so these never render)
static class SpotModel {
    public static readonly List<SpotPlacement> Spots = [];
    public static int Selected = -1;
    public static double Radius = 0.02;
    public static bool Heal = true;

    static SpotModel() => EngineModel.Shared.Opened += () => { Spots.Clear(); Selected = -1; };

    /// `addSpot(atFrame:)`: placed, with a first-guess source a couple of radii aside.
    public static void Add(Point displayed, FrameDisplayMap fd) {
        if (Spots.Count >= 64) return;
        var p = fd.Frame(displayed);
        double step = Radius * 2.5, sx = p.X + step, sy = p.Y;
        if (sx > 1 - Radius) sx = p.X - step;
        if (sx < Radius || sx > 1 - Radius) { sx = p.X; sy = p.Y + step; if (sy > 1 - Radius) sy = p.Y - step; }
        Spots.Add(new SpotPlacement(p, new Point(Math.Clamp(sx, 0, 1), Math.Clamp(sy, 0, 1)), Radius, Heal));
        Selected = Spots.Count - 1;
        CanvasState.Touch();
    }

    /// `moveSpot`: either end, given in displayed coordinates.
    public static void Move(int i, Point? destination, Point? source, FrameDisplayMap fd) {
        if (i < 0 || i >= Spots.Count) return;
        var s = Spots[i];
        Spots[i] = s with { Destination = destination is { } d ? fd.Frame(d) : s.Destination,
                            Source = source is { } src ? fd.Frame(src) : s.Source };
        CanvasState.Touch();
    }
}

/// A port of app/SpotOverlay.swift: each spot's source (dashed) linked to its
/// destination (solid), the selected one in accent, a tick on a clone. Drag
/// either disc; with the tool armed, a press places a spot and drags its source out.
[Panel("canvas-spots", Region.Canvas, 20)]
public sealed class SpotOverlay : Control, ICustomHitTest {
    (int Index, bool Source)? _dragging;
    bool _placing;

    public SpotOverlay() {
        Margin = new Thickness(CanvasState.Inset);
        ClipToBounds = true;
        CanvasState.Changed += InvalidateVisual;
        if (CanvasState.Alone("canvas-spots")) CanvasScenes.Spots();
    }

    static bool Armed => EngineModel.Shared.Tool == CanvasTool.Spot;
    static bool Showing => CanvasState.Tab == "Detail" && (SpotModel.Spots.Count > 0 || Armed);
    PictureMap Map => CanvasState.Map(Bounds.Size);

    public override void Render(DrawingContext c) {
        if (!Showing) return;
        var map = Map;
        for (int i = 0; i < SpotModel.Spots.Count; i++) Draw(c, SpotModel.Spots[i], i == SpotModel.Selected, map);
    }

    static IDashStyle Dash(double on, double off, double width) => new DashStyle([on / width, off / width], 0);

    static void Draw(DrawingContext c, SpotPlacement s, bool selected, PictureMap map) {
        Point d = map.FramePoint(s.Destination), src = map.FramePoint(s.Source);
        double r = Math.Max(MaskLayout.SpotRadius(s, map), 4);
        // The link first, so it passes under both discs: "this came from there".
        c.DrawLine(new Pen(Ink.Halo, 3), src, d);
        c.DrawLine(new Pen(Ink.White(selected ? 0.85 : 0.45), 1, Dash(3, 3, 1)), src, d);
        Circle(c, src, r, dashed: true, selected);
        Circle(c, d, r, dashed: false, selected);
        if (s.Heal) return;
        // Clone carries no tone across, which is invisible until it is wrong: say so.
        Point a = new(d.X - r * 0.35, d.Y), b = new(d.X + r * 0.35, d.Y);
        c.DrawLine(new Pen(Ink.Halo, 3), a, b);
        c.DrawLine(new Pen(Ink.White(0.9), 1), a, b);
    }

    static void Circle(DrawingContext c, Point at, double r, bool dashed, bool selected) {
        c.DrawEllipse(null, new Pen(Ink.Halo, selected ? 4 : 3), at, r, r);
        double w = selected ? 2 : 1;
        c.DrawEllipse(null, new Pen(selected ? Palette.Accent : Ink.White(0.85), w, dashed ? Dash(4, 3, w) : null), at, r, r);
    }

    /// Only where a spot is, plus the whole picture while the tool is armed: a
    /// full-canvas target would swallow every pan for as long as one spot existed.
    public bool HitTest(Point p) {
        if (!Showing) return false;
        var map = Map;
        return (Armed && map.Rect.Contains(p)) || MaskLayout.SpotHit(p, SpotModel.Spots, map) != null;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        var map = Map;
        var p = e.GetPosition(this);
        if (MaskLayout.SpotHit(p, SpotModel.Spots, map) is { } hit) {
            _dragging = hit;
            SpotModel.Selected = hit.Index;
        } else if (Armed) {
            // Placed on press, then its source dragged out: one act, one undo.
            SpotModel.Add(MaskLayout.SpotDrag(p, map), map.Fd);
            _placing = true;
        } else return;
        EngineModel.Shared.BeginInteraction();   // decision #84
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        if (_dragging == null && !_placing) return;
        var map = Map;
        var u = MaskLayout.SpotDrag(e.GetPosition(this), map);
        if (_placing) SpotModel.Move(SpotModel.Selected, null, u, map.Fd);
        else if (_dragging is { } d) SpotModel.Move(d.Index, d.Source ? null : u, d.Source ? u : null, map.Fd);
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (_dragging == null && !_placing) return;
        // addSpot's "Spot", or commitSpotEdit's "Move spot": one step, recorded at the full render the release settles on.
        Develop.EditHistory.Label(_placing ? "Spot" : "Move spot");
        (_dragging, _placing) = (null, false);
        e.Pointer.Capture(null);
        EngineModel.Shared.EndInteraction();
    }
}

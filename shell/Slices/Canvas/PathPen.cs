using Avalonia;
using Avalonia.Media;
using OrionShell.Develop;
using OrionShell.Masks;

namespace OrionShell.CanvasSlice;

/// The pen, for MaskOverlay (research/masking.md §5b). Armed, a click places a
/// corner, a click-drag a smooth point whose handle follows the pointer and
/// whose other handle mirrors it, and a click on the first point closes the
/// path. A selected path row shows its anchors and handles; dragging one moves
/// it, and the matte is filled again on every tick.
sealed class PathPen {
    const int S = PathMatte.Stride;
    /// Half the 44 pt target the other mask handles have (MaskLayout.HandleBox); 4 pt of travel makes a click a drag.
    const double Reach = 22, Travel = 4;
    const int Samples = 24;

    List<float>? _points;   // what the gesture edits: the draft, or the selected row's points
    float[] _start = [];
    int _at = -1, _part;    // the point, and its part: 0 the anchor, 1 the handle in, 2 the handle out
    Point _grab;
    bool _row;

    static MaskGroup G => MaskGroup.Shared;
    static bool Armed => G.Drawing == MaskGroup.PathTool;
    static MaskShape? Row => G.Drawing == 0 && G.Current is { Kind: 4, PathPoints.Count: > 0 } m ? m : null;

    static Point At(IReadOnlyList<float> pts, int i, int part) => new(pts[i * S + part * 2], pts[i * S + part * 2 + 1]);
    static bool HasHandle(IReadOnlyList<float> pts, int i, int part) => At(pts, i, part) != At(pts, i, 0);

    /// The nearest anchor or handle within reach; an anchor wins a tie with its own handle.
    static (int Point, int Part)? Hit(IReadOnlyList<float> pts, Point p, PictureMap map) {
        (int, int)? best = null;
        double bestD = Reach;
        for (int i = 0; i < pts.Count / S; i++)
            for (int part = 0; part < 3; part++) {
                if (part > 0 && !HasHandle(pts, i, part)) continue;
                double d = PictureMap.Dist(p, map.FramePoint(At(pts, i, part)));
                if (d < bestD) (best, bestD) = ((i, part), d);
            }
        return best;
    }

    public static bool Closes(Point p, PictureMap map) =>
        G.CanClosePath && PictureMap.Dist(p, map.FramePoint(At(G.PathDraft, 0, 0))) < Reach;

    /// A press on the selected path's anchors or handles, which the overlay must take.
    public static bool Hits(Point p, PictureMap map) => Row is { } m && Hit(m.PathPoints, p, map) != null;

    /// True when the press is the pen's; the overlay captures the pointer.
    public bool Press(Point p, PictureMap map) {
        if (Armed) {
            if (Closes(p, map)) { G.ClosePath(); return true; }
            var q = map.FrameUnit(p);
            for (int k = 0; k < 3; k++) G.PathDraft.AddRange([(float)q.X, (float)q.Y]);
            (_points, _at, _part, _grab, _row) = (G.PathDraft, G.PathDraft.Count / S - 1, 2, p, false);
            return true;
        }
        if (Row is not { } m || Hit(m.PathPoints, p, map) is not { } h) return false;
        (_points, _at, _part, _grab, _row, _start) = (m.PathPoints, h.Point, h.Part, p, true, m.PathPoints.ToArray());
        EngineModel.Shared.BeginInteraction();
        return true;
    }

    public void Move(Point p, PictureMap map) {
        if (_points is not { } pts || _at < 0) return;
        var q = map.FrameUnit(p);
        int o = _at * S;
        if (!_row) {
            if (PictureMap.Dist(p, _grab) < Travel) return;   // a click stays a corner
            (pts[o + 4], pts[o + 5]) = ((float)q.X, (float)q.Y);
            (pts[o + 2], pts[o + 3]) = (2 * pts[o] - (float)q.X, 2 * pts[o + 1] - (float)q.Y);
            return;
        }
        var g = map.FrameUnit(_grab);
        float dx = (float)(q.X - g.X), dy = (float)(q.Y - g.Y);
        if (_part == 0) {
            for (int j = 0; j < S; j += 2) (pts[o + j], pts[o + j + 1]) = (_start[o + j] + dx, _start[o + j + 1] + dy);
        } else {
            int k = o + _part * 2, other = o + (3 - _part) * 2;
            (pts[k], pts[k + 1]) = (_start[k] + dx, _start[k + 1] + dy);
            // A smooth point stays smooth: its other handle mirrors this one through the anchor.
            if (Math.Abs(_start[o + 2] + _start[o + 4] - 2 * _start[o]) < 1e-5 && Math.Abs(_start[o + 3] + _start[o + 5] - 2 * _start[o + 1]) < 1e-5)
                (pts[other], pts[other + 1]) = (2 * pts[o] - pts[k], 2 * pts[o + 1] - pts[k + 1]);
        }
        G.Push();
    }

    /// A row's drag is one undo step, settled by the full render EndInteraction asks for.
    public void Release() {
        if (_row) {
            EditHistory.Label(_part == 0 ? "Move path point" : "Move path handle");
            EngineModel.Shared.EndInteraction();
        }
        (_points, _at, _row) = (null, -1, false);
    }

    /// The draft while armed, else the selected path row; false when there is neither.
    public bool Draw(DrawingContext c, PictureMap map) {
        var pts = Armed ? G.PathDraft : Row?.PathPoints;
        if (pts == null) return false;
        int n = pts.Count / S;
        if (n == 0) return true;
        using (c.PushClip(map.Rect)) MaskOverlay.Stroke(c, Outline(pts, map, closed: !Armed), 1, null);
        var tie = new Pen(Ink.White(0.7), 1);
        for (int i = 0; i < n; i++) {
            var a = map.FramePoint(At(pts, i, 0));
            for (int part = 1; part < 3; part++) {
                if (!HasHandle(pts, i, part)) continue;
                var h = map.FramePoint(At(pts, i, part));
                c.DrawLine(tie, a, h);
                c.DrawEllipse(Ink.Halo, new Pen(Hot(pts, i, part) ? Palette.Accent : Ink.White(), 1.5), h, 3.5, 3.5);
            }
        }
        for (int i = 0; i < n; i++) {
            var a = map.FramePoint(At(pts, i, 0));
            bool hot = Hot(pts, i, 0);
            double outer = hot ? 14 : 10, inner = hot ? 10 : 6;
            c.DrawEllipse(Ink.Halo, null, a, outer / 2, outer / 2);
            c.DrawEllipse(hot ? Palette.Accent : Ink.White(), null, a, inner / 2, inner / 2);
            if (i == 0 && Armed && G.CanClosePath) c.DrawEllipse(null, new Pen(Ink.White(), 1.5), a, 9, 9);   // a click here closes
        }
        return true;
    }

    bool Hot(IReadOnlyList<float> pts, int i, int part) => ReferenceEquals(pts, _points) && _at == i && _part == part;

    /// Each segment sampled in frame space and then carried to the view, so the outline is exact under any geometry.
    static Geometry Outline(IReadOnlyList<float> pts, PictureMap map, bool closed) {
        int n = pts.Count / S;
        var line = new List<Point> { map.FramePoint(At(pts, 0, 0)) };
        for (int i = 0; i < (closed ? n : n - 1); i++) {
            Point a = At(pts, i, 0), b = At(pts, i, 2), c = At(pts, (i + 1) % n, 1), d = At(pts, (i + 1) % n, 0);
            for (int k = 1; k <= Samples; k++) {
                double t = (double)k / Samples, u = 1 - t;
                line.Add(map.FramePoint(new Point(u * u * u * a.X + 3 * u * u * t * b.X + 3 * u * t * t * c.X + t * t * t * d.X,
                                                  u * u * u * a.Y + 3 * u * u * t * b.Y + 3 * u * t * t * c.Y + t * t * t * d.Y)));
            }
        }
        return new PolylineGeometry(line, closed);
    }
}

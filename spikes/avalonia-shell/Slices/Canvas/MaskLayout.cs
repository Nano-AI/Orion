using Avalonia;

namespace OrionShell.CanvasSlice;

/// A gradient mask as stored: normalized against the frame (CanvasLayout.MaskPlacement).
sealed record MaskPlacement {
    public int Kind { get; init; }                      // 0 none, 1 linear, 2 radial, 3 brush
    public Point Center { get; init; } = new(0.5, 0.5);
    public double Angle { get; init; }                  // radians
    public double Length { get; init; } = 0.5;          // linear: zero-to-full distance
    public Size Radius { get; init; } = new(0.3, 0.3);  // radial semi-axes
    public double Feather { get; init; } = 0.5;
    public double Roundness { get; init; } = 2;

    /// The mask's own axes, read off mask_gradient.slang: +X is (cos, sin), +Y is (−sin, cos).
    public Vector AxisX => new(Math.Cos(Angle), Math.Sin(Angle));
    public Vector AxisY => new(-Math.Sin(Angle), Math.Cos(Angle));
    public Point ZeroEnd => Along(AxisX, -Length / 2);
    public Point FullEnd => Along(AxisX, Length / 2);
    public Point Along(Vector a, double d) => new(Center.X + a.X * d, Center.Y + a.Y * d);

    public Point? HandleUnit(MaskHandle h) => h switch {
        MaskHandle.Center => Center,
        MaskHandle.ZeroEnd => ZeroEnd,
        MaskHandle.FullEnd => FullEnd,
        MaskHandle.PlusX => Along(AxisX, Radius.Width),
        MaskHandle.MinusX => Along(AxisX, -Radius.Width),
        MaskHandle.PlusY => Along(AxisY, Radius.Height),
        MaskHandle.MinusY => Along(AxisY, -Radius.Height),
        MaskHandle.Feather => MaskLayout.OnOutline(this, Math.Max(1 - Feather, 0.2), Math.PI / 4),   // kept clear of the centre
        _ => null,
    };

    public static MaskPlacement Of(MaskShape m) => new() {
        Kind = m.Kind, Center = new Point(m.CenterX, m.CenterY), Angle = m.Angle, Length = m.Length,
        Radius = new Size(m.RadiusX, m.RadiusY), Feather = m.Feather, Roundness = m.Roundness,
    };

    public void WriteTo(MaskShape m) {
        (m.CenterX, m.CenterY, m.Angle, m.Length) = ((float)Center.X, (float)Center.Y, (float)Angle, (float)Length);
        (m.RadiusX, m.RadiusY, m.Feather) = ((float)Radius.Width, (float)Radius.Height, (float)Feather);
    }

    /// Inside what the engine is given: centre on the photo, sizes it can draw.
    public MaskPlacement Limited() => this with {
        Center = new Point(Math.Clamp(Center.X, 0, 1), Math.Clamp(Center.Y, 0, 1)),
        Radius = new Size(Math.Clamp(Radius.Width, MinRadius, 1), Math.Clamp(Radius.Height, MinRadius, 1)),
        Length = Math.Clamp(Length, MinLength, 1.5), Feather = Math.Clamp(Feather, 0, 1),
    };

    /// Past a limit, what the outline shows: Apple's rubber band, so a drag
    /// against a bound resists progressively instead of stopping dead.
    public MaskPlacement Banded() {
        static double B(double v, double lo, double hi) =>
            v < lo ? lo - MaskLayout.Rubber(lo - v) : v > hi ? hi + MaskLayout.Rubber(v - hi) : v;
        return this with {
            Center = new Point(B(Center.X, 0, 1), B(Center.Y, 0, 1)),
            Radius = new Size(Math.Max(B(Radius.Width, MinRadius, 1), MinRadius / 4), Math.Max(B(Radius.Height, MinRadius, 1), MinRadius / 4)),
            Length = Math.Max(B(Length, MinLength, 1.5), MinLength / 4), Feather = B(Feather, 0, 1),
        };
    }

    /// `from` + (this − from) · t, field by field: the spring's presented value.
    public MaskPlacement Toward(MaskPlacement from, double t) {
        static double L(double a, double b, double t) => a + (b - a) * t;
        return this with {
            Center = new Point(L(from.Center.X, Center.X, t), L(from.Center.Y, Center.Y, t)),
            Angle = L(from.Angle, Angle, t), Length = L(from.Length, Length, t), Feather = L(from.Feather, Feather, t),
            Radius = new Size(L(from.Radius.Width, Radius.Width, t), L(from.Radius.Height, Radius.Height, t)),
        };
    }

    /// How far apart two placements are, summed over their numbers: the spring's measure of overshoot.
    public double Distance(MaskPlacement o) =>
        Math.Abs(Center.X - o.Center.X) + Math.Abs(Center.Y - o.Center.Y) + Math.Abs(Radius.Width - o.Radius.Width)
        + Math.Abs(Radius.Height - o.Radius.Height) + Math.Abs(Length - o.Length) + Math.Abs(Feather - o.Feather);

    public const double MinRadius = 0.02, MinLength = 0.05;
}

enum MaskHandle { Center, ZeroEnd, FullEnd, PlusX, MinusX, PlusY, MinusY, Rotate, Feather, Body }

/// A critically damped spring from 1 to 0 (Apple's damping 1.0), started with
/// a velocity in units of the gap per second. Interruptible by construction: the
/// caller reads Value when it is interrupted and starts the next motion from there.
readonly record struct SpringBack(double Velocity, double Response = 0.3) {
    double Omega => 2 * Math.PI / Response;
    /// x(t) = (1 + (v + ω) t) e^(−ωt): starts at 1 with slope v, settles at 0 without overshoot.
    public double Value(double t) => (1 + (Velocity + Omega) * t) * Math.Exp(-Omega * t);
    public bool Settled(double t) => t > 0 && Math.Abs(Value(t)) < 0.002;
}

/// One spot in frame coordinates; radius is a fraction of the frame's width.
sealed record SpotPlacement(Point Destination, Point Source, double Radius, bool Heal);

/// The mask, spot and brush half of app/CanvasLayout.swift. No view lives here:
/// where handles are, what a press grabs and what a drag means.
static class MaskLayout {
    const double HandleBox = 44, RotateStem = 34;
    public const double SpotHandleMin = 11;
    /// research/brush-nib.md: a quarter radius, tied to the shader's hardness clamp.
    const double BrushSpacing = 0.25;

    public static MaskHandle[] Handles(MaskPlacement m) => m.Kind switch {
        1 => [MaskHandle.ZeroEnd, MaskHandle.FullEnd, MaskHandle.Center],
        2 => [MaskHandle.PlusX, MaskHandle.MinusX, MaskHandle.PlusY, MaskHandle.MinusY, MaskHandle.Rotate, MaskHandle.Feather, MaskHandle.Center],
        _ => [],
    };

    /// Apple's rubber band (Designing Fluid Interfaces, WWDC 2018): how far the
    /// outline follows a drag `over` past a bound, in frame units (dimension 1).
    public static double Rubber(double over, double dimension = 1, double c = 0.55) =>
        over * dimension * c / (dimension + c * Math.Abs(over));

    /// A point on a radial's boundary at `level`, `t` radians round, as Outline samples it.
    public static Point OnOutline(MaskPlacement m, double level, double t) {
        double n = Math.Max(m.Roundness, 0.1), c = Math.Cos(m.Angle), s = Math.Sin(m.Angle), ct = Math.Cos(t), st = Math.Sin(t);
        double ex = Math.CopySign(Math.Pow(Math.Abs(ct), 2 / n), ct) * level * m.Radius.Width;
        double ey = Math.CopySign(Math.Pow(Math.Abs(st), 2 / n), st) * level * m.Radius.Height;
        return new Point(m.Center.X + c * ex - s * ey, m.Center.Y + s * ex + c * ey);
    }

    public static Point HandlePoint(MaskHandle h, MaskPlacement m, PictureMap map) {
        if (h == MaskHandle.Rotate) {
            // The stem follows the +X axis as drawn, a fixed number of view points out.
            var tip = m.Along(m.AxisX, m.Radius.Width);
            var b = map.FramePoint(tip);
            var step = map.FramePoint(m.Along(m.AxisX, m.Radius.Width + 1e-3));
            double n = Math.Max(PictureMap.Dist(b, step), 1e-9);
            return new Point(b.X + (step.X - b.X) / n * RotateStem, b.Y + (step.Y - b.Y) / n * RotateStem);
        }
        return map.FramePoint(m.HandleUnit(h) ?? m.Center);
    }

    /// Nearest handle within reach wins; the body is tested last.
    public static MaskHandle? Hit(Point p, MaskPlacement m, PictureMap map) {
        if (m.Kind == 0) return null;
        MaskHandle? best = null;
        double bestD = HandleBox / 2;
        foreach (var h in Handles(m)) {
            double d = PictureMap.Dist(p, HandlePoint(h, m, map));
            if (best == null ? d <= bestD : d < bestD) { best = h; bestD = d; }
        }
        if (best != null) return best;
        var q = map.FrameUnit(p);
        bool inside = m.Kind switch {
            1 => LinearT(q, m) is >= 0 and <= 1,
            2 => RadialR(q, m) <= 1,
            _ => false,
        };
        return inside ? MaskHandle.Body : null;
    }

    static double LinearT(Point q, MaskPlacement m) {
        Point z = m.ZeroEnd, f = m.FullEnd;
        double dx = f.X - z.X, dy = f.Y - z.Y, denom = dx * dx + dy * dy;
        return denom <= 1e-9 ? 1 : ((q.X - z.X) * dx + (q.Y - z.Y) * dy) / denom;
    }

    static double RadialR(Point q, MaskPlacement m) {
        double c = Math.Cos(m.Angle), s = Math.Sin(m.Angle), ex = q.X - m.Center.X, ey = q.Y - m.Center.Y;
        double u = (c * ex + s * ey) / Math.Max(m.Radius.Width, 1e-6), v = (-s * ex + c * ey) / Math.Max(m.Radius.Height, 1e-6);
        double n = Math.Max(m.Roundness, 0.1);
        return Math.Pow(Math.Pow(Math.Abs(u), n) + Math.Pow(Math.Abs(v), n), 1 / n);
    }

    /// A radial mask's boundary at `level` (1 the edge, 1 − feather where the
    /// falloff begins), sampled in the mask's own space: frame points.
    public static Point[] Outline(MaskPlacement m, double level, int samples = 128) =>
        m.Kind != 2 || level <= 0 ? [] : Enumerable.Range(0, samples).Select(i => OnOutline(m, level, (double)i / samples * 2 * Math.PI)).ToArray();

    /// A linear gradient's iso-alpha line at `t` (0 zero, 0.5 center, 1 full): frame points.
    public static (Point, Point) IsoLine(MaskPlacement m, double t, double halfLength = 4) {
        var mid = m.Along(m.AxisX, (t - 0.5) * m.Length);
        var a = m.AxisY;
        return (new Point(mid.X - a.X * halfLength, mid.Y - a.Y * halfLength),
                new Point(mid.X + a.X * halfLength, mid.Y + a.Y * halfLength));
    }

    /// What a drag does to the mask, before any limit (Limited clamps it,
    /// Banded shows it). The pointer moves the handle by its delta from the
    /// grab, so a handle taken off-centre does not jump to the pointer (1:1
    /// tracking with the grab offset). Every angle is taken in frame space.
    public static MaskPlacement Drag(MaskHandle h, Point grab, Point p, MaskPlacement start, PictureMap map) {
        var a = map.FrameUnit(grab);
        var at = h == MaskHandle.Rotate ? map.FrameUnit(HandlePoint(h, start, map))
               : start.HandleUnit(h == MaskHandle.Body ? MaskHandle.Center : h) ?? a;
        var now = map.FrameUnit(p);
        var q = new Point(at.X + now.X - a.X, at.Y + now.Y - a.Y);
        double dx = q.X - start.Center.X, dy = q.Y - start.Center.Y, r = Math.Sqrt(dx * dx + dy * dy);
        switch (h) {
            case MaskHandle.Center or MaskHandle.Body:
                return start with { Center = q };
            case MaskHandle.ZeroEnd or MaskHandle.FullEnd when r > 1e-9:
                return start with { Length = 2 * r, Angle = h == MaskHandle.FullEnd ? Math.Atan2(dy, dx) : Math.Atan2(-dy, -dx) };
            case MaskHandle.PlusX or MaskHandle.MinusX:
                return start with { Radius = new Size(Math.Abs(dx * start.AxisX.X + dy * start.AxisX.Y), start.Radius.Height) };
            case MaskHandle.PlusY or MaskHandle.MinusY:
                return start with { Radius = new Size(start.Radius.Width, Math.Abs(dx * start.AxisY.X + dy * start.AxisY.Y)) };
            case MaskHandle.Rotate when r > 1e-9:
                return start with { Angle = Math.Atan2(dy, dx) };
            case MaskHandle.Feather:
                return start with { Feather = 1 - RadialR(q, start) };
            default:
                return start;
        }
    }

    /// A gradient drawn by a drag from `a` to `b`, frame units. A radial is
    /// centred on the press and reaches the pointer on each axis; a linear is
    /// full at the press and gone at the pointer, which is how a sky is drawn
    /// from its top. Too short a drag (a click) drops the default size there.
    public static MaskPlacement Drawn(int kind, Point a, Point b, bool click) {
        var m = new MaskPlacement { Kind = kind, Center = a };
        if (kind == 2)
            return click ? m : m with { Radius = new Size(Math.Abs(b.X - a.X), Math.Abs(b.Y - a.Y)) };
        if (click) return m with { Angle = -Math.PI / 2 };   // full at the top, as a sky
        double dx = a.X - b.X, dy = a.Y - b.Y;
        return m with { Center = new Point((a.X + b.X) / 2, (a.Y + b.Y) / 2), Length = Math.Sqrt(dx * dx + dy * dy), Angle = Math.Atan2(dy, dx) };
    }

    /// Dab centers along a → b at a fixed spacing; `carry` keeps it continuous across events.
    public static List<Point> BrushDabs(Point a, Point b, double radius, ref double carry) {
        double step = Math.Max(radius * BrushSpacing, 1e-4), length = PictureMap.Dist(a, b);
        var output = new List<Point>();
        if (length <= 1e-9) return output;
        double t = step - carry;
        for (; t <= length; t += step) output.Add(new Point(a.X + (b.X - a.X) * t / length, a.Y + (b.Y - a.Y) * t / length));
        carry = length - (t - step);
        return output;
    }

    /// The brush outline: round, measured against the smaller frame axis, as the kernel stamps it.
    public static Point[] BrushCursor(Point frameUnit, double radius, PictureMap map, int samples = 64) {
        if (radius <= 0) return [];
        var c = map.FramePoint(frameUnit);
        var s = map.FrameScale(frameUnit);
        double r = radius * Math.Min(s.Width, s.Height);
        return Enumerable.Range(0, samples)
            .Select(i => (double)i / samples * 2 * Math.PI)
            .Select(t => new Point(c.X + Math.Cos(t) * r, c.Y + Math.Sin(t) * r)).ToArray();
    }

    /// A spot's radius in view points (normalized against the frame's width).
    public static double SpotRadius(SpotPlacement s, PictureMap map) => s.Radius * map.Scale.Width;

    /// Sources before destinations, later spots before earlier ones.
    public static (int Index, bool Source)? SpotHit(Point p, IReadOnlyList<SpotPlacement> spots, PictureMap map) {
        foreach (bool source in new[] { true, false })
            for (int i = spots.Count - 1; i >= 0; i--) {
                var c = map.FramePoint(source ? spots[i].Source : spots[i].Destination);
                if (PictureMap.Dist(p, c) <= Math.Max(SpotRadius(spots[i], map), SpotHandleMin)) return (i, source);
            }
        return null;
    }

    /// Where a drag puts a point, in displayed coordinates, clamped to the picture.
    public static Point SpotDrag(Point p, PictureMap map) {
        var u = map.ToUnit(p);
        return new Point(Math.Clamp(u.X, 0, 1), Math.Clamp(u.Y, 0, 1));
    }
}

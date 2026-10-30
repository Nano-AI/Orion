using Avalonia;

namespace OrionShell.CanvasSlice;

/// A port of the picture and crop half of the Swift app's CanvasLayout.swift, same maths
/// and names (Rect for CGRect): where the photo sits in the canvas, and how a
/// straightened frame bounds the crop. The mask and spot half is MaskLayout.cs.
static class CanvasLayout {
    /// The picture turns about the frame's center, never the crop's.
    static readonly Point Pivot = new(0.5, 0.5);

    /// The frame's rectangle inside a view of `size`, letterboxed to fit.
    public static Rect FrameRect(double imageAspect, Size size) {
        if (imageAspect <= 0 || size.Width <= 0 || size.Height <= 0) return default;
        double w = size.Width, h = size.Height;
        if (imageAspect > w / h) h = w / imageAspect; else w = h * imageAspect;
        return new Rect((size.Width - w) / 2, (size.Height - h) / 2, w, h);
    }

    /// Where the picture is drawn once zoom has had its say; `q` is the quad's
    /// half-extent per axis, as the renderer gets it.
    public static Rect DrawnRect(Size q, Size size) {
        if (size.Width <= 0 || size.Height <= 0) return default;
        double w = size.Width * Math.Clamp(q.Width, 0, 1), h = size.Height * Math.Clamp(q.Height, 0, 1);
        return new Rect((size.Width - w) / 2, (size.Height - h) / 2, w, h);
    }

    /// How far a w × h rectangle reaches per axis once turned, in coordinates
    /// normalized to a frame of aspect `a`.
    static Size Extent(double w, double h, double a, double angleDeg) {
        double t = angleDeg * Math.PI / 180, c = Math.Abs(Math.Cos(t)), s = Math.Abs(Math.Sin(t));
        return new Size(w * c + h * s / a, w * s * a + h * c);
    }

    /// Shrinks and slides a crop until its turned form lies inside the frame.
    public static Rect ConstrainedCrop(Rect crop, double a, double angleDeg) {
        if (a <= 0) return crop;
        var r = ClampedCrop(crop);
        var center = r.Center;

        var reach = Extent(r.Width, r.Height, a, angleDeg);
        double scale = Math.Min(1, Math.Min(1 / Math.Max(reach.Width, 1e-6), 1 / Math.Max(reach.Height, 1e-6)));
        if (scale < 1) {
            double sw = r.Width * scale, sh = r.Height * scale;
            r = new Rect(center.X - sw / 2, center.Y - sh / 2, sw, sh);
        }

        // Position, along the turned frame's own axes, in units where height is 1.
        double t = angleDeg * Math.PI / 180, c = Math.Cos(t), s = Math.Sin(t);
        double hw = r.Width * a / 2, hh = r.Height / 2;
        double dx = (r.Center.X - Pivot.X) * a, dy = r.Center.Y - Pivot.Y;
        double u = dx * c - dy * s, v = dx * s + dy * c;
        double limitU = Math.Max(0, a / 2 - (hw * Math.Abs(c) + hh * Math.Abs(s)));
        double limitV = Math.Max(0, 0.5 - (hw * Math.Abs(s) + hh * Math.Abs(c)));
        u = Math.Clamp(u, -limitU, limitU);
        v = Math.Clamp(v, -limitV, limitV);
        dx = u * c + v * s;
        dy = -u * s + v * c;
        return new Rect(Pivot.X + dx / a - r.Width / 2, Pivot.Y + dy - r.Height / 2, r.Width, r.Height);
    }

    /// The region the crop preview renders, in crop coordinates: square, concentric
    /// with the frame, covering its turned bounding box with 6% air.
    public static (Point Origin, double Size) PreviewCanvas(double a, double angleDeg) {
        if (a <= 0) return (default, 1);
        var reach = Extent(1, 1, a, angleDeg);
        double m = Math.Max(reach.Width, reach.Height) * 1.06;
        return (new Point(Pivot.X - m / 2, Pivot.Y - m / 2), m);
    }

    /// The turned frame's four corners in crop coordinates: the inverse of the
    /// rotation `ConstrainedCrop` clamps against, so the two agree on its edges.
    public static Point[] TurnedFrame(double a, double angleDeg) {
        double t = angleDeg * Math.PI / 180, c = Math.Cos(t), s = Math.Sin(t);
        return new[] { (-1, -1), (1, -1), (1, 1), (-1, 1) }.Select(k => {
            double u = k.Item1 * a / 2, v = k.Item2 * 0.5;
            return new Point(Pivot.X + (u * c + v * s) / a, Pivot.Y - u * s + v * c);
        }).ToArray();
    }

    /// A frame-coordinate rectangle placed on the preview canvas.
    public static Rect OnCanvas(Rect r, Point origin, double size) => size <= 0 ? r
        : new Rect((r.X - origin.X) / size, (r.Y - origin.Y) / size, r.Width / size, r.Height / size);

    /// A normalized rectangle placed inside a view rectangle.
    public static Rect InView(Rect unit, Rect frame) =>
        new(frame.X + unit.X * frame.Width, frame.Y + unit.Y * frame.Height, unit.Width * frame.Width, unit.Height * frame.Height);

    /// A crop cannot invert or leave the frame.
    public static Rect ClampedCrop(Rect r) {
        double w = Math.Clamp(r.Width, 0.05, 1), h = Math.Clamp(r.Height, 0.05, 1);
        return new Rect(Math.Clamp(r.X, 0, 1 - w), Math.Clamp(r.Y, 0, 1 - h), w, h);
    }

    public enum CropHandle { Move, TopLeft, TopRight, BottomLeft, BottomRight }

    /// What a crop drag does to the rectangle, recomputed from `start` every tick.
    /// `dx`/`dy` are already crop fractions. Shift locks `start`'s ratio about the
    /// opposite corner.
    public static Rect CropDrag(CropHandle h, Rect start, double dx, double dy, bool lockAspect) {
        if (h == CropHandle.Move)
            return new Rect(Math.Clamp(start.X + dx, 0, 1 - start.Width), Math.Clamp(start.Y + dy, 0, 1 - start.Height),
                            start.Width, start.Height);
        bool left = h is CropHandle.TopLeft or CropHandle.BottomLeft, top = h is CropHandle.TopLeft or CropHandle.TopRight;
        if (!lockAspect) {
            double x = start.X, y = start.Y, w = start.Width, hgt = start.Height;
            if (left) { x += dx; w -= dx; } else w += dx;
            if (top) { y += dy; hgt -= dy; } else hgt += dy;
            return ClampedCrop(new Rect(x, y, w, hgt));
        }

        double sx = left ? -1 : 1, sy = top ? -1 : 1;
        var anchor = new Point(sx > 0 ? start.Left : start.Right, sy > 0 ? start.Top : start.Bottom);
        double movingX = sx > 0 ? start.Right : start.Left, movingY = sy > 0 ? start.Bottom : start.Top;
        double rho = start.Width / Math.Max(start.Height, 1e-6);

        double w0 = Math.Max(0, (movingX + dx - anchor.X) * sx), h0 = Math.Max(0, (movingY + dy - anchor.Y) * sy);
        double width = Math.Max(Math.Max(w0, h0 * rho), Math.Max(0.05, 0.05 * rho));
        double roomX = sx > 0 ? 1 - anchor.X : anchor.X, roomY = sy > 0 ? 1 - anchor.Y : anchor.Y;
        width = Math.Min(width, Math.Min(roomX, roomY * rho));
        double height = width / rho;
        return new Rect(sx > 0 ? anchor.X : anchor.X - width, sy > 0 ? anchor.Y : anchor.Y - height, width, height);
    }

    /// The map the renderer is using right now (`pictureMap`): built from the
    /// viewport's own quad scale and visible fraction.
    public static PictureMap Map(Size q, Size visible, Point center, Size size, FrameDisplayMap fd) {
        var v = new Size(Math.Max(visible.Width, 1e-9), Math.Max(visible.Height, 1e-9));
        return new PictureMap(DrawnRect(q, size), v, new Point(center.X - v.Width / 2, center.Y - v.Height / 2), fd);
    }
}

/// CanvasLayout.PictureMap: normalized picture coordinates ↔ view points, zoom
/// and pan included. Anisotropic: a normalized circle is a screen ellipse.
readonly record struct PictureMap(Rect Rect, Size Visible, Point Origin, FrameDisplayMap Fd) {
    public Size Scale => new(Rect.Width / Math.Max(Visible.Width, 1e-9), Rect.Height / Math.Max(Visible.Height, 1e-9));

    /// A normalized displayed point, in the view (Swift `point`).
    public Point ToView(Point u) => new(Rect.X + (u.X - Origin.X) * Scale.Width, Rect.Y + (u.Y - Origin.Y) * Scale.Height);

    /// A view point, back in normalized displayed coordinates (Swift `unit`).
    public Point ToUnit(Point p) {
        var s = Scale;
        if (s.Width <= 1e-12 || s.Height <= 1e-12) return Origin;
        return new Point(Origin.X + (p.X - Rect.X) / s.Width, Origin.Y + (p.Y - Rect.Y) / s.Height);
    }

    public Point FramePoint(Point f) => ToView(Fd.Display(f));
    public Point FrameUnit(Point p) => Fd.Frame(ToUnit(p));

    /// View points per frame unit at `f`, per axis: the geometry map is projective.
    public Size FrameScale(Point f) {
        const double h = 5e-4;
        Point x0 = FramePoint(new(f.X - h, f.Y)), x1 = FramePoint(new(f.X + h, f.Y));
        Point y0 = FramePoint(new(f.X, f.Y - h)), y1 = FramePoint(new(f.X, f.Y + h));
        return new Size(Dist(x0, x1) / (2 * h), Dist(y0, y1) / (2 * h));
    }

    public static double Dist(Point a, Point b) => Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
}

/// FrameDisplayMap.swift: two row-major projective 3×3 matrices from the engine.
sealed class FrameDisplayMap(double[] toDisplay, double[] toFrame) {
    public static readonly FrameDisplayMap Identity = new([1, 0, 0, 0, 1, 0, 0, 0, 1], [1, 0, 0, 0, 1, 0, 0, 0, 1]);

    static Point Apply(double[] m, Point p) {
        double w = m[6] * p.X + m[7] * p.Y + m[8];
        double d = Math.Abs(w) > 1e-9 ? w : 1e-9;
        return new Point((m[0] * p.X + m[1] * p.Y + m[2]) / d, (m[3] * p.X + m[4] * p.Y + m[5]) / d);
    }

    public Point Display(Point f) => Apply(toDisplay, f);
    public Point Frame(Point d) => Apply(toFrame, d);
}

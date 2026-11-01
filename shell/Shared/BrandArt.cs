using System.Globalization;
using System.Xml.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace OrionShell;

/// Orion's logo, drawn from the brand's own files in brand/app/ (decision #407),
/// embedded in the assembly as they are: never redrawn, recoloured or cropped
/// here. The brand's SVGs are plain filled paths on a viewBox (no masks, clips,
/// filters, text or transforms), which is exactly what Avalonia's path parser
/// reads, so this is a reader of that subset and no SVG library. Anything
/// outside it throws, and the selftest loads every file, so a richer export
/// fails there rather than drawing wrong.
public static class BrandArt {
    /// One filled path: its geometry in viewBox units, its fill (null for
    /// currentColor), and its id, which the loading symbol's stars carry.
    public sealed record Shape(Geometry Data, Color? Fill, string? Id);

    /// A file's viewBox and its shapes, in drawing order.
    public sealed record Art(Rect ViewBox, IReadOnlyList<Shape> Shapes);

    /// The files Orion.csproj embeds from brand/app/, by name.
    public static readonly string[] Files = ["toolbar-mark.svg", "welcome-mark.svg", "symbol-animatable.svg"];

    static readonly Dictionary<string, Art> Loaded = [];

    public static Art Load(string file) {
        if (Loaded.TryGetValue(file, out var art)) return art;
        using var stream = typeof(BrandArt).Assembly.GetManifestResourceStream("brand/" + file)
                           ?? throw new InvalidOperationException($"brand/app/{file} is not embedded (Orion.csproj)");
        return Loaded[file] = Parse(XDocument.Load(stream), file);
    }

    static Art Parse(XDocument doc, string file) {
        XNamespace svg = "http://www.w3.org/2000/svg";
        var root = doc.Root!;
        var box = (root.Attribute("viewBox")?.Value ?? throw new InvalidOperationException($"{file}: no viewBox"))
            .Split([' ', ','], StringSplitOptions.RemoveEmptyEntries).Select(v => double.Parse(v, CultureInfo.InvariantCulture)).ToArray();
        var shapes = new List<Shape>();
        foreach (var e in root.Elements()) {
            if (e.Name == svg + "title") continue;
            if (e.Name != svg + "path" || e.Attributes().Any(a => a.Name.LocalName is not ("d" or "fill" or "id")))
                throw new InvalidOperationException($"{file}: <{e.Name.LocalName}> with {string.Join(" ", e.Attributes().Select(a => a.Name.LocalName))} " +
                                                    "is outside the plain filled paths BrandArt draws");
            string fill = e.Attribute("fill")?.Value ?? "black";
            shapes.Add(new Shape(Geometry.Parse(e.Attribute("d")!.Value), fill == "currentColor" ? null : Color.Parse(fill), e.Attribute("id")?.Value));
        }
        return new Art(new Rect(box[0], box[1], box[2], box[3]), shapes);
    }
}

/// A brand file drawn in a square of `size` points: the artboard scaled whole,
/// so the brand's own margin around the mark is kept. `ink` fills the
/// currentColor paths (the toolbar mark); a coloured file keeps its colours.
/// `Opacity(id)` dims one shape, for the loading symbol's stars.
public sealed class Artwork : Control {
    readonly BrandArt.Art _art;
    readonly IBrush?[] _fills;
    readonly Dictionary<string, double> _opacity = [];

    public Artwork(string file, double size, IBrush? ink = null) {
        _art = BrandArt.Load(file);
        _fills = [.. _art.Shapes.Select(s => s.Fill is { } c ? new ImmutableSolidColorBrush(c) : ink)];
        Width = Height = size;
        IsHitTestVisible = false;
    }

    public void SetOpacity(string id, double opacity) {
        _opacity[id] = opacity;
        InvalidateVisual();
    }

    public override void Render(DrawingContext c) {
        var box = _art.ViewBox;
        double s = Math.Min(Bounds.Width / box.Width, Bounds.Height / box.Height);
        var place = Matrix.CreateTranslation(-box.X, -box.Y) * Matrix.CreateScale(s, s)
                    * Matrix.CreateTranslation((Bounds.Width - box.Width * s) / 2, (Bounds.Height - box.Height * s) / 2);
        using var _ = c.PushTransform(place);
        for (int i = 0; i < _art.Shapes.Count; i++) {
            var shape = _art.Shapes[i];
            if (_fills[i] is not { } fill) continue;
            double a = shape.Id is { } id && _opacity.TryGetValue(id, out var o) ? o : 1;
            if (a <= 0) continue;
            using var __ = c.PushOpacity(a);
            c.DrawGeometry(fill, null, shape.Data);
        }
    }
}

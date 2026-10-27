using Path = Avalonia.Controls.Shapes.Path;
using Avalonia.Media;

namespace OrionShell;

/// SF Symbols by their Swift names, redrawn on a 24-unit grid. Every symbol the
/// Swift UI uses is here; a name that is not draws a dot and says so on stderr,
/// so a slice is never blocked on an icon. A real icon set replaces this table.
public static class Icons {
    static readonly Dictionary<string, string> Paths = new() {
        ["xmark"] = "M6,6 L18,18 M18,6 L6,18",
        ["plus"] = "M12,5 L12,19 M5,12 L19,12",
        ["minus.circle"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M8,12 L16,12",
        ["chevron.down"] = "M6,9 L12,15 L18,9",
        ["chevron.up"] = "M6,15 L12,9 L18,15",
        ["arrow.uturn.backward"] = "M9,14 L4,9 L9,4 M4,9 L15,9 A5,5 0 0 1 15,19 L11,19",
        ["arrow.clockwise"] = "M20,12 A8,8 0 1 1 17.7,6.3 M18,2 L18,7 L13,7",
        ["rotate.left"] = "M4,12 A8,8 0 1 0 6.3,6.3 M6,2 L6,7 L11,7",
        ["rotate.right"] = "M20,12 A8,8 0 1 1 17.7,6.3 M18,2 L18,7 L13,7",
        ["star.fill"] = "M12,2 L15,9 L22,9 L16.5,13.5 L18.5,21 L12,16.8 L5.5,21 L7.5,13.5 L2,9 L9,9 Z",
        ["square.grid.2x2"] = "M4,4 H10 V10 H4 Z M14,4 H20 V10 H14 Z M4,14 H10 V20 H4 Z M14,14 H20 V20 H14 Z",
        ["square.grid.3x3"] = "M3,3 h5 v5 h-5 Z M9.5,3 h5 v5 h-5 Z M16,3 h5 v5 h-5 Z M3,9.5 h5 v5 h-5 Z M9.5,9.5 h5 v5 h-5 Z " +
                              "M16,9.5 h5 v5 h-5 Z M3,16 h5 v5 h-5 Z M9.5,16 h5 v5 h-5 Z M16,16 h5 v5 h-5 Z",
        ["pencil"] = "M4,20 L8,19 L19,8 L16,5 L5,16 Z M14,7 L17,10",
        ["info.circle"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M12,11 L12,16 M12,7.5 L12,8",
        ["folder"] = "M3,6 L9,6 L11,8 L21,8 L21,19 L3,19 Z",
        ["folder.badge.gearshape"] = "M3,6 L9,6 L11,8 L21,8 L21,13 M14,19 L3,19 L3,6 M16,17 A2.5,2.5 0 1 0 21,17 A2.5,2.5 0 1 0 16,17 Z",
        ["bandage"] = "M4,14 L14,4 A3,3 0 0 1 20,10 L10,20 A3,3 0 0 1 4,14 Z M10.5,10.5 L13.5,13.5",
        ["eyedropper"] = "M14,8 L5,17 L4,20 L7,19 L16,10 M13,7 L17,11 M15,5 A2.8,2.8 0 0 1 19,9 L17,11 L13,7 Z",
        ["scope"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M12,1 L12,6 M12,18 L12,23 M1,12 L6,12 M18,12 L23,12",
    };

    public static bool Has(string sfName) => Paths.ContainsKey(sfName);

    /// The symbol at `size` points in `brush`. ".fill" symbols fill; the rest stroke.
    public static Path Create(string sfName, double size, IBrush brush) {
        if (!Paths.TryGetValue(sfName, out var data)) {
            Console.Error.WriteLine($"Icons: no drawing for SF Symbol '{sfName}'");
            data = "M10,12 A2,2 0 1 0 14,12 A2,2 0 1 0 10,12 Z";
        }
        bool fill = sfName.EndsWith(".fill");
        return new Path {
            Data = Geometry.Parse(data), Width = size, Height = size, Stretch = Stretch.Uniform,
            Fill = fill ? brush : null, Stroke = fill ? null : brush,
            StrokeThickness = 1.6, StrokeLineCap = PenLineCap.Round, StrokeJoin = PenLineJoin.Round,
        };
    }
}

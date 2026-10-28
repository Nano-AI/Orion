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
        // A frame under a quarter-turn arrow, as SF Symbols draws them: Reset's bare arrow is a different thing.
        ["rotate.left"] = "M4,11 H14 V21 H4 Z M19,15 A8,8 0 0 0 10,6 M13,3 L10,6 L13,9",
        ["rotate.right"] = "M20,11 H10 V21 H20 Z M5,15 A8,8 0 0 1 14,6 M11,3 L14,6 L11,9",
        ["arrow.counterclockwise"] = "M4,12 A8,8 0 1 0 6.3,6.3 M6,2 L6,7 L11,7",
        ["arrow.up.arrow.down"] = "M7,20 V4 M3,8 L7,4 L11,8 M17,4 V20 M13,16 L17,20 L21,16",
        ["chevron.up.chevron.down"] = "M8,9 L12,5 L16,9 M8,15 L12,19 L16,15",
        ["trash"] = "M4,7 H20 M9,7 V4 H15 V7 M6,7 L7,21 H17 L18,7 M10,11 V17 M14,11 V17",
        ["slider.horizontal.3"] = "M3,6 H12.5 M17.5,6 H21 M12.5,6 A2.5,2.5 0 1 0 17.5,6 A2.5,2.5 0 1 0 12.5,6 " +
                                  "M3,12 H5.5 M10.5,12 H21 M5.5,12 A2.5,2.5 0 1 0 10.5,12 A2.5,2.5 0 1 0 5.5,12 " +
                                  "M3,18 H13.5 M18.5,18 H21 M13.5,18 A2.5,2.5 0 1 0 18.5,18 A2.5,2.5 0 1 0 13.5,18",
        ["star.fill"] = "M12,2 L15,9 L22,9 L16.5,13.5 L18.5,21 L12,16.8 L5.5,21 L7.5,13.5 L2,9 L9,9 Z",
        ["square.grid.2x2"] = "M4,4 H10 V10 H4 Z M14,4 H20 V10 H14 Z M4,14 H10 V20 H4 Z M14,14 H20 V20 H14 Z",
        ["square.grid.3x3"] = "M3,3 h5 v5 h-5 Z M9.5,3 h5 v5 h-5 Z M16,3 h5 v5 h-5 Z M3,9.5 h5 v5 h-5 Z M9.5,9.5 h5 v5 h-5 Z " +
                              "M16,9.5 h5 v5 h-5 Z M3,16 h5 v5 h-5 Z M9.5,16 h5 v5 h-5 Z M16,16 h5 v5 h-5 Z",
        ["pencil"] = "M4,20 L8,19 L19,8 L16,5 L5,16 Z M14,7 L17,10",
        ["info.circle"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M12,11 L12,16 M12,7.5 L12,8",
        ["folder"] = "M3,6 L9,6 L11,8 L21,8 L21,19 L3,19 Z",
        ["square.and.arrow.up"] = "M12,15 L12,3 M7,8 L12,3 L17,8 M4,15 L4,20 L20,20 L20,15",
        ["folder.badge.gearshape"] = "M3,6 L9,6 L11,8 L21,8 L21,13 M14,19 L3,19 L3,6 M16,17 A2.5,2.5 0 1 0 21,17 A2.5,2.5 0 1 0 16,17 Z",
        ["bandage"] = "M4,14 L14,4 A3,3 0 0 1 20,10 L10,20 A3,3 0 0 1 4,14 Z M10.5,10.5 L13.5,13.5",
        ["eyedropper"] = "M14,8 L5,17 L4,20 L7,19 L16,10 M13,7 L17,11 M15,5 A2.8,2.8 0 0 1 19,9 L17,11 L13,7 Z",
        ["scope"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M12,1 L12,6 M12,18 L12,23 M1,12 L6,12 M18,12 L23,12",
        ["arrow.uturn.forward"] = "M15,14 L20,9 L15,4 M20,9 L9,9 A5,5 0 0 0 9,19 L13,19",
        ["arrow.left.and.right"] = "M3,12 L21,12 M7,8 L3,12 L7,16 M17,8 L21,12 L17,16",
        ["arrow.up.and.down"] = "M12,3 L12,21 M8,7 L12,3 L16,7 M8,17 L12,21 L16,17",
        ["eye"] = "M2,12 C5,6 19,6 22,12 C19,18 5,18 2,12 Z M9,12 A3,3 0 1 0 15,12 A3,3 0 1 0 9,12 Z",
        ["eye.slash"] = "M2,12 C5,6 19,6 22,12 C19,18 5,18 2,12 Z M9,12 A3,3 0 1 0 15,12 A3,3 0 1 0 9,12 Z M4,4 L20,20",
        ["circle"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z",
        ["plus.circle"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M12,8 L12,16 M8,12 L16,12",
        // Twelve round-capped dots on the circle.
        ["circle.dotted"] = "M21,12 l0,0.01 M19.8,16.5 l0,0.01 M16.5,19.8 l0,0.01 M12,21 l0,0.01 M7.5,19.8 l0,0.01 M4.2,16.5 l0,0.01 " +
                            "M3,12 l0,0.01 M4.2,7.5 l0,0.01 M7.5,4.2 l0,0.01 M12,3 l0,0.01 M16.5,4.2 l0,0.01 M19.8,7.5 l0,0.01",
        // Stroked, so the filled half is hatched.
        ["circle.lefthalf.filled"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M12,3 L12,21 M12,6 L6.5,6 M12,9 L4,9 M12,12 L3,12 M12,15 L4,15 M12,18 L6.5,18",
        ["largecircle.fill.circle"] = "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M9.5,12 A2.5,2.5 0 1 0 14.5,12 A2.5,2.5 0 1 0 9.5,12 Z M11,12 A1,1 0 1 0 13,12 A1,1 0 1 0 11,12 Z",
        ["star"] = "M12,2 L15,9 L22,9 L16.5,13.5 L18.5,21 L12,16.8 L5.5,21 L7.5,13.5 L2,9 L9,9 Z",
        ["rectangle.split.2x1"] = "M3,5 H21 V19 H3 Z M12,5 L12,19",
        ["rectangle.split.1x2"] = "M3,5 H21 V19 H3 Z M3,12 L21,12",
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

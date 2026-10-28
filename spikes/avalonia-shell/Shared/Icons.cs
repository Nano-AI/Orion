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
        // The tool tabs and section headers (#350). Drawn on the 24 grid, one 1.6 stroke, round ends.
        ["sun.max"] = "M12,8.5 A3.5,3.5 0 1 0 12,15.5 A3.5,3.5 0 1 0 12,8.5 M12,2.5 V4.5 M12,19.5 V21.5 M2.5,12 H4.5 M19.5,12 H21.5 " +
                      "M5.3,5.3 L6.7,6.7 M17.3,17.3 L18.7,18.7 M5.3,18.7 L6.7,17.3 M17.3,6.7 L18.7,5.3",
        ["circle.3"] = "M12,3.5 A4.5,4.5 0 1 0 12,12.5 A4.5,4.5 0 1 0 12,3.5 M8.5,10 A4.5,4.5 0 1 0 8.5,19 A4.5,4.5 0 1 0 8.5,10 " +
                       "M15.5,10 A4.5,4.5 0 1 0 15.5,19 A4.5,4.5 0 1 0 15.5,10",
        ["magnifyingglass"] = "M10.5,4 A6.5,6.5 0 1 0 10.5,17 A6.5,6.5 0 1 0 10.5,4 M15.3,15.3 L20,20",
        ["camera.aperture"] = "M12,3 A9,9 0 1 0 12,21 A9,9 0 1 0 12,3 M16.5,12 L14.25,15.9 L9.75,15.9 L7.5,12 L9.75,8.1 L14.25,8.1 Z " +
                              "M14.25,8.1 L16.5,4.2 M16.5,12 L21,12 M14.25,15.9 L16.5,19.8 M9.75,15.9 L7.5,19.8 M7.5,12 L3,12 M9.75,8.1 L7.5,4.2",
        ["crop"] = "M6,2.5 V18 H21.5 M2.5,6 H18 V21.5",
        ["square.stack"] = "M8,8 H20 V20 H8 Z M5,16 V5 H16",
        ["wand.and.stars"] = "M4,20 L14,10 M12.5,8.5 L15.5,11.5 M18,3 V7 M16,5 H20 M20.5,10 V12.5 M19.25,11.25 H21.75 M9,3.5 V6 M7.75,4.75 H10.25",
        ["thermometer"] = "M10,14 V5 A2,2 0 0 1 14,5 V14 A4,4 0 1 1 10,14 Z M12,9 V17",
        ["sun.haze"] = "M7,13 A5,5 0 0 1 17,13 M12,3.5 V5.5 M4.6,6.6 L6,8 M19.4,6.6 L18,8 M3,17 H21 M6,20.5 H18",
        ["sun.horizon"] = "M6,14 A6,6 0 0 1 18,14 M2.5,14 H21.5 M12,17.5 V21.5 M9.75,19.25 L12,21.5 L14.25,19.25",
        ["moon"] = "M19.5,14.5 A8,8 0 1 1 9.5,4.5 A6.5,6.5 0 0 0 19.5,14.5 Z",
        ["triangle"] = "M12,4 L21,19.5 H3 Z",
        ["waveform"] = "M3,12 H4.5 M7,8.5 V15.5 M10,5 V19 M13,9 V15 M16,6.5 V17.5 M19,10.5 V13.5 M21,12 H21.5",
        ["grain"] = "M6,6 h0.01 M12,5 h0.01 M18,7 h0.01 M5,12 h0.01 M10,11 h0.01 M16,13 h0.01 M19,12 h0.01 M7,18 h0.01 M13,17 h0.01 M18,19 h0.01",
        ["vignette"] = "M3,5 H21 V19 H3 Z M12,8.5 A3.5,3.5 0 1 0 12,15.5 A3.5,3.5 0 1 0 12,8.5",
        ["curve"] = "M3,3 V21 H21 M5,19 C11,19 9,5 20,5",
        ["drop"] = "M12,3 C12,3 5.5,10.5 5.5,14.5 A6.5,6.5 0 0 0 18.5,14.5 C18.5,10.5 12,3 12,3 Z",
        ["dial"] = "M12,3 A9,9 0 1 0 12,21 A9,9 0 1 0 12,3 M12,12 L17,8 M12,12 h0.01",
        ["perspective"] = "M7.5,5 H16.5 L21,19 H3 Z",
        ["aspectratio"] = "M3,6 H21 V18 H3 Z M6.5,10.5 V9 H8 M17.5,13.5 V15 H16",
        ["camera"] = "M3,8 H7 L9,5 H15 L17,8 H21 V19 H3 Z M12,10 A3.5,3.5 0 1 0 12,17 A3.5,3.5 0 1 0 12,10",
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

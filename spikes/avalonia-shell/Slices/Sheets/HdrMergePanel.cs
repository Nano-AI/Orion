using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Sheets;

/// A port of app/HdrMergePanel.swift's sheet. It exists for the one decision
/// the software cannot make: which frame's framing wins.
[Panel("sheet-hdr-merge", Region.Sheet)]
public sealed class HdrMergePanel : Border {
    internal sealed record Candidate(string Path, string Camera, float Iso, float Shutter, float Aperture);

    readonly List<Candidate> _candidates;
    readonly StackPanel _rows = new() { Spacing = Space.Md + 2 };
    int _reference;

    /// Closes the sheet. Set by whatever opened it.
    public Action? Dismiss { get; set; }

    public HdrMergePanel() {
        Width = 440;
        Padding = new Thickness(16);
        Background = Palette.Panel;
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Top;
        SheetKit.Restyle(this);
        // askHdrMerge: the frames ringed on the strip (Library.targets), checked
        // for two or more from one camera (HdrMergeFlow.ineligibility), which is said in place of the rows.
        _candidates = Library.LibraryModel.Shared.Targets.Select(p => Read(p.Path)).ToList();
        _reference = DefaultReference(_candidates);
        if (Ineligibility(_candidates) is { } why) _rows.Children.Add(SheetKit.Note(why, Palette.Star));
        else Paint();

        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm + 2, HorizontalAlignment = HorizontalAlignment.Right };
        actions.Children.Add(SheetKit.Button("Cancel", () => Dismiss?.Invoke()));
        // engine.hdrMerge, progress, cancel, then Library rescans:
        // stub(#315): Engine.swift owns this until the logic moves below the facade
        actions.Children.Add(SheetKit.Button("Merge", () => NotBuilt.Say("the HDR merge", "Engine.swift"), prominent: true));

        Child = new StackPanel {
            Spacing = Space.Md + 2,
            Children = {
                new TextBlock { Text = "Merge to HDR", FontSize = TypeSize.Md, FontWeight = FontWeight.SemiBold },
                SheetKit.Note("The chosen frame sets the framing; the others are aligned to it and the result is " +
                              "written beside the originals as a new DNG.", Palette.Dim),
                _rows,
                actions,
            },
        };
    }

    void Paint() {
        _rows.Children.Clear();
        for (int i = 0; i < _candidates.Count; i++) {
            int index = i;
            var c = _candidates[i];
            var line = Faces.Figure(ExposureLine(c), Faces.Label, Palette.Dim);
            var row = new DockPanel { Background = Brushes.Transparent, Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand) };
            var mark = Icons.Create(i == _reference ? "largecircle.fill.circle" : "circle", Faces.Mark, Palette.Text);
            mark.Margin = new Thickness(0, 0, Space.Sm + 2, 0);
            DockPanel.SetDock(mark, Dock.Left);
            DockPanel.SetDock(line, Dock.Right);
            row.Children.AddRange([mark, line, new TextBlock {
                Text = System.IO.Path.GetFileName(c.Path), FontSize = Faces.Label, VerticalAlignment = VerticalAlignment.Center,
                FontFamily = Faces.Numeral, FontWeight = FontWeight.Medium, FontFeatures = Faces.Tabular,
            }]);
            row.PointerPressed += (_, _) => { _reference = index; Paint(); };
            _rows.Children.Add(row);
        }
    }

    static Candidate Read(string path) =>
        Chrome.CaptureInfo.ReadFile(path) is { } info
            ? new(path, info.Camera, info.Iso, info.Shutter, info.Aperture)
            : new(path, "", 0, 0, 0);

    /// HdrMergeFlow.ineligibility: why these frames cannot merge, or null.
    internal static string? Ineligibility(List<Candidate> cs) {
        if (cs.Count < 2) return "Select at least two exposures to merge.";
        var cameras = cs.Select(c => c.Camera).Where(c => c.Length > 0).Distinct().Order(StringComparer.Ordinal).ToList();
        return cameras.Count <= 1 ? null : $"These photos come from different cameras: {string.Join(", ", cameras)}.";
    }

    /// HdrMergeFlow.defaultReference: the middle of the exposure ladder by t·ISO/N².
    static int DefaultReference(List<Candidate> cs) {
        if (cs.Count == 0) return 0;
        static double Light(Candidate c) => c.Shutter > 0 && c.Iso > 0 && c.Aperture > 0
            ? (double)c.Shutter * c.Iso / ((double)c.Aperture * c.Aperture) : 0;
        var order = Enumerable.Range(0, cs.Count).OrderBy(i => Light(cs[i])).ToList();
        return order[order.Count / 2];
    }

    /// CaptureInfo.exposureLine: aperture, shutter, ISO, spaced rather than dotted.
    static string ExposureLine(Candidate c) {
        if (c.Shutter <= 0 || c.Iso <= 0) return "no exposure data";
        var inv = CultureInfo.InvariantCulture;
        string? aperture = c.Aperture > 0 && float.IsFinite(c.Aperture)
            ? "f/" + c.Aperture.ToString(c.Aperture < 9.95f ? "0.0" : "0", inv) : null;
        string shutter;
        if (c.Shutter < 0.35f) shutter = "1/" + Math.Round(1 / c.Shutter).ToString(inv);
        else {
            double tenths = Math.Round(c.Shutter * 10) / 10;
            shutter = tenths == Math.Round(tenths) ? $"{(int)tenths} s" : tenths.ToString("0.0", inv) + " s";
        }
        return string.Join("   ", new[] { aperture, shutter, "ISO " + c.Iso.ToString("0", inv) }.Where(s => s != null));
    }
}

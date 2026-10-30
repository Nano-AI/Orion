using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Sheets;

/// A port of the Swift app's HdrMergePanel.swift's sheet. It exists for the one decision
/// the software cannot make: which frame's framing wins.
[Panel("sheet-hdr-merge", Region.Sheet)]
public sealed class HdrMergePanel : Border {
    internal sealed record Candidate(string Path, string Camera, float Iso, float Shutter, float Aperture);

    readonly List<Candidate> _candidates;
    readonly StackPanel _rows = new() { Spacing = Space.Pad };
    readonly StackPanel _actions = new() { Orientation = Orientation.Horizontal, Spacing = Space.Md, HorizontalAlignment = HorizontalAlignment.Right };
    int _reference;
    bool _merging;

    /// Closes the sheet. Set by whatever opened it.
    public Action? Dismiss { get; set; }

    public HdrMergePanel() {
        Width = 440;
        Padding = new Thickness(Space.Lg);
        Background = Palette.Panel;
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Top;
        SheetKit.Restyle(this);
        // askHdrMerge: the frames ringed on the strip (Library.targets), checked
        // for two or more from one camera (HdrMergeFlow.ineligibility), which is said in place of the rows.
        _candidates = Library.LibraryModel.Shared.Targets.Select(p => Read(p.Path)).ToList();
        _reference = DefaultReference(_candidates);
        if (Ineligibility(_candidates) is { } why) _rows.Children.Add(SheetKit.Note(why, Palette.Warn));
        else Paint();

        Idle();

        Child = new StackPanel {
            Spacing = Space.Pad,
            Children = {
                SheetKit.Title("Merge to HDR"),
                SheetKit.Note("The chosen frame sets the framing; the others are aligned to it and the result is " +
                              "written beside the originals as a new DNG.", Palette.Dim),
                _rows,
                _actions,
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
            var mark = Icons.Create(i == _reference ? "largecircle.fill.circle" : "circle", IconSize.Sm, Palette.Text);
            mark.Margin = new Thickness(0, 0, Space.Md, 0);
            DockPanel.SetDock(mark, Dock.Left);
            DockPanel.SetDock(line, Dock.Right);
            row.Children.AddRange([mark, line, new TextBlock {
                Text = System.IO.Path.GetFileName(c.Path), FontSize = Faces.Label, VerticalAlignment = VerticalAlignment.Center,
                FontFamily = Faces.Numeral, FontWeight = FontWeight.Medium, FontFeatures = Faces.Tabular,
            }]);
            row.PointerPressed += (_, _) => { if (!_merging) { _reference = index; Paint(); } };
            _rows.Children.Add(row);
        }
    }

    void Idle() {
        _actions.Children.Clear();
        _actions.Children.Add(SheetKit.Button("Cancel", () => Dismiss?.Invoke()));
        _actions.Children.Add(SheetKit.Button("Merge", Merge, prominent: true));
    }

    /// runHdrMerge: what is owed to the open photo lands first (the merge reads files,
    /// not the session), a progress bar and Stop while it runs, then the new DNG is
    /// a library photo like any other: its folder rescanned around it, and opened.
    async void Merge() {
        if (_merging || Ineligibility(_candidates) != null) return;
        _merging = true;
        var paths = _candidates.Select(c => c.Path).ToList();
        int reference = _reference;
        string output = HdrMerge.OutputPath(paths[reference]);
        Console.Error.WriteLine($"orion merge: start ref={reference} -> {output}");
        Autosave.Shared.Flush();
        var bar = new ProgressBar { Minimum = 0, Maximum = 1, Width = 300, MinHeight = 0, Height = 4, VerticalAlignment = VerticalAlignment.Center };
        _actions.Children.Clear();
        _actions.Children.AddRange([bar, SheetKit.Button("Stop", HdrMerge.Cancel)]);
        using var poll = Avalonia.Threading.DispatcherTimer.Run(() => { bar.Value = HdrMerge.Progress ?? bar.Value; return true; },
                                                                TimeSpan.FromMilliseconds(150));
        string? failed = await HdrMerge.Run(paths, reference, output);
        _merging = false;
        Dismiss?.Invoke();
        if (failed != null) {
            Console.Error.WriteLine($"orion merge: failed: {failed}");
            EngineModel.Shared.Say(failed);
            return;
        }
        Console.Error.WriteLine($"orion merge: wrote {output}");
        Library.LibraryFiles.OpenPhoto(output);
        EngineModel.Shared.Say($"Merged {paths.Count} exposures into {System.IO.Path.GetFileName(output)}");
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

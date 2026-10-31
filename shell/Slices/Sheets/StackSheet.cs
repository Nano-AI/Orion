using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using OrionShell.Library;
using Rows = Avalonia.Controls.StackPanel;

namespace OrionShell.Sheets;

/// Stacking by hand (#391), modelled on HdrMergePanel's sheet: which frames, how they combine,
/// which one sets the framing, and in Landscape where the sky is. (Not "StackPanel": that name
/// would shadow Avalonia's in every sheet of this namespace.)
[Panel("sheet-stack", Region.Sheet)]
public sealed class StackSheet : Border {
    // The facade's rejection ints 0…4, in order; the menu puts Auto (-1) first.
    static readonly string[] Rejections = ["None", "Percentile", "Sigma", "Winsorized", "Linear fit"];
    static readonly string[] ModeNotes = [
        "Frames of one scene aligned and averaged: the same picture with less noise.",
        "Aligned on the stars, which stay sharp; the ground smears.",
        "The sky aligned on its stars, the ground kept still, joined where the sky is painted.",
    ];

    readonly List<HdrMergePanel.Candidate> _candidates;
    readonly Rows _rows = new() { Spacing = Space.Sm };
    readonly Rows _split = new();
    readonly Rows _actions = new() { Orientation = Orientation.Horizontal, Spacing = Space.Md, HorizontalAlignment = HorizontalAlignment.Right };
    readonly TextBlock _modeNote = SheetKit.Note("", Palette.Dim);
    readonly StackCalibration _calibration = new();
    Stack.Mode _mode;
    int _reference, _rejection = -1;
    bool _weight, _stacking;
    StackSplit? _painter;
    Button? _stack;
    string? _splitFor;   // the reference the Sky row was built for

    /// Closes the sheet. Set by whatever opened it.
    public Action? Dismiss { get; set; }

    /// The command's CanRun, so cheap: a real selection, or an already-scanned group holding the open photo.
    public static bool HasCandidates =>
        LibraryModel.Shared.HasExplicitSelection || OpenGroup(StackGroups.Cached(EngineModel.Shared.Photos)) != null;

    static StackGroup? OpenGroup(IReadOnlyList<StackGroup>? groups) =>
        groups != null && EngineModel.Shared.Current is var i and >= 0
            ? groups.FirstOrDefault(g => g.Paths.Contains(EngineModel.Shared.Photos[i])) : null;

    public StackSheet() {
        Width = 440;
        Padding = new Thickness(Space.Lg);
        Background = Palette.Panel;
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Top;
        SheetKit.Restyle(this);
        // The selection when it is real (what the suggestion's Stack… makes), else the stack the open photo belongs to.
        var library = LibraryModel.Shared;
        var paths = library.HasExplicitSelection ? library.Targets.Select(p => p.Path).ToList()
                  : OpenGroup(StackGroups.Of(EngineModel.Shared.Photos))?.Paths.ToList() ?? [];
        _candidates = paths.Select(HdrMergePanel.Read).ToList();
        _reference = _candidates.Count / 2;   // the middle frame, as StackGroups' dark check
        _mode = DefaultMode(paths, StackGroups.Cached(EngineModel.Shared.Photos));

        var body = new Rows {
            Spacing = Space.Pad,
            Children = {
                SheetKit.Title("Stack photos"),
                SheetKit.Note("The chosen frame sets the framing; the others are aligned to it and the result is " +
                              "written beside the originals as a new DNG.", Palette.Dim),
            },
        };
        if (Ineligibility(_candidates) is { } why) body.Children.Add(SheetKit.Note(why, Palette.Warn));
        else body.Children.AddRange(Choices());
        Idle();
        // As ExportPanel: the choices scroll on a small display; Cancel and Stack stay put. 760, not Export's 540:
        // Landscape with a portrait frame's painter and its brush is about 730 tall (measured in a screenshot).
        Child = new Rows { Spacing = Space.Pad, Children = { new ScrollViewer { Content = body, MaxHeight = 760 }, _actions } };
    }

    /// Landscape needs a painted sky, so a raw with no preview to paint it on cannot stack that way.
    bool Ready => _mode != Stack.Mode.Landscape || _painter != null;

    IEnumerable<Control> Choices() {
        var mode = new Segmented(["Noise", "Stars", "Landscape"], (int)_mode);
        mode.Changed += i => { _mode = (Stack.Mode)i; ShowMode(); };
        string auto = AutoRejection(_candidates.Count);
        var rejection = SheetKit.Menu(Rejections.Prepend($"Auto ({auto})"), 0, i => _rejection = i - 1);
        rejection.HorizontalAlignment = HorizontalAlignment.Left;
        var weight = new CheckBox {
            VerticalContentAlignment = VerticalAlignment.Center,
            Content = new Rows {
                Orientation = Orientation.Horizontal, Spacing = Space.Gap,
                Children = {
                    new TextBlock { Text = "Weight by noise", FontSize = Faces.Label },
                    new TextBlock { Text = "cleaner frames count for more", FontSize = Faces.Mark, Foreground = Palette.Dim },
                },
            },
        };
        weight.IsCheckedChanged += (_, _) => _weight = weight.IsChecked == true;
        PaintRows();
        ShowMode();
        return [
            SheetKit.Row("Mode", mode, _modeNote),
            SheetKit.Row("Rejection", rejection),
            weight,
            SheetKit.Row("Calibration", _calibration),
            SheetKit.Row("Reference frame", new ScrollViewer { MaxHeight = 140, Content = _rows }),
            _split,
        ];
    }

    void PaintRows() {
        _rows.Children.Clear();
        for (int i = 0; i < _candidates.Count; i++) {
            int index = i;
            var row = new DockPanel { Background = Brushes.Transparent, Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand) };
            var mark = Icons.Create(i == _reference ? "largecircle.fill.circle" : "circle", IconSize.Sm, Palette.Text);
            mark.Margin = new Thickness(0, 0, Space.Md, 0);
            DockPanel.SetDock(mark, Dock.Left);
            row.Children.AddRange([mark, new TextBlock {
                Text = System.IO.Path.GetFileName(_candidates[i].Path), FontSize = Faces.Label, VerticalAlignment = VerticalAlignment.Center,
                FontFamily = Faces.Numeral, FontWeight = FontWeight.Medium, FontFeatures = Faces.Tabular,
            }]);
            row.PointerPressed += (_, _) => { if (!_stacking) { _reference = index; PaintRows(); ShowMode(); } };
            _rows.Children.Add(row);
        }
    }

    /// The mode's note, and in Landscape the Sky row for the reference frame, seeded when the reference changes.
    void ShowMode() {
        _modeNote.Text = ModeNotes[(int)_mode];
        _split.IsVisible = _mode == Stack.Mode.Landscape;
        string path = _candidates[_reference].Path;
        if (_split.IsVisible && _splitFor != path) {
            _splitFor = path;
            _painter = StackSplit.For(path);
            _split.Children.Clear();
            _split.Children.Add(SheetKit.Row("Sky", _painter?.Editor() ?? SheetKit.Note("This raw carries no preview to paint the sky on.", Palette.Warn)));
        }
        if (_stack != null) _stack.IsEnabled = Ready;
    }

    void Idle() {
        _actions.Children.Clear();
        _actions.Children.Add(SheetKit.Button("Cancel", () => Dismiss?.Invoke()));
        _actions.Children.Add(_stack = SheetKit.Button("Stack", Run, prominent: true));
        _stack.IsEnabled = Ready;
    }

    /// As HdrMergePanel.Merge: what is owed to the open photo lands first (the stack reads files),
    /// a progress bar and Stop while it runs, then the new DNG is opened like any library photo.
    async void Run() {
        if (_stacking || !Ready || Ineligibility(_candidates) != null) return;
        _stacking = true;
        _calibration.IsEnabled = false;
        var paths = _candidates.Select(c => c.Path).ToList();
        var darks = _calibration.Darks.ToList();
        var flats = _calibration.Flats.ToList();
        int reference = _reference;
        var mode = _mode;
        string output = Stack.OutputPath(paths[reference]);
        // The matte as painted now, copied: a stroke made while the stack runs cannot reach the engine's reads.
        var painter = mode == Stack.Mode.Landscape ? _painter : null;
        float[]? matte = (float[]?)painter?.Matte.Clone();
        Console.Error.WriteLine($"orion stack: start ref={reference} mode={mode} darks={darks.Count} flats={flats.Count} -> {output}");
        Autosave.Shared.Flush();
        var bar = new ProgressBar { Minimum = 0, Maximum = 1, Width = 300, MinHeight = 0, Height = 4, VerticalAlignment = VerticalAlignment.Center };
        _actions.Children.Clear();
        _actions.Children.AddRange([bar, SheetKit.Button("Stop", Stack.Cancel)]);
        using var poll = Avalonia.Threading.DispatcherTimer.Run(() => { bar.Value = Stack.Progress ?? bar.Value; return true; },
                                                                TimeSpan.FromMilliseconds(150));
        string? failed = await Stack.Run(paths, reference, mode, _rejection, _weight, darks, flats, matte,
                                         painter?.MatteWidth ?? 0, painter?.MatteHeight ?? 0, output);
        _stacking = false;
        Dismiss?.Invoke();
        if (failed != null) {
            Console.Error.WriteLine($"orion stack: failed: {failed}");
            EngineModel.Shared.Say(failed);
            return;
        }
        Console.Error.WriteLine($"orion stack: wrote {output}");
        LibraryFiles.OpenPhoto(output);
        EngineModel.Shared.Say($"Stacked {paths.Count} frames into {System.IO.Path.GetFileName(output)}");
    }

    /// Why these frames cannot stack, or null: HDR merge's camera rule, in a stack's words for too few.
    internal static string? Ineligibility(List<HdrMergePanel.Candidate> cs) =>
        cs.Count < 2 ? "Select at least two frames to stack." : HdrMergePanel.Ineligibility(cs);

    /// Stars when `paths` are exactly an astro group found in the folder (in any order), else Noise.
    internal static Stack.Mode DefaultMode(IReadOnlyCollection<string> paths, IReadOnlyList<StackGroup>? groups) =>
        groups?.FirstOrDefault(g => g.Paths.Count == paths.Count && g.Paths.ToHashSet().SetEquals(paths))?.Astro == true
            ? Stack.Mode.Stars : Stack.Mode.Noise;

    /// What the engine's Auto picks for `frames` (engine/src/stack/Rejection.cpp autoRejection).
    internal static string AutoRejection(int frames) => Rejections[frames <= 3 ? 0 : frames <= 20 ? 3 : 4];
}

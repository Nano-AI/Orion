using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using OrionShell.Library;
using Rows = Avalonia.Controls.StackPanel;

namespace OrionShell.Sheets;

/// Stacking by hand (#391), modelled on HdrMergePanel's sheet: which frames, how they combine, which one
/// sets the framing, and in Landscape where the sky is. Laid out as Orion's window (#411): the reference
/// frame large with every frame in a strip under it (StackFrames), the choices in a tool column on the
/// right, Cancel and Stack pinned at its foot. It resizes; the picture takes the room. (Not "StackPanel":
/// that name would shadow Avalonia's in every sheet of this namespace.)
[Panel("sheet-stack", Region.Sheet)]
public sealed class StackSheet : Border {
    /// The size the sheet's window opens at (CommandTable.Sheet keeps it inside the screen).
    public static readonly Size Opening = new(1080, 700);

    // The facade's rejection ints 0…4, in order; the menu puts Auto (-1) first.
    static readonly string[] Rejections = ["None", "Percentile", "Sigma", "Winsorized", "Linear fit"];
    static readonly string[] ModeNotes = [
        "Frames of one scene aligned and averaged: the same picture with less noise.",
        "Aligned on the stars, which stay sharp; the ground smears.",
        "The sky aligned on its stars, the ground kept still, joined where the sky is painted.",
    ];

    readonly List<HdrMergePanel.Candidate> _candidates;
    readonly StackFrames _frames;
    readonly ScrollViewer _controls;
    readonly Rows _actions = new() { Orientation = Orientation.Horizontal, Spacing = Space.Md, HorizontalAlignment = HorizontalAlignment.Right };
    readonly TextBlock _modeNote = SheetKit.Note("", Palette.Dim);
    readonly SheetAlert _notice = new();    // why Stack did nothing, or failed
    readonly SheetAlert _warning;           // over the memory available: the two answers, in place of Cancel and Stack
    readonly SheetAlert _noSky = new();     // the detector found no horizon to seed the sky from
    readonly Decorator _skyTools = new();
    readonly Control _sky;                  // Landscape's row: the brush for the painter, which is the preview
    readonly StackCalibration _calibration = new();
    Segmented? _modes;
    Stack.Mode _mode;
    int _reference, _rejection = -1, _previews;
    bool _weight, _stacking, _closed;
    StackSplit? _painter;
    Button? _stack;

    /// Closes the sheet. Set by whatever opened it.
    public Action? Dismiss { get; set; }

    /// The command's CanRun, so cheap: a real selection, or an already-scanned group holding the open photo.
    public static bool HasCandidates =>
        LibraryModel.Shared.HasExplicitSelection || OpenGroup(StackGroups.Cached(EngineModel.Shared.Photos)) != null;

    static StackGroup? OpenGroup(IReadOnlyList<StackGroup>? groups) =>
        groups != null && EngineModel.Shared.Current is var i and >= 0
            ? groups.FirstOrDefault(g => g.Paths.Contains(EngineModel.Shared.Photos[i])) : null;

    public StackSheet() {
        Background = Palette.Panel;
        // The controls' column at its tallest (Landscape, with the no-horizon alert) beside a picture that
        // still reads; SelfTest.StackSheetResize holds the column to fitting here without a scroll.
        (MinWidth, MinHeight) = (Layout.ToolCol + 360, 640);
        SheetKit.Restyle(this);
        // The selection when it is real (what the suggestion's Stack… makes), else the stack the open photo belongs to.
        var library = LibraryModel.Shared;
        var paths = library.HasExplicitSelection ? library.Targets.Select(p => p.Path).ToList()
                  : OpenGroup(StackGroups.Of(EngineModel.Shared.Photos))?.Paths.ToList() ?? [];
        _candidates = paths.Select(HdrMergePanel.Read).ToList();
        _reference = _candidates.Count / 2;   // the middle frame, as StackGroups' dark check
        _mode = DefaultMode(paths, StackGroups.Cached(EngineModel.Shared.Photos));
        _frames = new StackFrames(_candidates, _reference);
        _frames.Chose += Choose;
        _sky = SheetKit.Row("Sky", _skyTools, _noSky);

        var body = new Rows {
            Spacing = Space.Lg,
            Children = {
                new Rows { Spacing = Space.Sm, Children = {
                    SheetKit.Title("Stack photos"),
                    SheetKit.Note("The chosen frame sets the framing; the others are aligned to it and the result is " +
                                  "written beside the originals as a new DNG.", Palette.Dim),
                } },
            },
        };
        if (Ineligibility(_candidates) is { } why) {
            var alert = new SheetAlert();
            alert.Show("These frames cannot stack", why);
            body.Children.Add(alert);
        } else body.Children.AddRange(Choices());
        Idle();
        _warning = new SheetAlert(SheetKit.Button("Cancel", () => Warn(null)), SheetKit.Button("Proceed anyway", () => _ = Proceed()));
        // The choices scroll only when the window is under its minimum (it cannot be); the foot never does.
        _controls = new ScrollViewer { Content = body, Padding = new Thickness(Space.Lg, Space.Lg, Space.Lg, Space.Pad) };
        var foot = new Rows { Spacing = Space.Md, Margin = new Thickness(Space.Lg, 0, Space.Lg, Space.Lg), Children = { _notice, _warning, _actions } };
        Grid.SetRow(foot, 1);
        var column = new Border {
            Width = Layout.ToolCol, BorderBrush = Palette.Line, BorderThickness = new Thickness(1, 0, 0, 0),
            Child = new Grid { RowDefinitions = new RowDefinitions("*,Auto"), Children = { _controls, foot } },
        };
        Grid.SetColumn(column, 1);
        Child = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), Children = { _frames, column } };
        _calibration.Changed += () => Warn(null);   // the warning's numbers described the old lists
        ShowMode(animate: false);
    }

    string? ReferencePath => _candidates.Count > 0 ? _candidates[_reference].Path : null;

    /// Landscape needs a painted sky, so a raw with no preview to paint it on cannot stack that way.
    bool Paintable => _mode != Stack.Mode.Landscape || (_painter != null && _painter.Path == ReferencePath);

    /// And some sky painted: an empty matte finds no stars, and the engine would say only that nothing registered.
    /// (Checked on Stack, not live: the painter has no change event.)
    bool Ready => Paintable && (_mode != Stack.Mode.Landscape || _painter!.Matte.Any(v => v >= 0.5f));

    void Notice(string? headline, string? detail = null) {
        if (headline == null) _notice.Hide();
        else _notice.Show(headline, detail);
    }

    IEnumerable<Control> Choices() {
        _modes = new Segmented(["Noise", "Stars", "Landscape"], (int)_mode);
        _modes.Changed += i => { _mode = (Stack.Mode)i; ShowMode(animate: true); };
        string auto = AutoRejection(_candidates.Count);
        var rejection = SheetKit.Menu(Rejections.Prepend($"Auto ({auto})"), 0, i => _rejection = i - 1);
        rejection.HorizontalAlignment = HorizontalAlignment.Left;
        var weight = new CheckBox {
            VerticalContentAlignment = VerticalAlignment.Top,
            Content = new Rows {
                Spacing = Space.Xs,
                Children = {
                    new TextBlock { Text = "Weight by noise", FontSize = Faces.Label },
                    new TextBlock { Text = "Cleaner frames count for more.", FontSize = Faces.Mark, Foreground = Palette.Dim },
                },
            },
        };
        weight.IsCheckedChanged += (_, _) => _weight = weight.IsChecked == true;
        return [SheetKit.Row("Mode", _modes, _modeNote), _sky, SheetKit.Row("Rejection", rejection, weight), SheetKit.Row("Calibration", _calibration)];
    }

    /// A frame chosen in the strip: the preview follows, crossfaded unless the key chose it.
    void Choose(int index, bool byKey) {
        if (_stacking || index == _reference) return;
        _reference = index;
        _frames.Mark(index);
        ShowMode(animate: !byKey);
    }

    /// The mode's note, Landscape's Sky row, and the preview for the reference in this mode.
    void ShowMode(bool animate) {
        Notice(null);   // "Paint the sky first" answered by a change of mode or frame
        Warn(null);     // and a memory warning about the old frame or mode no longer applies
        _modeNote.Text = ModeNotes[(int)_mode];
        _sky.IsVisible = _mode == Stack.Mode.Landscape;
        if (_stack != null) _stack.IsEnabled = Paintable;
        _ = Preview(animate);
    }

    /// The reference frame's picture once read (at once when it has been), or in Landscape the painter seeded on
    /// it; a later choice overtakes a read still running. Discarded by its callers, so nothing may escape it.
    async Task Preview(bool animate) {
        if (ReferencePath is not { } path) {
            _frames.Show(StackFrames.Empty("No frames to stack", "Select two or more frames of one scene, then choose Stack photos… again."), false);
            return;
        }
        int ticket = ++_previews;
        Bitmap? picture = null;
        try { picture = await _frames.Picture(path); }
        catch (Exception e) { Console.Error.WriteLine($"orion stack: preview of {path}: {e.Message}"); }
        if (ticket != _previews || _closed) return;
        bool landscape = _mode == Stack.Mode.Landscape;
        if (picture != null && landscape && _painter?.Path != path) {
            _painter = StackSplit.For(path, picture);
            // Painting answers "Paint the sky first" (the painter handles its own presses, hence handledEventsToo).
            _painter.AddHandler(InputElement.PointerPressedEvent, (_, _) => Notice(null), handledEventsToo: true);
            _skyTools.Child = _painter.Tools();
        }
        bool painting = picture != null && landscape;
        _skyTools.IsVisible = painting;
        if (painting && _painter!.NoSky != null) _noSky.Show("No horizon found", "Paint the sky on the picture with Add sky.");
        else _noSky.Hide();
        Control view = picture == null
            ? landscape ? StackFrames.Empty("No preview to paint the sky on", "Landscape paints the sky on the raw's embedded preview, and this one has none. Choose another frame, or another mode.")
                        : StackFrames.Empty("No preview in this raw", "It stacks all the same; the camera saved no picture to show here.")
            : landscape ? _painter! : StackFrames.Framed(picture);
        PreviewPath = path;
        _frames.Show(view, animate);
        if (_stack != null) _stack.IsEnabled = Paintable;
    }

    void Idle() {
        _actions.Children.Clear();
        _actions.Children.Add(SheetKit.Button("Cancel", () => Dismiss?.Invoke()));
        _actions.Children.Add(_stack = SheetKit.Button("Stack", Run, prominent: true));
        _stack.IsEnabled = Paintable;
    }

    void Run() => _ = Press();

    /// Stack pressed: the memory this stack needs against what is free, fresh on every press (closing apps
    /// can clear it), then the stack, or the warning whose Proceed anyway forces it.
    internal async Task Press() {
        if (_stacking || !Paintable || Ineligibility(_candidates) != null) return;
        if (!Ready) {
            Notice("Paint the sky first", "Brush over the sky in the picture. Landscape aligns what you paint on the stars and keeps the rest still.");
            return;
        }
        Notice(null);
        Warn(null);
        _stacking = true;   // a second press while the check runs is ignored
        (ulong Need, ulong Free)? memory = null;
        try {
            memory = await Stack.Memory(_candidates.Select(c => c.Path).ToList(), _calibration.Darks.Count, _calibration.Flats.Count);
        } catch (Exception e) {
            Console.Error.WriteLine($"orion stack: memory check: {e.Message}");   // unknown: the engine checks again
        } finally {
            _stacking = false;
        }
        if (_closed) return;   // closed while the check ran: no stack nobody can see
        if (memory is { Need: var need, Free: var free } && free > 0 && need > free) {
            Warn(MemoryWarning(_candidates.Count, need, free));
            return;
        }
        await Start(force: false);
    }

    /// The warning's Cancel and Proceed anyway stand in for the sheet's own row while it shows: one Cancel, not two.
    /// Null takes it away.
    void Warn(string? detail) {
        if (detail == null) _warning.Hide();
        else _warning.Show("Not enough free memory", detail);
        _actions.IsVisible = detail == null;
    }

    internal Task Proceed() {
        Warn(null);
        return Start(force: true);
    }

    // Test seams: what the selftest and the screenshot scenes read of the sheet, and the mode they set.
    internal bool WarningShown => _warning.IsVisible;
    internal string WarningText => _warning.Detail;
    internal string? PreviewPath { get; private set; }
    internal Control? PreviewShown => _frames.Shown;
    internal string Caption => _frames.Caption;
    internal bool PreviewsRead => _frames.Read;
    internal bool StackEnabled => _stack?.IsEnabled == true && _actions.IsVisible;
    internal Control Strip => _frames.Strip;
    internal Control PreviewPane => _frames.Pane;
    internal ScrollViewer Controls => _controls;
    internal Control Actions => _actions;
    internal Stack.Mode Mode {
        get => _mode;
        set {
            _mode = value;
            if (_modes != null) _modes.Selected = (int)value;
            ShowMode(animate: false);
        }
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnDetachedFromVisualTree(e);
        _closed = true;
    }

    /// Decimal GB, one decimal, invariant: "memory" and "free", never "space".
    internal static string MemoryWarning(int frames, ulong need, ulong free) => FormattableString.Invariant(
        $"These {frames} frames need about {need / 1e9:0.0} GB of memory and about {free / 1e9:0.0} GB is free. Close other apps to free memory, or proceed anyway.");

    /// As HdrMergePanel.Merge: what is owed to the open photo lands first (the stack reads files),
    /// a progress bar and Stop while it runs, then the new DNG is opened like any library photo.
    async Task Start(bool force) {
        if (_stacking) return;
        _stacking = true;
        try {
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
            var bar = new ProgressBar { Minimum = 0, Maximum = 1, Width = 180, MinHeight = 0, Height = 4, VerticalAlignment = VerticalAlignment.Center };
            _actions.Children.Clear();
            _actions.Children.AddRange([bar, SheetKit.Button("Stop", Stack.Cancel)]);
            using var poll = Avalonia.Threading.DispatcherTimer.Run(() => { bar.Value = Stack.Progress ?? bar.Value; return true; },
                                                                    TimeSpan.FromMilliseconds(150));
            string? failed = await Stack.Run(paths, reference, mode, _rejection, _weight, darks, flats, matte,
                                             painter?.MatteWidth ?? 0, painter?.MatteHeight ?? 0, output, force);
            if (failed != null && !force && Stack.LastStatus == Stack.NotEnoughMemory) {
                // The engine read less memory free than the sheet's check did (or the check could not tell):
                // the same warning, with its Proceed anyway, rather than a closed sheet and nothing to press.
                Idle();
                _calibration.IsEnabled = true;
                Warn(failed);
                return;
            }
            Dismiss?.Invoke();
            if (failed != null) {
                Console.Error.WriteLine($"orion stack: failed: {failed}");
                EngineModel.Shared.Say(failed);
                return;
            }
            Console.Error.WriteLine($"orion stack: wrote {output}");
            Opened(paths, output);
            int skipped = Stack.Skipped;
            string name = System.IO.Path.GetFileName(output);
            EngineModel.Shared.Say(skipped == 0 ? $"Stacked {paths.Count} frames into {name}"
                : $"Stacked {paths.Count - skipped} of {paths.Count} frames ({skipped} did not align) into {name}");
        } catch (Exception e) {
            // Run discards this Task: an exception escaping here would go unobserved.
            Console.Error.WriteLine($"orion stack: {e}");
            Notice("The stack failed", e.Message);
            EngineModel.Shared.Say(e.Message);   // in case the sheet has already closed
            Idle();
            _calibration.IsEnabled = true;
        } finally {
            _stacking = false;
        }
    }

    /// The stack is written: open it like any library photo, then dismiss its frames' suggestion so the
    /// rescan the new DNG causes does not offer them again. In that order: opening loads the folder, and
    /// FolderLoaded clears the dismissals.
    internal static void Opened(IReadOnlyList<string> paths, string output) {
        LibraryFiles.OpenPhoto(output);
        StackSuggestion.DismissFrames(paths);
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

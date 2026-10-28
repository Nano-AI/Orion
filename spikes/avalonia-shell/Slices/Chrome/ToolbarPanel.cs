using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using OrionShell.CanvasSlice;
using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Sheets;

namespace OrionShell.Chrome;

/// A port of OrionApp+Chrome.swift's `toolbar`. It reads left to right in the
/// order the work happens: what you open, what you are looking at, what you do
/// to it, and what comes out. Rebuilt on every open; a render changes nothing here.
[Panel("toolbar", Region.Toolbar)]
public sealed class ToolbarPanel : Grid {
    readonly CaptureLine _capture = new();
    readonly StackPanel _looking = Group(), _editing = Group(), _out = Group();

    public ToolbarPanel() {
        ColumnDefinitions = new("Auto,Auto,*,Auto,Auto,Auto,Auto,Auto");
        ColumnSpacing = 12;
        Margin = new Thickness(Space.Pad, 0);
        Height = Layout.Toolbar;

        // Serif wordmark against a sans interface: the instrument is read at a
        // glance, the name is read once.
        var wordmark = new TextBlock {
            Text = "Orion", FontSize = 17, LetterSpacing = 0.5, Foreground = Palette.Text,
            FontFamily = new FontFamily("New York, Georgia, Times New Roman, serif"), VerticalAlignment = VerticalAlignment.Center,
        };
        Control[] columns = [wordmark, OpenMenu(), _capture, _looking, Chip.Divider(), _editing, Chip.Divider(), _out];
        for (int i = 0; i < columns.Length; i++) { SetColumn(columns[i], i); Children.Add(columns[i]); }

        EngineModel.Shared.Opened += Refresh;
        EngineModel.Shared.PropertyChanged += (_, _) => RefreshIfChanged();
        CanvasState.Changed += RefreshIfChanged;   // compare on, off, swapped
        EditHistory.Changed += RefreshIfChanged;
        GalleryMode.Changed += RefreshIfChanged;
        ExportRun.Changed += RefreshIfChanged;
        LibraryModel.Shared.Changed += RefreshIfChanged;
        Refresh();
    }

    /// Everything the chips show, so a pan or a render does not rebuild them.
    static string State => string.Join("|", EngineModel.Shared.Handle, EngineModel.Shared.Current, CompareState.Comparing,
        CompareState.Vertical, EditHistory.UndoLabel, EditHistory.RedoLabel, GalleryMode.On, ExportRun.Progress,
        string.Join(",", Commands.All.Select(c => c.CanRun() ? 1 : 0)));
    string _shown = "";

    void RefreshIfChanged() { if (State != _shown) Refresh(); }

    static StackPanel Group() => new() { Orientation = Orientation.Horizontal, Spacing = 7, VerticalAlignment = VerticalAlignment.Center };

    /// The Open menu, as the reference shots draw it rather than as the Swift
    /// asks: a borderless menu button on macOS keeps the title and the first
    /// image, drops the chevron, and ignores the 23-point frame, so it sits
    /// 16 high with tight padding beside 23-high chips.
    static Control OpenMenu() {
        var label = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 5, Children = {
            Icons.Create("folder", 11, Palette.Text),
            new TextBlock { Text = "Open", FontSize = TypeSize.Sm, FontWeight = FontWeight.Medium, Foreground = Palette.Text },
        } };
        var open = new Chip(label, "Open a photo or a folder", () => { }) {
            Background = Palette.Raised, Height = 16, Padding = new Thickness(Space.Sm, 0),
        };
        open.Flyout = new MenuFlyout { Placement = PlacementMode.BottomEdgeAlignedLeft, Items = {
            Item("Open Photo…", () => Commands.Run("open-photo")),
            Item("Open Folder…", () => Commands.Run("open-folder")),
        } };
        return open;
    }

    /// Every button runs a command by name, and is greyed by that command's
    /// CanRun (Swift's `.disabled`), so the keys and the buttons cannot disagree.
    void Refresh() {
        _shown = State;
        var engine = EngineModel.Shared;
        bool loaded = engine.Handle != 0, develop = !ChromeStubs.Culling;
        _capture.Show(loaded ? CaptureInfo.Read(engine.Handle) : null, engine.Current >= 0 ? engine.Photos[engine.Current] : null);

        // ── Looking: accented while up, because both answer "what am I looking at".
        _looking.Children.Clear();
        _looking.Children.Add(Toggle("Gallery", ChromeStubs.Culling, Commands.CanRun("gallery"),
            "Every photo in the folder at once, for culling (G)", () => Commands.Run("gallery")));
        if (loaded && develop && ChromeStubs.Percent is { } percent)
            _looking.Children.Add(new TextBlock { Text = $"{percent}%", FontSize = TypeSize.Sm, Foreground = Palette.Dim, Width = 42,
                                                  TextAlignment = TextAlignment.Right, Classes = { "tnum" }, VerticalAlignment = VerticalAlignment.Center });
        _looking.Children.Add(Toggle("Compare", CompareState.Comparing, Commands.CanRun("compare"), "Split the view against the original",
            () => Commands.Run("compare")));
        if (CompareState.Comparing)
            _looking.Children.Add(Chip.Icon(CompareState.Vertical ? "rectangle.split.2x1" : "rectangle.split.1x2",
                "Swap between a vertical and horizontal split", Commands.CanRun("swap-split"), () => Commands.Run("swap-split")));

        // ── Editing: greyed as a group in the gallery, named after the step.
        _editing.Children.Clear();
        _editing.Children.AddRange([
            Chip.Icon("arrow.uturn.backward", EditHistory.UndoLabel is { } u ? $"Undo {u}" : "Undo", Commands.CanRun("undo"),
                      () => Commands.Run("undo")),
            Chip.Icon("arrow.uturn.forward", EditHistory.RedoLabel is { } r ? $"Redo {r}" : "Redo", Commands.CanRun("redo"),
                      () => Commands.Run("redo")),
            Chip.Icon("rotate.left", "Rotate left", Commands.CanRun("rotate-left"), () => Commands.Run("rotate-left")),
            Chip.Icon("rotate.right", "Rotate right", Commands.CanRun("rotate-right"), () => Commands.Run("rotate-right")),
            Chip.Text("Reset", Commands.CanRun("reset-adjustments"), () => Commands.Run("reset-adjustments"), "Put every adjustment back"),
        ]);

        // ── Out: the button counts the selection; while a batch runs, the
        // readout and Stop take its place.
        _out.Children.Clear();
        if (ChromeStubs.BatchProgress is { } p) {
            _out.Children.Add(Chip.Ink($"Exporting {p.Done} of {p.Total}…", Palette.Dim));
            _out.Children.Add(Chip.Text("Stop", Commands.CanRun("stop-batch"), () => Commands.Run("stop-batch"),
                                        "Stop after the photo being written"));
        } else {
            bool exportable = Commands.CanRun("export");
            int selected = ChromeStubs.ExportSelected;
            var text = Chip.Ink(selected > 0 ? $"Export {selected}…" : "Export…", exportable ? Palette.Text : Palette.Faint);
            text.FontWeight = FontWeight.Medium;
            text.Classes.Add("tnum");
            _out.Children.Add(new Chip(text, selected > 0 ? $"Export the {selected} selected photos to a folder" : "Export this photo",
                                       () => Commands.Run("export"), exportable) {
                Padding = new Thickness(11, 0),
                Background = exportable ? Chip.Faded(Palette.Accent, 0.18) : Brushes.Transparent,
                BorderBrush = exportable ? Chip.Faded(Palette.Accent, 0.55) : Palette.Line,
            });
        }
    }

    static MenuItem Item(string header, Action action) {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => action();
        return item;
    }

    static Chip Toggle(string title, bool on, bool enabled, string help, Action action) =>
        new(Chip.Ink(title, on ? Palette.Accent : enabled ? Palette.Dim : Palette.Faint), help, action, enabled) {
            BorderBrush = on ? Palette.Accent : Palette.Line,
        };
}

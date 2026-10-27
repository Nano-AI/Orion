using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

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
        Refresh();
    }

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
            Item("Open Photo…", ChromeStubs.OpenPhoto),
            Item("Open Folder…", ChromeStubs.OpenFolder),
        } };
        return open;
    }

    void Refresh() {
        var engine = EngineModel.Shared;
        bool loaded = engine.Handle != 0, develop = !ChromeStubs.Culling, locked = ChromeStubs.DocumentEditsLocked;
        bool editable = loaded && develop && !locked;
        _capture.Show(loaded ? CaptureInfo.Read(engine.Handle) : null, engine.Current >= 0 ? engine.Photos[engine.Current] : null);

        // ── Looking: accented while up, because both answer "what am I looking at".
        _looking.Children.Clear();
        _looking.Children.Add(Toggle("Gallery", ChromeStubs.Culling, engine.Photos.Count > 0,
            "Every photo in the folder at once, for culling (G)", ChromeStubs.ToggleGallery));
        if (loaded && develop && ChromeStubs.Percent is { } percent)
            _looking.Children.Add(new TextBlock { Text = $"{percent}%", FontSize = TypeSize.Sm, Foreground = Palette.Dim, Width = 42,
                                                  TextAlignment = TextAlignment.Right, Classes = { "tnum" }, VerticalAlignment = VerticalAlignment.Center });
        _looking.Children.Add(Toggle("Compare", ChromeStubs.Comparing, loaded && develop, "Split the view against the original",
            ChromeStubs.ToggleCompare));
        if (ChromeStubs.Comparing)
            _looking.Children.Add(Chip.Icon("rectangle.split.2x1", "Swap between a vertical and horizontal split", true,
                ChromeStubs.SwapSplit));

        // ── Editing: greyed as a group in the gallery. Swift names the undo
        // chips after the step ("Undo Exposure"); the history is not here.
        _editing.Children.Clear();
        _editing.Children.AddRange([
            Chip.Icon("arrow.uturn.backward", "Undo", ChromeStubs.CanUndo && develop && !locked, ChromeStubs.Undo),
            Chip.Icon("arrow.uturn.forward", "Redo", ChromeStubs.CanRedo && develop && !locked, ChromeStubs.Redo),
            Chip.Icon("rotate.left", "Rotate left", editable, () => ChromeStubs.Rotate(-1)),
            Chip.Icon("rotate.right", "Rotate right", editable, () => ChromeStubs.Rotate(1)),
            Chip.Text("Reset", editable, ChromeStubs.ResetEdits, "Put every adjustment back"),
        ]);

        // ── Out: the button counts the selection; while a batch runs, the
        // readout and Stop take its place.
        _out.Children.Clear();
        if (ChromeStubs.BatchProgress is { } p) {
            _out.Children.Add(Chip.Ink($"Exporting {p.Done} of {p.Total}…", Palette.Dim));
            _out.Children.Add(Chip.Text("Stop", true, ChromeStubs.StopBatch, "Stop after the photo being written"));
        } else {
            bool exportable = loaded && !locked;
            int selected = ChromeStubs.ExportSelected;
            var text = Chip.Ink(selected > 0 ? $"Export {selected}…" : "Export…", exportable ? Palette.Text : Palette.Faint);
            text.FontWeight = FontWeight.Medium;
            text.Classes.Add("tnum");
            _out.Children.Add(new Chip(text, selected > 0 ? $"Export the {selected} selected photos to a folder" : "Export this photo",
                                       OpenExport, exportable) {
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

    /// `showingExport = true`: the sheets slice's export panel, by its registered name.
    void OpenExport() {
        if (Panels.Find("sheet-export") is not { } sheet || TopLevel.GetTopLevel(this) is not Window owner) return;
        new Window { Content = Panels.Create(sheet.Type), Background = Palette.Panel, SizeToContent = SizeToContent.WidthAndHeight,
                     Title = "Export" }.ShowDialog(owner);
    }
}

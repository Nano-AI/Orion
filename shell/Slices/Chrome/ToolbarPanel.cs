using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Layout;
using Avalonia.Media;
using OrionShell.CanvasSlice;
using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Sheets;

namespace OrionShell.Chrome;

/// Main.dc.html's toolbar, from OrionApp+Chrome.swift's `toolbar`: what is
/// open and the Batch menu on the left, the Library/Develop switch in the middle,
/// and what you do to it on the right: Export, the one filled button, then
/// Feedback. Rebuilt on every open; a render changes nothing here.
[Panel("toolbar", Region.Toolbar)]
public sealed class ToolbarPanel : Grid {
    const double ButtonHeight = 28;
    const double MarkSize = 20;

    readonly DockPanel _left = new() { VerticalAlignment = VerticalAlignment.Center };
    readonly TextBlock _crumb = new() { TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center };
    readonly Segmented _mode = new(["Library", "Develop"], 1, ["square.grid.2x2", "slider.horizontal.3"], words: true);
    readonly StackPanel _actions = new() { Orientation = Orientation.Horizontal, Spacing = Space.Md, HorizontalAlignment = HorizontalAlignment.Right,
                                           VerticalAlignment = VerticalAlignment.Center };

    public ToolbarPanel() {
        ColumnDefinitions = new("*,Auto,*");
        ColumnSpacing = Space.Pad;
        Margin = new Thickness(Space.Pad, 0);
        Height = Layout.Toolbar;

        // The brand's toolbar mark at 20 points, the size its file is drawn for (the
        // 16 point one is hinted for 16 and would blur here), in its own ink (#407).
        var wordmark = new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = Space.Sm, VerticalAlignment = VerticalAlignment.Center,
            Children = {
                new Artwork("toolbar-mark.svg", MarkSize, Palette.MarkInk) { VerticalAlignment = VerticalAlignment.Center },
                new TextBlock { Text = "Orion", FontSize = Faces.Body, FontWeight = FontWeight.SemiBold, LetterSpacing = -0.14,
                                VerticalAlignment = VerticalAlignment.Center },
            },
        };
        foreach (var c in new Control[] { wordmark, OpenMenu(), BatchMenu() }) {
            c.Margin = new Thickness(0, 0, Space.Pad, 0);
            DockPanel.SetDock(c, Dock.Left);
            _left.Children.Add(c);
        }
        _left.Children.Add(_crumb);
        _mode.VerticalAlignment = VerticalAlignment.Center;
        _mode.Changed += i => { if ((i == 0) != GalleryMode.On) Commands.Run("gallery"); };
        SetColumn(_mode, 1);
        SetColumn(_actions, 2);
        Children.AddRange([_left, _mode, _actions]);
        SizeChanged += (_, e) => { if (_needs == 0 || (e.NewSize.Width < _needs) != _compact) Refresh(); };

        EngineModel.Shared.Opened += Refresh;
        EngineModel.Shared.PropertyChanged += (_, _) => RefreshIfChanged();
        CanvasState.Changed += RefreshIfChanged;   // compare on, off, swapped
        EditHistory.Changed += RefreshIfChanged;
        GalleryMode.Changed += RefreshIfChanged;
        ExportRun.Changed += RefreshIfChanged;
        LibraryModel.Shared.Changed += RefreshIfChanged;
        LibraryModel.Shared.SelectionChanged += RefreshIfChanged;   // "Export 12"
        Refresh();
    }

    /// Everything the buttons show, so a pan or a render does not rebuild them.
    static string State => string.Join("|", EngineModel.Shared.Handle, EngineModel.Shared.Current, CompareState.Comparing,
        CompareState.Vertical, EditHistory.UndoLabel, EditHistory.RedoLabel, GalleryMode.On, ExportRun.Progress, ChromeStubs.ExportSelected,
        string.Join(",", Commands.All.Select(c => c.CanRun() ? 1 : 0)));
    string _shown = "";
    /// The width every word on the toolbar needs, measured on each refresh once it is laid out.
    double _needs;
    bool _compact;

    void RefreshIfChanged() { if (State != _shown) Refresh(); }

    /// Open, with the choice of a photo or a folder under it.
    static Control OpenMenu() {
        var open = new StateButton("folder", "Open", () => { }, help: "Open a photo or a folder") { Height = ButtonHeight };
        open.Flyout = new MenuFlyout { Placement = PlacementMode.BottomEdgeAlignedLeft, Items = {
            Item("Open photo…", () => Commands.Run("open-photo")),
            Item("Open folder…", () => Commands.Run("open-folder")),
            new Separator(),
            Item("Check for updates", () => Commands.Run("check-for-updates")),
        } };
        return open;
    }

    /// The Presets tab's copy, paste and sync, and select and export all, where a first look finds them.
    static Control BatchMenu() {
        var batch = new StateButton("square.stack", "Batch", () => { }, help: "Put one photo's settings on many, or export many photos at once") { Height = ButtonHeight };
        var menu = new MenuFlyout { Placement = PlacementMode.BottomEdgeAlignedLeft, Items = {
            Command("copy-settings"), Command("paste-settings"), Command("sync-settings"),
            new Separator(),
            Command("select-all"), Command("export-all"),
        } };
        menu.Opening += (_, _) => {
            foreach (var item in menu.Items.OfType<MenuItem>()) {
                item.IsEnabled = Commands.CanRun((string)item.Tag!);
                // The theme greys a header it made from a string, not this panel of words: a greyed button's ink instead.
                foreach (var (words, i) in ((Panel)item.Header!).Children.OfType<TextBlock>().Select((t, i) => (t, i)))
                    words.Foreground = !item.IsEnabled ? Palette.Faint : i == 0 ? Palette.Text : Palette.Dim;
            }
        };
        batch.Flyout = menu;
        return batch;
    }

    void Refresh() {
        _shown = State;
        var engine = EngineModel.Shared;
        bool gallery = ChromeStubs.Culling;

        // The folder, then the photo; the gallery is the folder's, so it names only that.
        var crumb = new InlineCollection();
        if (engine.Current >= 0) {
            string path = engine.Photos[engine.Current];
            crumb.Add(new Run(Path.GetFileName(Path.GetDirectoryName(path)) ?? "") { Foreground = Palette.Dim });
            if (!gallery) crumb.AddRange([new Run("  /  ") { Foreground = Palette.Faint }, new Run(Path.GetFileName(path))]);
        }
        _crumb.Inlines = crumb;
        _mode.Selected = gallery ? 0 : 1;
        _mode.IsEnabled = Commands.CanRun("gallery");
        _compact = false;
        Actions();
        if (Bounds.Width > 0) Fit();
    }

    /// The switch stays centred while each side fits beside it in half the toolbar; a side that needs more
    /// takes it and moves the switch over, rather than run under it or off the window. Short of room for
    /// every word, Compare and Reset show their icons alone, still named by their tooltips.
    void Fit() {
        double left = Wants(_left) - _crumb.DesiredSize.Width, right = Wants(_actions);
        _needs = left + Wants(_mode) + right + 2 * ColumnSpacing;
        if (_needs > Bounds.Width) {
            _compact = true;
            Actions();
            right = Wants(_actions);
        }
        (ColumnDefinitions[0].MinWidth, ColumnDefinitions[2].MinWidth) = (left, right);
        // Measured unbounded here; the layout measures it again in its column, where the crumb trims.
        static double Wants(Control c) { c.Measure(Size.Infinity); c.InvalidateMeasure(); return c.DesiredSize.Width; }
    }

    /// Every button runs a command by name, and is greyed by that command's
    /// CanRun (Swift's `.disabled`), so the keys and the buttons cannot disagree.
    void Actions() {
        bool gallery = ChromeStubs.Culling;
        _actions.Children.Clear();
        if (gallery) {
            _actions.Children.Add(Button("trash", "Delete rejected…", () => Commands.Run("delete-rejected"), Commands.CanRun("delete-rejected"), ButtonKind.Quiet, "Move every rejected photo to the Trash"));
        } else {
            _actions.Children.Add(Group(
                Glyph("arrow.uturn.backward", Keyed(EditHistory.UndoLabel is { } u ? $"Undo {u}" : "Undo", "undo"), () => Commands.Run("undo"), Commands.CanRun("undo")),
                Glyph("arrow.uturn.forward", Keyed(EditHistory.RedoLabel is { } r ? $"Redo {r}" : "Redo", "redo"), () => Commands.Run("redo"), Commands.CanRun("redo"))));
            // History and geometry are different kinds of thing with look-alike curved arrows: a divider keeps them apart.
            _actions.Children.Add(new Border { Width = 1, Height = 16, Background = Palette.Line, Margin = new Thickness(Space.Gap, 0), VerticalAlignment = VerticalAlignment.Center });
            _actions.Children.Add(Group(Glyph("rotate.left", Keyed("Rotate left", "rotate-left"), () => Commands.Run("rotate-left"), Commands.CanRun("rotate-left")),
                                        Glyph("rotate.right", Keyed("Rotate right", "rotate-right"), () => Commands.Run("rotate-right"), Commands.CanRun("rotate-right"))));
            var compare = Shrinkable("rectangle.split.2x1", "Compare", () => Commands.Run("compare"), Commands.CanRun("compare"), ButtonKind.Standard, "Split the view against the original (\\)");
            compare.On = CompareState.Comparing;
            _actions.Children.Add(compare);
            if (CompareState.Comparing)
                _actions.Children.Add(Group(Glyph(CompareState.Vertical ? "rectangle.split.2x1" : "rectangle.split.1x2",
                                                  "Swap between a vertical and horizontal split", () => Commands.Run("swap-split"), Commands.CanRun("swap-split"))));
            _actions.Children.Add(Shrinkable("arrow.counterclockwise", "Reset", () => Commands.Run("reset-adjustments"), Commands.CanRun("reset-adjustments"), ButtonKind.Quiet, "Put every adjustment back"));
        }

        // While a batch runs, its count and Stop take Export's place.
        if (ChromeStubs.BatchProgress is { } p) {
            _actions.Children.Add(new TextBlock { VerticalAlignment = VerticalAlignment.Center, Foreground = Palette.Dim,
                Inlines = new InlineCollection { new Run("Exporting "), Faces.Num($"{p.Done}"), new Run(" of "), Faces.Num($"{p.Total}") } });
            _actions.Children.Add(Button("xmark", "Stop", () => Commands.Run("stop-batch"), Commands.CanRun("stop-batch"), ButtonKind.Quiet, "Stop after the photo being written"));
        } else {
            int selected = ChromeStubs.ExportSelected;
            _actions.Children.Add(Button("square.and.arrow.up", selected > 0 ? $"Export {selected}" : "Export", () => Commands.Run("export"), Commands.CanRun("export"), ButtonKind.Primary,
                                         selected > 0 ? $"Export the {selected} selected photos to a folder" : Keyed("Export this photo", "export")));
        }
        _actions.Children.Add(Button(null, "Feedback", () => Commands.Run("feedback"), true, ButtonKind.Quiet, "Tell us how Orion is working for you (opens a form in your browser)"));
    }

    /// A tooltip that names the command's key, read from the command table: "Rotate left ([)", "Undo (⌘Z)".
    static string Keyed(string help, string command) => Commands.KeyLabel(command) is { } key ? $"{help} ({key})" : help;

    static StateButton Button(string? icon, string title, Action run, bool enabled, ButtonKind kind, string help) =>
        new(icon, title, run, kind, help) { Height = ButtonHeight, IsEnabled = enabled };

    StateButton Shrinkable(string icon, string title, Action run, bool enabled, ButtonKind kind, string help) {
        if (!_compact) return Button(icon, title, run, enabled, kind, help);
        var b = StateButton.Glyph(icon, help, run, kind);
        (b.Height, b.IsEnabled) = (ButtonHeight, enabled);
        return b;
    }

    static StateButton Glyph(string icon, string help, Action run, bool enabled) {
        var b = StateButton.Glyph(icon, help, run);
        (b.Height, b.CornerRadius, b.IsEnabled, b.Ink) = (ButtonHeight - 2, default, enabled, Palette.Text);
        return b;
    }

    /// Related icon buttons in one raised well, a hairline between them.
    static Border Group(params StateButton[] buttons) {
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        for (int i = 0; i < buttons.Length; i++) {
            if (i > 0) row.Children.Add(new Border { Width = 1, Background = Palette.Line });
            row.Children.Add(buttons[i]);
        }
        return new Border { Background = Palette.Raised, BorderBrush = Palette.Line, BorderThickness = new Thickness(1), ClipToBounds = true,
                            CornerRadius = new CornerRadius(Radius.Md), VerticalAlignment = VerticalAlignment.Center, Child = row };
    }

    static MenuItem Item(string header, Action action) {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => action();
        return item;
    }

    /// A command as a menu row: its title, and its key where it has one.
    static MenuItem Command(string name) {
        var row = new DockPanel { Children = { new TextBlock { Text = Commands.Find(name)!.Title, [DockPanel.DockProperty] = Dock.Left } } };
        if (Commands.KeyLabel(name) is { } key)
            row.Children.Add(new TextBlock { Text = key, TextAlignment = TextAlignment.Right, Margin = new Thickness(Space.Xl, 0, 0, 0) });
        var item = new MenuItem { Header = row, Tag = name };
        item.Click += (_, _) => Commands.Run(name);
        return item;
    }
}

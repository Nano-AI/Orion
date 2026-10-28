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
/// open on the left, the Library/Develop switch in the middle, and what you do
/// to it on the right, ending in Export, the one filled button. Rebuilt on
/// every open; a render changes nothing here.
[Panel("toolbar", Region.Toolbar)]
public sealed class ToolbarPanel : Grid {
    const double ButtonHeight = 28;

    readonly TextBlock _crumb = new() { TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center };
    readonly Segmented _mode = new(["Library", "Develop"], 1, ["square.grid.2x2", "slider.horizontal.3"], words: true);
    readonly StackPanel _actions = new() { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right,
                                           VerticalAlignment = VerticalAlignment.Center };

    public ToolbarPanel() {
        ColumnDefinitions = new("*,Auto,*");
        ColumnSpacing = 12;
        Margin = new Thickness(Space.Pad, 0);
        Height = Layout.Toolbar;

        var wordmark = new TextBlock { Text = "Orion", FontSize = 14, FontWeight = FontWeight.SemiBold, LetterSpacing = -0.14,
                                       VerticalAlignment = VerticalAlignment.Center };
        var left = new DockPanel { VerticalAlignment = VerticalAlignment.Center };
        foreach (var c in new Control[] { wordmark, OpenMenu() }) {
            c.Margin = new Thickness(0, 0, 12, 0);
            DockPanel.SetDock(c, Dock.Left);
            left.Children.Add(c);
        }
        left.Children.Add(_crumb);
        _mode.VerticalAlignment = VerticalAlignment.Center;
        _mode.Changed += i => { if ((i == 0) != GalleryMode.On) Commands.Run("gallery"); };
        SetColumn(_mode, 1);
        SetColumn(_actions, 2);
        Children.AddRange([left, _mode, _actions]);

        EngineModel.Shared.Opened += Refresh;
        EngineModel.Shared.PropertyChanged += (_, _) => RefreshIfChanged();
        CanvasState.Changed += RefreshIfChanged;   // compare on, off, swapped
        EditHistory.Changed += RefreshIfChanged;
        GalleryMode.Changed += RefreshIfChanged;
        ExportRun.Changed += RefreshIfChanged;
        LibraryModel.Shared.Changed += RefreshIfChanged;
        Refresh();
    }

    /// Everything the buttons show, so a pan or a render does not rebuild them.
    static string State => string.Join("|", EngineModel.Shared.Handle, EngineModel.Shared.Current, CompareState.Comparing,
        CompareState.Vertical, EditHistory.UndoLabel, EditHistory.RedoLabel, GalleryMode.On, ExportRun.Progress,
        string.Join(",", Commands.All.Select(c => c.CanRun() ? 1 : 0)));
    string _shown = "";

    void RefreshIfChanged() { if (State != _shown) Refresh(); }

    /// Open, with the choice of a photo or a folder under it.
    static Control OpenMenu() {
        var open = new StateButton("folder", "Open", () => { }, help: "Open a photo or a folder") { Height = ButtonHeight };
        open.Flyout = new MenuFlyout { Placement = PlacementMode.BottomEdgeAlignedLeft, Items = {
            Item("Open photo…", () => Commands.Run("open-photo")),
            Item("Open folder…", () => Commands.Run("open-folder")),
        } };
        return open;
    }

    /// Every button runs a command by name, and is greyed by that command's
    /// CanRun (Swift's `.disabled`), so the keys and the buttons cannot disagree.
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

        _actions.Children.Clear();
        if (gallery) {
            _actions.Children.Add(Button("trash", "Delete rejected…", () => Commands.Run("delete-rejected"), Commands.CanRun("delete-rejected"), ButtonKind.Quiet, "Move every rejected photo to the Trash"));
        } else {
            _actions.Children.Add(Group(
                Glyph("arrow.uturn.backward", Keyed(EditHistory.UndoLabel is { } u ? $"Undo {u}" : "Undo", "undo"), () => Commands.Run("undo"), Commands.CanRun("undo")),
                Glyph("arrow.uturn.forward", Keyed(EditHistory.RedoLabel is { } r ? $"Redo {r}" : "Redo", "redo"), () => Commands.Run("redo"), Commands.CanRun("redo"))));
            // History and geometry are different kinds of thing with look-alike curved arrows: a divider keeps them apart.
            _actions.Children.Add(new Border { Width = 1, Height = 16, Background = Palette.Line, Margin = new Thickness(4, 0), VerticalAlignment = VerticalAlignment.Center });
            _actions.Children.Add(Group(Glyph("rotate.left", Keyed("Rotate left", "rotate-left"), () => Commands.Run("rotate-left"), Commands.CanRun("rotate-left")),
                                        Glyph("rotate.right", Keyed("Rotate right", "rotate-right"), () => Commands.Run("rotate-right"), Commands.CanRun("rotate-right"))));
            var compare = Button("rectangle.split.2x1", "Compare", () => Commands.Run("compare"), Commands.CanRun("compare"), ButtonKind.Standard, "Split the view against the original (\\)");
            compare.On = CompareState.Comparing;
            _actions.Children.Add(compare);
            if (CompareState.Comparing)
                _actions.Children.Add(Group(Glyph(CompareState.Vertical ? "rectangle.split.2x1" : "rectangle.split.1x2",
                                                  "Swap between a vertical and horizontal split", () => Commands.Run("swap-split"), Commands.CanRun("swap-split"))));
            _actions.Children.Add(Button("arrow.counterclockwise", "Reset", () => Commands.Run("reset-adjustments"), Commands.CanRun("reset-adjustments"), ButtonKind.Quiet, "Put every adjustment back"));
        }

        // While a batch runs, its count and Stop take Export's place.
        if (ChromeStubs.BatchProgress is { } p) {
            _actions.Children.Add(new TextBlock { VerticalAlignment = VerticalAlignment.Center, Foreground = Palette.Dim,
                Inlines = new InlineCollection { new Run("Exporting "), Faces.Num($"{p.Done}"), new Run(" of "), Faces.Num($"{p.Total}") } });
            _actions.Children.Add(Button("xmark", "Stop", () => Commands.Run("stop-batch"), Commands.CanRun("stop-batch"), ButtonKind.Quiet, "Stop after the photo being written"));
        } else {
            int selected = ChromeStubs.ExportSelected;
            _actions.Children.Add(Button("square.and.arrow.up", selected > 0 ? $"Export {selected}" : "Export", () => Commands.Run("export"), Commands.CanRun("export"), ButtonKind.Primary,
                                         selected > 0 ? $"Export the {selected} selected photos to a folder" : "Export this photo (⌘E)"));
        }
    }

    /// A tooltip that names the command's key, read from the command table: "Rotate left ([)", "Undo (⌘Z)".
    static string Keyed(string help, string command) {
        if (Commands.Find(command)?.Keys.FirstOrDefault() is not { } key) return help;
        string text = key.ToString("p", null);
        if (OperatingSystem.IsMacOS()) text = text.Replace("Shift+", "⇧").Replace("Alt+", "⌥").Replace("Ctrl+", "⌃").Replace("Cmd+", "⌘");
        return $"{help} ({text})";
    }

    static StateButton Button(string? icon, string title, Action run, bool enabled, ButtonKind kind, string help) =>
        new(icon, title, run, kind, help) { Height = ButtonHeight, IsEnabled = enabled };

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
}

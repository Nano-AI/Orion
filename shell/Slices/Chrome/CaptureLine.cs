using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Chrome;

/// A port of the Swift app's OrionApp+Capture.swift, moved under the histogram as
/// Main.dc.html draws it, and read as a caption: the camera and the lens,
/// then the exposure as a viewfinder prints it, in numerals (1/640 s  f/4
/// ISO 800  24 mm). One button that opens the full list. Always its full
/// height, empty with nothing open, so the tools under it never move.
public sealed class CaptureLine : Border {
    /// Two lines and the button's room around them.
    public const double LineHeight = 42;

    readonly TextBlock _camera = Words(Palette.Label), _lens = Words(Palette.Dim);
    readonly StackPanel _figures = new() { Orientation = Orientation.Horizontal, Spacing = Space.Md };
    readonly StateButton _button;

    public CaptureLine() {
        Height = LineHeight;
        _camera.MaxWidth = 150;
        var gear = new Grid { ColumnDefinitions = new("Auto,*"), ColumnSpacing = Space.Md, Children = { _camera, _lens } };
        Grid.SetColumn(_lens, 1);
        // The button's padding hangs into the column's margin, so the words line up with the plate's edge.
        _button = new StateButton { Kind = ButtonKind.Bare, Height = LineHeight, Padding = new Thickness(Space.Sm, Space.Gap), Margin = new Thickness(-Space.Sm, 0),
                                    HorizontalAlignment = HorizontalAlignment.Left,
                                    Content = new StackPanel { Spacing = Space.Xs, Children = { gear, _figures } } };
        _button.Help("Capture settings");
    }

    static TextBlock Words(IBrush ink) => new() {
        FontSize = Faces.Label, Foreground = ink, Tag = StateButton.Keep, TextTrimming = TextTrimming.CharacterEllipsis,
        VerticalAlignment = VerticalAlignment.Center,
    };

    /// Re-read on every open. A file that records none of it draws nothing.
    public void Show(CaptureInfo? capture, string? path) {
        _figures.Children.Clear();
        Child = null;
        if (capture == null || capture.Viewfinder.Count == 0 && capture.Camera.Length == 0 && capture.Lens.Length == 0) return;
        _camera.Text = capture.Camera;
        _lens.Text = capture.Lens;
        ToolTip.SetTip(_lens, capture.Lens.Length > 0 ? capture.Lens : null);   // the whole name when it is cut short
        foreach (var text in capture.Viewfinder) {
            var figure = Faces.Figure(text, Faces.Body);
            figure.Tag = StateButton.Keep;
            _figures.Children.Add(figure);
        }
        // Built when somebody asks: a stat per open is a cost paid for a popover nobody opened.
        var flyout = new Flyout { Placement = PlacementMode.BottomEdgeAlignedLeft };
        flyout.Opening += (_, _) => flyout.Content ??= CapturePopover.Build(capture, path);
        _button.Flyout = flyout;
        Child = _button;
    }
}

/// CapturePopover: every field the file records, label beside value, the file last.
static class CapturePopover {
    public static Control Build(CaptureInfo capture, string? path) {
        var rows = capture.Rows.ToList();
        if (path != null) {
            rows.Add(("File", Path.GetFileName(path)));
            if (FileSize(path) is { } size) rows.Add(("Size", size));
        }
        return Grid(rows, Space.Pad);
    }

    /// Label beside value: words dim, values in numerals (Library.dc.html's info list).
    public static Grid Grid(IReadOnlyList<(string Label, string Value)> rows, double margin = 0) {
        var grid = new Grid { ColumnDefinitions = new("Auto,*"), ColumnSpacing = Space.Pad, RowSpacing = Space.Md,
                              Margin = new Thickness(margin), MinWidth = 220 };
        for (int i = 0; i < rows.Count; i++) {
            grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            var label = new TextBlock { Text = rows[i].Label, Foreground = Palette.Dim };
            var value = new SelectableTextBlock { Text = rows[i].Value, FontFamily = Faces.Numeral, FontWeight = FontWeight.Medium,
                                                  FontFeatures = Faces.Tabular, TextWrapping = TextWrapping.Wrap };
            Avalonia.Controls.Grid.SetRow(label, i);
            Avalonia.Controls.Grid.SetRow(value, i);
            Avalonia.Controls.Grid.SetColumn(value, 1);
            grid.Children.AddRange([label, value]);
        }
        return grid;
    }

    /// Through any symlink, as ByteCountFormatter's .file style spells it.
    static string? FileSize(string path) {
        try {
            var info = new FileInfo(path);
            if (info.ResolveLinkTarget(returnFinalTarget: true) is FileInfo target) info = target;
            double b = info.Length;
            return b >= 1e9 ? $"{b / 1e9:0.##} GB" : b >= 1e6 ? $"{b / 1e6:0.#} MB" : b >= 1e3 ? $"{b / 1e3:0} KB" : $"{b} bytes";
        } catch (IOException) {
            return null;
        }
    }
}

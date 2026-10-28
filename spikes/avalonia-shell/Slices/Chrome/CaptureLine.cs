using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Chrome;

/// A port of app/OrionApp+Capture.swift, moved under the histogram as
/// Main.dc.html draws it: the exposure as a viewfinder prints it, in numerals,
/// that opens the full list. With nothing open it is empty, and the clip
/// buttons either side stay where they are.
public sealed class CaptureLine : Border {
    readonly StackPanel _figures = new() { Orientation = Orientation.Horizontal, Spacing = 12 };
    readonly StateButton _button;

    public CaptureLine() {
        _button = new StateButton { Kind = ButtonKind.Bare, Content = _figures, Padding = new Thickness(6, 0), Height = 24, Ink = Palette.Text };
        _button.Help("Capture settings");
        VerticalAlignment = VerticalAlignment.Center;
        HorizontalAlignment = HorizontalAlignment.Center;
    }

    /// Re-read on every open. A file that records none of it draws nothing.
    public void Show(CaptureInfo? capture, string? path) {
        _figures.Children.Clear();
        Child = null;
        if (capture == null || capture.Viewfinder.Count == 0) return;
        foreach (var text in capture.Viewfinder) _figures.Children.Add(Faces.Figure(text, Faces.Body));
        // Built when somebody asks: a stat per open is a cost paid for a popover nobody opened.
        var flyout = new Flyout { Placement = PlacementMode.Bottom };
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
        var grid = new Grid { ColumnDefinitions = new("Auto,*"), ColumnSpacing = Space.Pad, RowSpacing = 8,
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

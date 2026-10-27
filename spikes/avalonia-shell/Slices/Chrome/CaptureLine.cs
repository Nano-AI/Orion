using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell;

/// A port of app/OrionApp+Capture.swift: the toolbar's middle slot, one dim
/// line of what the camera recorded, that opens the full list. With nothing
/// open it is the same frame with nothing in it, so the controls either side
/// stay where they are.
public sealed class CaptureLine : Border {
    readonly FirstThatFits _fits = new() { HorizontalAlignment = HorizontalAlignment.Center };
    readonly Button _button;
    bool _hovering, _showing;

    public CaptureLine() {
        _button = new Chip(_fits, "Capture settings", () => { }) {
            BorderThickness = new Thickness(0), Padding = new Thickness(0), HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        _button.PointerEntered += (_, _) => { _hovering = true; Ink(); };
        _button.PointerExited += (_, _) => { _hovering = false; Ink(); };
        VerticalAlignment = VerticalAlignment.Center;
    }

    /// Re-read on every open. A file that records none of it draws the empty frame.
    public void Show(CaptureInfo? capture, string? path) {
        _fits.Children.Clear();
        Child = null;
        if (capture is not { Line.Length: > 0 }) return;
        // Widest first: camera and all, then the exposure with its focal
        // length, then the three numbers that set it; the last truncates.
        foreach (var text in new[] { capture.Line, capture.CompactLine, capture.ExposureLine })
            _fits.Children.Add(new TextBlock {
                Text = text, FontSize = TypeSize.Base, Classes = { "tnum" }, TextTrimming = TextTrimming.CharacterEllipsis,
            });
        // Built when somebody asks: a stat per open is a cost paid for a popover nobody opened.
        var flyout = new Flyout { Placement = PlacementMode.Bottom };
        flyout.Opening += (_, _) => flyout.Content ??= CapturePopover.Build(capture, path);
        flyout.Opened += (_, _) => { _showing = true; Ink(); };
        flyout.Closed += (_, _) => { _showing = false; Ink(); };
        _button.Flyout = flyout;
        Child = _button;
        Ink();
    }

    void Ink() {
        foreach (var t in _fits.Children.OfType<TextBlock>()) t.Foreground = _hovering || _showing ? Palette.Text : Palette.Dim;
    }
}

/// SwiftUI's ViewThatFits(in: .horizontal): the first child whose natural
/// width fits, else the last, squeezed to the space it has.
public sealed class FirstThatFits : Panel {
    Control? _chosen;

    protected override Size MeasureOverride(Size available) {
        _chosen = null;
        foreach (var child in Children) {
            child.Measure(new Size(double.PositiveInfinity, available.Height));
            if (child.DesiredSize.Width <= available.Width) { _chosen = child; break; }
        }
        if (_chosen == null && Children.Count > 0) {
            _chosen = Children[^1];
            _chosen.Measure(available);
        }
        return _chosen?.DesiredSize ?? default;
    }

    protected override Size ArrangeOverride(Size final) {
        foreach (var child in Children) child.Arrange(child == _chosen ? new Rect(final) : default);
        return final;
    }
}

/// CapturePopover: every field the file records, label beside value, the file last.
static class CapturePopover {
    public static Control Build(CaptureInfo capture, string? path) {
        var rows = capture.Rows.ToList();
        if (path != null) rows.Add(("File", Path.GetFileName(path) + FileSize(path)));
        var grid = new Grid { ColumnDefinitions = new("Auto,Auto"), ColumnSpacing = Space.Pad, RowSpacing = Space.Sm,
                              Margin = new Thickness(Space.Pad), MinWidth = 240 };
        for (int i = 0; i < rows.Count; i++) {
            grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            var label = new TextBlock { Text = rows[i].Item1, Foreground = Palette.Dim, HorizontalAlignment = HorizontalAlignment.Right };
            var value = new SelectableTextBlock { Text = rows[i].Item2, Foreground = Palette.Text };
            foreach (var t in new TextBlock[] { label, value }) { t.FontSize = TypeSize.Base; t.Classes.Add("tnum"); }
            Grid.SetRow(label, i);
            Grid.SetRow(value, i);
            Grid.SetColumn(value, 1);
            grid.Children.Add(label);
            grid.Children.Add(value);
        }
        return grid;
    }

    /// Through any symlink, as ByteCountFormatter's .file style spells it.
    static string FileSize(string path) {
        try {
            var info = new FileInfo(path);
            if (info.ResolveLinkTarget(returnFinalTarget: true) is FileInfo target) info = target;
            double b = info.Length;
            string size = b >= 1e9 ? $"{b / 1e9:0.##} GB" : b >= 1e6 ? $"{b / 1e6:0.#} MB" : b >= 1e3 ? $"{b / 1e3:0} KB" : $"{b} bytes";
            return CaptureInfo.Separator + size;
        } catch (IOException) {
            return "";
        }
    }
}

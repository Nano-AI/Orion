using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.LogicalTree;
using Avalonia.Media;

namespace OrionShell;

/// A port of app/Engraved.swift: the type that names controls the way a camera
/// engraves its dials, and the nameplate that heads a section.
public static class Engraved {
    /// Upper case, tracked, medium weight, never under 10 pt.
    public static TextBlock Label(string text, IBrush? color = null, double size = 10, double tracking = 1.0) => new() {
        Text = text.ToUpperInvariant(), FontSize = Math.Max(size, 10), FontWeight = FontWeight.Medium,
        LetterSpacing = tracking, Foreground = color ?? Palette.Dim, VerticalAlignment = VerticalAlignment.Center,
    };

    /// A number: monospaced digits.
    public static TextBlock Readout(string text, IBrush? color = null, double size = 10) => new() {
        Text = text, FontSize = size, FontWeight = FontWeight.Medium, Foreground = color ?? Palette.Text,
        FontFamily = new FontFamily("Menlo, Consolas, monospace"),
    };

    /// The info mark at a nameplate's right edge; the text shows on hover.
    public static Control Info(string text) {
        var icon = Icons.Create("info.circle", 10, Palette.Faint);
        var host = new Border { Width = 14, Height = 14, Background = Brushes.Transparent, Child = icon };
        ToolTip.SetTip(host, new TextBlock { Text = text, MaxWidth = 250, TextWrapping = TextWrapping.Wrap, FontSize = 11 });
        host.PointerEntered += (_, _) => icon.Stroke = Palette.Text;
        host.PointerExited += (_, _) => icon.Stroke = Palette.Faint;
        return host;
    }
}

/// Engraved.Plate + SectionPlate: the title, a star-coloured dot when anything
/// inside is off its default, a hairline, the info mark; then the rows.
/// 9 between the plate and its rows, 11 between rows (Engraved.swift).
public sealed class SectionPlate : StackPanel {
    readonly Ellipse _dot = new() { Width = 3, Height = 3, Fill = Palette.Star, Opacity = 0, VerticalAlignment = VerticalAlignment.Center };
    public StackPanel Rows { get; } = new() { Spacing = 11 };

    public SectionPlate(string title, string? info = null, params Control[] rows) {
        Spacing = 9;
        var plate = new DockPanel();
        var name = Engraved.Label(title, Palette.Text);
        DockPanel.SetDock(name, Dock.Left);
        _dot.Margin = new Thickness(8, 0);
        DockPanel.SetDock(_dot, Dock.Left);
        plate.Children.Add(name);
        plate.Children.Add(_dot);
        if (info != null) {
            var mark = Engraved.Info(info);
            mark.Margin = new Thickness(8, 0, 0, 0);
            DockPanel.SetDock(mark, Dock.Right);
            plate.Children.Add(mark);
        }
        plate.Children.Add(new Border { Height = 1, Background = Palette.Line, VerticalAlignment = VerticalAlignment.Center });
        Children.Add(plate);
        Children.Add(Rows);
        Rows.Children.AddRange(rows);
        AddHandler(AdjustmentSlider.ModifiedChangedEvent, (_, _) => Refresh());
        Refresh();
    }

    void Refresh() => _dot.Opacity = this.GetLogicalDescendants().OfType<AdjustmentSlider>().Any(s => s.Modified) ? 1 : 0;
}

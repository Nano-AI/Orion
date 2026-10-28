using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.LogicalTree;
using Avalonia.Media;

namespace OrionShell;

/// A port of app/Engraved.swift: the type that names controls, and the
/// nameplate that heads a section. Redrawn for DESIGN.md: sentence case in the
/// platform face (text is shown as given, never upper-cased), numbers in the
/// numeral face, nothing under 11 px.
public static class Engraved {
    /// A control's name. `tracking` stays for old callers; DESIGN.md drops it with the caps.
    public static TextBlock Label(string text, IBrush? color = null, double size = Faces.Label, double tracking = 0) => new() {
        Text = text, FontSize = Math.Max(size, Faces.Mark), LetterSpacing = tracking,
        Foreground = color ?? Palette.Label, VerticalAlignment = VerticalAlignment.Center,
    };

    /// A number: Faces.Figure, the numeral face with tabular figures.
    public static TextBlock Readout(string text, IBrush? color = null, double size = Faces.Mark) => Faces.Figure(text, size, color);

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
/// 14 between the plate and its rows and between rows (Main.dc.html).
public sealed class SectionPlate : StackPanel {
    readonly Ellipse _dot = new() { Width = 5, Height = 5, Fill = Palette.Star, Opacity = 0, VerticalAlignment = VerticalAlignment.Center };
    public StackPanel Rows { get; } = new() { Spacing = 14 };

    public SectionPlate(string title, string? info = null, params Control[] rows) {
        Spacing = 14;
        var plate = new DockPanel { MinHeight = 22 };
        var name = Engraved.Label(title, Palette.Text, Faces.Body);
        name.FontWeight = FontWeight.SemiBold;
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

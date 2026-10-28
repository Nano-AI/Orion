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

    /// A sentence under a control, as Main.dc.html sets its notes: label size, dim, room between lines.
    public static TextBlock Note(string text, IBrush? color = null) => new() {
        Text = text, FontSize = Faces.Label, LineHeight = 18, Foreground = color ?? Palette.Dim, TextWrapping = TextWrapping.Wrap,
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
/// inside is off its default, a hairline, the info mark, the section's one
/// action (With); then the rows. 14 between the plate and its rows and between
/// rows, `Gap` between sections (Main.dc.html).
public sealed class SectionPlate : StackPanel {
    /// Between one section and the next in a tool tab.
    public const double Gap = 24;

    readonly Ellipse _dot = new() { Width = 5, Height = 5, Fill = Palette.Star, Opacity = 0, VerticalAlignment = VerticalAlignment.Center };
    readonly DockPanel _plate = new() { MinHeight = 22 };
    public StackPanel Rows { get; } = new() { Spacing = 14 };

    public SectionPlate(string title, string? info = null, params Control[] rows) {
        Spacing = 14;
        var plate = _plate;
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

    /// The section's action after the hairline (Light's Auto), 24 high to keep the target size.
    public SectionPlate With(Control action) {
        action.Margin = new Thickness(8, 0, 0, 0);
        action.Height = 24;
        DockPanel.SetDock(action, Dock.Right);
        _plate.Children.Insert(_plate.Children.Count - 1, action);   // before the hairline, which fills
        return this;
    }

    void Refresh() => _dot.Opacity = this.GetLogicalDescendants().OfType<AdjustmentSlider>().Any(s => s.Modified) ? 1 : 0;
}

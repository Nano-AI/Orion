using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Media.Transformation;

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

    /// A section's icon, beside its name, as Capture One heads its tools (#350). A title missing here has none.
    static readonly Dictionary<string, string> IconFor = new() {
        ["White balance"] = "thermometer", ["Light"] = "sun.max", ["Highlight recovery"] = "sun.horizon", ["Shadow lift"] = "moon",
        ["Curve"] = "curve", ["Presence"] = "drop", ["Clarity"] = "circle.lefthalf.filled", ["Dehaze"] = "sun.haze",
        ["Color mixer"] = "circle.3", ["Color grading"] = "dial", ["Look"] = "wand.and.stars",
        ["Sharpening"] = "triangle", ["Noise reduction"] = "waveform", ["Grain"] = "grain", ["Vignette"] = "vignette",
        ["Lens"] = "camera.aperture", ["Perspective"] = "perspective", ["Capture"] = "camera",
        ["Masks"] = "theatermasks.fill", ["Spots"] = "bandage", ["Aspect"] = "aspectratio", ["Rotate"] = "rotate.right",
    };

    public SectionPlate(string title, string? info = null, params Control[] rows) {
        Spacing = 14;
        var plate = _plate;
        var name = Engraved.Label(title, Palette.Text, Faces.Body);
        name.FontWeight = FontWeight.SemiBold;
        if (IconFor.TryGetValue(title, out var icon)) {
            var glyph = Icons.Create(icon, 14, Palette.Dim);
            glyph.Margin = new Thickness(0, 0, 7, 0);
            glyph.VerticalAlignment = VerticalAlignment.Center;
            DockPanel.SetDock(glyph, Dock.Left);
            plate.Children.Add(glyph);
        }
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
        AddHandler(AdjustmentSlider.ModifiedChangedEvent, (_, e) => Refresh(animate: e is not ModifiedChangedEventArgs { Opened: true }));
        Refresh(animate: false);
    }

    /// The dot arrives from half size as it fades in, and leaves faster than it came
    /// (DESIGN.md, Motion): a section has just been edited, or just reset. A photo
    /// opening sets it at once; under reduced motion it only fades.
    void Refresh(bool animate) {
        bool on = this.GetLogicalDescendants().OfType<AdjustmentSlider>().Any(s => s.Modified);
        var time = on ? Motion.Enter : Motion.Exit;
        _dot.Transitions = !animate ? null : [
            new DoubleTransition { Property = OpacityProperty, Duration = time, Easing = Motion.EaseOut },
            new TransformOperationsTransition { Property = RenderTransformProperty, Duration = time, Easing = Motion.EaseOut },
        ];
        _dot.Opacity = on ? 1 : 0;
        _dot.RenderTransform = on || Motion.Reduced ? Whole : Half;
    }

    static readonly ITransform Whole = TransformOperations.Parse("scale(1)"), Half = TransformOperations.Parse("scale(0.5)");

    /// The section's action after the hairline (Light's Auto), 24 high to keep the target size.
    public SectionPlate With(Control action) {
        action.Margin = new Thickness(8, 0, 0, 0);
        action.Height = 24;
        DockPanel.SetDock(action, Dock.Right);
        _plate.Children.Insert(_plate.Children.Count - 1, action);   // before the hairline, which fills
        return this;
    }
}

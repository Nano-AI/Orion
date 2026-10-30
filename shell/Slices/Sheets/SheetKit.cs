using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;

namespace OrionShell.Sheets;

/// The pieces the sheets and the Presets tab share: the `row` helper the
/// Export and Watermark sheets both declare, small bordered buttons, rounded
/// text fields and notes. The segmented picker is the shared Segmented.
static class SheetKit {
    /// Fluent's controls at AppKit's small sizes and in Orion's colours, for
    /// one panel's subtree: a white slider knob on an accent track (`.tint`),
    /// and the inactive-window grey of a checked mini checkbox.
    public static void Restyle(StyledElement root) {
        root.Resources["SliderHorizontalHeight"] = 18.0;
        root.Resources["SliderPreContentMargin"] = new GridLength(0);
        root.Resources["SliderPostContentMargin"] = new GridLength(0);
        root.Resources["SliderHorizontalThumbWidth"] = 15.0;
        root.Resources["SliderHorizontalThumbHeight"] = 15.0;
        root.Resources["CheckBoxMinHeight"] = 14.0;
        void Add(Func<Selector?, Selector> selector, params (AvaloniaProperty, object)[] setters) {
            var style = new Style(selector);
            foreach (var (p, v) in setters) style.Setters.Add(new Setter(p, v));
            root.Styles.Add(style);
        }
        // A selector with an activator, :not(.fluent), sets at trigger
        // priority: above the values a Fluent template sets on its own parts.
        static Selector Live<T>(Selector? x) where T : StyledElement => x.OfType<T>().Not(y => y.Class("fluent"));
        Add(x => Live<Slider>(x).Template().OfType<Thumb>(), (TemplatedControl.BackgroundProperty, Palette.Text));
        Add(x => Live<CheckBox>(x), (TemplatedControl.PaddingProperty, new Thickness(Space.Xs, 0, 0, 0)));
        Add(x => Live<CheckBox>(x).Template().OfType<Border>().Name("NormalRectangle"),
            (Layoutable.WidthProperty, 12.0), (Layoutable.HeightProperty, 12.0),
            (Border.CornerRadiusProperty, new CornerRadius(Radius.Sm)),
            (Border.BackgroundProperty, Palette.Raised), (Border.BorderBrushProperty, Palette.Faint));
        Add(x => Live<CheckBox>(x).Template().OfType<Viewbox>(), (Layoutable.WidthProperty, 11.0), (Layoutable.HeightProperty, 11.0));
        Add(x => x.OfType<CheckBox>().Class(":checked").Template().OfType<Border>().Name("NormalRectangle"),
            (Border.BackgroundProperty, Palette.Line), (Border.BorderBrushProperty, Palette.Line));
        Add(x => x.OfType<CheckBox>().Class(":checked").Template().OfType<Avalonia.Controls.Shapes.Path>().Name("CheckGlyph"),
            (Avalonia.Controls.Shapes.Shape.FillProperty, Palette.Text));
    }

    /// ExportPanel/WatermarkPanel `row`: a control's name over it, sentence case (DESIGN.md: no caps labels).
    public static StackPanel Row(string label, params Control[] content) {
        var row = new StackPanel { Spacing = Space.Sm };
        row.Children.Add(Engraved.Label(label));
        row.Children.AddRange(content);
        return row;
    }

    /// A sheet's title: the words face, a step above the panel's section titles.
    public static TextBlock Title(string text) => new() { Text = text, FontSize = Faces.Title, FontWeight = FontWeight.SemiBold };

    /// A wrapping line of small print.
    public static TextBlock Note(string text, IBrush? color = null) => Engraved.Note(text, color ?? Palette.Dim);

    /// The standard button; `prominent` is the default action, drawn as the one
    /// primary button (`.keyboardShortcut(.defaultAction)`, `.borderedProminent`).
    public static Button Button(string text, Action? click = null, bool prominent = false) =>
        new StateButton(null, text, () => click?.Invoke(), prominent ? ButtonKind.Primary : ButtonKind.Standard) {
            IsDefault = prominent, VerticalAlignment = VerticalAlignment.Center,
        };

    /// `.textFieldStyle(.roundedBorder)`.
    public static TextBox Field(string placeholder) => new() {
        PlaceholderText = placeholder, FontSize = Faces.Label, MinHeight = 0,
        Background = Palette.Panel, BorderBrush = Palette.Faint,
        Padding = new Thickness(Space.Sm, Space.Gap), CornerRadius = new CornerRadius(Radius.Md),
        VerticalContentAlignment = VerticalAlignment.Center,
    };

    /// A text field with its Save button on the right, as both tabs draw it.
    public static DockPanel FieldWithButton(TextBox field, Button button) {
        var row = new DockPanel();
        button.Margin = new Thickness(Space.Sm, 0, 0, 0);
        DockPanel.SetDock(button, Dock.Right);
        row.Children.Add(button);
        row.Children.Add(field);
        return row;
    }

    /// The `minus.circle` / `pencil` glyph buttons beside a row.
    public static Button Glyph(string symbol, string help, Action click) {
        var b = StateButton.Glyph(symbol, help, click, size: IconSize.Md);
        (b.Width, b.Height, b.VerticalAlignment) = (24, 24, VerticalAlignment.Center);
        return b;
    }

    /// `.pickerStyle(.menu)`: a pop-up of titles.
    public static ComboBox Menu(IEnumerable<string> titles, int selected, Action<int> changed) {
        var box = new ComboBox {
            ItemsSource = titles.ToList(), SelectedIndex = selected, FontSize = Faces.Label,
            MinHeight = 0, Padding = new Thickness(Space.Md, Space.Gap), CornerRadius = new CornerRadius(Radius.Md),
            Background = Palette.Raised, BorderBrush = Palette.Line,
        };
        box.SelectionChanged += (_, _) => { if (box.SelectedIndex >= 0) changed(box.SelectedIndex); };
        return box;
    }
}

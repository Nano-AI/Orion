using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;

namespace OrionShell.Sheets;

/// The pieces the sheets and the Presets tab share: the `row` helper the
/// Export and Watermark sheets both declare, small bordered buttons, rounded
/// text fields, faint notes, and the segmented picker SwiftUI draws natively.
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
        Add(x => Live<CheckBox>(x), (TemplatedControl.PaddingProperty, new Thickness(2, 0, 0, 0)));
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

    /// ExportPanel/WatermarkPanel `row`: an upper-case 10 pt semibold label over its control.
    public static StackPanel Row(string label, params Control[] content) {
        var row = new StackPanel { Spacing = Space.Sm };
        row.Children.Add(new TextBlock {
            Text = label.ToUpperInvariant(), FontSize = TypeSize.Xs, FontWeight = FontWeight.SemiBold,
            LetterSpacing = 0.8, Foreground = Palette.Dim,
        });
        row.Children.AddRange(content);
        return row;
    }

    /// `.font(.system(size: 19, design: .serif))`: a sheet's title.
    public static TextBlock Title(string text) => new() {
        Text = text, FontSize = 19, Foreground = Palette.Text,
        FontFamily = new FontFamily("New York, Georgia, Times New Roman, serif"),
    };

    /// A wrapping line of small print.
    public static TextBlock Note(string text, IBrush? color = null, double size = TypeSize.Xs) => new() {
        Text = text, FontSize = size, Foreground = color ?? Palette.Faint, TextWrapping = TextWrapping.Wrap,
    };

    /// `.buttonStyle(.bordered).controlSize(.small)`. `prominent` is the
    /// default action (`.keyboardShortcut(.defaultAction)`, `.borderedProminent`).
    public static Button Button(string text, Action? click = null, bool prominent = false) {
        var b = new Button {
            Content = text, FontSize = TypeSize.Sm, Padding = new Thickness(Space.Sm + 2, Space.Xs),
            MinHeight = 0, CornerRadius = new CornerRadius(Radius.Md), VerticalAlignment = VerticalAlignment.Center,
            IsDefault = prominent,
        };
        if (prominent) { b.Background = Palette.Accent; b.Foreground = Palette.Ground; }
        if (click != null) b.Click += (_, _) => click();
        return b;
    }

    /// `.textFieldStyle(.roundedBorder)` at 11 pt.
    public static TextBox Field(string placeholder) => new() {
        PlaceholderText = placeholder, FontSize = TypeSize.Sm, MinHeight = 0,
        Background = Palette.Panel, BorderBrush = Palette.Faint,
        Padding = new Thickness(Space.Sm, 3), CornerRadius = new CornerRadius(Radius.Md),
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
        var b = new Button {
            Content = Icons.Create(symbol, TypeSize.Xs, Palette.Faint), Padding = new Thickness(Space.Xs),
            MinHeight = 0, MinWidth = 0, Background = Brushes.Transparent, BorderThickness = new Thickness(0),
            VerticalAlignment = VerticalAlignment.Center,
        };
        ToolTip.SetTip(b, help);
        b.Click += (_, _) => click();
        return b;
    }

    /// `.pickerStyle(.menu)`: a pop-up of titles.
    public static ComboBox Menu(IEnumerable<string> titles, int selected, Action<int> changed) {
        var box = new ComboBox {
            ItemsSource = titles.ToList(), SelectedIndex = selected, FontSize = TypeSize.Sm,
            MinHeight = 0, Padding = new Thickness(Space.Sm + 2, 3), CornerRadius = new CornerRadius(Radius.Md),
            Background = Palette.Raised, BorderBrush = Palette.Line,
        };
        box.SelectionChanged += (_, _) => { if (box.SelectedIndex >= 0) changed(box.SelectedIndex); };
        return box;
    }
}

/// `.pickerStyle(.segmented)`: equal cells in a well, the chosen one raised.
sealed class Segmented : Border {
    readonly List<Border> _cells = new();
    public int Selected { get; private set; }
    public event Action<int>? Changed;

    public Segmented(IReadOnlyList<string> titles, int selected) {
        Background = Palette.Raised;
        BorderBrush = Palette.Line;
        BorderThickness = new Thickness(0.5);
        CornerRadius = new CornerRadius(Radius.Md);
        Padding = new Thickness(1);
        var grid = new UniformGrid { Rows = 1 };
        for (int i = 0; i < titles.Count; i++) {
            int index = i;
            var cell = new Border {
                CornerRadius = new CornerRadius(Radius.Md - 1), Padding = new Thickness(0, 3),
                Background = Brushes.Transparent,
                Child = new TextBlock { Text = titles[i], FontSize = TypeSize.Sm, HorizontalAlignment = HorizontalAlignment.Center },
            };
            cell.PointerPressed += (_, _) => { if (index != Selected) { Paint(index); Changed?.Invoke(index); } };
            _cells.Add(cell);
            grid.Children.Add(cell);
        }
        Child = grid;
        Paint(selected);
        // Greyed out rather than hidden, as the Depth picker is for JPEG.
        PropertyChanged += (_, e) => { if (e.Property == IsEffectivelyEnabledProperty) Opacity = IsEffectivelyEnabled ? 1 : 0.45; };
    }

    void Paint(int selected) {
        Selected = selected;
        for (int i = 0; i < _cells.Count; i++) {
            _cells[i].Background = i == selected ? Palette.Line : Brushes.Transparent;
            ((TextBlock)_cells[i].Child!).Foreground = i == selected ? Palette.Text : Palette.Dim;
        }
    }
}

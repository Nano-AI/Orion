using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Animation;
using Avalonia.Media.Transformation;

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

/// The sheets' one alert language: the warning triangle in `warn`, a headline, a line or two of detail and,
/// when there is a choice to make, its buttons, on a raised plate. Not a dialog: it sits in the sheet where
/// the trouble is. It arrives on Motion.Enter, ease-out, rising 4 px as it fades in (a fade alone under
/// reduced motion), and goes at once: the next thing the sheet shows should not wait on it.
sealed class SheetAlert : Border {
    readonly TextBlock _headline = new() { FontSize = Faces.Body, FontWeight = FontWeight.SemiBold, Foreground = Palette.Text, TextWrapping = TextWrapping.Wrap };
    // `label`, not `dim`: 5.9:1 on the raised plate where dim is 4.6.
    readonly TextBlock _detail = new() { FontSize = Faces.Label, Foreground = Palette.Label, TextWrapping = TextWrapping.Wrap, LineHeight = 16 };

    public SheetAlert(params Control[] buttons) {
        Background = Palette.Raised;
        BorderBrush = Palette.Line;
        BorderThickness = new Thickness(1);
        CornerRadius = new CornerRadius(Radius.Lg);
        Padding = new Thickness(Space.Md, Space.Md, Space.Pad, Space.Md);
        IsVisible = false;
        var icon = Icons.Create("exclamationmark.triangle", IconSize.Md, Palette.Warn);
        icon.VerticalAlignment = VerticalAlignment.Top;
        icon.Margin = new Thickness(0, 0, Space.Md, 0);
        var words = new StackPanel { Spacing = Space.Xs, Children = { _headline, _detail } };
        if (buttons.Length > 0) {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, HorizontalAlignment = HorizontalAlignment.Right,
                                       Margin = new Thickness(0, Space.Sm, 0, 0) };
            row.Children.AddRange(buttons);
            words.Children.Add(row);
        }
        DockPanel.SetDock(icon, Dock.Left);
        Child = new DockPanel { Children = { icon, words } };
        Avalonia.Automation.AutomationProperties.SetLiveSetting(this, Avalonia.Automation.AutomationLiveSetting.Assertive);
    }

    public string Headline => _headline.Text ?? "";
    public string Detail => _detail.Text ?? "";

    /// Shown with these words; arriving only if it was not already showing (new words in place do not replay it).
    public void Show(string headline, string? detail = null) {
        _headline.Text = headline;
        _detail.Text = detail ?? "";
        _detail.IsVisible = !string.IsNullOrEmpty(detail);
        Avalonia.Automation.AutomationProperties.SetName(this, detail == null ? headline : $"{headline}. {detail}");
        if (IsVisible) return;
        Transitions = null;
        Opacity = 0;
        RenderTransform = Motion.Reduced ? null : TransformOperations.Parse("translateY(4px)");
        IsVisible = true;
        Transitions = [
            new DoubleTransition { Property = OpacityProperty, Duration = Motion.Enter, Easing = Motion.EaseOut },
            new TransformOperationsTransition { Property = RenderTransformProperty, Duration = Motion.Enter, Easing = Motion.EaseOut },
        ];
        Opacity = 1;
        if (!Motion.Reduced) RenderTransform = TransformOperations.Parse("translateY(0px)");
    }

    public void Hide() {
        Transitions = null;
        IsVisible = false;
    }
}

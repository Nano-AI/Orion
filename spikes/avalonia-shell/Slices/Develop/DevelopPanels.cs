using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;

namespace OrionShell;

/// A port of app/DevelopPanels.swift: furniture more than one develop tab needs.
static class Develop {
    /// PanelButton: a panel button that looks like one — raised fill, hairline,
    /// 11 pt medium, rather than text that reads as a label.
    public static Button PanelButton(string title, Action action) {
        var button = new Button {
            Content = new TextBlock { Text = title, FontSize = TypeSize.Sm, FontWeight = FontWeight.Medium,
                                      TextTrimming = TextTrimming.CharacterEllipsis },
            Padding = new Thickness(9, 4), Background = Palette.Raised, BorderBrush = Palette.Line,
            BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(Radius.Md), MinHeight = 0,
        };
        button.Click += (_, _) => action();
        return button;
    }

    /// A plain text button: Swift's `.buttonStyle(.plain)` with a 10 pt label.
    public static Button TextButton(string title, IBrush ink, Action action) {
        var button = new Button {
            Content = new TextBlock { Text = title, FontSize = TypeSize.Xs, Foreground = ink },
            Background = Brushes.Transparent, BorderThickness = new Thickness(0), Padding = new Thickness(0), MinHeight = 0,
            VerticalAlignment = VerticalAlignment.Center,
        };
        button.Click += (_, _) => action();
        return button;
    }

    /// Swift's `.bordered .small` button: Undo spot, Clear.
    public static Button SmallButton(string title, Action action) {
        var button = new Button {
            Content = new TextBlock { Text = title, FontSize = TypeSize.Sm },
            Padding = new Thickness(9, 3), MinHeight = 0, Background = Palette.Raised,
            CornerRadius = new CornerRadius(Radius.Md),
        };
        // Disabled is the panel's raised well with faint text, not Fluent's grey slab.
        button.Styles.Add(new Style(x => x.OfType<Button>().Class(":disabled").Template().OfType<ContentPresenter>()) {
            Setters = { new Setter(ContentPresenter.BackgroundProperty, Palette.Raised) } });
        button.Styles.Add(new Style(x => x.OfType<Button>().Class(":disabled").Descendant().OfType<TextBlock>()) {
            Setters = { new Setter(TextBlock.ForegroundProperty, Palette.Faint) } });
        button.Click += (_, _) => action();
        return button;
    }

    /// The 10 pt faint sentence a section prints under a control.
    public static TextBlock Note(string text) =>
        new() { Text = text, FontSize = TypeSize.Xs, Foreground = Palette.Faint, TextWrapping = TextWrapping.Wrap };

    /// An HStack with a Spacer: `leading` left to right, `trailing` at the far end, centred vertically.
    public static DockPanel Row(double spacing, Control[] leading, params Control[] trailing) {
        var row = new DockPanel { LastChildFill = false };
        foreach (var c in leading.Concat(trailing)) c.VerticalAlignment = VerticalAlignment.Center;
        foreach (var c in leading) { c.Margin = new Thickness(0, 0, spacing, 0); DockPanel.SetDock(c, Dock.Left); row.Children.Add(c); }
        foreach (var c in trailing.Reverse()) { c.Margin = new Thickness(spacing, 0, 0, 0); DockPanel.SetDock(c, Dock.Right); row.Children.Add(c); }
        return row;
    }

    /// A slider on a value Engine.swift keeps outside OrionAdjustments (the
    /// spot tool's next radius and feather), which AdjustmentSlider cannot bind.
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public static Control LocalSlider(string name, float min, float max, float baseValue, int decimals) {
        var text = new TextBlock { FontSize = TypeSize.Sm, Classes = { "tnum" } };
        var window = new Border { Padding = new Thickness(5, 1.5), CornerRadius = new CornerRadius(Radius.Sm),
                                  Background = Palette.Ground, Margin = new Thickness(0, 0, -5, 0), Child = text };
        var track = new AnalogTrack { Min = min, Max = max, Base = baseValue };
        void Show(float v) {
            text.Text = v.ToString("F" + decimals, CultureInfo.InvariantCulture);
            text.Foreground = AdjustmentMath.IsModified(v, baseValue, decimals) ? Palette.Accent : Palette.Text;
        }
        track.Changed += Show;
        track.Set(baseValue);
        Show(baseValue);
        var head = new DockPanel();
        DockPanel.SetDock(window, Dock.Right);
        head.Children.Add(window);
        head.Children.Add(Engraved.Label(name));
        return new StackPanel { Spacing = 3, Children = { head, track } };
    }
}

/// Swift's `.pickerStyle(.segmented)`: equal segments in a raised well, the
/// chosen one lifted.
sealed class Segmented : Border {
    readonly Grid _grid = new();
    int _selected;
    public event Action<int>? Picked;

    public Segmented(int selected, params string[] titles) {
        Background = Palette.Raised;
        CornerRadius = new CornerRadius(6);
        Padding = new Thickness(2);
        Child = _grid;
        for (int i = 0; i < titles.Length; i++) {
            int index = i;
            _grid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            var segment = new Border {
                CornerRadius = new CornerRadius(Radius.Md), Padding = new Thickness(8, 3),
                Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
                Child = new TextBlock { Text = titles[i], FontSize = TypeSize.Md, HorizontalAlignment = HorizontalAlignment.Center },
            };
            segment.PointerPressed += (_, _) => { Selected = index; Picked?.Invoke(index); };
            Grid.SetColumn(segment, i);
            _grid.Children.Add(segment);
        }
        Selected = selected;
    }

    public int Selected {
        get => _selected;
        set {
            _selected = value;
            for (int i = 0; i < _grid.Children.Count; i++)
                ((Border)_grid.Children[i]).Background = i == value ? Palette.Rail : Brushes.Transparent;
        }
    }
}

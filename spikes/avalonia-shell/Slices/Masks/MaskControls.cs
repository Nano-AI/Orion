using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;

namespace OrionShell.Masks;

/// AdjustmentSlider's look for a number that is not an OrionAdjustments field:
/// a mask shape's own geometry, which lives in MaskGroup (see there for why).
/// Same name, readout and AnalogTrack; click the readout to reset.
/// ponytail: no typed entry, no plate dot. Both come free once AdjustmentSlider
/// takes a getter and setter instead of a field name (report, Shared change).
sealed class MaskSlider : StackPanel {
    readonly TextBlock _readout = Faces.Figure("");
    readonly AnalogTrack _track;
    readonly Action<float> _set;
    readonly string _unit;
    readonly int _decimals;

    public MaskSlider(string name, Func<float> get, Action<float> set, float min, float max,
                      string unit, int decimals, float baseValue) {
        (_set, _unit, _decimals) = (set, unit, decimals);
        Spacing = 5;
        _track = new AnalogTrack { Min = min, Max = max, Base = baseValue };
        _track.Changed += Apply;
        _readout.HorizontalAlignment = HorizontalAlignment.Right;
        var window = new Border {
            MinWidth = 54, Height = 21, Padding = new Thickness(6, 0), CornerRadius = new CornerRadius(Radius.Sm),
            Background = Palette.Ground, BorderBrush = Palette.Line, BorderThickness = new Thickness(1), Child = _readout,
        };
        window.Tapped += (_, _) => Apply(baseValue);
        DockPanel.SetDock(window, Dock.Right);
        Children.Add(new DockPanel { Children = { window, Engraved.Label(name) } });
        Children.Add(_track);
        _track.Set(get());
        Paint();
    }

    void Apply(float v) {
        _set(v);
        _track.Set(v);
        Paint();
    }

    void Paint() {
        _readout.Text = _track.Value.ToString("F" + _decimals, System.Globalization.CultureInfo.InvariantCulture) + _unit;
        _readout.Foreground = AdjustmentMath.IsModified(_track.Value, _track.Base, _decimals) ? Palette.Accent : Palette.Text;
    }
}

/// The small controls the Mask tab draws in system style on macOS.
static class MaskControls {
    public static TextBlock Caption(string text) => Engraved.Note(text);

    /// A small standard button: Remove, Clear stroke, the reorder chevrons.
    /// Like the menus, the click runs after the event, since it may rebuild the panel.
    public static Button Push(object content, Action click, bool enabled = true, double padX = Space.Md) {
        var b = new StateButton {
            Content = content is string s ? new TextBlock { Text = s } : content, IsEnabled = enabled,
            Padding = new Thickness(padX, 0), VerticalAlignment = VerticalAlignment.Center,
        };
        b.Click += (_, _) => Dispatcher.UIThread.Post(click);
        return b;
    }

    /// A pull-down with no frame until hovered: "More ⌄", the card's "+" and "−".
    /// `icon` is an SF Symbol name or a drawn glyph. Items run after the menu
    /// closes, because most of them rebuild the panel this button sits in.
    public static Button Menu(object? icon, string? title, IEnumerable<Control> items, bool enabled = true,
                              string help = "", bool chevron = true) {
        var label = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        if (icon is string name) label.Children.Add(Icons.Create(name, 11, Palette.Text));
        else if (icon is Control glyph) label.Children.Add(glyph);
        if (title != null) label.Children.Add(new TextBlock { Text = title, VerticalAlignment = VerticalAlignment.Center });
        if (chevron) label.Children.Add(Icons.Create("chevron.down", 7, Palette.Text));
        var flyout = new MenuFlyout();
        foreach (var item in items) flyout.Items.Add(item);
        var b = new StateButton { Kind = ButtonKind.Bare, Content = label, Flyout = flyout, IsEnabled = enabled, Ink = Palette.Text,
                                  Padding = new Thickness(title == null ? 4 : 6, 0), MinWidth = 0, Height = title == null ? 20 : 24,
                                  VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Left };
        return b.Help(help);
    }

    // Glyphs the shared icon table has no drawing for, on its 24-unit grid.
    public const string Plus = "M12,5 L12,19 M5,12 L19,12", Minus = "M5,12 L19,12", Cross = "M6,6 L18,18 M18,6 L6,18",
                        Ellipsis = "M5,12 L5.01,12 M12,12 L12.01,12 M19,12 L19.01,12";

    /// Drawn on the whole 24-unit grid and scaled as a box, so a flat mark (−, ⋯)
    /// keeps its proportions instead of being stretched to fill; the stroke is
    /// thickened to come out at the icon table's 1.6 points.
    public static Control Glyph(string data, double size, IBrush brush) => new Viewbox {
        Width = size, Height = size, VerticalAlignment = VerticalAlignment.Center,
        Child = new Avalonia.Controls.Shapes.Path {
            Data = Geometry.Parse(data), Width = 24, Height = 24, Stretch = Stretch.None, Stroke = brush,
            StrokeThickness = (data == Ellipsis ? 2.6 : 1.6) * 24 / size, StrokeLineCap = PenLineCap.Round, StrokeJoin = PenLineJoin.Round,
        },
    };

    /// A mask kind's mark: a ring in a ring for a radial (not an eye: the eye is
    /// visibility), ramp lines for a linear, the pencil for a brush.
    public static Control KindGlyph(int kind, double size = 12) => kind switch {
        2 => Glyph("M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 Z M8.5,12 A3.5,3.5 0 1 0 15.5,12 A3.5,3.5 0 1 0 8.5,12 Z", size, Palette.Text),
        1 => Glyph("M4,5 L20,5 M4,12 L20,12 M4,19 L20,19", size, Palette.Text),
        3 => Icons.Create("pencil", size, Palette.Text),
        5 => Glyph("M3,18 L9,18 L12,6 L15,18 L21,18", size, Palette.Text),
        6 => Icons.Create("eyedropper", size, Palette.Text),
        _ => Icons.Create("circle", size, Palette.Text),
    };

    public static MenuItem Item(string header, Action act) {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => Dispatcher.UIThread.Post(act);
        return item;
    }

    /// A section title inside a menu (SwiftUI's Section("Draw")).
    public static Control Heading(string header) => new MenuItem { Header = header, IsEnabled = false };

    public static MenuItem Submenu(string header, IEnumerable<Control> items) {
        var item = new MenuItem { Header = header };
        foreach (var i in items) item.Items.Add(i);
        return item;
    }

    /// A segmented picker (the shared one); the pick runs after the event, since it rebuilds the panel.
    public static Control Segmented(string[] labels, int chosen, Action<int> pick) {
        var picker = new OrionShell.Segmented(labels, chosen) { CellPadding = new Thickness(4, 0) };
        picker.Changed += i => Dispatcher.UIThread.Post(() => pick(i));
        return picker;
    }

    /// The mini switch beside an engraved label ("Show mask"): Fluent's,
    /// recoloured from the palette, with the knob filling the track as the
    /// macOS one does. The knob style is a trigger (:not(:disabled)) so it
    /// outranks the size Fluent's template sets.
    public static Control Switch(string label, bool on, Action<bool> changed) {
        var toggle = new ToggleSwitch {
            IsChecked = on, OnContent = null, OffContent = null, MinWidth = 0, Padding = new Thickness(0),
            VerticalAlignment = VerticalAlignment.Center,
        };
        foreach (var state in new[] { "", "PointerOver", "Pressed" }) {
            toggle.Resources["ToggleSwitchFillOff" + state] = Palette.Line;
            toggle.Resources["ToggleSwitchStrokeOff" + state] = Palette.Line;
            toggle.Resources["ToggleSwitchKnobFillOff" + state] = Palette.Text;
            toggle.Resources["ToggleSwitchFillOn" + state] = Palette.Accent;
            toggle.Resources["ToggleSwitchStrokeOn" + state] = Palette.Accent;
            toggle.Resources["ToggleSwitchKnobFillOn" + state] = Palette.Text;
        }
        foreach (var knob in new[] { "SwitchKnobOff", "SwitchKnobOn" })
            toggle.Styles.Add(new Style(x => x.OfType<ToggleSwitch>().Not(y => y.Class(":disabled")).Template().Name(knob)) {
                Setters = { new Setter(Layoutable.WidthProperty, 16.0), new Setter(Layoutable.HeightProperty, 16.0) },
            });
        toggle.IsCheckedChanged += (_, _) => changed(toggle.IsChecked == true);
        return new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = Space.Md,
            Children = { Engraved.Label(label), new LayoutTransformControl {
                LayoutTransform = new ScaleTransform(0.85, 0.85), Child = toggle,
                Margin = new Thickness(0, -Space.Sm, 0, -Space.Sm),   // the template pads for a header it does not have
            } },
        };
    }

    /// A small checkbox beside an engraved label ("Invert"); the label toggles it too.
    public static Control Check(string label, bool on, Action<bool> changed) {
        var box = new CheckBox { IsChecked = on, MinHeight = 0, Padding = new Thickness(0), VerticalAlignment = VerticalAlignment.Center };
        box.IsCheckedChanged += (_, _) => changed(box.IsChecked == true);
        var name = Engraved.Label(label);
        name.Tapped += (_, _) => box.IsChecked = box.IsChecked != true;
        return new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = Space.Sm,
            Children = { new LayoutTransformControl { LayoutTransform = new ScaleTransform(0.7, 0.7), Child = box }, name },
        };
    }
}

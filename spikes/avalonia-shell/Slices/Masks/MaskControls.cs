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
    readonly TextBlock _readout = new() { FontSize = TypeSize.Sm, Classes = { "tnum" } };
    readonly AnalogTrack _track;
    readonly Action<float> _set;
    readonly string _unit;
    readonly int _decimals;

    public MaskSlider(string name, Func<float> get, Action<float> set, float min, float max,
                      string unit, int decimals, float baseValue) {
        (_set, _unit, _decimals) = (set, unit, decimals);
        Spacing = 3;
        _track = new AnalogTrack { Min = min, Max = max, Base = baseValue };
        _track.Changed += Apply;
        var window = new Border {
            Padding = new Thickness(5, 1.5), CornerRadius = new CornerRadius(Radius.Sm), Margin = new Thickness(0, 0, -5, 0),
            Background = Palette.Ground, BorderBrush = Palette.Line, BorderThickness = new Thickness(0.5), Child = _readout,
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
    public static TextBlock Caption(string text, double size = TypeSize.Xs) => new() {
        Text = text, FontSize = size, Foreground = Palette.Faint, TextWrapping = TextWrapping.Wrap,
    };

    /// A bordered small button: Remove, Clear stroke, the reorder chevrons.
    /// Like the menus, the click runs after the event, since it may rebuild the panel.
    public static Button Push(object content, Action click, bool enabled = true, double padX = Space.Md) {
        var b = new Button {
            Content = content, IsEnabled = enabled, FontSize = TypeSize.Md, MinHeight = 22,
            Padding = new Thickness(padX, Space.Xs), CornerRadius = new CornerRadius(Radius.Md),
            Background = Palette.Raised, Foreground = Palette.Text,
            VerticalAlignment = VerticalAlignment.Center, VerticalContentAlignment = VerticalAlignment.Center,
        };
        b.Resources["ButtonBackgroundDisabled"] = Palette.Raised;   // dimmed, not Fluent's light grey
        b.Resources["ButtonForegroundDisabled"] = Palette.Faint;
        b.Click += (_, _) => Dispatcher.UIThread.Post(click);
        return b;
    }

    /// A borderless pull-down: "+ Add ⌄", "Radial ⌄". Items run after the menu
    /// closes, because most of them rebuild the panel this button sits in.
    public static Button Menu(string? icon, string title, IEnumerable<Control> items, bool enabled = true) {
        var label = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        if (icon != null) label.Children.Add(Icons.Create(icon, 11, Palette.Text));
        label.Children.Add(new TextBlock { Text = title, FontSize = TypeSize.Md, VerticalAlignment = VerticalAlignment.Center });
        label.Children.Add(Icons.Create("chevron.down", 7, Palette.Text));
        var flyout = new MenuFlyout();
        foreach (var item in items) flyout.Items.Add(item);
        return new Button {
            Content = label, Flyout = flyout, IsEnabled = enabled, Background = Brushes.Transparent,
            BorderThickness = new Thickness(0), Padding = new Thickness(Space.Xs, Space.Xs), MinHeight = 0,
        };
    }

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

    /// A segmented picker: equal cells, the chosen one raised.
    public static Control Segmented(string[] labels, int chosen, Action<int> pick) {
        var cells = new Avalonia.Controls.Primitives.UniformGrid { Rows = 1 };
        for (int i = 0; i < labels.Length; i++) {
            int index = i;
            var cell = new Border {
                CornerRadius = new CornerRadius(Radius.Sm), Padding = new Thickness(0, Space.Xs),
                Background = i == chosen ? Palette.Rail : Brushes.Transparent,
                Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
                Child = new TextBlock { Text = labels[i], FontSize = TypeSize.Sm, HorizontalAlignment = HorizontalAlignment.Center },
            };
            cell.Tapped += (_, _) => { if (index != chosen) Dispatcher.UIThread.Post(() => pick(index)); };
            cells.Children.Add(cell);
        }
        return new Border {
            Background = Palette.Raised, BorderBrush = Palette.Line, BorderThickness = new Thickness(0.5),
            CornerRadius = new CornerRadius(Radius.Md), Padding = new Thickness(1), Child = cells,
        };
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

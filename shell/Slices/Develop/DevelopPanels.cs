using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Develop;

/// A port of the Swift app's DevelopPanels.swift: furniture more than one develop tab needs.
static class Develop {
    /// PanelButton: a panel button that looks like one (Controls.dc.html's standard button).
    public static StateButton PanelButton(string title, Action action) => new(null, title, action) { HorizontalAlignment = HorizontalAlignment.Left };

    /// A word that acts, with no frame until hovered: Remove, Clear, a lens name.
    public static StateButton TextButton(string title, IBrush ink, Action action) =>
        new(null, title, action, ButtonKind.Bare) { Ink = ink, Padding = new Thickness(Space.Sm, 0), Height = 24 };

    /// Undo spot, Clear: the standard button, as the board draws every in-panel one.
    public static StateButton SmallButton(string title, Action action) => new(null, title, action);

    /// The sentence a section prints under a control.
    public static TextBlock Note(string text) => Engraved.Note(text);

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
    /// Its readout is AdjustmentSlider's box, in the numeral face. `changed` gets each value.
    public static Control LocalSlider(string name, float min, float max, float baseValue, int decimals, Action<float>? changed = null) {
        var text = Faces.Figure("");
        text.HorizontalAlignment = HorizontalAlignment.Right;
        var window = new Border { MinWidth = 54, Height = 21, Padding = new Thickness(Space.Sm, 0), CornerRadius = new CornerRadius(Radius.Sm),
                                  Background = Palette.Ground, BorderBrush = Palette.Line, BorderThickness = new Thickness(1), Child = text };
        var track = new AnalogTrack { Min = min, Max = max, Base = baseValue };
        void Show(float v) {
            text.Text = v.ToString("F" + decimals, CultureInfo.InvariantCulture);
            text.Foreground = AdjustmentMath.IsModified(v, baseValue, decimals) ? Palette.Accent : Palette.Text;
        }
        track.Changed += v => { Show(v); changed?.Invoke(v); };
        track.Set(baseValue);
        Show(baseValue);
        var head = new DockPanel();
        DockPanel.SetDock(window, Dock.Right);
        head.Children.Add(window);
        head.Children.Add(Engraved.Label(name));
        return new StackPanel { Spacing = Space.Gap, Children = { head, track } };
    }
}

using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace OrionShell;

/// A port of app/AdjustmentSlider.swift bound to one OrionAdjustments field:
/// an engraved name, a readout that turns accent when the value is off its
/// default, and an AnalogTrack. Click the readout to reset; double-click to
/// type (Return commits, Escape cancels, Up/Down nudge, Shift for ten).
/// stub(#315): no undo history — Engine.edit's EditHistory is Swift-only.
public sealed class AdjustmentSlider : StackPanel {
    /// Bubbles when Modified flips, so a SectionPlate can light its dot.
    public static readonly RoutedEvent<RoutedEventArgs> ModifiedChangedEvent =
        RoutedEvent.Register<AdjustmentSlider, RoutedEventArgs>("ModifiedChanged", RoutingStrategies.Bubble);

    readonly string _name, _field, _unit;
    readonly int _index, _decimals;
    readonly float? _fixedBase;
    readonly AnalogTrack _track;
    readonly Border _window = new() { Padding = new Thickness(5, 1.5), CornerRadius = new CornerRadius(3) };
    readonly StackPanel _windowRow = new() { Orientation = Orientation.Horizontal, Spacing = 3 };
    readonly TextBlock _text = new() { FontSize = 11, Classes = { "tnum" } };
    TextBox? _editor;
    bool _hovering, _modified;
    IDisposable? _pendingReset;

    public float Value { get; private set; }
    public float Base { get; private set; }
    public bool Modified => _modified;

    /// `baseValue` is the control's default; null means the value the photo opened with.
    public AdjustmentSlider(string name, string field, float min, float max, string unit = "", int decimals = 2,
                            int index = 0, float? baseValue = null, IBrush? tint = null) {
        (_name, _field, _unit, _index, _decimals, _fixedBase) = (name, field, unit, index, decimals, baseValue);
        Spacing = 3;
        _track = new AnalogTrack { Min = min, Max = max, Tint = tint };
        _track.Changed += v => Apply(v);
        _track.Interacting += on => { if (on) EngineModel.Shared.BeginInteraction(); else EngineModel.Shared.EndInteraction(); };

        _window.Child = _windowRow;
        _window.Margin = new Thickness(0, 0, -5, 0);
        _window.BorderThickness = new Thickness(0.5);
        _window.BorderBrush = new LinearGradientBrush {   // the hairline fades down the window
            StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
            GradientStops = { new(((ISolidColorBrush)Palette.Line).Color, 0), new(Color.FromArgb(38, 49, 49, 53), 1) },
        };
        _window.PointerEntered += (_, _) => { _hovering = true; Paint(); };
        _window.PointerExited += (_, _) => { _hovering = false; Paint(); };
        _window.Tapped += (_, _) => {
            // Wait out a double-click before treating this as a reset click.
            _pendingReset?.Dispose();
            _pendingReset = DispatcherTimer.RunOnce(() => { if (_modified && _editor == null) Apply(Base); },
                                                    TimeSpan.FromMilliseconds(250));
        };
        _window.DoubleTapped += (_, _) => { _pendingReset?.Dispose(); BeginEditing(); };
        ToolTip.SetTip(_window, "Double-click to type a value");

        var head = new DockPanel();
        DockPanel.SetDock(_window, Dock.Right);
        head.Children.Add(_window);
        head.Children.Add(Engraved.Label(name));
        Children.Add(head);
        Children.Add(_track);

        EngineModel.Shared.Opened += Reload;
        EngineModel.Shared.ValuesChanged += Reread;
        Reload();
    }

    /// The value moved under the slider (auto-enhance): follow it, keep the detent.
    void Reread() {
        Value = EngineModel.Shared.Get(_field, _index);
        _track.Set(Value);
        Paint();
    }

    void Reload() {
        Value = EngineModel.Shared.Get(_field, _index);
        Base = _fixedBase ?? Value;
        _track.Base = Base;
        _track.Set(Value);
        Paint();
    }

    void Apply(float v) {
        Value = v;
        _track.Set(v);
        EngineModel.Shared.Set(_field, v, _index);
        Paint();
    }

    string Text(float v) => v.ToString("F" + _decimals, System.Globalization.CultureInfo.InvariantCulture) + _unit;

    void Paint() {
        bool modified = AdjustmentMath.IsModified(Value, Base, _decimals);
        bool accent = modified && _editor == null;
        _windowRow.Children.Clear();
        if (accent && _hovering) _windowRow.Children.Add(Icons.Create("arrow.uturn.backward", 9, Palette.Accent));
        _windowRow.Children.Add((Control?)_editor ?? _text);
        _text.Text = Text(Value);
        _text.Foreground = accent ? Palette.Accent : Palette.Text;
        _window.Background = _hovering && accent ? Palette.Raised : Palette.Ground;
        ToolTip.SetTip(_window, modified ? $"Reset {_name} to {Text(Base)}. Double-click to type a value" : "Double-click to type a value");
        if (modified != _modified) {
            _modified = modified;
            RaiseEvent(new RoutedEventArgs(ModifiedChangedEvent));
        }
    }

    void BeginEditing() {
        if (_editor != null) return;
        _editor = new TextBox {
            Text = Value.ToString("F" + _decimals, System.Globalization.CultureInfo.InvariantCulture),
            FontSize = 11, MinWidth = 48, Padding = new Thickness(0), BorderThickness = new Thickness(0),
            Background = Brushes.Transparent, TextAlignment = TextAlignment.Right, MinHeight = 0,
        };
        _editor.KeyDown += (_, e) => {
            switch (e.Key) {
                case Key.Enter: Commit(); e.Handled = true; break;
                case Key.Escape: Close(); e.Handled = true; break;
                case Key.Up or Key.Down:
                    int steps = (e.Key == Key.Up ? 1 : -1) * (e.KeyModifiers.HasFlag(KeyModifiers.Shift) ? 10 : 1);
                    if (AdjustmentMath.Nudged(_editor!.Text ?? "", steps, _unit, _decimals, _track.Min, _track.Max) is { } n)
                        _editor.Text = n.ToString("F" + _decimals, System.Globalization.CultureInfo.InvariantCulture);
                    e.Handled = true;
                    break;
            }
        };
        _editor.LostFocus += (_, _) => Commit();
        Paint();
        Dispatcher.UIThread.Post(() => { _editor?.Focus(); _editor?.SelectAll(); });
    }

    void Commit() {
        if (_editor == null) return;
        var parsed = AdjustmentMath.Parsed(_editor.Text ?? "", _unit, _track.Min, _track.Max);
        Close();
        if (parsed is { } v && v != Value) Apply(v);
    }

    void Close() {
        _editor = null;
        Paint();
    }
}

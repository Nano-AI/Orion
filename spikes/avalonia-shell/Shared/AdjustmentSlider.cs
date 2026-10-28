using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace OrionShell;

/// A port of app/AdjustmentSlider.swift bound to one OrionAdjustments field:
/// a name, a boxed readout in the numeral face that turns accent when the value
/// is off its default, and an AnalogTrack marked in the slider's unit. Hover the
/// readout of an edited slider for its reset; a click takes it. Double-click,
/// or Tab to it and press Return, to type (Return commits, Escape cancels,
/// Up/Down nudge, Shift for ten).
/// stub(#315): no undo history — Engine.edit's EditHistory is Swift-only.
public sealed class AdjustmentSlider : StackPanel {
    /// Bubbles when Modified flips, so a SectionPlate can light its dot.
    public static readonly RoutedEvent<RoutedEventArgs> ModifiedChangedEvent =
        RoutedEvent.Register<AdjustmentSlider, RoutedEventArgs>("ModifiedChanged", RoutingStrategies.Bubble);

    readonly string _name, _field, _unit;
    readonly int _index, _decimals;
    readonly float? _fixedBase;
    readonly AnalogTrack _track;
    readonly Readout _window = new() {
        MinWidth = 54, Height = 21, Padding = new Thickness(6, 0), Focusable = true,
    };
    readonly StackPanel _windowRow = new() { Orientation = Orientation.Horizontal, Spacing = 4, HorizontalAlignment = HorizontalAlignment.Right };
    readonly TextBlock _text = Faces.Figure("");
    readonly Control _reset = Icons.Create("arrow.uturn.backward", 9, Palette.Accent);
    TextBox? _editor;
    bool _hovering, _modified, _keyboardFocus;
    IDisposable? _pendingReset;

    public float Value { get; private set; }
    public float Base { get; private set; }
    public bool Modified => _modified;

    /// `baseValue` is the control's default; null means the value the photo opened with.
    /// `scale` marks the track; null means the one `unit` implies (Scale.For).
    public AdjustmentSlider(string name, string field, float min, float max, string unit = "", int decimals = 2,
                            int index = 0, float? baseValue = null, IBrush? tint = null, Scale? scale = null) {
        (_name, _field, _unit, _index, _decimals, _fixedBase) = (name, field, unit, index, decimals, baseValue);
        Spacing = 5;
        _track = new AnalogTrack { Min = min, Max = max, Tint = tint, Scale = scale ?? Scale.For(unit, min, max) };
        _track.Changed += v => Apply(v);
        _track.Interacting += on => { if (on) EngineModel.Shared.BeginInteraction(); else EngineModel.Shared.EndInteraction(); };

        _window.Child = _windowRow;
        _window.PointerEntered += (_, _) => { _hovering = true; Paint(); };
        _window.PointerExited += (_, _) => { _hovering = false; Paint(); };
        _window.Tapped += (_, _) => {
            // Wait out a double-click before treating this as a reset click.
            _pendingReset?.Dispose();
            _pendingReset = DispatcherTimer.RunOnce(() => { if (_modified && _editor == null) Apply(Base); },
                                                    TimeSpan.FromMilliseconds(250));
        };
        _window.DoubleTapped += (_, _) => { _pendingReset?.Dispose(); BeginEditing(); };
        _window.KeyDown += (_, e) => {
            if (e.Key is Key.Enter or Key.Space && _editor == null) { BeginEditing(); e.Handled = true; }
        };
        _window.GotFocus += (_, e) => { _keyboardFocus = e.NavigationMethod is NavigationMethod.Tab or NavigationMethod.Directional; Paint(); };
        _window.LostFocus += (_, _) => { _keyboardFocus = false; Paint(); };

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

    /// As the meter prints it: a true minus, and none on a value that shows as zero.
    string Text(float v) {
        string digits = MathF.Abs(v).ToString("F" + _decimals, System.Globalization.CultureInfo.InvariantCulture);
        return (v < 0 && digits.Any(d => d is >= '1' and <= '9') ? "\u2212" : "") + digits + _unit;
    }

    /// The readout's states (Controls.dc.html): at rest the box is engraved in
    /// ground; hover lifts it; hovering an edit offers the reset; typing rings it
    /// in the accent; keyboard focus gets the two-ring halo.
    void Paint() {
        bool modified = AdjustmentMath.IsModified(Value, Base, _decimals);
        bool editing = _editor != null, offer = modified && _hovering && !editing;
        _windowRow.Children.Clear();
        if (offer) _windowRow.Children.Add(_reset);
        _windowRow.Children.Add((Control?)_editor ?? _text);
        _text.Text = Text(Value);
        _text.Foreground = modified && !editing ? Palette.Accent : Palette.Text;
        _window.Fill = offer ? Palette.Raised : Palette.Ground;
        _window.Edge = editing ? Palette.Accent : _hovering ? Palette.Rail : Palette.Line;
        _window.Ring = _keyboardFocus && !editing;
        _window.InvalidateVisual();
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
            FontFamily = Faces.Numeral, FontWeight = FontWeight.Medium, FontFeatures = Faces.Tabular, FontSize = Faces.Readout,
            MinWidth = 40, Padding = new Thickness(0), BorderThickness = new Thickness(0), Background = Brushes.Transparent,
            TextAlignment = TextAlignment.Right, MinHeight = 0, VerticalAlignment = VerticalAlignment.Center,
            CaretBrush = Palette.Accent, SelectionBrush = new SolidColorBrush(((ISolidColorBrush)Palette.Accent).Color, 0.35),
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

    /// The readout's box: a 1 px edge round a fill, and StateButton's focus halo.
    /// Drawn rather than a Border, whose Render is sealed and whose BoxShadow
    /// clips to it in Avalonia 12.
    sealed class Readout : Decorator {
        public IBrush Fill = Palette.Ground, Edge = Palette.Line;
        public bool Ring;
        public override void Render(DrawingContext c) {
            var r = new Rect(Bounds.Size);
            c.DrawRectangle(Fill, new Pen(Edge, 1), r.Deflate(0.5), Radius.Sm, Radius.Sm);
            if (Ring) StateButton.DrawFocusRing(c, r, Radius.Sm);
        }
    }
}

using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace OrionShell;

/// A port of app/AnalogTrack.swift: an engraved groove, an index scale, an
/// accent detent at the control's default and a milled thumb. Same constants.
sealed class AnalogTrack : Control {
    const double ThumbW = 19, ThumbH = 13, GrooveH = 5, ScaleH = 5, GrooveY = 13.5;

    public float Min, Max, Base, Value;
    public IBrush? Tint;                      // TrackTint: what each extreme does
    public event Action<float>? Changed;
    public event Action<bool>? Interacting;   // arms the preview graph, as in Swift

    double _startX;
    float _startValue;
    bool _dragging, _departing;

    static readonly IBrush AccentFill = new SolidColorBrush(((ISolidColorBrush)Palette.Accent).Color, 0.55);
    static readonly IBrush ThumbFill = new LinearGradientBrush {
        StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
        EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
        GradientStops = { new(Color.FromRgb(189, 189, 189), 0), new(Color.FromRgb(133, 133, 133), 1) },
    };
    static readonly IPen GripDark = new Pen(new SolidColorBrush(Colors.Black, 0.20), 0.75);
    static readonly IPen GripLight = new Pen(new SolidColorBrush(Colors.White, 0.12), 0.75);

    public AnalogTrack() => Height = 21;

    double Travel => Math.Max(Bounds.Width - ThumbW, 1);
    double X(float v) => ThumbW / 2 + (v - Min) / (Max - Min) * Travel;

    public void Set(float v) { Value = v; InvalidateVisual(); }

    public override void Render(DrawingContext c) {
        var tick = new Pen(Palette.Faint, 1);
        for (int i = 0; i <= 4; i++) {
            double x = ThumbW / 2 + i / 4.0 * Travel;
            c.DrawLine(tick, new Point(x, 0), new Point(x, ScaleH));
        }
        double bx = X(Base), vx = X(Value);
        c.DrawLine(new Pen(Palette.Accent, 1.5), new Point(bx, 0), new Point(bx, ScaleH + 2));

        var groove = new Rect(0, GrooveY - GrooveH / 2, Bounds.Width, GrooveH);
        c.DrawRectangle(Palette.Ground, new Pen(Palette.Line, 0.5), groove, GrooveH / 2, GrooveH / 2);
        var inner = groove.Deflate(new Thickness(0, 0.75));
        if (Tint != null)
            using (c.PushOpacity(0.45)) c.DrawRectangle(Tint, null, inner, 2, 2);
        double lo = Math.Min(bx, vx), hi = Math.Max(bx, vx);
        if (hi - lo > 0.5)
            c.DrawRectangle(AccentFill, null, new Rect(lo, inner.Y, hi - lo, inner.Height), 2, 2);

        var thumb = new Rect(vx - ThumbW / 2, GrooveY - ThumbH / 2, ThumbW, ThumbH);
        var shadow = new BoxShadows(new BoxShadow {
            OffsetY = 1, Blur = _dragging ? 5 : 2,
            Color = Color.FromArgb(_dragging ? (byte)115 : (byte)71, 0, 0, 0),
        });
        c.DrawRectangle(ThumbFill, new Pen(new SolidColorBrush(Colors.Black, 0.30), 0.5),
                        new RoundedRect(thumb, 3), shadow);
        for (int i = 0; i < 3; i++) {
            double x = thumb.Center.X + (i - 1) * 3.5;
            c.DrawLine(GripDark, new Point(x, thumb.Y + 3.5), new Point(x, thumb.Bottom - 3.5));
            c.DrawLine(GripLight, new Point(x + 0.75, thumb.Y + 3.5), new Point(x + 0.75, thumb.Bottom - 3.5));
        }
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        _startX = e.GetPosition(this).X;
        _startValue = Value;
        _dragging = true;
        // A drag that begins on the detent is a departure and must not snap
        // back to it (decision #294).
        _departing = Value == Base;
        e.Pointer.Capture(this);
        Interacting?.Invoke(true);
        InvalidateVisual();
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        if (!_dragging) return;
        bool fine = e.KeyModifiers.HasFlag(KeyModifiers.Alt);
        double moved = (e.GetPosition(this).X - _startX) / Travel * (fine ? 0.2 : 1);
        float v = AdjustmentMath.Detented(_startValue + (float)moved * (Max - Min), Base, Min, Max, _departing);
        if (v != Base) _departing = false;
        if (v == Value) return;
        Set(v);
        Changed?.Invoke(v);
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (!_dragging) return;
        _dragging = false;
        e.Pointer.Capture(null);
        Interacting?.Invoke(false);
        InvalidateVisual();
    }
}

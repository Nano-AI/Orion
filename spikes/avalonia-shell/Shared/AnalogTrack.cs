using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace OrionShell;

/// How a track is marked, like the scale in a light meter (DESIGN.md's
/// signature move): a tick every `Step` in the slider's own unit, and a numeral
/// on every `LabelEvery`-th tick counted from zero. Step 0 is five plain ticks
/// and no numerals, for unitless sliders.
public sealed record Scale(float Step = 0, int LabelEvery = 2, Func<float, string>? Text = null) {
    public static readonly Scale Plain = new();
    /// Exposure: a tick every stop, a numeral every other (−4 −2 0 +2 +4 on ±5).
    public static readonly Scale Stops = new(1, 2, v => Faces.Signed(v));
    /// White balance: a tick every 1000 K, a numeral every 2000 (4k 6k 8k).
    public static readonly Scale Kelvin = new(1000, 2, v => (v / 1000).ToString("0.#", CultureInfo.InvariantCulture) + "k");
    /// Angles: a tick every `step` degrees, a numeral every other; signed when the range crosses zero.
    public static Scale Degrees(float step, bool signed = true) =>
        new(step, 2, v => (signed ? Faces.Signed(v) : v.ToString("0.##", CultureInfo.InvariantCulture)) + "°");

    /// The scale a unit implies when a slider names none: EV in stops, K in
    /// Kelvin, ° in degrees (15° ticks over a quarter turn or more). Else plain.
    public static Scale For(string unit, float min, float max) => unit.Trim() switch {
        "EV" => Stops,
        "K" => Kelvin,
        "°" => Degrees(max - min >= 90 ? 15 : max - min > 30 ? 10 : 5, min < 0),
        _ => Plain,
    };

    public bool Numbered => Step > 0 && Text != null;

    /// Every tick from min to max, and whether it carries a numeral.
    public IEnumerable<(float At, bool Numbered)> Marks(float min, float max) {
        if (Step <= 0) {
            for (int i = 0; i <= 4; i++) yield return (min + i / 4f * (max - min), false);
            yield break;
        }
        for (long k = (long)MathF.Ceiling(min / Step - 1e-4f); k * Step <= max + Step * 1e-4f; k++)
            yield return (k * Step, Numbered && k % LabelEvery == 0);
    }
}

/// A port of app/AnalogTrack.swift, redrawn as a meter scale: ticks in the
/// slider's unit with numerals under them, the groove engraved in `ground`,
/// the edit filled in the accent from the default (the detent) to the value,
/// and a flat thumb. A tinted track shows its tint instead of the fill.
sealed class AnalogTrack : Control {
    const double ThumbW = 12, ThumbH = 15, GrooveH = 4, GrooveY = 9.5, TickBottom = 6, NumeralY = 20;
    const double PlainHeight = 19, NumberedHeight = 34;

    public float Min, Max, Base, Value;
    public IBrush? Tint;                      // TrackTint: what each extreme does
    public event Action<float>? Changed;
    public event Action<bool>? Interacting;   // arms the preview graph, as in Swift

    Scale _scale = Scale.Plain;
    public Scale Scale {
        get => _scale;
        set { _scale = value; _numerals = null; InvalidateMeasure(); InvalidateVisual(); }
    }

    double _startX;
    float _startValue;
    bool _dragging, _departing;
    List<(float At, FormattedText Text)>? _numerals;
    (float, float) _numeralsFor;

    static readonly IPen Tick = new Pen(Palette.Faint, 1);
    static readonly IPen Detent = new Pen(Palette.Accent, 1);
    static readonly IPen Engraving = new Pen(Palette.Line, 0.5);
    static readonly BoxShadows Lift = new(new BoxShadow { OffsetY = 1, Blur = 3, Color = Color.FromArgb(179, 0, 0, 0) });
    static readonly BoxShadows Held = new(new BoxShadow { OffsetY = 1, Blur = 5, Color = Color.FromArgb(200, 0, 0, 0) });

    double Travel => Math.Max(Bounds.Width - ThumbW, 1);
    double X(float v) => ThumbW / 2 + (v - Min) / (Max - Min) * Travel;
    static double Crisp(double x) => Math.Round(x) + 0.5;   // a 1 px line on a pixel, not across two

    public void Set(float v) { Value = v; InvalidateVisual(); }

    protected override Size MeasureOverride(Size available) => new(0, _scale.Numbered ? NumberedHeight : PlainHeight);

    public override void Render(DrawingContext c) {
        double w = Bounds.Width;
        foreach (var (at, numbered) in _scale.Marks(Min, Max)) {
            double x = Crisp(X(at)), h = _scale.Step <= 0 ? 4 : numbered ? 6 : 3;
            c.DrawLine(Tick, new Point(x, TickBottom - h), new Point(x, TickBottom));
        }
        foreach (var (at, text) in Numerals())
            c.DrawText(text, new Point(Math.Clamp(X(at) - text.Width / 2, 0, w - text.Width), NumeralY));

        double bx = X(Base), vx = X(Value);
        c.DrawLine(Detent, new Point(Crisp(bx), 0), new Point(Crisp(bx), TickBottom));

        var groove = new Rect(0, GrooveY - GrooveH / 2, w, GrooveH);
        c.DrawRectangle(Palette.Ground, Engraving, groove, GrooveH / 2, GrooveH / 2);
        double lo = Math.Min(bx, vx), hi = Math.Max(bx, vx);
        if (Tint != null)
            c.DrawRectangle(Tint, null, groove.Deflate(0.5), GrooveH / 2, GrooveH / 2);
        else if (hi - lo > 0.5)
            c.DrawRectangle(Palette.Accent, null, new Rect(lo, groove.Y, hi - lo, GrooveH), 1, 1);

        var thumb = new Rect(vx - ThumbW / 2, GrooveY - ThumbH / 2, ThumbW, ThumbH);
        c.DrawRectangle(Palette.Text, null, new RoundedRect(thumb, Radius.Sm), _dragging ? Held : Lift);
    }

    /// The numerals, shaped once per range rather than on every drag frame.
    List<(float At, FormattedText Text)> Numerals() {
        if (_numerals != null && _numeralsFor == (Min, Max)) return _numerals;
        _numeralsFor = (Min, Max);
        return _numerals = _scale.Marks(Min, Max).Where(m => m.Numbered).Select(m => {
            var text = new FormattedText(_scale.Text!(m.At), CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                                         Faces.NumeralFace, Faces.Mark, Palette.Dim);
            text.SetFontFeatures(Faces.Tabular);
            return (m.At, text);
        }).ToList();
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

using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace OrionShell.Develop;

/// Lightroom's histogram as a control, laid over the plate: five regions left to
/// right, Blacks, Shadows, Exposure, Highlights, Whites (Tones.All), and a
/// sideways drag in one moves that slider, the plate's whole width its whole
/// range. The drag goes through the same engine write and interaction as the
/// slider's, so it renders on the preview graph and settles into one undo step.
/// The region under the pointer lightens and names itself (Said).
sealed class HistogramDrag : Control {
    /// Where each region ends, as a share of the width. Lightroom names the five regions
    /// but documents no widths: these are a choice, Exposure taking the middle.
    static readonly double[] Ends = [0.1, 0.3, 0.7, 0.9, 1];
    static readonly IBrush Lit = CanvasSlice.Ink.White(0.08);
    static readonly Cursor Sideways = new(StandardCursorType.SizeWestEast);

    int? _hot;
    bool _dragging;
    double _startX;
    float _startValue;

    /// The region's name, and its value while it drags; null when the pointer leaves.
    public event Action<string?>? Said;

    public HistogramDrag() => Cursor = Sideways;

    static EngineModel E => EngineModel.Shared;
    static bool Editable => E.Handle != 0 && !Chrome.ChromeStubs.DocumentEditsLocked && !Chrome.ChromeStubs.Culling;

    /// The region a point `x` across a plate `width` wide falls in.
    public static int Region(double x, double width) {
        double at = x / Math.Max(width, 1);
        int i = Array.FindIndex(Ends, e => at < e);
        return i < 0 ? Ends.Length - 1 : i;
    }

    public override void Render(DrawingContext c) {
        c.FillRectangle(Brushes.Transparent, new Rect(Bounds.Size));   // the whole plate takes a press
        if (_hot is not { } i) return;
        double from = i == 0 ? 0 : Ends[i - 1];
        c.FillRectangle(Lit, new Rect(from * Bounds.Width, 0, (Ends[i] - from) * Bounds.Width, Bounds.Height));
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        double x = e.GetPosition(this).X;
        if (!_dragging) { Hover(Editable ? Region(x, Bounds.Width) : null); return; }
        var tone = Tones.All[_hot!.Value];
        float v = Math.Clamp(_startValue + (float)((x - _startX) / Math.Max(Bounds.Width, 1)) * (tone.Max - tone.Min), tone.Min, tone.Max);
        if (v != E.Get(tone.Field)) E.Apply(s => s.Set(tone.Field, v), reread: true);
        Say();
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        if (!Editable || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
        _startX = e.GetPosition(this).X;
        Hover(Region(_startX, Bounds.Width));
        _startValue = E.Get(Tones.All[_hot!.Value].Field);
        _dragging = true;
        E.BeginInteraction();
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (!_dragging) return;
        e.Pointer.Capture(null);
        End();
    }

    /// A capture taken mid-drag still ends the drag, so its full render and its undo step come.
    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
        base.OnPointerCaptureLost(e);
        End();
    }

    protected override void OnPointerExited(PointerEventArgs e) {
        if (!_dragging) Hover(null);
    }

    void End() {
        if (!_dragging) return;
        _dragging = false;
        E.EndInteraction();
        Say();
    }

    void Hover(int? region) {
        if (region == _hot) return;
        _hot = region;
        InvalidateVisual();
        Say();
    }

    void Say() {
        if (_hot is not { } i) { Said?.Invoke(null); return; }
        var tone = Tones.All[i];
        string value = E.Get(tone.Field).ToString("+0.00;−0.00;0.00", CultureInfo.InvariantCulture) + (tone.Field == "exposure_ev" ? " EV" : "");
        Said?.Invoke(_dragging ? $"{tone.Name} {value}" : tone.Name);
    }
}

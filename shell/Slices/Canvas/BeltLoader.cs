using Avalonia.Animation.Easings;
using Avalonia.Controls;

namespace OrionShell.CanvasSlice;

/// What the canvas shows while a photo decodes: the brand's loading symbol
/// (brand/app/symbol-animatable.svg, decision #407), its belt stars lighting in
/// turn as the brand's `.orion-loading` keyframes do. It waits a moment before
/// it appears, so a photo that opens fast never flashes it. The stars keep the
/// mark's own colours: the accent is never in the mark.
sealed class BeltLoader : Panel {
    public const double Size = 48;
    /// One star's cycle: dim (0.3) to 15%, up to full by 30%, held to 75%, back down by the end;
    /// each star 0.2 s behind the one before, under CSS's ease-in-out.
    const double Period = 1.6, Delay = 0.2, Dim = 0.3;
    static readonly string[] Stars = ["star-left", "star-mid", "star-right"];
    static readonly SplineEasing EaseInOut = new(0.42, 0, 0.58, 1);
    readonly Artwork _symbol = new("symbol-animatable.svg", Size);
    double _t;

    public BeltLoader() { Width = Height = Size; IsHitTestVisible = false; Opacity = 0; Children.Add(_symbol); }

    public void Reset() { _t = 0; Opacity = 0; Light(); }

    public void Advance(double dt) {
        _t += dt;
        Opacity = Math.Clamp((_t - 0.15) / 0.25, 0, 1);   // nothing for 150 ms, then in over 250
        Light();
    }

    /// Under Reduce Motion the brand holds the middle star lit and the outer two dim.
    void Light() {
        for (int i = 0; i < Stars.Length; i++)
            _symbol.SetOpacity(Stars[i], CanvasSlice.Motion.Reduced ? (i == 1 ? 1 : Dim) : Star(_t - i * Delay));
    }

    /// One star's opacity `t` seconds into its own cycle.
    public static double Star(double t) {
        if (t < 0) return Dim;   // before its delay, the resting dim
        double p = t % Period / Period;
        double rise(double a, double b, double from, double to) => from + (to - from) * EaseInOut.Ease((p - a) / (b - a));
        return p < 0.15 ? Dim : p < 0.30 ? rise(0.15, 0.30, Dim, 1) : p < 0.75 ? 1 : rise(0.75, 1, 1, Dim);
    }
}

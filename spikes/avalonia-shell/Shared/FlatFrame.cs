using Avalonia;
using OrionShell.CanvasSlice;

namespace OrionShell;

/// Engine.flatFrame and Engine+Render.swift's checkFlatFrame: a render that is
/// one constant colour is named in the footer, because a constant is the one
/// thing a real frame never is — sensor noise alone makes five far-apart pixels
/// differ, so all five agreeing means the graph collapsed. It is invisible to
/// every check that does not look at the running app's own picture.
static class FlatFrameCheck {
    /// Five points far apart, and not the corners: a crop or straighten puts
    /// something other than the photograph near an edge.
    static readonly Point[] Probe = [new(0.2, 0.2), new(0.8, 0.25), new(0.5, 0.5), new(0.25, 0.8), new(0.8, 0.8)];

    /// Why the last full render looks flat, or null.
    public static string? Why { get; private set; }

    /// Armed by the footer's first read of Why, which comes before any full render it could miss.
    static FlatFrameCheck() => EngineModel.Shared.Rendered += Check;

    /// The samples read the full graph (orion_engine_sample), so only a full render is judged;
    /// a drag's preview renders leave the verdict of the last one standing.
    static void Check() {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) { Why = null; return; }
        if (engine.Shown.Graph != Graph.Full) return;
        Why = Judge(Probe.Select(p => CanvasNative.Sample(p)?.Display).ToList());
    }

    /// Null unless every sample landed and all agree to 1e-4.
    public static string? Judge(IReadOnlyList<float[]?> samples) {
        if (samples.Count == 0 || samples.Any(s => s == null)) return null;
        var f = samples[0]!;
        if (samples.Skip(1).Any(s => Enumerable.Range(0, 3).Any(c => Math.Abs(s![c] - f[c]) > 1e-4))) return null;
        return string.Create(System.Globalization.CultureInfo.InvariantCulture,
                             $"the render is one flat colour, rgb({f[0]:0.000}, {f[1]:0.000}, {f[2]:0.000})");
    }
}

using System.Runtime.InteropServices;
using Avalonia;

namespace OrionShell;

/// The session calls stage 1b built below the facade (#315), each a port of
/// what the Swift app does through orion.h. Callers go through
/// EngineModel.Apply, which renders afterwards.
/// The signatures are the contract: 1b fills the bodies, nobody else edits them.
public sealed partial class Session {
    const int MaxCurvePoints = 16, MaxMaskComponents = 8;   // ORION_CURVE_MAX_POINTS, ORION_MAX_MASK_COMPONENTS

    /// One channel of the tone curve: 0 master, 1-3 red, green, blue
    /// (OrionAdjustments.curve_master..curve_blue). Up to ORION_CURVE_MAX_POINTS
    /// (16) points, x ascending, both axes 0..1; (0,0),(1,1) is identity, and a
    /// channel never drawn on reads as that.
    public unsafe IReadOnlyList<(float X, float Y)> Curve(int channel) {
        CheckChannel(channel);
        float* xy = stackalloc float[MaxCurvePoints * 2];
        int n = Native.shim_curve(Shim, channel, xy, MaxCurvePoints);
        var points = new (float X, float Y)[n];
        for (int i = 0; i < n; i++) points[i] = (xy[2 * i], xy[2 * i + 1]);
        return points;
    }

    /// Writes one channel of the tone curve. Does not render, as Set does not.
    /// Points past 16 are dropped, as Engine.swift's curveChannel drops them.
    public unsafe void SetCurve(int channel, IReadOnlyList<(float X, float Y)> points) {
        CheckChannel(channel);
        int n = Math.Min(points.Count, MaxCurvePoints);
        float* xy = stackalloc float[MaxCurvePoints * 2];
        for (int i = 0; i < n; i++) (xy[2 * i], xy[2 * i + 1]) = points[i];
        Native.shim_set_curve(Shim, channel, xy, n);
    }

    /// orion_engine_auto_enhance into this session's adjustments: it rewrites
    /// exposure, whites, blacks, lift and clarity. Does not render. A refusal
    /// changes nothing, as Engine.swift's autoEnhance returns on one.
    public void AutoEnhance() => Native.shim_auto_enhance(Shim);

    /// Each MaskShape number by its OrionMaskComponent name: Engine.swift's
    /// cAdjustments row. The selftest holds this to shim_mask_field_count.
    internal static readonly (string Name, Func<MaskShape, float> Value)[] MaskFields = [
        ("kind", m => m.Kind), ("compose", m => m.Compose), ("invert", m => m.Invert ? 1 : 0),
        ("hidden", m => m.Hidden ? 1 : 0), ("starts_layer", m => m.StartsLayer ? 1 : 0),
        ("center_x", m => m.CenterX), ("center_y", m => m.CenterY), ("angle", m => m.Angle), ("length", m => m.Length),
        ("radius_x", m => m.RadiusX), ("radius_y", m => m.RadiusY), ("feather", m => m.Feather), ("roundness", m => m.Roundness),
        ("range_lo", m => m.RangeLo), ("range_hi", m => m.RangeHi), ("range_soft", m => m.RangeSoft),
        ("color_r", m => m.ColorR), ("color_g", m => m.ColorG), ("color_b", m => m.ColorB),
        ("color_tol", m => m.ColorTol), ("color_soft", m => m.ColorSoft),
        ("brush_radius", m => m.BrushRadius), ("brush_flow", m => m.BrushFlow), ("brush_hardness", m => m.BrushHardness),
    ];

    /// The mask group, rows in order: mask_components and mask_count, and each
    /// brush row's dabs (orion_engine_set_brush_stroke, bumping brush_revision
    /// only when the dabs changed). At most ORION_MAX_MASK_COMPONENTS (8) rows.
    /// Mattes (kind 4) stay with SubjectMatte.swift. Does not render.
    public unsafe void SetMaskGroup(IReadOnlyList<MaskShape> shapes) {
        if (shapes.Count > MaxMaskComponents)
            throw new ArgumentException($"at most {MaxMaskComponents} mask rows, not {shapes.Count}", nameof(shapes));
        for (int i = 0; i < MaxMaskComponents; i++) {
            if (i < shapes.Count)
                foreach (var (name, value) in MaskFields)
                    if (Native.shim_mask_set(Shim, i, name, value(shapes[i])) == 0)
                        throw new InvalidOperationException($"OrionMaskComponent has no field '{name}'");
            // Rows past the group are cleared too (Engine+Brush.swift's pushStrokes),
            // or the next row added there would inherit their paint.
            var dabs = i < shapes.Count ? CollectionsMarshal.AsSpan(shapes[i].BrushStroke) : default;
            fixed (float* xy = dabs)
                if (Native.shim_mask_stroke(Shim, i, xy, dabs.Length / 2) == 0)
                    throw new InvalidOperationException($"brush stroke for mask row {i}: {LastError}");
        }
        Native.shim_mask_commit(Shim, shapes.Count);
    }

    /// Keeps the decoded photo and drops both graphs' render textures: what a
    /// cache does to a photo it keeps but is not showing (decision #318: 115 MB
    /// decoded, 1.35 GB more once rendered). The next render rebuilds them; a
    /// Read before it returns null. This session's CPU bitmaps are untouched.
    public void ReleaseRenderMemory() => Native.orion_engine_release_render_memory(Native.shim_engine(Shim));

    /// The preview graph's picture: the image region, not the square texture
    /// it sits in, and what Read(Graph.Preview) returns. Empty when the photo
    /// has no preview graph (Render then falls back to the full one).
    public PixelSize PreviewSize =>
        Native.orion_engine_preview_size(Native.shim_engine(Shim), out uint w, out uint h) == 0 ? new PixelSize((int)w, (int)h) : default;

    static void CheckChannel(int channel) {
        if (channel is < 0 or > 3) throw new ArgumentOutOfRangeException(nameof(channel), channel, "curve channel: 0 master, 1-3 R G B");
    }
}

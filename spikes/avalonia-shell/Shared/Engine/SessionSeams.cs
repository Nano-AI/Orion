using System.Runtime.CompilerServices;
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
    /// cAdjustments row, both ways. The selftest holds this to shim_mask_field_count.
    internal static readonly (string Name, Func<MaskShape, float> Value, Action<MaskShape, float> Put)[] MaskFields = [
        ("kind", m => m.Kind, (m, v) => m.Kind = (int)v), ("compose", m => m.Compose, (m, v) => m.Compose = (int)v),
        ("invert", m => m.Invert ? 1 : 0, (m, v) => m.Invert = v != 0), ("hidden", m => m.Hidden ? 1 : 0, (m, v) => m.Hidden = v != 0),
        ("starts_layer", m => m.StartsLayer ? 1 : 0, (m, v) => m.StartsLayer = v != 0),
        ("center_x", m => m.CenterX, (m, v) => m.CenterX = v), ("center_y", m => m.CenterY, (m, v) => m.CenterY = v),
        ("angle", m => m.Angle, (m, v) => m.Angle = v), ("length", m => m.Length, (m, v) => m.Length = v),
        ("radius_x", m => m.RadiusX, (m, v) => m.RadiusX = v), ("radius_y", m => m.RadiusY, (m, v) => m.RadiusY = v),
        ("feather", m => m.Feather, (m, v) => m.Feather = v), ("roundness", m => m.Roundness, (m, v) => m.Roundness = v),
        ("range_lo", m => m.RangeLo, (m, v) => m.RangeLo = v), ("range_hi", m => m.RangeHi, (m, v) => m.RangeHi = v),
        ("range_soft", m => m.RangeSoft, (m, v) => m.RangeSoft = v),
        ("color_r", m => m.ColorR, (m, v) => m.ColorR = v), ("color_g", m => m.ColorG, (m, v) => m.ColorG = v),
        ("color_b", m => m.ColorB, (m, v) => m.ColorB = v), ("color_tol", m => m.ColorTol, (m, v) => m.ColorTol = v),
        ("color_soft", m => m.ColorSoft, (m, v) => m.ColorSoft = v),
        ("brush_radius", m => m.BrushRadius, (m, v) => m.BrushRadius = v), ("brush_flow", m => m.BrushFlow, (m, v) => m.BrushFlow = v),
        ("brush_hardness", m => m.BrushHardness, (m, v) => m.BrushHardness = v),
    ];

    /// The mask group, rows in order: mask_components and mask_count, and each
    /// brush row's dabs with their erase flags (orion_engine_set_brush_stroke,
    /// a new brush_revision only when the dabs changed). At most
    /// ORION_MAX_MASK_COMPONENTS (8) rows. Mattes (kind 4) stay with
    /// SubjectMatte.swift. Does not render.
    public unsafe void SetMaskGroup(IReadOnlyList<MaskShape> shapes) {
        if (shapes.Count > MaxMaskComponents)
            throw new ArgumentException($"at most {MaxMaskComponents} mask rows, not {shapes.Count}", nameof(shapes));
        for (int i = 0; i < MaxMaskComponents; i++) {
            if (i < shapes.Count)
                foreach (var (name, value, _) in MaskFields)
                    if (Native.shim_mask_set(Shim, i, name, value(shapes[i])) == 0)
                        throw new InvalidOperationException($"OrionMaskComponent has no field '{name}'");
            // Rows past the group are cleared too (Engine+Brush.swift's pushStrokes),
            // or the next row added there would inherit their paint.
            var dabs = i < shapes.Count ? CollectionsMarshal.AsSpan(shapes[i].BrushStroke) : default;
            // Padded rather than trusted, as pushStroke pads: short means "paints".
            var erase = i < shapes.Count ? shapes[i].BrushErase : [];
            while (erase.Count < dabs.Length / 2) erase.Add(0);
            fixed (float* xy = dabs)
            fixed (float* e = CollectionsMarshal.AsSpan(erase))
                if (Native.shim_mask_stroke(Shim, i, xy, e, dabs.Length / 2) == 0)
                    throw new InvalidOperationException($"brush stroke for mask row {i}: {LastError}");
        }
        Native.shim_mask_commit(Shim, shapes.Count);
    }

    /// The mask group as the engine holds it: mask_count rows, every number and
    /// each brush row's dabs and erase flags. Names and matte ids never cross
    /// the facade (#207), so they come back null. What undo restores is read here.
    public unsafe List<MaskShape> ReadMaskGroup() {
        int count = Math.Clamp((int)Get("mask_count"), 0, MaxMaskComponents);
        var rows = new List<MaskShape>(count);
        for (int i = 0; i < count; i++) {
            var m = new MaskShape();
            foreach (var (name, _, put) in MaskFields) put(m, Get("mask_components." + name, i));
            int dabs = Native.shim_mask_stroke_get(Shim, i, null, null, 0);
            var xy = new float[dabs * 2];
            var erase = new float[dabs];
            fixed (float* p = xy) fixed (float* e = erase) Native.shim_mask_stroke_get(Shim, i, p, e, dabs);
            m.BrushStroke.AddRange(xy);
            m.BrushErase.AddRange(erase);
            rows.Add(m);
        }
        return rows;
    }

    /// Paints mask `layer`'s coverage over the renders that follow, or stops.
    /// A way of looking, not an edit (#57): the shim applies it inside each
    /// render only, so no snapshot or export sees it. Does not render.
    public void SetMaskOverlay(bool on, int layer) => Native.shim_mask_overlay(Shim, on ? 1 : 0, layer);

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

/// A brush row's per-dab polarity, 1 erases (MaskComponentState.brushErase in
/// Swift), read as if MaskShape had the member.
/// ponytail: a side table because MaskShape.cs is frozen; once it gains
/// `public List<float> BrushErase { get; } = [];` this class is deleted and no caller changes.
public static class MaskShapeErase {
    static readonly ConditionalWeakTable<MaskShape, List<float>> Erase = new();

    extension(MaskShape m) {
        public List<float> BrushErase => Erase.GetValue(m, _ => []);
    }
}

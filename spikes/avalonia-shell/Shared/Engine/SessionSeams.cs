using Avalonia;

namespace OrionShell;

/// The session calls stage 1b builds below the facade (#315). Each throws
/// NotBuiltException until native/ and engine/ have the C function it needs;
/// callers go through EngineModel.Apply, which puts that on the status line.
/// The signatures are the contract: 1b fills the bodies, nobody else edits them.
public sealed partial class Session {
    /// One channel of the tone curve: 0 master, 1-3 red, green, blue
    /// (OrionAdjustments.curve_master..curve_blue). Up to ORION_CURVE_MAX_POINTS
    /// (16) points, x ascending, both axes 0..1; (0,0),(1,1) is identity.
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public IReadOnlyList<(float X, float Y)> Curve(int channel) {
        CheckChannel(channel);
        throw new NotBuiltException("curve", "Engine.swift");
    }

    /// Writes one channel of the tone curve. Does not render, as Set does not.
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public void SetCurve(int channel, IReadOnlyList<(float X, float Y)> points) {
        CheckChannel(channel);
        throw new NotBuiltException("curve", "Engine.swift");
    }

    /// orion_engine_auto_enhance into this session's adjustments: it rewrites
    /// exposure, whites, blacks, lift and clarity. Does not render.
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public void AutoEnhance() => throw new NotBuiltException("auto-enhance", "Engine.swift");

    /// The mask group, rows in order: mask_components and mask_count, and each
    /// brush row's dabs (orion_engine_set_brush_stroke, bumping brush_revision).
    /// At most ORION_MAX_MASK_COMPONENTS (8) rows. Mattes (kind 4) stay with
    /// SubjectMatte.swift. Does not render.
    // stub(#315): Engine+Mask.swift owns this until the logic moves below the facade
    public void SetMaskGroup(IReadOnlyList<MaskShape> shapes) => throw new NotBuiltException("mask group", "Engine+Mask.swift");

    /// Keeps the decoded photo and drops the graphs' intermediates: what a cache
    /// does to a photo it keeps but is not showing (decision #318: 115 MB
    /// decoded, 1.35 GB more once rendered). The next render rebuilds them.
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public void ReleaseRenderMemory() => throw new NotBuiltException("release render memory", "Engine.swift");

    /// The preview graph's picture: the image region, not the square texture
    /// it sits in. shim_read derives it from the full output texture today, so
    /// a preview read before any full render comes back 2183×2183.
    // stub(#315): Engine+Render.swift owns this until the logic moves below the facade
    public PixelSize PreviewSize => throw new NotBuiltException("preview size", "Engine+Render.swift");

    static void CheckChannel(int channel) {
        if (channel is < 0 or > 3) throw new ArgumentOutOfRangeException(nameof(channel), channel, "curve channel: 0 master, 1-3 R G B");
    }
}

namespace OrionShell;

/// One row of the mask group: MaskComponentState (EditHistory.swift), same
/// defaults. A plain bag of numbers the panel's shape sliders write directly.
/// In Shared because Session.SetMaskGroup takes it (OrionMaskComponent in
/// orion.h, plus the brush dabs and the names the struct does not carry).
public sealed class MaskShape {
    public int Kind, Compose;              // kind: 1 linear, 2 radial, 3 brush, 4 matte, 5 range, 6 color
    public bool Invert, Hidden, StartsLayer;
    public float CenterX = 0.5f, CenterY = 0.5f, Angle, Length = 0.5f;
    public float RadiusX = 0.3f, RadiusY = 0.3f, Feather = 0.5f, Roundness = 2;
    public float RangeLo = -2, RangeHi = 2, RangeSoft = 1;
    public float ColorR = 0.18f, ColorG = 0.18f, ColorB = 0.18f, ColorTol = 0.10f, ColorSoft = 0.05f;
    public float BrushRadius = 0.08f, BrushFlow = 0.5f, BrushHardness = 0.5f;
    public List<float> BrushStroke { get; } = [];   // interleaved x, y
    public string? MatteId, MatteSource, Name;
}

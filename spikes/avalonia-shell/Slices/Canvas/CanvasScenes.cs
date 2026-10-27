using Avalonia;
using Avalonia.Controls;

namespace OrionShell.CanvasSlice;

/// The states the Swift reference scenes are posed in (Screenshot+Scenes.swift),
/// same numbers, applied only when a panel is shown alone with --panel, so its
/// screenshot shows what the reference shows. The window and --selftest never
/// pose: CanvasState.Alone is false there.
static class CanvasScenes {
    /// Every reference scene of this slice brightens the sample by 2.6 EV first.
    static void Exposure() => CanvasState.Write(("exposure_ev", 2.6));

    public static void Canvas() => Exposure();

    /// "crop-angle": the "crop" rectangle with the picture turned 12°.
    public static void CropAngle() {
        CanvasState.Write(("exposure_ev", 2.6), ("straighten_deg", 12));
        CropEdit.SetCrop(new Rect(0.1, 0.08, 0.62, 0.7));
    }

    public static void Crop() {
        CropAngle();
        CanvasState.SetTab("Crop");
    }

    /// "spots": two spots with their sources dragged somewhere deliberate, the second a clone, selected.
    public static void Spots() {
        Exposure();
        CanvasState.SetTab("Detail");
        var fd = CanvasNative.DisplayMap();
        SpotModel.Radius = 0.045;
        SpotModel.Add(new Point(0.36, 0.55), fd);
        SpotModel.Move(0, null, new Point(0.22, 0.74), fd);
        SpotModel.Heal = false;
        SpotModel.Add(new Point(0.62, 0.42), fd);
        SpotModel.Move(1, null, new Point(0.78, 0.60), fd);
        SpotModel.Selected = 1;
    }

    /// "mask-radial-overlay": a radial, turned, with unequal semi-axes.
    public static void Mask() {
        Exposure();
        CanvasState.SetTab("Mask");
        MaskModel.Placement = new MaskPlacement {
            Kind = 2, Center = new Point(0.34, 0.40), Angle = 0.6, Radius = new Size(0.30, 0.14), Feather = 0.10,
        };
    }

    /// "compare": the split at half.
    public static void Compare() {
        Exposure();
        CompareState.SetSplit(0.5);
    }

    /// No Swift scene: 3× about the upper right, so the navigator has a region to show.
    public static void Navigator() => CanvasState.Viewport.ZoomBy(3, new Point(0.62, 0.3), new Size(1, 1));

    /// No Swift scene: the targeted tool armed, the pointer on the sky.
    public static void Loupe(Control loupe) {
        EngineModel.Shared.Tool = CanvasTool.Targeted;
        loupe.SizeChanged += (_, e) =>
            ColorLoupe.Track(CanvasState.Map(e.NewSize).ToView(new Point(0.75, 0.35)), e.NewSize);
    }
}

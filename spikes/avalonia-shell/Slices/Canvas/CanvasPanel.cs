using Avalonia.Controls;
using Avalonia.Media;

namespace OrionShell;

/// The photograph. Order 0, so canvas overlays (crop, mask, spots, compare)
/// register at higher orders and stack over it. Seed from the #315 spike.
[Panel("canvas", Region.Canvas, 0)]
public sealed class CanvasPanel : Image {
    public CanvasPanel() {
        Stretch = Stretch.Uniform;
        Margin = new(Space.Xl);
        RenderOptions.SetBitmapInterpolationMode(this, Avalonia.Media.Imaging.BitmapInterpolationMode.HighQuality);
        Source = EngineModel.Shared.Picture;
        EngineModel.Shared.Rendered += () => {
            Source = EngineModel.Shared.Picture;
            InvalidateVisual();   // same bitmap, new pixels
        };
    }
}

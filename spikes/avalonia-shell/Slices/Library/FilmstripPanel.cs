using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace OrionShell;

/// Seed from the #315 spike: the Library slice replaces it with a port of
/// Filmstrip.swift. Thumbnails are the camera's embedded JPEGs.
[Panel("filmstrip", Region.Filmstrip)]
public sealed class FilmstripPanel : ScrollViewer {
    readonly List<Border> _cells = new();

    public unsafe FilmstripPanel() {
        HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto;
        VerticalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled;
        var row = new StackPanel { Orientation = Avalonia.Layout.Orientation.Horizontal,
                                   Margin = new(Space.Md, 0), VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center };
        var buffer = new byte[8 << 20];
        var engine = EngineModel.Shared;
        for (int i = 0; i < engine.Photos.Count; i++) {
            int index = i, size, turns;
            fixed (byte* p = buffer) size = Native.shim_thumbnail(engine.Photos[i], p, buffer.Length, out turns);
            var cell = new Border {
                Width = Layout.ThumbW, Height = Layout.ThumbH, Margin = new(5, 0),
                CornerRadius = new(Radius.Sm), ClipToBounds = true, BorderThickness = new(1.5),
                Background = Palette.Raised, Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
            };
            if (size > 0) {
                using var stream = new MemoryStream(buffer, 0, size);
                cell.Child = new Image {
                    Source = Bitmap.DecodeToWidth(stream, 192), Stretch = Stretch.UniformToFill,
                    RenderTransform = new RotateTransform(90 * turns),
                };
            }
            cell.PointerPressed += (_, _) => engine.Open(index);
            _cells.Add(cell);
            row.Children.Add(cell);
        }
        Content = row;
        engine.Opened += Highlight;
        Highlight();
    }

    void Highlight() {
        for (int i = 0; i < _cells.Count; i++)
            _cells[i].BorderBrush = i == EngineModel.Shared.Current ? Palette.Accent : Brushes.Transparent;
    }
}

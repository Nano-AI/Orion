using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// A port of app/Filmstrip.swift: browsing and culling, drawn as film. A fixed
/// filter bar on the left (the filter menu, a reject shortcut, the count), a
/// hairline, then the frames on a perforated film base.
///
/// Ratings are dots, not stars: at thumbnail scale a star is mush and five
/// dots are countable. The accent gate is the photograph on the canvas.
[Panel("filmstrip", Region.Filmstrip)]
public sealed class FilmstripPanel : Grid {
    const double CellHeight = 58;

    readonly StackPanel _frames = new() { Orientation = Orientation.Horizontal, Margin = new Thickness(8, 0), VerticalAlignment = VerticalAlignment.Center };
    readonly StackPanel _controls = new() { Orientation = Orientation.Horizontal, Spacing = Space.Sm };
    readonly TextBlock _summary = new() { FontSize = TypeSize.Xs, Foreground = Palette.Faint, Classes = { "tnum" }, TextTrimming = TextTrimming.CharacterEllipsis };
    readonly Dictionary<LibraryModel.Photo, Control> _cells = new();
    readonly Control _picker = LibraryUi.FilterPicker();

    public FilmstripPanel() {
        // 172, fixed: a filter bar that grows when the summary does shoves the
        // strip sideways under the cursor (Filmstrip.swift).
        ColumnDefinitions = new ColumnDefinitions("172,1,*");
        var bar = new StackPanel { Spacing = 5, Margin = new Thickness(12, 0), VerticalAlignment = VerticalAlignment.Center,
                                   Children = { _controls, _summary } };
        Children.Add(new Border { Background = Palette.Panel, Child = bar });
        var line = new Border { Background = Palette.Line };
        SetColumn(line, 1);
        Children.Add(line);
        var film = new ScrollViewer {
            Content = _frames, Background = Palette.FilmBase,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Hidden, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
        SetColumn(film, 2);
        Children.Add(film);

        var library = LibraryModel.Shared;
        library.Changed += Rebuild;
        EngineModel.Shared.Opened += () => {
            Rebuild();
            if (library.Open is { } open && _cells.TryGetValue(open, out var cell)) cell.BringIntoView();
        };
        Rebuild();
    }

    void Rebuild() {
        var library = LibraryModel.Shared;
        _controls.Children.Clear();
        _controls.Children.Add(_picker);
        if (library.Open is { } open) _controls.Children.Add(RejectButton(open));
        _summary.Text = library.Summary;

        _frames.Children.Clear();
        _cells.Clear();
        foreach (var photo in library.Visible) {
            var cell = Cell(photo, photo == library.Open);
            _cells[photo] = cell;
            _frames.Children.Add(cell);
        }
    }

    /// Icon only: the named button over the canvas says it in full, so this
    /// one is a shortcut for the hand already down here.
    static Control RejectButton(LibraryModel.Photo photo) {
        var face = new Border {
            Width = 22, Height = 20, CornerRadius = new CornerRadius(4),
            Background = photo.Rejected ? Palette.Reject : Palette.Raised,
            Child = Icons.Create(photo.Rejected ? "arrow.uturn.backward" : "xmark", 9, photo.Rejected ? Palette.Text : Palette.Dim),
        };
        return LibraryUi.Plain(face, () => LibraryModel.Shared.ToggleRejected(photo),
                               photo.Rejected ? "Keep this photo (R)" : "Reject this photo (R)");
    }

    /// One frame: perforations, the picture inside its gate, perforations.
    /// As wide as the picture at the strip's height, clamped so a panorama
    /// widens gently; no thumbnail is the classic 3:2 stand-in.
    static Control Cell(LibraryModel.Photo photo, bool isCurrent) {
        var image = photo.Thumbnail;
        double aspect = image is { Size.Height: > 0 } ? Math.Clamp(image.Size.Width / image.Size.Height, 0.5, 2.0) : 1.5;
        double width = CellHeight * aspect + 6;

        var picture = new Border {
            ClipToBounds = true, Opacity = photo.Rejected ? 0.4 : 1, Background = Palette.Raised,
            Child = image == null ? null : new Image { Source = image, Stretch = Stretch.UniformToFill },
        };
        // The gate is the image's edge; the base outside it separates frames.
        var gate = new Border {
            BorderBrush = isCurrent ? Palette.Accent : LibraryUi.Ink(Palette.Text, 0.09),
            BorderThickness = new Thickness(isCurrent ? 2 : 1),
        };
        var frame = new Panel { Height = CellHeight, Margin = new Thickness(3, 0), Children = { picture, Marks(photo), gate } };

        var cell = new StackPanel {
            Width = width, Background = Palette.FilmBase, Focusable = true,
            Cursor = new Cursor(StandardCursorType.Hand),
            Children = { new Sprockets(top: true), frame, new Sprockets(top: false) },
        };
        ToolTip.SetTip(cell, photo.Tooltip);
        Avalonia.Automation.AutomationProperties.SetName(cell, LibraryUi.Spoken(photo));
        // Command/shift-click selection, PhotoSelection.
        // stub(#315): Library.swift owns this until the logic moves below the facade
        cell.Tapped += (_, _) => EngineModel.Shared.Open(photo.Index);
        cell.KeyDown += (_, e) => {
            if (e.Key is Key.Enter or Key.Space) { EngineModel.Shared.Open(photo.Index); e.Handled = true; }
        };
        // Merge to HDR… and Export… need a group selection.
        // stub(#315): OrionApp.swift owns this until the logic moves below the facade
        cell.ContextMenu = new ContextMenu { ItemsSource = LibraryUi.CullItems(photo) };
        return cell;
    }

    /// Rejection as a cross, otherwise one dot per star.
    static Control Marks(LibraryModel.Photo photo) {
        var marks = new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(4, 0, 0, 3),
            HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Bottom,
            Effect = LibraryUi.MarkShadow,
        };
        if (photo.Rejected)
            marks.Children.Add(new TextBlock { Text = "✕", FontSize = 9, FontWeight = FontWeight.Bold, Foreground = Palette.Reject });
        else
            for (int i = 0; i < photo.Rating; i++)
                marks.Children.Add(new Ellipse { Width = 4, Height = 4, Fill = Palette.Star });
        return marks;
    }
}

/// The perforated margin for one frame: four KS-1870 holes per frame, the
/// 35 mm pull-down, pitched to the cell so the holes line up with the frame
/// line at every gate and travel with the pictures (Filmstrip.swift).
sealed class Sprockets : Control {
    const double Band = 13, PerFrame = 4;
    static readonly IPen Lip = new Pen(LibraryUi.Ink(Palette.FilmBase, 0.6), 1);
    static readonly IPen Rebate = new Pen(LibraryUi.Ink(Palette.Text, 0.12), 1);
    readonly bool _top;

    public Sprockets(bool top) { _top = top; Height = Band; IsHitTestVisible = false; }

    public override void Render(DrawingContext c) {
        double w = Bounds.Width, pitch = w / PerFrame;
        // A hole is a little under half the pitch, slightly wider than tall.
        double holeW = Math.Min(pitch * 0.42, 10), holeH = holeW * 0.78, y = (Band - holeH) / 2;
        for (int i = 0; i < PerFrame; i++) {
            var hole = new Rect((i + 0.5) * pitch - holeW / 2, y, holeW, holeH);
            c.DrawRectangle(Palette.FilmHole, null, hole, 1.5, 1.5);
            c.DrawLine(Lip, hole.BottomLeft, hole.BottomRight);   // a hole has a thickness
        }
        double edge = _top ? Band - 0.5 : 0.5;   // the rebate, brighter than the base
        c.DrawLine(Rebate, new Point(0, edge), new Point(w, edge));
    }
}

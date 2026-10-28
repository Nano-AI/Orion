using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// A port of app/Filmstrip.swift, drawn as Main.dc.html's strip: a filter bar
/// on the left (what to show, how many), a hairline, then the frames on film
/// base between two rows of perforations.
///
/// Ratings are dots, not stars: at thumbnail scale a star is mush and five
/// dots are countable. The accent ring is the photograph on the canvas.
[Panel("filmstrip", Region.Filmstrip)]
public sealed class FilmstripPanel : Grid {
    const double CellWidth = 96, CellHeight = 62;

    readonly StackPanel _frames = new() { Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(12, 0),
                                          VerticalAlignment = VerticalAlignment.Center };
    readonly TextBlock _summary = new() { FontSize = Faces.Mark, Foreground = Palette.Dim, TextTrimming = TextTrimming.CharacterEllipsis };
    readonly Dictionary<LibraryModel.Photo, Control> _cells = new();

    public FilmstripPanel() {
        ColumnDefinitions = new ColumnDefinitions("Auto,1,*");
        var show = new StackPanel { Spacing = 4, Children = {
            new TextBlock { Text = "Show", FontSize = Faces.Mark, Foreground = Palette.Dim }, LibraryUi.FilterPicker() } };
        var bar = new StackPanel { Spacing = 6, Margin = new Thickness(Space.Pad, 0), VerticalAlignment = VerticalAlignment.Center,
                                   Children = { show, _summary } };
        Children.Add(new Border { Background = Palette.Panel, Child = bar });
        var line = new Border { Background = Palette.Line };
        SetColumn(line, 1);
        Children.Add(line);
        var film = new ScrollViewer {
            Content = _frames,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Hidden, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
        var strip = new DockPanel { Background = Palette.FilmBase, Margin = new Thickness(0, 6) };
        foreach (var dock in new[] { Dock.Top, Dock.Bottom }) {
            var holes = new Perforations();
            DockPanel.SetDock(holes, dock);
            strip.Children.Add(holes);
        }
        strip.Children.Add(film);
        SetColumn(strip, 2);
        Children.Add(strip);

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
        _summary.Inlines = LibraryUi.Summary();
        _frames.Children.Clear();
        _cells.Clear();
        foreach (var photo in library.Visible) {
            var cell = Cell(photo, photo == library.Open);
            _cells[photo] = cell;
            _frames.Children.Add(cell);
        }
    }

    /// One frame, 96 by 62, the picture filling it; the current one ringed in the accent.
    static Control Cell(LibraryModel.Photo photo, bool isCurrent) {
        var picture = new Panel { Opacity = photo.Rejected ? 0.4 : 1, Children = {
            photo.Thumbnail is { } image ? new Image { Source = image, Stretch = Stretch.UniformToFill } : new Panel(),
        } };
        var cell = new Border {
            Width = CellWidth, Height = CellHeight, CornerRadius = new CornerRadius(Radius.Sm), ClipToBounds = true,
            Background = Palette.Ground, BorderThickness = new Thickness(2),
            BorderBrush = isCurrent ? Palette.Accent : Brushes.Transparent, Focusable = true, Cursor = new Cursor(StandardCursorType.Hand),
            Child = new Panel { Children = { picture, Marks(photo) } },
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

    /// Rejection as a cross at the top left (Controls.dc.html), otherwise one dot per star.
    static Control Marks(LibraryModel.Photo photo) {
        if (photo.Rejected) {
            var cross = Icons.Create("xmark", 10, Palette.Reject);
            cross.StrokeThickness = 3;
            (cross.Margin, cross.HorizontalAlignment, cross.VerticalAlignment) = (new Thickness(4), HorizontalAlignment.Left, VerticalAlignment.Top);
            return cross;
        }
        var marks = new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(4, 0, 0, 4),
            HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Bottom,
            Effect = LibraryUi.MarkShadow,
        };
        for (int i = 0; i < photo.Rating; i++) marks.Children.Add(new Ellipse { Width = 4, Height = 4, Fill = Palette.Star });
        return marks;
    }
}

/// A row of perforations along the film's edge: 7 wide every 16, the base's
/// light showing through (Main.dc.html). Fixed to the strip, not to the frames.
sealed class Perforations : Control {
    public Perforations() { Height = 6; IsHitTestVisible = false; }

    public override void Render(DrawingContext c) {
        for (double x = 0; x < Bounds.Width; x += 16) c.FillRectangle(Palette.FilmHole, new Rect(x, 0, Math.Min(7, Bounds.Width - x), 6));
    }
}

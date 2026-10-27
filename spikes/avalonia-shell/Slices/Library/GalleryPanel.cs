using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace OrionShell.Library;

/// A port of app/GalleryView.swift: every photograph in the folder at once,
/// for culling. A contact sheet, not a second editor: a click moves focus
/// without decoding, and only a double-click opens a raw. Cells are 3:2 and
/// the picture is letterboxed inside, never cropped, because the question
/// this view answers is framing.
[Panel("gallery", Region.Sheet)]
public sealed class GalleryPanel : DockPanel {
    readonly GalleryGrid _grid = new();
    readonly TextBlock _summary = new() { FontSize = TypeSize.Sm, Foreground = Palette.Faint, Classes = { "tnum" }, VerticalAlignment = VerticalAlignment.Center };
    readonly TextBlock _deleteLabel = new() { Text = "Delete Rejected…", FontSize = TypeSize.Sm };
    readonly Button _delete;
    readonly Dictionary<LibraryModel.Photo, Control> _cells = new();
    readonly List<Border> _pictures = new();
    /// The photo the keys act on: Editor.focused in Swift, distinct from the
    /// one decoded on the canvas. Starts on the photograph being edited.
    LibraryModel.Photo? _focused = LibraryModel.Shared.Open;

    public GalleryPanel() {
        Background = Palette.Ground;
        // Editor.confirmTrash, then moveToTrash.
        // stub(#315): Library.swift owns this until the logic moves below the facade
        _delete = LibraryUi.Plain(_deleteLabel, () => { });

        var header = new DockPanel { Height = 34, Background = Palette.Panel };
        var left = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Md, Margin = new Thickness(12, 0, 0, 0),
                                    Children = { LibraryUi.FilterPicker(), _summary } };
        var right = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Md, Margin = new Thickness(0, 0, 12, 0),
                                     Children = { _delete, SizeSlider() } };
        SetDock(right, Dock.Right);
        header.Children.Add(right);
        header.Children.Add(left);
        SetDock(header, Dock.Top);
        Children.Add(header);
        var line = new Border { Height = 1, Background = Palette.Line };
        SetDock(line, Dock.Top);
        Children.Add(line);
        Children.Add(new ScrollViewer { Content = _grid, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled });

        LibraryModel.Shared.Changed += Rebuild;
        EngineModel.Shared.Opened += Rebuild;
        Rebuild();
        // Opening the gallery lands on the photograph being edited, once the
        // grid has laid out enough to know where its cell is.
        AttachedToVisualTree += (_, _) => Dispatcher.UIThread.Post(() => {
            if (_focused != null && _cells.TryGetValue(_focused, out var cell)) cell.BringIntoView();
        });
    }

    /// Trades photos per screen against the detail a framing judgement needs.
    /// Unlabelled but for the two glyphs. The size is not remembered: Swift keeps it in @AppStorage("galleryCellSize").
    // stub(#315): GalleryView.swift owns this until the logic moves below the facade
    Control SizeSlider() {
        var slider = new Slider {
            Minimum = GalleryGrid.MinCell, Maximum = GalleryGrid.MaxCell, Value = GalleryGrid.DefaultCell,
            Width = 110, VerticalAlignment = VerticalAlignment.Center,
        };
        // .controlSize(.mini) in the Palette: a slim groove and a small pill.
        var r = slider.Resources;
        r["SliderHorizontalHeight"] = 16.0;
        r["SliderTrackThemeHeight"] = 4.0;
        r["SliderHorizontalThumbWidth"] = 16.0;
        r["SliderHorizontalThumbHeight"] = 12.0;
        r["SliderThumbCornerRadius"] = new CornerRadius(6);
        foreach (var state in new[] { "", "PointerOver", "Pressed" }) {
            r["SliderTrackFill" + state] = Palette.Raised;
            r["SliderTrackValueFill" + state] = Palette.Faint;
            r["SliderThumbBackground" + state] = Palette.Text;
        }
        slider.PropertyChanged += (_, e) => { if (e.Property != RangeBase.ValueProperty) return;
            _grid.Cell = slider.Value;
            foreach (var picture in _pictures) picture.Height = _grid.Cell / GalleryGrid.CellAspect;
        };
        var group = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 5, VerticalAlignment = VerticalAlignment.Center,
            Children = { Icons.Create("square.grid.3x3", 9, Palette.Faint), slider, Icons.Create("square.grid.2x2", 12, Palette.Faint) } };
        ToolTip.SetTip(group, "Cell size");
        return group;
    }

    void Rebuild() {
        var library = LibraryModel.Shared;
        _summary.Text = library.Summary;
        int rejected = library.RejectedCount;
        _deleteLabel.Foreground = rejected > 0 ? Palette.Reject : Palette.Faint;
        _delete.IsEnabled = rejected > 0;
        ToolTip.SetTip(_delete, rejected > 0
            ? $"Move the {rejected} rejected photo{(rejected == 1 ? "" : "s")} to the Trash" : "No rejected photos");

        _grid.Children.Clear();
        _cells.Clear();
        _pictures.Clear();
        foreach (var photo in library.Visible) {
            var cell = Cell(photo);
            _cells[photo] = cell;
            _grid.Children.Add(cell);
        }
    }

    Control Cell(LibraryModel.Photo photo) {
        bool isFocused = photo == _focused;
        var picture = new Border {
            Height = _grid.Cell / GalleryGrid.CellAspect, Background = Palette.FilmBase, Opacity = photo.Rejected ? 0.35 : 1,
            Child = photo.Thumbnail is { } image ? new Image { Source = image, Stretch = Stretch.Uniform }
                : new Border { Background = Palette.Raised, Width = _grid.Cell, HorizontalAlignment = HorizontalAlignment.Center },
        };
        _pictures.Add(picture);
        var ring = new Border {
            CornerRadius = new CornerRadius(Radius.Sm),
            BorderBrush = isFocused ? Palette.Accent : LibraryUi.Ink(Palette.Text, 0.08),
            BorderThickness = new Thickness(isFocused ? 2 : 1),
        };
        var name = new TextBlock {
            Text = photo.Name, FontSize = TypeSize.Xs, Foreground = isFocused ? Palette.Text : Palette.Dim,
            HorizontalAlignment = HorizontalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis,
        };
        var cell = new StackPanel {
            Spacing = 4, Background = Brushes.Transparent, Focusable = true,
            Children = { new Panel { Children = { picture, Marks(photo), ring } }, name },
        };
        ToolTip.SetTip(cell, photo.Tooltip);
        Avalonia.Automation.AutomationProperties.SetName(cell, LibraryUi.Spoken(photo));
        // A plain click focuses; it never decodes. Opening is the double-click's job.
        // Command/shift-click selection, PhotoSelection.
        // stub(#315): Library.swift owns this until the logic moves below the facade
        cell.Tapped += (_, _) => { _focused = photo; Rebuild(); };
        // onOpen also leaves the gallery for develop (EditorMode).
        // stub(#315): OrionApp.swift owns this until the logic moves below the facade
        cell.DoubleTapped += (_, _) => EngineModel.Shared.Open(photo.Index);
        cell.KeyDown += (_, e) => { if (e.Key == Key.Enter) { EngineModel.Shared.Open(photo.Index); e.Handled = true; } };
        cell.ContextMenu = new ContextMenu { ItemsSource = Menu(photo) };
        return cell;
    }

    static List<Control> Menu(LibraryModel.Photo photo) {
        var items = new List<Control> { LibraryUi.Item("Open in Editor", () => EngineModel.Shared.Open(photo.Index)), new Separator() };
        items.AddRange(LibraryUi.CullItems(photo));
        items.Add(new Separator());
        // Editor.confirmTrash, then moveToTrash.
        // stub(#315): Library.swift owns this until the logic moves below the facade
        items.Add(LibraryUi.Item("Move to Trash…", () => { }));
        return items;
    }

    /// Stars here, not the strip's dots: at gallery size a star is legible.
    static Control Marks(LibraryModel.Photo photo) {
        var marks = new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(6, 0, 0, 5),
            HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Bottom,
            Effect = LibraryUi.MarkShadow,
        };
        if (photo.Rejected)
            marks.Children.Add(Icons.Create("xmark", 11, Palette.Reject));
        else
            for (int i = 0; i < photo.Rating; i++)
                marks.Children.Add(Icons.Create("star.fill", 9, Palette.Star));
        return marks;
    }
}

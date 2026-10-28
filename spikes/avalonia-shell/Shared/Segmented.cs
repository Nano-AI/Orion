using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell;

/// One choice of a few, side by side, as macOS draws a segmented control: a
/// ground well with no edge, the cells sharing it evenly, the chosen one a
/// filled pill inset by the well's padding, and a short hairline between two
/// cells that are both unchosen. Each cell is a StateButton, so hover, press,
/// focus and disabled come with it. A cell may carry a count in the numeral
/// face (the library filter's All 3, Rated 1).
public sealed class Segmented : Border {
    /// A cell's height; the well is this plus its padding and never stretches,
    /// so the pill always fills it (a stretched well left the pill floating).
    const double CellHeight = 24, Inset = 2;
    static readonly IPen Separator = new Pen(Palette.Line, 1);

    readonly UniformGrid _cells = new() { Rows = 1 };
    readonly List<TextBlock> _figures = new();
    readonly Hairlines? _hairlines;
    int _selected;

    /// A cell was picked by hand (not by setting Selected).
    public event Action<int>? Changed;

    /// `icons`: an SF Symbol name per cell, or null for its words. An icon cell
    /// shows the icon (and its count), and keeps its title as tooltip and spoken name;
    /// with `words`, it shows the icon beside its title instead (Library, Develop).
    public Segmented(IReadOnlyList<string> titles, int selected, IReadOnlyList<string?>? icons = null, bool words = false) {
        Background = Palette.Ground;
        CornerRadius = new CornerRadius(Radius.Md + 1);   // concentric: the pill's radius plus the inset
        Padding = new Thickness(Inset);
        Height = CellHeight + 2 * Inset;
        VerticalAlignment = VerticalAlignment.Center;
        _hairlines = new Hairlines(this);
        Child = new Panel { Children = { _hairlines, _cells } };
        for (int i = 0; i < titles.Count; i++) {
            int index = i;
            var figure = Faces.Figure("", Faces.Body, Palette.Dim);
            figure.Tag = StateButton.Keep;
            figure.IsVisible = false;
            _figures.Add(figure);
            // A title that is a number (1:1, 16:9) is set in the numeral face.
            string? icon = icons != null && i < icons.Count ? icons[i] : null;
            Control text = char.IsDigit(titles[i][0]) ? Faces.Figure(titles[i], Faces.Body)
                : new TextBlock { Text = titles[i], FontSize = Faces.Body, VerticalAlignment = VerticalAlignment.Center };
            Control title = icon == null ? text
                : words ? new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { Icons.Create(icon, 13, Palette.Text), text } }
                : Icons.Create(icon, 11, Palette.Text);
            var cell = new StateButton {
                Kind = ButtonKind.Segment, Height = CellHeight, MinWidth = 0, Padding = new Thickness(12, 0),
                HorizontalAlignment = HorizontalAlignment.Stretch,
                CornerRadius = new CornerRadius(Radius.Md - 1),
                Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { title, figure } },
            };
            if (icon != null && !words) {
                ToolTip.SetTip(cell, titles[i]);
                Avalonia.Automation.AutomationProperties.SetName(cell, titles[i]);
            }
            cell.Click += (_, _) => { if (index != _selected) { Selected = index; Changed?.Invoke(index); } };
            _cells.Children.Add(cell);
        }
        Selected = selected;
    }

    public int Selected {
        get => _selected;
        set {
            _selected = value;
            for (int i = 0; i < _cells.Children.Count; i++) ((StateButton)_cells.Children[i]).On = i == value;
            _hairlines?.InvalidateVisual();   // the hairlines either side of the pill go
        }
    }

    /// Room inside each cell; narrow it when many cells share a tool column.
    public Thickness CellPadding { set { foreach (var cell in _cells.Children) ((StateButton)cell).Padding = value; } }

    /// A count after each title; null hides that cell's.
    public void Figures(params string?[] figures) {
        for (int i = 0; i < _figures.Count; i++) {
            _figures[i].Text = i < figures.Length ? figures[i] : null;
            _figures[i].IsVisible = !string.IsNullOrEmpty(_figures[i].Text);
        }
        _hairlines?.InvalidateVisual();
    }

    /// A hairline between each pair of unchosen neighbours, under the cells, so
    /// a hovered cell's fill covers its own edges. (Border.Render is sealed.)
    sealed class Hairlines(Segmented owner) : Control {
        public override void Render(DrawingContext context) {
            var cells = owner._cells.Children;
            for (int i = 1; i < cells.Count; i++) {
                if (i == owner._selected || i - 1 == owner._selected) continue;
                double x = Math.Round(cells[i].Bounds.X) + 0.5, mid = Bounds.Height / 2;
                context.DrawLine(Separator, new Point(x, mid - 6), new Point(x, mid + 6));
            }
        }
    }
}

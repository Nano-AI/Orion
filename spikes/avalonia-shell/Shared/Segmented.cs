using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell;

/// One choice of a few, side by side (Main.dc.html's Library/Develop switch):
/// equal cells in a ground well, the chosen one raised. Each cell is a
/// StateButton, so hover, press, focus and disabled come with it. A cell may
/// carry a count in the numeral face (the gallery's All 3 · Rated 1).
public sealed class Segmented : Border {
    readonly UniformGrid _cells = new() { Rows = 1 };
    readonly List<TextBlock> _figures = new();
    int _selected;

    /// A cell was picked by hand (not by setting Selected).
    public event Action<int>? Changed;

    public Segmented(IReadOnlyList<string> titles, int selected) {
        Background = Palette.Ground;
        BorderBrush = Palette.Line;
        BorderThickness = new Thickness(1);
        CornerRadius = new CornerRadius(Radius.Md + 1);
        Padding = new Thickness(2);
        Child = _cells;
        for (int i = 0; i < titles.Count; i++) {
            int index = i;
            var figure = Faces.Figure("", Faces.Body, Palette.Dim);
            figure.Tag = StateButton.Keep;
            figure.IsVisible = false;
            _figures.Add(figure);
            // A title that is a number (1:1, 16:9) is set in the numeral face.
            var title = char.IsDigit(titles[i][0]) ? Faces.Figure(titles[i], Faces.Body)
                : new TextBlock { Text = titles[i], FontSize = Faces.Body, VerticalAlignment = VerticalAlignment.Center };
            var cell = new StateButton {
                Kind = ButtonKind.Segment, Height = 24, MinWidth = 0, Padding = new Thickness(12, 0),
                CornerRadius = new CornerRadius(Radius.Md - 1),
                Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 5, Children = { title, figure } },
            };
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
    }
}

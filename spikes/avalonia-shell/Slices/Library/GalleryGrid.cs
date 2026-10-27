using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;

namespace OrionShell.Library;

/// app/GalleryLayout.swift's column rule as a layout panel: as many flexible
/// columns as cells of `Cell` points fit, never fewer than one, with
/// `Spacing` between cells and around the grid (LazyVGrid + .padding).
sealed class GalleryGrid : Panel {
    /// The cell-size slider's range: 200 is the smallest cell on which framing
    /// is still judgeable, 400 at 2x is what a 1024-edge thumbnail covers.
    public const double MinCell = 200, MaxCell = 400, DefaultCell = 260, Spacing = 10, CellAspect = 1.5;

    static GalleryGrid() {
        // ViewportTests+Gallery.swift's cases.
        Debug.Assert(Columns(800, 260) == 3 && Columns(799, 260) == 2 && Columns(100, 260) == 1 &&
                     Columns(0, 260) == 1 && Columns(800, 0) == 1, "GalleryGrid.Columns disagrees with GalleryLayout.columns");
    }

    /// n cells and (n - 1) gaps: n·cell + (n − 1)·spacing ≤ width.
    public static int Columns(double width, double cell, double spacing = Spacing) =>
        width > 0 && cell > 0 ? Math.Max(1, (int)((width + spacing) / (cell + spacing))) : 1;

    double _cell = DefaultCell;
    public double Cell {
        get => _cell;
        set { _cell = value; InvalidateMeasure(); }
    }

    /// How many columns the last layout used.
    public int ColumnCount { get; private set; } = 1;
    readonly List<double> _rows = new();
    double _columnWidth;

    protected override Size MeasureOverride(Size available) {
        double width = double.IsFinite(available.Width) ? available.Width : Cell * 4 + Spacing * 5;
        ColumnCount = Columns(width - Spacing * 2, Cell);
        _columnWidth = Math.Max(0, (width - Spacing * (ColumnCount + 1)) / ColumnCount);
        _rows.Clear();
        double height = Spacing;
        for (int i = 0; i < Children.Count; i += ColumnCount) {
            double row = 0;
            for (int j = i; j < Math.Min(i + ColumnCount, Children.Count); j++) {
                Children[j].Measure(new Size(_columnWidth, double.PositiveInfinity));
                row = Math.Max(row, Children[j].DesiredSize.Height);
            }
            _rows.Add(row);
            height += row + Spacing;
        }
        return new Size(width, height);
    }

    protected override Size ArrangeOverride(Size final) {
        double y = Spacing;
        for (int i = 0; i < Children.Count; i++) {
            int column = i % ColumnCount, row = i / ColumnCount;
            if (column == 0 && row > 0) y += _rows[row - 1] + Spacing;
            Children[i].Arrange(new Rect(Spacing + column * (_columnWidth + Spacing), y, _columnWidth, _rows[row]));
        }
        return final;
    }
}

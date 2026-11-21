using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.TextFormatting;

namespace OrionShell;

/// The tab cells and the tool column's widest, from the face the tab names are
/// drawn in (#403). Every tab shows its icon over its name. At 322 each cell is
/// at least its name wide, and the names share what is left of the bar evenly
/// as room each side: `TabPadding` at most, never under `TabRoomMin`; a face
/// whose names need more than 322 at that raises the column's floor. At the
/// widest the seven cells are equal, with `TabRoom` each side of the widest
/// name: 560 where that holds, else wider, up to `ToolColCap`.
public partial class MainWindow {
    public const double ToolColBase = 560, ToolColCap = 600, TabRoom = 7.75, TabRoomMin = 4;
    /// A cell's room each side of its name at most, off the scale on purpose.
    const double TabPadding = 8;

    /// The column's widest for the face on screen now, and the room each side of a name at 322.
    public double ToolColMax { get; private set; } = ToolColBase;
    public double TabRoomNow { get; private set; } = TabPadding;
    /// Which case the measurement hit, in words, for the selftest to report.
    public string ToolColFit { get; private set; } = "not measured";

    /// Measured once the bar is built, and again whenever the names' face changes.
    void WatchTabFace() {
        FitToolColumn();
        Words(_tabButtons[0]).PropertyChanged += (_, e) => {
            if (e.Property == TextBlock.FontFamilyProperty || e.Property == TextBlock.FontSizeProperty
                || e.Property == TextBlock.FontWeightProperty || e.Property == TextBlock.FontStyleProperty
                || e.Property == TextBlock.FontStretchProperty)
                FitToolColumn();
        };
    }

    /// From the real TextLayout of each name, never an estimate. Every width is whole
    /// points, so layout rounding cannot make a cell a fraction narrower than its floor.
    void FitToolColumn() {
        var first = Words(_tabButtons[0]);
        var face = new Typeface(first.FontFamily, first.FontStyle, first.FontWeight, first.FontStretch);
        var icon = ((Panel)_tabButtons[0].Content!).Children[0];
        icon.Measure(Size.Infinity);
        var content = _tabButtons.Select(b => Math.Ceiling(Math.Max(icon.DesiredSize.Width,
            new TextLayout(Words(b).Text, face, first.FontSize, null).WidthIncludingTrailingWhitespace))).ToArray();
        double chrome = TabWell.Margin.Left + TabWell.Margin.Right + TabWell.Padding.Left + TabWell.Padding.Right;
        int cells = content.Length;

        double spare = Math.Floor((Layout.ToolCol - chrome - content.Sum()) / (2 * cells));
        double padding = Math.Clamp(spare, TabRoomMin, TabPadding);
        double floor = Math.Max(Layout.ToolCol, content.Sum() + 2 * cells * TabRoomMin + chrome);
        double need = cells * Math.Ceiling(content.Max() + 2 * TabRoom) + chrome;
        ToolColMax = Math.Clamp(need, ToolColBase, ToolColCap);
        ToolColFit = (spare < TabRoomMin ? $"the names do not fit {Layout.ToolCol} even at {TabRoomMin} each side: the floor widened to {floor}"
                                         : $"{padding} pt each side of a name at {Layout.ToolCol}")
                   + (need <= ToolColBase ? $", fits {ToolColBase}" : need <= ToolColCap ? $", widened to {need}" : $", does not fit {ToolColCap}")
                   + $"; {content.Sum()} pt of names, the widest {content.Max()}";
        TabRoomNow = padding;
        for (int i = 0; i < cells; i++) {
            _tabButtons[i].Padding = new Thickness(padding, Space.Gap);
            TabBar.ColumnDefinitions[i].MinWidth = content[i] + 2 * padding;
        }
        Body.ColumnDefinitions[2].MinWidth = floor;
        Body.ColumnDefinitions[2].MaxWidth = ToolColMax;
    }
}

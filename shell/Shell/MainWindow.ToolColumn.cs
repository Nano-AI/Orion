using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.TextFormatting;

namespace OrionShell;

/// The tool column's widest, from the face the tab names are drawn in (#403).
/// At its widest the seven tabs are equal cells, and the chosen one shows its
/// icon and name with `TabRoom` spare each side. 560 holds that in SF Pro and
/// Segoe UI; fontconfig's default on Linux (DejaVu Sans here) draws "Presets"
/// wider, so the column may widen to `ToolColCap`. Past that the room shrinks,
/// evenly, never under `TabRoomMin`.
public partial class MainWindow {
    public const double ToolColBase = 560, ToolColCap = 600, TabRoom = 7.75, TabRoomMin = 4;
    /// The cells' padding at 560, off the scale on purpose: 8 each side holds TabRoom with a quarter to spare.
    const double TabPadding = 8;

    /// The column's widest for the face on screen now, and the room each side of the widest tab at it.
    public double ToolColMax { get; private set; } = ToolColBase;
    public double TabRoomNow { get; private set; } = TabRoom;
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
    /// points, so layout rounding cannot make one cell a fraction wider than the rest.
    void FitToolColumn() {
        var first = Words(_tabButtons[0]);
        var face = new Typeface(first.FontFamily, first.FontStyle, first.FontWeight, first.FontStretch);
        double name = _tabButtons.Max(b => new TextLayout(Words(b).Text, face, first.FontSize, null).WidthIncludingTrailingWhitespace);
        var content = (StackPanel)_tabButtons[0].Content!;
        var icon = content.Children[0];
        icon.Measure(Size.Infinity);
        double widest = Math.Ceiling(icon.DesiredSize.Width + content.Spacing + name);
        double chrome = TabWell.Margin.Left + TabWell.Margin.Right + TabWell.Padding.Left + TabWell.Padding.Right;
        int cells = _tabButtons.Count;
        double need = cells * Math.Ceiling(widest + 2 * TabRoom) + chrome;

        double room = TabRoom, padding = TabPadding;
        if (need <= ToolColBase) {
            ToolColMax = ToolColBase;
            ToolColFit = $"fits {ToolColBase}";
        } else if (need <= ToolColCap) {
            ToolColMax = need;
            padding = TabRoom;
            ToolColFit = $"widened to {need} for a {widest} pt tab";
        } else {
            ToolColMax = ToolColCap;
            // Against the narrowest cell: seven cells of 600 are not whole points, and layout rounding makes some a point narrower.
            double spare = Math.Floor((Math.Floor((ToolColCap - chrome) / cells) - widest) / 2 * 4) / 4;   // quarter points
            room = padding = Math.Max(TabRoomMin, spare);
            ToolColFit = spare >= TabRoomMin ? $"{ToolColCap} with room shrunk to {room} for a {widest} pt tab"
                                              : $"does not fit {ToolColCap} even at {TabRoomMin} for a {widest} pt tab";
        }
        TabRoomNow = room;
        foreach (var b in _tabButtons) b.Padding = new Thickness(padding, 0);
        Body.ColumnDefinitions[2].MaxWidth = ToolColMax;
        int on = Array.IndexOf(Panels.Tabs, Tab);   // the chosen cell's minimum follows its padding
        _tabButtons[on].InvalidateMeasure();
        _tabButtons[on].Measure(Size.Infinity);
        TabBar.ColumnDefinitions[on].MinWidth = _tabButtons[on].DesiredSize.Width;
    }
}

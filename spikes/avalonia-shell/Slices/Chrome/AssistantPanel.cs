using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Chrome;

/// A port of AssistantPanel.swift's AssistantColumn: the photographer's coding
/// agent in a column at the window's leading edge. A header (which agent,
/// restart, tools folder, close), a hairline, the terminal, and a handle on
/// the trailing edge that drags the column between 360 and 800 wide.
[Panel("assistant", Region.Sheet)]
public sealed class AssistantPanel : DockPanel {
    // AssistantPanelModel's defaults (AssistantProcess.swift).
    static readonly string[] Commands = ["claude", "codex"];
    const double Narrowest = 360, Widest = 800;

    readonly DockPanel _column = new() { Width = Narrowest, Background = Palette.Panel };
    double? _dragStartWidth, _dragStartX;

    public AssistantPanel() {
        HorizontalAlignment = HorizontalAlignment.Left;
        var hairline = new Border { Height = 1, Background = Palette.Line };
        DockPanel.SetDock(hairline, Dock.Top);
        var header = Header();
        DockPanel.SetDock(header, Dock.Top);
        _column.Children.Add(header);
        _column.Children.Add(hairline);
        // SwiftTerm's LocalProcessTerminalView running `/bin/zsh -l -i -c claude`:
        // a PTY and a terminal emulator, neither of which this shell has.
        // stub(#315): AssistantPanel.swift owns this until the logic moves below the facade
        _column.Children.Add(new Border { Background = Palette.Ground });

        var handle = Handle();
        DockPanel.SetDock(handle, Dock.Right);
        Children.Add(handle);
        Children.Add(_column);
    }

    Control Header() {
        var picker = new ComboBox {
            ItemsSource = Commands, SelectedIndex = 0, Width = 110, Height = 22, MinHeight = 0,
            FontSize = TypeSize.Base, Padding = new Thickness(8, 0), VerticalAlignment = VerticalAlignment.Center,
            Background = Palette.Raised, BorderBrush = Palette.Line, CornerRadius = new CornerRadius(Radius.Md),
        };
        // stub(#315): AssistantProcess.swift owns this until the logic moves below the facade
        static void Restart() { }                       // restartToken = UUID(): the terminal relaunches
        // stub(#315): AssistantProcess.swift owns this until the logic moves below the facade
        static void ChooseToolsFolder() { }             // NSOpenPanel, then setWorkingDirectory
        // stub(#315): AssistantProcess.swift owns this until the logic moves below the facade
        static void Close() { }                         // isOpen = false; the window drops the column
        // AssistantPanelModel.notice and toolsQuarantined (the Allow button)
        // read .mcp.json and the bundle's quarantine flag: nil and false here.

        var close = Glyph("xmark", "Close", Close);
        DockPanel.SetDock(close, Dock.Right);
        var row = new DockPanel { Height = 28, Margin = new Thickness(Space.Md, 0) };
        row.Children.Add(close);
        row.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, VerticalAlignment = VerticalAlignment.Center,
            Children = {
                picker,
                Glyph("arrow.clockwise", "Restart", Restart),
                Glyph("folder.badge.gearshape", "Choose the assistant’s working folder (.mcp.json)", ChooseToolsFolder),
            } });
        return row;
    }

    /// An icon-only `.plain` button in dim ink.
    static Chip Glyph(string symbol, string help, Action action) =>
        new(Icons.Create(symbol, 13, Palette.Dim), help, action) { BorderThickness = new Thickness(0), Padding = new Thickness(2, 0) };

    /// Wider than the line it draws, for an easier drag target.
    Control Handle() {
        var handle = new Border {
            Width = 8, Background = Brushes.Transparent, Cursor = new Cursor(StandardCursorType.SizeWestEast),
            Child = new Border { Width = 1, Background = Palette.Line, HorizontalAlignment = HorizontalAlignment.Center },
        };
        handle.PointerPressed += (_, e) => {
            (_dragStartWidth, _dragStartX) = (_column.Width, e.GetPosition(this).X);
            e.Pointer.Capture(handle);
        };
        handle.PointerMoved += (_, e) => {
            if (_dragStartWidth is not { } start || _dragStartX is not { } x) return;
            // The column is at the leading edge, so dragging right widens it.
            _column.Width = Math.Clamp(start + e.GetPosition(this).X - x, Narrowest, Widest);
        };
        handle.PointerReleased += (_, e) => { _dragStartWidth = _dragStartX = null; e.Pointer.Capture(null); };
        return handle;
    }
}

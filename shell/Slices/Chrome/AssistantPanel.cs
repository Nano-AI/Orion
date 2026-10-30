using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.VisualTree;
using Iciclecreek.Terminal;

namespace OrionShell.Chrome;

/// A port of AssistantPanel.swift's AssistantColumn: the photographer's coding
/// agent in a column at the window's leading edge. A header (which agent,
/// restart, tools folder, the notice, close), a hairline, the terminal, and a
/// handle on the trailing edge that drags the column between 360 and 800 wide.
///
/// The terminal is Iciclecreek.Avalonia.Terminal (MIT): an xterm emulator over
/// a real PTY, as SwiftTerm's LocalProcessTerminalView is in Swift, running
/// `/bin/zsh -l -i -c 'exec "$0" "$@"' claude …` in the working directory.
/// zsh always starts, so a missing agent is "command not found" in the
/// terminal rather than a launch failure.
[Panel("assistant", Region.Sheet)]
public sealed class AssistantPanel : DockPanel {
    static AssistantModel Model => AssistantModel.Shared;

    readonly DockPanel _column = new() { Background = Palette.Panel };
    readonly TerminalControl _terminal = new() {
        Background = Palette.Ground, Foreground = Palette.Text, BufferSize = 5000,
        HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Stretch,
    };
    readonly ComboBox _picker;
    readonly TextBlock _notice = new() { FontSize = Faces.Mark, Foreground = Palette.Dim, TextTrimming = TextTrimming.CharacterEllipsis,
                                         VerticalAlignment = VerticalAlignment.Center };
    readonly Button _allow;
    (string Command, string Directory, int Token)? _launched;
    double _font;
    double? _dragStartWidth, _dragStartX;
    IInputElement? _priorFocus;
    bool _loaded;   // the terminal's template is applied: it can take a process

    public AssistantPanel() {
        HorizontalAlignment = HorizontalAlignment.Left;
        _picker = new ComboBox {
            ItemsSource = AssistantModel.Commands, SelectedItem = Model.Command, Width = 110, Height = 22, MinHeight = 0,
            FontSize = Faces.Label, Padding = new Thickness(Space.Md, 0), VerticalAlignment = VerticalAlignment.Center,
            Background = Palette.Raised, BorderBrush = Palette.Line, CornerRadius = new CornerRadius(Radius.Md),
        };
        _picker.SelectionChanged += (_, _) => { if (_picker.SelectedItem is string c) Model.SetCommand(c); };
        _allow = Sheets.SheetKit.Button("Allow", Model.AllowTools);
        ToolTip.SetTip(_allow, "Clear the download’s quarantine from Orion so the assistant can run its tools");

        var hairline = new Border { Height = 1, Background = Palette.Line };
        DockPanel.SetDock(hairline, Dock.Top);
        var header = Header();
        DockPanel.SetDock(header, Dock.Top);
        _column.Children.AddRange([header, hairline, _terminal]);
        _terminal.ProcessExited += (_, e) => Console.Error.WriteLine($"orion: assistant exited ({e.ExitCode})");
        _terminal.Loaded += (_, _) => { _loaded = true; Sync(); };
        _terminal.Unloaded += (_, _) => _loaded = false;
        // ⌘= ⌘- ⌘0 while the terminal has the keys: its font, Terminal.app's convention.
        _terminal.AddHandler(KeyDownEvent, (_, e) => { if (FontShortcut(e)) e.Handled = true; }, Avalonia.Interactivity.RoutingStrategies.Tunnel);

        var handle = Handle();
        DockPanel.SetDock(handle, Dock.Right);
        Children.Add(handle);
        Children.Add(_column);

        AttachedToVisualTree += (_, _) => {
            Model.Changed += Sync;
            Sync();
            // The terminal takes the keys when the column appears, and hands them back as it goes.
            _priorFocus = TopLevel.GetTopLevel(this)?.FocusManager?.GetFocusedElement();
            Avalonia.Threading.Dispatcher.UIThread.Post(() => _terminal.Focus());
            if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime app) app.Exit += Quit;
        };
        DetachedFromVisualTree += (_, _) => {
            Model.Changed -= Sync;
            if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime app) app.Exit -= Quit;
            Terminate();
            _priorFocus?.Focus();
        };
    }

    void Quit(object? sender, ControlledApplicationLifetimeExitEventArgs e) => Terminate();

    /// The model moved: the width, the notice, the font, and a relaunch when the
    /// agent, its folder or the restart token changed (relaunchIfNeeded).
    void Sync() {
        _column.Width = Model.Width;
        _notice.Text = Model.Notice ?? "";
        _notice.IsVisible = Model.Notice != null;
        _allow.IsVisible = Model.ToolsQuarantined;
        if (!Equals(_picker.SelectedItem, Model.Command)) _picker.SelectedItem = Model.Command;
        // Only when it changed: setting the font clears a selection the hand may be making.
        if (_font != Model.FontSize) (_font, _terminal.FontSize) = (Model.FontSize, Model.FontSize);
        if (!Model.IsOpen || !_loaded) return;
        var want = (Model.Command, Model.WorkingDirectory, Model.RestartToken);
        if (_launched == want) return;
        if (_launched != null) Terminate();
        _launched = want;
        // Headless (the selftest's command sweep, --command, --screenshot) never starts the agent.
        var launch = Launcher ?? (AppFolder.Writable ? Launch : null);
        if (launch == null) return;
        try {
            launch(_terminal, Model.Command, Model.WorkingDirectory, Model.Server);
        } catch (Exception e) {
            // Nothing from the PTY may take the window down with it.
            Console.Error.WriteLine($"orion: the assistant could not start ({e.Message})");
            (_notice.Text, _notice.IsVisible) = ("The assistant could not start: " + e.Message, true);
        }
    }

    /// The agent in `directory`, under an interactive login zsh, as Swift's startProcess.
    static void Launch(TerminalControl terminal, string command, string directory, string? server) =>
        terminal.LaunchProcess(directory, "/bin/zsh", AssistantModel.BuildArgv(command, server));

    /// What starts the process instead of Launch: the selftest points it at a
    /// harmless echo. Null is Launch, at a window only.
    internal static Action<TerminalControl, string, string, string?>? Launcher;

    void Terminate() {
        if (_terminal.IsLive) {
            // Porta.Pty's kill signals the pid alone. forkpty made it a session and group
            // leader, so the group takes the agent's own children (its MCP server) with it.
            if (_terminal.Pid > 0) kill(-_terminal.Pid, SIGHUP);
            _terminal.Kill();
        }
        _launched = null;
    }

    const int SIGHUP = 1;
    [System.Runtime.InteropServices.DllImport("libc")] static extern int kill(int pid, int signal);

    static bool FontShortcut(KeyEventArgs e) {
        if (!e.KeyModifiers.HasFlag(OperatingSystem.IsMacOS() ? KeyModifiers.Meta : KeyModifiers.Control)) return false;
        switch (e.Key) {
            case Key.OemPlus or Key.Add: Model.IncreaseFontSize(); return true;
            case Key.OemMinus or Key.Subtract: Model.DecreaseFontSize(); return true;
            case Key.D0 or Key.NumPad0: Model.ResetFontSize(); return true;
            default: return false;
        }
    }

    /// True when a key comes from inside the terminal and is the terminal's:
    /// every bare key (Swift's AssistantKeyRoute). A ⌘ key still reaches
    /// Orion's commands, as AppKit's menu equivalents do from SwiftTerm.
    public static bool Owns(KeyEventArgs e) =>
        e.Source is Visual v && v.FindAncestorOfType<TerminalControl>(includeSelf: true) != null
        && !e.KeyModifiers.HasFlag(OperatingSystem.IsMacOS() ? KeyModifiers.Meta : KeyModifiers.Control);

    Control Header() {
        var close = Glyph("xmark", "Close", Model.Close);
        DockPanel.SetDock(close, Dock.Right);
        var row = new DockPanel { Height = 28, Margin = new Thickness(Space.Md, 0) };
        row.Children.Add(close);
        row.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Md, VerticalAlignment = VerticalAlignment.Center,
            Children = {
                _picker,
                Glyph("arrow.clockwise", "Restart", Model.Restart),
                Glyph("folder.badge.gearshape", "Choose the assistant’s working folder (.mcp.json)", ChooseToolsFolder),
                _notice,
                _allow,
            } });
        return row;
    }

    /// The tools-folder picker; the assistant restarts in the folder chosen.
    async void ChooseToolsFolder() {
        if (TopLevel.GetTopLevel(this)?.StorageProvider is not { CanOpen: true } storage) return;
        var start = await storage.TryGetFolderFromPathAsync(Model.WorkingDirectory);
        var chosen = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions {
            Title = "Choose the folder containing Orion’s .mcp.json. The assistant will restart in this folder.",
            AllowMultiple = false, SuggestedStartLocation = start,
        });
        if (chosen.Count > 0 && chosen[0].TryGetLocalPath() is { } path) Model.SetWorkingDirectory(path);
    }

    /// An icon-only `.plain` button in dim ink.
    static StateButton Glyph(string symbol, string help, Action action) => StateButton.Glyph(symbol, help, action, size: IconSize.Md);

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
            _column.Width = Math.Clamp(start + e.GetPosition(this).X - x, AssistantModel.Narrowest, AssistantModel.Widest);
        };
        handle.PointerReleased += (_, e) => {
            if (_dragStartWidth != null) Model.SetWidth(_column.Width);
            _dragStartWidth = _dragStartX = null;
            e.Pointer.Capture(null);
        };
        return handle;
    }

    // ── The column in the window (AssistantCommands, RootView) ──────────────

    /// Show/Hide Assistant: the column goes into the window's leading host, or
    /// out of it, so closed it takes no width at all.
    public static void Toggle() => Model.Toggle();

    static AssistantPanel? _shown;

    static AssistantPanel() => Model.Changed += Place;

    static void Place() {
        if (App.Window?.FindControl<Panel>("AssistantHost") is not { } host) return;
        if (Model.IsOpen && _shown == null) host.Children.Add(_shown = new AssistantPanel());
        else if (!Model.IsOpen && _shown != null) { (_shown.Parent as Panel)?.Children.Remove(_shown); _shown = null; }
    }
}

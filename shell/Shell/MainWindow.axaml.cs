using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Interactivity;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using OrionShell.Library;

namespace OrionShell;

public partial class MainWindow : Window {
    readonly Dictionary<string, List<Control>> _tabs = Panels.Tabs.ToDictionary(t => t, _ => new List<Control>());
    readonly List<StateButton> _tabButtons = new();

    public MainWindow() : this("Light") { }

    /// The tool column's floor: the boards' 322, which every panel is laid out
    /// for (seven tabs side by side), raised only where the tab names' face cannot
    /// fit them in it. Its widest is ToolColMax, measured from the
    /// tab names' face (MainWindow.ToolColumn.cs, #403): 560 where that face
    /// allows, since past it a slider is longer than any hand needs and the photo pays.
    public const double CanvasMin = 360;

    /// The title bar's and the task switcher's icon (decision #407). None on macOS,
    /// where a window icon would replace the bundle's Orion.icns in the Dock. On
    /// Windows the brand's ICO, every size hand-made; on Linux its 256 px tile,
    /// which X11 scales down (Wayland reads the .desktop file's Icon=orion instead).
    static WindowIcon? WindowIconFor() {
        if (OperatingSystem.IsMacOS()) return null;
        string name = OperatingSystem.IsWindows() ? "brand/window-icon.ico" : "brand/window-icon.png";
        using var stream = typeof(MainWindow).Assembly.GetManifestResourceStream(name);
        return stream == null ? null : new WindowIcon(stream);
    }

    public MainWindow(string tab) {
        InitializeComponent();
        if (OperatingSystem.IsMacOS()) ToolbarHost.Margin = new Thickness(80, 0, Space.Pad, 0); // traffic lights
        Icon = WindowIconFor();
        AboveTabsHost.Margin = new Thickness(Space.Pad, 0);
        TabWell.Margin = new Thickness(Space.Pad, Space.Md);
        TabWell.Padding = new Thickness(Space.Xs);
        TabWell.CornerRadius = new CornerRadius(Radius.Lg);   // concentric: the pill's Md plus the Xs inset
        TabPill.CornerRadius = new CornerRadius(Radius.Md);
        ToolsHost.Margin = new Thickness(Space.Pad, Space.Lg, Space.Pad, Space.Xl);
        // ponytail: the width is not remembered across launches (Swift keeps it in @AppStorage); add with a settings file.
        Body.ColumnDefinitions = [
            new ColumnDefinition(GridLength.Star) { MinWidth = CanvasMin },
            new ColumnDefinition(1, GridUnitType.Pixel),
            new ColumnDefinition(Layout.ToolCol, GridUnitType.Pixel) { MinWidth = Layout.ToolCol, MaxWidth = ToolColBase },
        ];
        DressSplitter();
        // Not a sheet: one is built when its command opens it (CommandTable.Sheet).
        // Built here, it was thrown away, after the HDR sheet had read every
        // photo's capture data on this thread: 336 raws, half a minute (#399).
        foreach (var entry in Panels.All.Where(p => p.Region != Region.Sheet)) {
            var panel = Panels.Create(entry.Type);
            if (entry.Tab is { } t) _tabs[t].Add(panel);
            else Host(entry.Region)?.Children.Add(panel);
        }
        BuildTabBar();
        Select(Panels.Tabs.Contains(tab) ? tab : "Light");
        WatchTabFace();
        GalleryMode.Changed += ShowFace;
        // The gallery's key map sees a key before any control does, as Swift's
        // local monitor does: the grid's ScrollViewer would take the arrows.
        AddHandler(KeyDownEvent, (_, e) => { if (GalleryMode.Handle(e)) e.Handled = true; }, RoutingStrategies.Tunnel);
        ShowFace();
        // No photo to edit (the welcome, or one that would not open): the tools are
        // locked and dimmed. A photo still decoding keeps them, so stepping never flickers.
        LockTools();   // the starting state, before the fade is attached
        ToolsHost.Transitions = [new DoubleTransition { Property = OpacityProperty, Duration = Motion.Fade, Easing = Motion.EaseOut }];
        EngineModel.Shared.Opened += LockTools;
        EngineModel.Shared.PropertyChanged += (_, _) => LockTools();
        ToastHost.Children.Add(FeedbackToast);
        Chrome.FeedbackReminder.Start(this, FeedbackToast);
    }

    public Chrome.FeedbackToast FeedbackToast { get; } = new() { IsVisible = false };

    public void LockTools() {
        bool open = CanvasSlice.CanvasState.Now is not (CanvasSlice.CanvasState.Scene.Welcome or CanvasSlice.CanvasState.Scene.Failed);
        ToolsHost.IsEnabled = open;
        ToolsHost.Opacity = open ? 1 : 0.4;
    }

    /// The splitter is 9 px of grab over a 1 px hairline. Under the pointer a
    /// 3 px grip fades in on the hairline (dim; the accent while dragging or
    /// focused from the keyboard), so the edge says it moves before it is
    /// pressed. Opacity only, 120 ms ease-out; the resize cursor does the rest.
    void DressSplitter() {
        var grip = new Border { Width = 3, CornerRadius = new CornerRadius(3 / 2.0), Opacity = 0, IsHitTestVisible = false,   // a pill: half its width
                                Background = Palette.Dim, Margin = new Thickness(0, Space.Gap),
                                Transitions = [new DoubleTransition { Property = OpacityProperty, Duration = Motion.Fade, Easing = Motion.EaseOut }] };
        Splitter.Template = new FuncControlTemplate<GridSplitter>((_, _) => new Panel { Background = Brushes.Transparent, Children = { grip } });
        Splitter.Cursor = new Cursor(StandardCursorType.SizeWestEast);
        Avalonia.Automation.AutomationProperties.SetName(Splitter, "Tool column width");
        // No tooltip: the grip and the resize cursor say it, as on macOS, and a tip covered the panel's text.
        bool dragging = false, keyboard = false;
        void Paint() {
            grip.Background = dragging || keyboard ? Palette.Accent : Palette.Dim;
            grip.Opacity = dragging || keyboard || Splitter.IsPointerOver ? 1 : 0;
        }
        Splitter.PropertyChanged += (_, e) => { if (e.Property == IsPointerOverProperty) Paint(); };
        Splitter.DragStarted += (_, _) => { dragging = true; Paint(); };
        Splitter.DragCompleted += (_, _) => { dragging = false; Paint(); };
        Splitter.GotFocus += (_, e) => { keyboard = e.NavigationMethod is NavigationMethod.Tab or NavigationMethod.Directional; Paint(); };
        Splitter.LostFocus += (_, _) => { keyboard = false; Paint(); };
    }

    /// `mode`: the develop face or the gallery, built the first time it is asked for.
    void ShowFace() {
        if (GalleryMode.On && GalleryHost.Children.Count == 0 && Panels.Find("gallery") is { } gallery)
            GalleryHost.Children.Add(Panels.Create(gallery.Type));
        GalleryHost.IsVisible = GalleryMode.On;
        DevelopFace.IsVisible = !GalleryMode.On;
    }

    Panel? Host(Region region) => region switch {
        Region.Toolbar => ToolbarHost,
        Region.Tools => AboveTabsHost,     // a Tools panel with no Tab: the histogram
        Region.Status => StatusHost,
        Region.Canvas => CanvasHost,
        Region.Filmstrip => FilmstripHost,
        _ => null,   // sheets open from commands, not from the layout
    };

    /// The tool tabs as one segmented control: every tab its icon over its name,
    /// the chosen one on a raised pill, each named by a tooltip with the key and
    /// to a screen reader. The cells' floors come from the names (FitToolColumn),
    /// never from the choice, so choosing a tab moves only the pill.
    void BuildTabBar() {
        for (int i = 0; i < Panels.Tabs.Length; i++) {
            string tab = Panels.Tabs[i];
            TabBar.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            var button = new StateButton {
                Kind = ButtonKind.Segment, Height = double.NaN, MinWidth = 0,
                HorizontalAlignment = HorizontalAlignment.Stretch, CornerRadius = new CornerRadius(Radius.Md),
                Content = new StackPanel { Spacing = Space.Xs, Children = {
                    Icons.Create(Panels.TabIcons[i], IconSize.Md, Palette.Text),
                    new TextBlock { Text = tab, FontSize = Faces.Mark, HorizontalAlignment = HorizontalAlignment.Center },
                } },
            };
            ToolTip.SetTip(button, $"{tab} ({Commands.KeyLabel($"tab-{tab.ToLowerInvariant()}")})");
            Avalonia.Automation.AutomationProperties.SetName(button, tab);
            button.Click += (_, _) => Select(tab, animate: true);
            Grid.SetColumn(button, i);
            TabBar.Children.Add(button);
            _tabButtons.Add(button);
        }
        // Ease-out under 200 ms, on a transform only: nothing reflows while it moves.
        TabPill.Transitions = [new TransformOperationsTransition { Property = RenderTransformProperty, Duration = TimeSpan.FromMilliseconds(180), Easing = Motion.EaseOut }];
    }

    /// The tool tab on show (ToolTab in Swift); commands read and set it.
    public string Tab { get; private set; } = "Light";

    /// A key nothing in the window took: maybe a command's (Commands.Key).
    protected override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        Commands.Key(e);
    }

    /// Shows a tab's panels. On a click the pill eases from the old tab to the
    /// new one (a FLIP: transforms only); a key (⌘1–7) or reduced motion lands at once.
    public void Select(string tab, bool animate = false) {
        Tab = tab;
        ToolsHost.Children.Clear();
        ToolsHost.Children.AddRange(_tabs[tab]);
        int to = Array.IndexOf(Panels.Tabs, tab);
        var pill = TabPill.Bounds;
        for (int i = 0; i < _tabButtons.Count; i++) {
            _tabButtons[i].Ink = i == to ? Palette.Text : null;
            _tabButtons[i].IsHitTestVisible = i != to;   // no hover fill over the pill, and nothing to click there
        }
        Grid.SetColumn(TabPill, to);
        bool glide = animate && !Motion.Reduced && pill.Width > 0;
        if (glide) TabBar.UpdateLayout();
        Ease(TabPill, RenderTransformProperty, glide ? Shift(pill.X - TabPill.Bounds.X, pill.Width / TabPill.Bounds.Width) : Still, Still);
    }

    static readonly TransformOperations Still = Shift(0, 1);
    /// Avalonia applies the list left to right (CSS reads it the other way), so the scale goes first and the move is not scaled.
    static TransformOperations Shift(double x, double scaleX) => TransformOperations.Parse(FormattableString.Invariant($"scaleX({scaleX}) translateX({x}px)"));
    static TextBlock Words(StateButton b) => ((Panel)b.Content!).Children.OfType<TextBlock>().Single();

    /// `property` jumps to `from`, then its transition carries it to `to`.
    static void Ease(Control c, AvaloniaProperty property, object from, object to) {
        var transitions = c.Transitions;
        c.Transitions = null;
        c.SetValue(property, from);
        c.Transitions = transitions;
        c.SetValue(property, to);
    }
}

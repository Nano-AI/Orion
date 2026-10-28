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

    /// The tool column's range: the boards' 322 is the floor every panel is laid
    /// out for (seven tabs side by side); past 560 a slider is longer than any
    /// hand needs, and the photo pays for it.
    public const double ToolColMax = 560, CanvasMin = 360;

    public MainWindow(string tab) {
        InitializeComponent();
        if (OperatingSystem.IsMacOS()) ToolbarHost.Margin = new Thickness(80, 0, Space.Pad, 0); // traffic lights
        // ponytail: the width is not remembered across launches (Swift keeps it in @AppStorage); add with a settings file.
        Body.ColumnDefinitions = [
            new ColumnDefinition(GridLength.Star) { MinWidth = CanvasMin },
            new ColumnDefinition(1, GridUnitType.Pixel),
            new ColumnDefinition(Layout.ToolCol, GridUnitType.Pixel) { MinWidth = Layout.ToolCol, MaxWidth = ToolColMax },
        ];
        DressSplitter();
        foreach (var entry in Panels.All) {
            var panel = Panels.Create(entry.Type);
            if (entry.Tab is { } t) _tabs[t].Add(panel);
            else Host(entry.Region)?.Children.Add(panel);
        }
        BuildTabBar();
        Select(Panels.Tabs.Contains(tab) ? tab : "Light");
        GalleryMode.Changed += ShowFace;
        // The gallery's key map sees a key before any control does, as Swift's
        // local monitor does: the grid's ScrollViewer would take the arrows.
        AddHandler(KeyDownEvent, (_, e) => { if (GalleryMode.Handle(e)) e.Handled = true; }, RoutingStrategies.Tunnel);
        ShowFace();
    }

    /// The splitter is 9 px of grab over a 1 px hairline. Under the pointer a
    /// 3 px grip fades in on the hairline (dim; the accent while dragging or
    /// focused from the keyboard), so the edge says it moves before it is
    /// pressed. Opacity only, 120 ms ease-out; the resize cursor does the rest.
    void DressSplitter() {
        var grip = new Border { Width = 3, CornerRadius = new CornerRadius(1.5), Opacity = 0, IsHitTestVisible = false,
                                Background = Palette.Dim, Margin = new Thickness(0, 4),
                                Transitions = [new DoubleTransition { Property = OpacityProperty, Duration = TimeSpan.FromMilliseconds(120), Easing = new CubicEaseOut() }] };
        Splitter.Template = new FuncControlTemplate<GridSplitter>((_, _) => new Panel { Background = Brushes.Transparent, Children = { grip } });
        Splitter.Cursor = new Cursor(StandardCursorType.SizeWestEast);
        Avalonia.Automation.AutomationProperties.SetName(Splitter, "Tool column width");
        ToolTip.SetTip(Splitter, "Drag to widen the tools");
        ToolTip.SetShowDelay(Splitter, 1200);
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

    /// Main.dc.html's tool tabs: equal, sentence case, the chosen one in text
    /// colour (never heavier), and an accent bar under it that slides there.
    void BuildTabBar() {
        for (int i = 0; i < Panels.Tabs.Length; i++) {
            string tab = Panels.Tabs[i];
            TabBar.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            var button = new StateButton {
                Kind = ButtonKind.Bare, Height = 34, MinWidth = 0, Padding = new Thickness(1, 0), CornerRadius = default,
                HorizontalAlignment = HorizontalAlignment.Stretch,
                Content = new TextBlock { Text = tab, FontSize = Faces.Label, FontWeight = FontWeight.Medium },
            };
            button.Click += (_, _) => Select(tab, animate: true);
            Grid.SetColumn(button, i);
            TabBar.Children.Add(button);
            _tabButtons.Add(button);
        }
        // A transform, so moving the bar reflows nothing; ease-out under 200 ms.
        TabIndicator.Transitions = [new TransformOperationsTransition {
            Property = RenderTransformProperty, Duration = TimeSpan.FromMilliseconds(180), Easing = new SplineEasing(0.22, 1, 0.36, 1),
        }];
        TabBar.SizeChanged += (_, _) => PlaceIndicator(animate: false);
    }

    /// The tool tab on show (ToolTab in Swift); commands read and set it.
    public string Tab { get; private set; } = "Light";

    /// A key nothing in the window took: maybe a command's (Commands.Key).
    protected override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        Commands.Key(e);
    }

    /// Shows a tab's panels. Only a click slides the bar; a key (⌘1–7) is answered at once.
    public void Select(string tab, bool animate = false) {
        Tab = tab;
        ToolsHost.Children.Clear();
        ToolsHost.Children.AddRange(_tabs[tab]);
        for (int i = 0; i < _tabButtons.Count; i++) _tabButtons[i].Ink = Panels.Tabs[i] == tab ? Palette.Text : Palette.Dim;
        PlaceIndicator(animate);
    }

    void PlaceIndicator(bool animate) {
        double width = TabBar.Bounds.Width / Panels.Tabs.Length, x = width * Array.IndexOf(Panels.Tabs, Tab);
        TabIndicator.Width = width;
        var transitions = TabIndicator.Transitions;
        bool snap = !animate || Motion.Reduced;
        if (snap) TabIndicator.Transitions = null;
        TabIndicator.RenderTransform = TransformOperations.Parse($"translateX({x.ToString(System.Globalization.CultureInfo.InvariantCulture)}px)");
        if (snap) TabIndicator.Transitions = transitions;
    }
}

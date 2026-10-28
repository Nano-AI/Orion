using Avalonia;
using Avalonia.Controls;
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

    public MainWindow(string tab) {
        InitializeComponent();
        if (OperatingSystem.IsMacOS()) ToolbarHost.Margin = new Thickness(80, 0, Space.Pad, 0); // traffic lights
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
        if (!animate) TabIndicator.Transitions = null;
        TabIndicator.RenderTransform = TransformOperations.Parse($"translateX({x.ToString(System.Globalization.CultureInfo.InvariantCulture)}px)");
        if (!animate) TabIndicator.Transitions = transitions;
    }
}

using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Interactivity;
using Avalonia.Media;
using OrionShell.Library;

namespace OrionShell;

public partial class MainWindow : Window {
    readonly Dictionary<string, List<Control>> _tabs = Panels.Tabs.ToDictionary(t => t, _ => new List<Control>());
    readonly List<Border> _tabButtons = new();

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

    /// OrionApp+Chrome.swift `tabBar`: equal tabs, 10 pt names, the selected
    /// one taller, on the panel colour, with a star-coloured top edge.
    void BuildTabBar() {
        for (int i = 0; i < Panels.Tabs.Length; i++) {
            string tab = Panels.Tabs[i];
            TabBar.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            var button = new Border {
                Margin = new Thickness(0.5, 0), VerticalAlignment = VerticalAlignment.Bottom,
                CornerRadius = new CornerRadius(4, 4, 0, 0), ClipToBounds = true,
                Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
                Child = new TextBlock {
                    Text = tab.ToUpperInvariant(), FontSize = TypeSize.Xs,
                    HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
                },
            };
            button.PointerPressed += (_, _) => Select(tab);
            Grid.SetColumn(button, i);
            TabBar.Children.Add(button);
            _tabButtons.Add(button);
        }
    }

    /// The tool tab on show (ToolTab in Swift); commands read and set it.
    public string Tab { get; private set; } = "Light";

    /// A key nothing in the window took: maybe a command's (Commands.Key).
    protected override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        Commands.Key(e);
    }

    public void Select(string tab) {
        Tab = tab;
        ToolsHost.Children.Clear();
        ToolsHost.Children.AddRange(_tabs[tab]);
        for (int i = 0; i < _tabButtons.Count; i++) {
            bool on = Panels.Tabs[i] == tab;
            var b = _tabButtons[i];
            b.Height = on ? 30 : 26;
            b.Background = on ? Palette.Panel : Palette.Raised;
            b.BorderBrush = Palette.Star;
            b.BorderThickness = new Thickness(0, on ? 2 : 0, 0, 0);
            ((TextBlock)b.Child!).Foreground = on ? Palette.Text : Palette.Dim;
        }
    }
}

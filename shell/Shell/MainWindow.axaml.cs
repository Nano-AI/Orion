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

    /// The tool tabs as one segmented control: the chosen tab a raised pill with
    /// its icon and name, the rest their icon alone, named by a tooltip with the
    /// key and to a screen reader. Seven equal cells, the chosen one never
    /// narrower than its icon and name: at 322 it takes what it needs and the
    /// icons share the rest; at 560 all seven are equal.
    void BuildTabBar() {
        for (int i = 0; i < Panels.Tabs.Length; i++) {
            string tab = Panels.Tabs[i];
            TabBar.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            var button = new StateButton {
                Kind = ButtonKind.Segment, Height = 24, MinWidth = 0, Padding = new Thickness(8, 0),
                HorizontalAlignment = HorizontalAlignment.Stretch, CornerRadius = new CornerRadius(Radius.Md - 1),
                Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Transitions = [Glide(RenderTransformProperty)], Children = {
                    Icons.Create(Panels.TabIcons[i], 14, Palette.Text),
                    new TextBlock { Text = tab, FontSize = Faces.Label, VerticalAlignment = VerticalAlignment.Center, Transitions = [Glide(OpacityProperty)] },
                } },
            };
            ToolTip.SetTip(button, $"{tab} (⌘{i + 1})");
            Avalonia.Automation.AutomationProperties.SetName(button, tab);
            button.Click += (_, _) => Select(tab, animate: true);
            Grid.SetColumn(button, i);
            TabBar.Children.Add(button);
            _tabButtons.Add(button);
        }
        TabPill.Transitions = [Glide(RenderTransformProperty)];
    }

    /// Ease-out under 200 ms, on a transform or opacity only: nothing reflows while it moves.
    static ITransition Glide(AvaloniaProperty property) => property == OpacityProperty
        ? new DoubleTransition { Property = property, Duration = Motion.Enter, Easing = Motion.EaseOut }
        : new TransformOperationsTransition { Property = property, Duration = TimeSpan.FromMilliseconds(180), Easing = Motion.EaseOut };

    /// The tool tab on show (ToolTab in Swift); commands read and set it.
    public string Tab { get; private set; } = "Light";

    /// A key nothing in the window took: maybe a command's (Commands.Key).
    protected override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        Commands.Key(e);
    }

    /// Shows a tab's panels. The bar is laid out for the new tab at once; on a
    /// click the pill and the icons then ease there from where they were (a
    /// FLIP: transforms only) and the name fades in. A key (⌘1–7) or reduced
    /// motion lands at once.
    public void Select(string tab, bool animate = false) {
        Tab = tab;
        ToolsHost.Children.Clear();
        ToolsHost.Children.AddRange(_tabs[tab]);
        int to = Array.IndexOf(Panels.Tabs, tab);
        var was = _tabButtons.Select(Left).ToArray();
        var pill = TabPill.Bounds;
        for (int i = 0; i < _tabButtons.Count; i++) {
            bool on = i == to;
            Words(_tabButtons[i]).IsVisible = on;
            _tabButtons[i].Ink = on ? Palette.Text : null;
            _tabButtons[i].IsHitTestVisible = !on;   // no hover fill over the pill, and nothing to click there
            if (on) _tabButtons[i].Measure(Size.Infinity);
            TabBar.ColumnDefinitions[i].MinWidth = on ? _tabButtons[i].DesiredSize.Width : 0;
        }
        Grid.SetColumn(TabPill, to);
        bool glide = animate && !Motion.Reduced && pill.Width > 0;
        if (glide) TabBar.UpdateLayout();
        Ease(TabPill, RenderTransformProperty, glide ? Shift(pill.X - TabPill.Bounds.X, pill.Width / TabPill.Bounds.Width) : Still, Still);
        for (int i = 0; i < _tabButtons.Count; i++)
            Ease((Control)_tabButtons[i].Content!, RenderTransformProperty, glide ? Shift(was[i] - Left(_tabButtons[i]), 1) : Still, Still);
        Ease(Words(_tabButtons[to]), OpacityProperty, glide ? 0.0 : 1.0, 1.0);
    }

    static readonly TransformOperations Still = Shift(0, 1);
    /// Avalonia applies the list left to right (CSS reads it the other way), so the scale goes first and the move is not scaled.
    static TransformOperations Shift(double x, double scaleX) => TransformOperations.Parse(FormattableString.Invariant($"scaleX({scaleX}) translateX({x}px)"));
    /// Where a tab's icon starts in the bar, by layout alone (a transform in flight is not counted).
    static double Left(StateButton b) => b.Bounds.X + ((Control)b.Content!).Bounds.X;
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

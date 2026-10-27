using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Sheets;

/// A port of app/WatermarkPanel.swift: the watermark editor, a sheet over the
/// Export sheet. The controls are a fixed set and are not scrolled: a fold
/// once put Opacity, the one control every mark needs, out of sight.
///
/// WatermarkRaster.swift (WatermarkLayout.place and the CoreText/SVG coverage
/// mask) is not ported: its only callers are the export and this preview.
// stub(#315): Engine+Document.swift owns this until the logic moves below the facade
[Panel("sheet-watermark", Region.Sheet)]
public sealed class WatermarkPanel : Border {
    readonly Watermark _mark = Watermark.Shared;
    readonly StackPanel _options = new() { Spacing = Space.Md + 6 };

    /// Closes the sheet. Set by whatever opened it.
    public Action? Dismiss { get; set; }

    public WatermarkPanel() {
        Width = 400;
        Padding = new Thickness(22);
        Background = Palette.Panel;
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Top;
        SheetKit.Restyle(this);
        Build();

        var footer = new DockPanel();
        var done = SheetKit.Button("Done", () => { _mark.Save(); Dismiss?.Invoke(); }, prominent: true);
        DockPanel.SetDock(done, Dock.Right);
        footer.Children.Add(done);
        if (_mark.LastFailure is { } why) footer.Children.Add(SheetKit.Note(why, Palette.Dim));
        else footer.Children.Add(new Control());

        Child = new StackPanel {
            Spacing = Space.Md + 6,
            Children = { SheetKit.Title("Watermark"), Preview(), _options, footer },
        };
    }

    /// The preview is the export itself in Swift: the open photo written at
    /// 1024 px with the mask and read back, so there is no second compositing
    /// path. Here it is the canvas's render, without the mark.
    // Engine.watermarkPreview:
    // stub(#315): Engine+Document.swift owns this until the logic moves below the facade
    static Control Preview() {
        var image = new Image { Source = EngineModel.Shared.Picture, Stretch = Stretch.Uniform };
        RenderOptions.SetBitmapInterpolationMode(image, Avalonia.Media.Imaging.BitmapInterpolationMode.HighQuality);
        return new Border { Height = 200, Background = Palette.Raised, CornerRadius = new CornerRadius(Radius.Md), Child = image };
    }

    /// Rebuilt whole when the kind or the placement changes, which swaps rows.
    void Build() {
        _options.Children.Clear();
        var kind = new Segmented(["Text", "SVG"], _mark.IsText ? 0 : 1);
        // Both kinds keep their fields, so flipping Text/SVG and back loses nothing.
        kind.Changed += i => { _mark.IsText = i == 0; Build(); };
        _options.Children.Add(SheetKit.Row("Mark", [kind, .. Content()]));

        var placement = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Pad };
        placement.Children.Add(AnchorGrid());
        placement.Children.Add(Chip("Diagonal", _mark.Diagonal, () => { _mark.Anchor = -1; Build(); }));
        _options.Children.Add(SheetKit.Row("Placement", placement));

        if (!_mark.Diagonal) {
            _options.Children.Add(Percent("Size", 0.01, 0.15, () => _mark.Size, v => _mark.Size = v));
            _options.Children.Add(Percent("Margin", 0, 0.1, () => _mark.Margin, v => _mark.Margin = v));
        } else {
            _options.Children.Add(Percent("Size", 0.2, 1, () => _mark.Span, v => _mark.Span = v));
        }
        _options.Children.Add(Percent("Opacity", 0.05, 1, () => _mark.Opacity, v => _mark.Opacity = v));
        _options.Children.Add(SheetKit.Note("One neutral gray, always. Opacity is how present it is.", Palette.Dim));
    }

    Control[] Content() {
        if (_mark.IsText) {
            var words = SheetKit.Field("Your name");
            words.Text = _mark.Words;
            words.TextChanged += (_, _) => _mark.Words = words.Text ?? "";
            var families = FontManager.Current.SystemFonts.Select(f => f.Name).Distinct().Order().ToList();
            var family = SheetKit.Menu(families, families.IndexOf(_mark.Family), i => _mark.Family = families[i]);
            var bold = new CheckBox { Content = "Bold", IsChecked = _mark.Bold, FontSize = TypeSize.Sm, MinHeight = 0 };
            bold.IsCheckedChanged += (_, _) => _mark.Bold = bold.IsChecked == true;
            return [words, new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Md, Children = { family, bold } }];
        }
        // NSOpenPanel for the SVG:
        // stub(#315): OrionApp.swift owns this until the logic moves below the facade
        var choose = SheetKit.Button("Choose SVG…", () => Console.Error.WriteLine("sheet-watermark: the SVG file panel is AppKit's"));
        var name = new TextBlock {
            Text = _mark.SvgName.Length == 0 ? "No file chosen" : _mark.SvgName, FontSize = TypeSize.Sm,
            Foreground = _mark.SvgName.Length == 0 ? Palette.Dim : Palette.Text,
            TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center,
        };
        return [
            new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Md, Children = { choose, name } },
            SheetKit.Note("Only the shape is used. Its colors become the one gray, and an opaque background becomes a gray box.", Palette.Dim),
        ];
    }

    /// The 3x3 grid, in the order Watermark.Anchors declares.
    Control AnchorGrid() {
        var grid = new UniformGrid { Columns = 3 };
        for (int i = 0; i < 9; i++) {
            int a = i;
            var cell = new Border {
                Width = 18, Height = 14, Margin = new Thickness(1.5), CornerRadius = new CornerRadius(2),
                Background = _mark.Anchor == a ? Palette.Accent : Palette.Raised,
                Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
            };
            ToolTip.SetTip(cell, char.ToUpperInvariant(Watermark.Anchors[a][0]) + Watermark.Anchors[a][1..]);
            cell.PointerPressed += (_, _) => { _mark.Anchor = a; Build(); };
            grid.Children.Add(cell);
        }
        return grid;
    }

    static Control Chip(string title, bool on, Action click) {
        var chip = new Border {
            Padding = new Thickness(Space.Md, 4), CornerRadius = new CornerRadius(4), VerticalAlignment = VerticalAlignment.Center,
            Background = on ? new SolidColorBrush(((ISolidColorBrush)Palette.Accent).Color, 0.25) : Palette.Raised,
            Child = new TextBlock { Text = title, FontSize = TypeSize.Sm, Foreground = on ? Palette.Text : Palette.Dim },
            Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
        };
        chip.PointerPressed += (_, _) => click();
        return chip;
    }

    /// A labelled slider with its value as a whole percentage.
    static Control Percent(string label, double min, double max, Func<double> get, Action<double> set) {
        var slider = new Slider { Minimum = min, Maximum = max, Value = get(), Foreground = Palette.Accent, MinHeight = 0 };
        var value = new TextBlock {
            FontSize = TypeSize.Xs, Foreground = Palette.Dim, Width = 34, TextAlignment = TextAlignment.Right,
            VerticalAlignment = VerticalAlignment.Center, Classes = { "tnum" },
        };
        void Show() => value.Text = $"{(int)Math.Round(get() * 100)}%";
        slider.ValueChanged += (_, e) => { set(e.NewValue); Show(); };
        Show();
        var row = new DockPanel();
        DockPanel.SetDock(value, Dock.Right);
        value.Margin = new Thickness(Space.Sm + 2, 0, 0, 0);
        row.Children.AddRange([value, slider]);
        return SheetKit.Row(label, row);
    }
}

using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// The pieces the strip, the gallery and the rating bar share, so a filter
/// menu or a cull menu cannot drift between them.
static class LibraryUi {
    /// A Palette colour at an opacity: Swift's `.white.opacity(x)` and
    /// `.black.opacity(x)` become Palette.Text and Palette.FilmBase (rule 5).
    public static IBrush Ink(IBrush palette, double opacity) =>
        new SolidColorBrush(((ISolidColorBrush)palette).Color, opacity);

    /// `.shadow(color: .black.opacity(0.8), radius: 1, y: 1)` under the marks.
    public static IEffect MarkShadow => new DropShadowEffect {
        OffsetX = 0, OffsetY = 1, BlurRadius = 2, Opacity = 0.8, Color = ((ISolidColorBrush)Palette.FilmBase).Color,
    };

    /// `.buttonStyle(.plain)`: a real button (focus, keyboard, screen reader)
    /// with none of Fluent's chrome or hover fill.
    public static Button Plain(Control content, Action click, string help = "") {
        var button = new Button {
            Content = content, Padding = new Thickness(0), BorderThickness = new Thickness(0), MinHeight = 0, MinWidth = 0,
            Background = Brushes.Transparent, VerticalAlignment = VerticalAlignment.Center,
        };
        foreach (var key in new[] { "ButtonBackgroundPointerOver", "ButtonBackgroundPressed", "ButtonBackgroundDisabled" })
            button.Resources[key] = Brushes.Transparent;
        button.Click += (_, _) => click();
        if (help.Length > 0) ToolTip.SetTip(button, help);
        return button;
    }

    /// The menu-style filter picker, 84 wide, bound to LibraryModel.Current.
    public static ComboBox FilterPicker() {
        var library = LibraryModel.Shared;
        var picker = new ComboBox {
            ItemsSource = LibraryModel.FilterTitles, SelectedIndex = (int)library.Current,
            Width = 84, Height = 24, MinHeight = 0, FontSize = TypeSize.Md, Padding = new Thickness(Space.Md, 0, 0, 0),
            Background = Palette.Raised, Foreground = Palette.Text, BorderThickness = new Thickness(0),
            CornerRadius = new CornerRadius(Radius.Md), VerticalAlignment = VerticalAlignment.Center,
            VerticalContentAlignment = VerticalAlignment.Center,
        };
        foreach (var key in new[] { "ComboBoxBackgroundPointerOver", "ComboBoxBackgroundPressed" })
            picker.Resources[key] = Palette.Raised;
        picker.Resources["ComboBoxDropDownGlyphForeground"] = Palette.Text;
        picker.SelectionChanged += (_, _) => library.Current = (LibraryModel.Filter)Math.Max(picker.SelectedIndex, 0);
        library.Changed += () => picker.SelectedIndex = (int)library.Current;
        return picker;
    }

    /// The cull entries both context menus share: Reject or Keep, then the
    /// five ratings and No rating. Swift counts the photos when the scope is a
    /// group; with no PhotoSelection here the scope is always the one clicked.
    public static List<Control> CullItems(LibraryModel.Photo photo) {
        var library = LibraryModel.Shared;
        var items = new List<Control> {
            Item(photo.Rejected ? "Keep" : "Reject", () => library.SetRejected(photo, !photo.Rejected)),
            new Separator(),
        };
        for (int n = 1; n <= 5; n++) {
            int stars = n;
            items.Add(Item($"{n} star{(n == 1 ? "" : "s")}", () => library.SetRating(photo, stars)));
        }
        items.Add(Item("No rating", () => library.SetRating(photo, 0)));
        return items;
    }

    /// What a screen reader says for one photograph, in both views: the name
    /// first, then the marks that decide whether it stays.
    public static string Spoken(LibraryModel.Photo photo) {
        var parts = new List<string> { photo.Name };
        if (photo.Rejected) parts.Add("rejected");
        if (photo.Rating > 0) parts.Add($"{photo.Rating} star{(photo.Rating == 1 ? "" : "s")}");
        return string.Join(", ", parts);
    }

    public static MenuItem Item(string header, Action act) {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => act();
        return item;
    }
}

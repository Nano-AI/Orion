using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// The pieces the strip, the gallery and the rating bar share, so a filter
/// menu or a cull menu cannot drift between them.
static class LibraryUi {
    /// The library filter, one control wherever it appears (the strip and the
    /// gallery): a segmented choice, each cell counting what it would show, so
    /// every option is in view and none has to be remembered behind a menu.
    /// Marks as icons (★ rated, ☆ unrated, ✕ rejected), each named in its tooltip:
    /// in words the four took 430 px of the strip.
    public static Segmented Filter() {
        var library = LibraryModel.Shared;
        var filter = new Segmented(LibraryModel.FilterTitles, (int)library.Current, [null, "star.fill", "star", "xmark"]);
        void Count() {
            var photos = library.Photos;
            filter.Selected = (int)library.Current;
            filter.Figures($"{photos.Count}", $"{photos.Count(p => p.Rating >= 1 && !p.Rejected)}",
                           $"{photos.Count(p => p.Rating == 0 && !p.Rejected)}", $"{library.RejectedCount}");
        }
        filter.Changed += i => library.Current = (LibraryModel.Filter)i;
        library.Changed += Count;
        Count();
        return filter;
    }

    /// The order, one pull-down wherever the filter is. Picking an order applies
    /// it now, picking it again re-sorts after new marks (LibraryModel.Sort).
    public static Button SortMenu() {
        var library = LibraryModel.Shared;
        var label = new TextBlock { FontSize = Faces.Body, VerticalAlignment = VerticalAlignment.Center };
        var flyout = new MenuFlyout();
        var items = LibraryModel.OrderTitles.Select((title, i) => {
            var item = new MenuItem { Header = title, ToggleType = MenuItemToggleType.Radio };
            item.Click += (_, _) => library.Sort((LibraryModel.Order)i);
            flyout.Items.Add(item);
            return item;
        }).ToList();
        void Show() {
            label.Text = library.Sorted == LibraryModel.Order.Rating ? "Rating" : "Name";
            for (int i = 0; i < items.Count; i++) items[i].IsChecked = (int)library.Sorted == i;
        }
        library.Changed += Show;
        Show();
        // A macOS pop-up button: what it does, the current choice, and the up-down chevrons
        // that say a menu opens in place (a lone down chevron reads as a disclosure).
        var button = new StateButton {
            Kind = ButtonKind.Standard, Flyout = flyout, MinWidth = 0, Height = 24, Padding = new Thickness(Space.Md, 0, Space.Sm, 0),
            VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Left,
            Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, Children = {
                Icons.Create("arrow.up.arrow.down", IconSize.Sm, Palette.Dim), label, Icons.Create("chevron.up.chevron.down", IconSize.Sm, Palette.Dim),
            } },
        };
        ToolTip.SetTip(button, "Sort the photos");
        Avalonia.Automation.AutomationProperties.SetName(button, "Sort by");
        return button;
    }

    /// How far a rejected photo's picture fades, in the strip and the gallery alike.
    public const double RejectedOpacity = 0.35;

    /// A photo's marks at a text size: five stars, the rating's in the star
    /// colour and the rest faint, so "unrated" is shown rather than inferred;
    /// a rejected photo shows the reject ✕ alone, in the reject colour.
    /// `unearned`: the stars above the rating too, faint; without them an unrated photo shows nothing.
    public static Control Marks(LibraryModel.Photo photo, double size, bool unearned = true) {
        if (photo.Rejected) {
            var x = Icons.Create("xmark", IconSize.Sm, Palette.Reject);
            x.VerticalAlignment = VerticalAlignment.Center;
            Avalonia.Automation.AutomationProperties.SetName(x, "Rejected");
            return x;
        }
        var stars = new StackPanel { Orientation = Orientation.Horizontal, Spacing = size > Faces.Mark ? Space.Xs : 1, VerticalAlignment = VerticalAlignment.Center };
        for (int i = 1; i <= (unearned ? 5 : photo.Rating); i++) stars.Children.Add(Icons.Create("star.fill", IconSize.Sm, i <= photo.Rating ? Palette.Star : Palette.Faint));
        Avalonia.Automation.AutomationProperties.SetName(stars, photo.Rating == 0 ? "Unrated" : $"{photo.Rating} stars");
        return stars;
    }

    /// The cull entries both context menus share: Reject or Keep, then the
    /// five ratings and No rating, over the selection when the photo is part of
    /// one (Library.cullScope), and counted then: "Reject" over forty and over
    /// one are different acts, and the menu is the last thing read before either.
    public static List<Control> CullItems(LibraryModel.Photo photo) {
        var library = LibraryModel.Shared;
        var scope = library.CullScope(photo);
        string suffix = Suffix(scope);
        var items = new List<Control> {
            Item((photo.Rejected ? "Keep" : "Reject") + suffix, () => library.SetRejected(scope, !photo.Rejected)),
            new Separator(),
        };
        for (int n = 1; n <= 5; n++) {
            int stars = n;
            items.Add(Item($"{n} star{(n == 1 ? "" : "s")}{suffix}", () => library.SetRating(scope, stars)));
        }
        items.Add(Item("No rating" + suffix, () => library.SetRating(scope, 0)));
        return items;
    }

    /// Library.summary's selection half, "12 selected", where Swift prints the
    /// summary (the strip and the gallery header). Hidden for the resting
    /// selection of one: "1 selected" beside a batch that acts on forty would mislead.
    public static TextBlock SelectionCount() {
        var library = LibraryModel.Shared;
        var text = new TextBlock { FontSize = Faces.Mark, Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center };
        void Show() { text.Text = library.SelectionSummary; text.IsVisible = text.Text.Length > 0; }
        library.Changed += Show;
        library.SelectionChanged += Show;
        Show();
        return text;
    }

    /// " (12 photos)" for a group, nothing for one.
    public static string Suffix(IReadOnlyCollection<LibraryModel.Photo> scope) => scope.Count > 1 ? $" ({scope.Count} photos)" : "";

    /// A context menu built when it opens, so its scope and count are the selection's now, not at the last rebuild.
    public static ContextMenu Menu(Func<List<Control>> items) {
        var menu = new ContextMenu { ItemsSource = items() };
        menu.Opening += (_, _) => menu.ItemsSource = items();
        return menu;
    }

    /// The dimmer ring of a photo in the selection but not in focus (Filmstrip.swift: accent at 0.55).
    public static readonly IBrush SelectedRing = new SolidColorBrush(((ISolidColorBrush)Palette.Accent).Color, 0.55).ToImmutable();

    /// The name now; the exposure under it when the tooltip first opens, so
    /// listing a folder never reads every file's metadata.
    public static void Tip(Control cell, LibraryModel.Photo photo) {
        ToolTip.SetTip(cell, photo.Name);
        cell.AddHandler(ToolTip.ToolTipOpeningEvent, (_, _) => ToolTip.SetTip(cell, photo.Tooltip));
    }

    /// What a screen reader says for one photograph, in both views: the name
    /// first, then the marks that decide whether it stays.
    public static string Spoken(LibraryModel.Photo photo) {
        var parts = new List<string> { photo.Name };
        if (LibraryModel.Shared.HasExplicitSelection && LibraryModel.Shared.IsSelected(photo)) parts.Add("selected");
        if (photo.Rejected) parts.Add("rejected");
        if (photo.Rating > 0) parts.Add($"{photo.Rating} star{(photo.Rating == 1 ? "" : "s")}");
        if (!string.IsNullOrEmpty(photo.Label)) parts.Add($"label {photo.Label}");
        return string.Join(", ", parts);
    }

    public static MenuItem Item(string header, Action act) {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => act();
        return item;
    }
}

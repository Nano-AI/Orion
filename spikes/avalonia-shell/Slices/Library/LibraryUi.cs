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
    public static Segmented Filter() {
        var library = LibraryModel.Shared;
        var filter = new Segmented(LibraryModel.FilterTitles, (int)library.Current);
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

    /// How far a rejected photo's picture fades, in the strip and the gallery alike.
    public const double RejectedOpacity = 0.35;

    /// A photo's marks at a text size: five stars, the rating's in the star
    /// colour and the rest faint, so "unrated" is shown rather than inferred;
    /// a rejected photo says so in words instead.
    public static Control Marks(LibraryModel.Photo photo, double size) {
        if (photo.Rejected) return new TextBlock { Text = "Rejected", FontSize = size, Foreground = Palette.Reject, VerticalAlignment = VerticalAlignment.Center };
        var stars = new StackPanel { Orientation = Orientation.Horizontal, Spacing = size > Faces.Mark ? 2 : 1, VerticalAlignment = VerticalAlignment.Center };
        for (int i = 1; i <= 5; i++) stars.Children.Add(Icons.Create("star.fill", size - 2, i <= photo.Rating ? Palette.Star : Palette.Faint));
        Avalonia.Automation.AutomationProperties.SetName(stars, photo.Rating == 0 ? "Unrated" : $"{photo.Rating} stars");
        return stars;
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

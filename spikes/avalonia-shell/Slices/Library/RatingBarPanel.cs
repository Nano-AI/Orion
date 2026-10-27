using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// A port of app/RatingBar.swift: rating and rejection over the photo, in the
/// canvas's lower-right corner (OrionApp+Canvas.swift pads it 14). Culling
/// happens while looking at the picture, so the mark being set belongs there,
/// not only as four-pixel dots on a thumbnail nobody is looking at.
///
/// Hovering a star previews the rating a click would set; clicking the star
/// already set clears it. A rejected photo cannot be rated until it is kept.
[Panel("rating-bar", Region.Canvas, 50)]
public sealed class RatingBarPanel : Border {
    readonly StackPanel _stars = new() { Orientation = Orientation.Horizontal, Spacing = 3 };
    readonly StackPanel _reject = new() { Orientation = Orientation.Horizontal, Spacing = 4, Height = 18, Margin = new Thickness(6, 0) };
    readonly Button _rejectButton;
    readonly Border[] _faces = new Border[5];
    readonly Button[] _starButtons = new Button[5];
    int? _hovered;

    public RatingBarPanel() {
        HorizontalAlignment = HorizontalAlignment.Right;
        VerticalAlignment = VerticalAlignment.Bottom;
        Margin = new Thickness(Space.Pad);
        Padding = new Thickness(Space.Md, 5);
        Height = 28;
        CornerRadius = new CornerRadius(14);   // a capsule
        Background = LibraryUi.Ink(Palette.FilmBase, 0.5);
        BorderBrush = LibraryUi.Ink(Palette.Text, 0.1);
        BorderThickness = new Thickness(1);

        for (int i = 0; i < 5; i++) {
            int n = i + 1;
            _faces[i] = new Border { Width = 16, Height = 18, Background = Brushes.Transparent };
            _starButtons[i] = LibraryUi.Plain(_faces[i], () => {
                if (LibraryModel.Shared.Open is { } p) LibraryModel.Shared.SetRating(p, n == p.Rating ? 0 : n);
            });
            _starButtons[i].PointerEntered += (_, _) => { _hovered = n; Refresh(); };
            _starButtons[i].PointerExited += (_, _) => { if (_hovered == n) { _hovered = null; Refresh(); } };
            _stars.Children.Add(_starButtons[i]);
        }
        _rejectButton = LibraryUi.Plain(_reject, () => { if (LibraryModel.Shared.Open is { } p) LibraryModel.Shared.ToggleRejected(p); });
        Child = new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = Space.Md,
            Children = { _stars, new Rectangle { Width = 1, Height = 14, Fill = LibraryUi.Ink(Palette.Text, 0.18) }, _rejectButton },
        };
        LibraryModel.Shared.Changed += Refresh;
        EngineModel.Shared.Opened += Refresh;
        Refresh();
    }

    void Refresh() {
        var photo = LibraryModel.Shared.Open;
        IsVisible = photo != null;
        if (photo == null) return;

        for (int n = 1; n <= 5; n++) {
            bool filled = n <= (_hovered ?? photo.Rating) && !photo.Rejected;
            var ink = photo.Rejected ? LibraryUi.Ink(Palette.Text, 0.25) : filled ? Palette.Star : LibraryUi.Ink(Palette.Text, 0.4);
            _faces[n - 1].Child = Icons.Create(filled ? "star.fill" : "star", 12, ink);
            _starButtons[n - 1].IsEnabled = !photo.Rejected;
            ToolTip.SetTip(_starButtons[n - 1], n == photo.Rating ? "Clear the rating" : $"{n} star{(n == 1 ? "" : "s")}  ({n})");
        }

        var mark = photo.Rejected ? Palette.Reject : LibraryUi.Ink(Palette.Text, 0.75);
        _reject.Children.Clear();
        _reject.Children.Add(Icons.Create(photo.Rejected ? "arrow.uturn.backward" : "xmark", 9, mark));
        _reject.Children.Add(new TextBlock { Text = photo.Rejected ? "Keep" : "Reject", FontSize = TypeSize.Xs,
                                             Foreground = mark, VerticalAlignment = VerticalAlignment.Center });
        ToolTip.SetTip(_rejectButton, photo.Rejected ? "Keep this photo (R)" : "Reject this photo (R)");
    }
}

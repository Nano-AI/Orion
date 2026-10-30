using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using OrionShell.CanvasSlice;

namespace OrionShell.Library;

/// A port of the Swift app's RatingBar.swift: rating and rejection over the photo, in the
/// canvas's lower-right corner, as Main.dc.html's translucent pill. Culling
/// happens while looking at the picture, so the mark being set belongs there,
/// not only as four-pixel dots on a thumbnail nobody is looking at.
///
/// Hovering a star previews the rating a click would set; clicking the star
/// already set clears it. A rejected photo cannot be rated until it is kept.
[Panel("rating-bar", Region.Canvas, 50)]
public sealed class RatingBarPanel : Border {
    readonly StateButton[] _stars = new StateButton[5];
    readonly StateButton _reject;
    int? _hovered;

    public RatingBarPanel() {
        HorizontalAlignment = HorizontalAlignment.Right;
        VerticalAlignment = VerticalAlignment.Bottom;
        Margin = new Thickness(16);
        for (int i = 0; i < 5; i++) {
            int n = i + 1;
            _stars[i] = new StateButton { Kind = ButtonKind.Bare, Width = 26, Height = 24, MinWidth = 0, Padding = default,
                                          CornerRadius = new CornerRadius(4) };
            _stars[i].Click += (_, _) => { if (LibraryModel.Shared.Open is { } p) LibraryModel.Shared.SetRating(p, n == p.Rating ? 0 : n); };
            _stars[i].PointerEntered += (_, _) => { _hovered = n; Refresh(); };
            _stars[i].PointerExited += (_, _) => { if (_hovered == n) { _hovered = null; Refresh(); } };
        }
        _reject = new StateButton { Kind = ButtonKind.Bare, Height = 24, MinWidth = 0, Padding = new Thickness(8, 0), CornerRadius = new CornerRadius(4) };
        _reject.Click += (_, _) => { if (LibraryModel.Shared.Open is { } p) LibraryModel.Shared.ToggleRejected(p); };
        var pill = Pill.Of([.. _stars, new Rectangle { Width = 1, Height = 16, Fill = Ink.White(0.12), Margin = new Thickness(4, 0) }, _reject]);
        pill.Padding = new Thickness(4, 3);
        Child = pill;
        LibraryModel.Shared.Changed += Refresh;
        EngineModel.Shared.Opened += Refresh;
        CanvasState.Changed += Place;   // every render: only whether to show, not the contents
        Refresh();
    }

    void Place() => IsVisible = LibraryModel.Shared.Open != null && CanvasState.Now == CanvasState.Scene.Picture;

    void Refresh() {
        var photo = LibraryModel.Shared.Open;
        Place();
        if (photo == null) return;

        for (int n = 1; n <= 5; n++) {
            bool filled = n <= (_hovered ?? photo.Rating) && !photo.Rejected;
            var star = _stars[n - 1];
            star.Content = Icons.Create(filled ? "star.fill" : "star", 15, Palette.Text);
            star.Ink = filled ? Palette.Star : Palette.Label;
            star.IsEnabled = !photo.Rejected;
            star.Help(n == photo.Rating ? "Clear the rating" : $"{n} star{(n == 1 ? "" : "s")} ({n})");
        }

        _reject.Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 5, Children = {
            Icons.Create(photo.Rejected ? "arrow.uturn.backward" : "xmark", 12, Palette.Text),
            new TextBlock { Text = photo.Rejected ? "Keep" : "Reject", VerticalAlignment = VerticalAlignment.Center },
        } };
        _reject.Ink = photo.Rejected ? Palette.Reject : Palette.Label;
        _reject.Help(photo.Rejected ? "Keep this photo (R)" : "Reject this photo (R)");
    }
}

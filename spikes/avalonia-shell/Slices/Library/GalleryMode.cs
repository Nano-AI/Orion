using Avalonia.Controls;
using Avalonia.Input;

namespace OrionShell.Library;

/// OrionApp.swift's `mode` (.develop or .cull) and `galleryFocus`, with the
/// gallery's comings and goings and its key map (OrionApp+Commands.swift).
/// The focus is deliberately not the photo on the canvas: browsing the grid
/// never decodes, and only Return or a double-click opens a raw.
static class GalleryMode {
    public static bool On { get; private set; }
    /// The cell the keys act on; null when the folder is empty.
    public static LibraryModel.Photo? Focus { get; private set; }
    /// How many columns the grid laid out last, so Up and Down move a row.
    public static int Columns { get; set; } = 1;
    /// On, off, or the focus moved.
    public static event Action? Changed;

    static EngineModel E => EngineModel.Shared;
    static LibraryModel L => LibraryModel.Shared;

    public static void Toggle() { if (On) Leave(); else Enter(); }

    /// `enterGallery`: lands on the photograph being edited.
    public static void Enter() {
        if (L.Photos.Count == 0) return;
        Focus = L.Open ?? L.Visible.FirstOrDefault();
        On = true;
        Changed?.Invoke();
    }

    /// `leaveGallery`: back to the canvas, which still holds what it held. Only a
    /// canvas with nothing on it (its photo was trashed from the grid) loads the focus.
    public static void Leave() {
        On = false;
        if (E.Current < 0 && (Focus ?? L.Visible.FirstOrDefault()) is { } next) E.Open(next.Index);
        Changed?.Invoke();
    }

    /// `openFromGallery`: Return or a double-click, the one gesture that decodes.
    public static void Open(LibraryModel.Photo photo) {
        Focus = photo;
        On = false;
        E.Open(photo.Index);
        Changed?.Invoke();
    }

    public static void SetFocus(LibraryModel.Photo? photo) {
        if (Focus == photo) return;
        Focus = photo;
        Changed?.Invoke();
    }

    public enum Direction { Left, Right, Up, Down }

    /// `moveGalleryFocus`, over the photos the filter shows.
    public static void Move(Direction d) {
        var list = L.Visible.ToList();
        if (list.Count == 0) return;
        int i = Focus is { } f ? list.IndexOf(f) : 0;
        SetFocus(list[Moved(i, d, Columns, list.Count)]);
    }

    /// GalleryLayout.move: clamps at the edges; Down into a ragged last row only
    /// when there is a row below at all.
    public static int Moved(int index, Direction d, int columns, int count) {
        if (count <= 0) return 0;
        if (index < 0 || index >= count || columns <= 0) return Math.Clamp(index, 0, count - 1);
        switch (d) {
            case Direction.Left: return Math.Max(index - 1, 0);
            case Direction.Right: return Math.Min(index + 1, count - 1);
            case Direction.Up: return index - columns >= 0 ? index - columns : index;
            default:
                if (index + columns < count) return index + columns;
                int lastRowStart = (count - 1) / columns * columns;
                return index < lastRowStart ? count - 1 : index;
        }
    }

    /// `handleGalleryKey`: arrows move the ring, Return opens, Escape and G leave.
    /// The culling keys are the command table's (they act on Focus through
    /// CommandTable.Focused); the canvas keys are swallowed, because acting on a
    /// view that is not showing and saying nothing is worse than doing nothing.
    /// True when the key was the gallery's.
    public static bool Handle(KeyEventArgs e) {
        if (!On || e.Handled || e.Source is TextBox || e.KeyModifiers.HasFlag(KeyModifiers.Meta)
            || e.KeyModifiers.HasFlag(KeyModifiers.Control)) return false;
        switch (e.Key) {
            case Key.Escape: case Key.G: Leave(); return true;
            case Key.Enter: if (Focus is { } f) Open(f); return true;
            case Key.Left: Move(Direction.Left); return true;
            case Key.Right: Move(Direction.Right); return true;
            case Key.Up: Move(Direction.Up); return true;
            case Key.Down: Move(Direction.Down); return true;
            case Key.D0: case Key.D9: case Key.OemPipe: case Key.OemOpenBrackets: case Key.OemCloseBrackets: return true;
            default: return false;
        }
    }
}

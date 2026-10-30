using Avalonia;
using Avalonia.Input;
using Avalonia.Media.Imaging;

namespace OrionShell.Library;

/// What the filmstrip, the gallery and the rating bar read: the folder's
/// photographs with their marks, the filter, and the one-line summary.
/// The names and rules are Library.swift's, so the three views can never
/// disagree about what a filter shows or what the count says.
///
/// Ratings, rejections and the colour label live in each photo's XMP sidecar,
/// merged as Library.persist merges them (Sidecar.Merge keeps the edit), and
/// read back from it when the folder lists. A cull acts on the group
/// (PhotoSelection) when the photo is part of a real one, else on that photo.
public sealed class LibraryModel {
    public static LibraryModel Shared { get; } = new();

    public enum Filter { All, Rated, Unrated, Rejected }

    /// Library.Filter.title, in allCases order.
    public static readonly string[] FilterTitles = ["All", "Rated", "Unrated", "Rejected"];

    public enum Order { Name, Rating }
    public static readonly string[] OrderTitles = ["File name", "Rating, best first"];

    public sealed class Photo(string path, int index) {
        public string Path { get; } = path;
        /// Its position in EngineModel.Photos, which is what Open takes.
        public int Index { get; internal set; } = index;
        public string Name { get; } = System.IO.Path.GetFileName(path);
        public int Rating { get; internal set; }
        public bool Rejected { get; internal set; }
        /// Free text, as XMP has it. Nothing here sets one; it is kept so a write never drops it.
        public string? Label { get; internal set; }

        /// The file name, and under it the exposure: CaptureInfo.tooltip. Read
        /// from the file on first ask, not at listing: the shell has no index.
        public string Tooltip => _tooltip ??= Chrome.CaptureInfo.Tooltip(Name, Chrome.CaptureInfo.ReadFile(Path));
        string? _tooltip;

        Bitmap? _thumbnail;
        bool _tried;
        /// The camera's embedded JPEG, turned upright. Null when the raw has none.
        public Bitmap? Thumbnail {
            get {
                if (!_tried) { _tried = true; _thumbnail = Thumbnails.Load(Path); }
                return _thumbnail;
            }
        }
    }

    /// A new list (a folder opened, photos trashed) is a change every view shows,
    /// and the selection keeps only what is still in view. The canvas moving to
    /// another photo collapses the selection onto it (Library.focus); a sort,
    /// which keeps the same photo open, does not.
    LibraryModel() {
        var engine = EngineModel.Shared;
        Selection.Focus(_canvas = Canvas());   // the first photo may have opened before anything asked for this
        engine.PropertyChanged += (_, e) => {
            if (e.PropertyName == nameof(EngineModel.Photos)) {
                Selection.Confine(VisiblePaths);
                Changed?.Invoke();
            } else if (e.PropertyName == nameof(EngineModel.Current)) {
                string? now = Canvas();
                if (now == _canvas) return;
                _canvas = now;
                bool shown = HasExplicitSelection;
                Selection.Focus(now);
                if (shown) SelectionChanged?.Invoke();   // a selection of one draws nothing, so only a group's collapse repaints
            }
        };
    }

    static string? Canvas() => EngineModel.Shared.Current is var i and >= 0 ? EngineModel.Shared.Photos[i] : null;

    string? _canvas;

    readonly Dictionary<string, Photo> _byPath = new();
    IReadOnlyList<string>? _source;
    List<Photo> _photos = [];
    Filter _filter;

    /// Something a view shows changed: a mark, the filter, the folder.
    public event Action? Changed;

    /// Every photograph in the folder, in EngineModel.Photos order.
    public IReadOnlyList<Photo> Photos {
        get {
            var source = EngineModel.Shared.Photos;
            if (!ReferenceEquals(source, _source)) {
                _source = source;
                // A photo keeps its marks when the list around it changes (a trash
                // shifts every index after it); only its position moves.
                _photos = source.Select((p, i) => {
                    if (!_byPath.TryGetValue(p, out var photo)) _byPath[p] = photo = Listed(p, i);
                    photo.Index = i;
                    return photo;
                }).ToList();
            }
            return _photos;
        }
    }

    public Filter Current {
        get => _filter;
        // The one moment a selection can come to hold photos nobody can see.
        set { if (_filter != value) { _filter = value; Selection.Confine(VisiblePaths); Changed?.Invoke(); } }
    }

    /// The photograph on the canvas, if any.
    public Photo? Open => EngineModel.Shared.Current is var i and >= 0 && i < Photos.Count ? Photos[i] : null;

    /// Photos passing the current filter, in the order they appear on disk.
    public IEnumerable<Photo> Visible => Photos.Where(p => _filter switch {
        Filter.Rated => p.Rating >= 1 && !p.Rejected,
        Filter.Unrated => p.Rating == 0 && !p.Rejected,
        Filter.Rejected => p.Rejected,
        _ => true,
    });

    public int RejectedCount => Photos.Count(p => p.Rejected);

    public Order Sorted { get; private set; }

    /// Puts the photos in `order`, and browsing with them: the engine's own list
    /// is reordered (EngineModel.Reorder), so ←/→, the strip, the gallery and
    /// the cache's prediction follow one order. Applied when chosen, never live:
    /// re-sorting as each photo is rated would pull it out from under the hand.
    /// Choosing it again re-sorts after new marks.
    public void Sort(Order order) {
        Sorted = order;
        var byName = StringComparer.OrdinalIgnoreCase;   // a folder's own order (LibraryFiles)
        var sorted = order == Order.Rating
            ? Photos.OrderBy(p => p.Rejected).ThenByDescending(p => p.Rating).ThenBy(p => p.Path, byName)
            : Photos.OrderBy(p => p.Path, byName);
        EngineModel.Shared.Reorder(sorted.Select(p => p.Path).ToList());
        Changed?.Invoke();
    }

    // ── Marks (Library.swift's Ratings), in the sidecar ─────────────────────

    /// Whether a mark reaches the sidecar. Only the app at a window writes: the
    /// headless flags read sidecars and never write the photographer's (as Autosave).
    public bool Writes { get; set; }

    /// A photo as the listing finds it: its marks from its sidecar, if it has one.
    internal static Photo Listed(string path, int index) {
        var photo = new Photo(path, index);
        if (Sidecar.Read(path) is { Unreadable: null } s) (photo.Rating, photo.Rejected, photo.Label) = (s.Rating, s.Rejected, s.Label);
        return photo;
    }

    /// A rating un-rejects, as Swift's does.
    public void SetRating(Photo photo, int stars) => SetRating([photo], stars);

    public void SetRating(IEnumerable<Photo> photos, int stars) {
        foreach (var p in photos) {
            (p.Rating, p.Rejected) = (Math.Clamp(stars, 0, 5), false);
            Persist(p);
        }
        Changed?.Invoke();
    }

    /// Set, never toggled, across a group: the clicked photo decides the
    /// direction, or a mixed selection would flip into its own negative.
    /// A rejection clears the rating.
    public void SetRejected(Photo photo, bool rejected) => SetRejected([photo], rejected);

    public void SetRejected(IEnumerable<Photo> photos, bool rejected) {
        foreach (var p in photos) {
            p.Rejected = rejected;
            if (rejected) p.Rating = 0;
            Persist(p);
        }
        Changed?.Invoke();
    }

    public void ToggleRejected(Photo photo) => SetRejected(photo, !photo.Rejected);

    /// Merged, not rebuilt: the edit lives in the same file. A mark that did not
    /// land is said on the status line, and the strip keeps showing it, as Swift's does.
    void Persist(Photo p) {
        if (!Writes) return;
        if (!Sidecar.Merge(p.Path, s => (s.Rating, s.Rejected, s.Label) = (p.Rating, p.Rejected, p.Label)))
            EngineModel.Shared.Say($"Ratings could not be saved for {p.Name}. The card or folder may be read-only.");
    }

    // ── Selection (Library.swift over PhotoSelection) ───────────────────────

    public PhotoSelection Selection { get; private set; } = new();

    /// The group changed and nothing else did: views repaint their rings
    /// rather than rebuild, so the cell under a double-click survives its first click.
    public event Action? SelectionChanged;

    List<string> VisiblePaths => Visible.Select(p => p.Path).ToList();

    public bool HasExplicitSelection => Selection.IsExplicit;
    public bool IsSelected(Photo photo) => Selection.Contains(photo.Path);

    /// What a batch acts on, in strip order: the selection when it is real, else everything in view.
    public List<Photo> Targets => HasExplicitSelection ? Visible.Where(IsSelected).ToList() : Visible.ToList();

    /// What a cull aimed at `photo` covers: the selection when it is part of a real one, else that photo.
    public List<Photo> CullScope(Photo photo) => HasExplicitSelection && IsSelected(photo) ? Targets : [photo];

    /// "12 selected", or empty for the resting state.
    public string SelectionSummary => Selection.Summary(VisiblePaths);

    /// A click in the gallery: the photo to focus, or null when it only changed the selection.
    public Photo? Click(Photo photo, PhotoSelection.Modifiers modifiers) {
        var focus = Selection.Click(photo.Path, modifiers, VisiblePaths);
        SelectionChanged?.Invoke();
        return focus == null ? null : photo;
    }

    /// A click on the strip: a plain one opens; a modified one only selects,
    /// unless its shift anchor is out of view, when it falls back to opening.
    /// Tried on a copy, so an open the canvas refuses (an unsaved edit) leaves the selection as it was.
    public void ClickStrip(Photo photo, PhotoSelection.Modifiers modifiers) {
        var trial = Selection.Copy();
        if (modifiers == PhotoSelection.Modifiers.None || trial.Click(photo.Path, modifiers, VisiblePaths) != null) {
            EngineModel.Shared.Open(photo.Index);
            return;
        }
        Selection = trial;
        SelectionChanged?.Invoke();
    }

    public void SelectAll() { Selection.SelectAll(VisiblePaths); SelectionChanged?.Invoke(); }
    public void CollapseSelection() { Selection.Collapse(); SelectionChanged?.Invoke(); }

    /// The platform's command key: ⌘ on macOS, Ctrl elsewhere.
    public static PhotoSelection.Modifiers From(KeyModifiers k) =>
        (k.HasFlag(OperatingSystem.IsMacOS() ? KeyModifiers.Meta : KeyModifiers.Control) ? PhotoSelection.Modifiers.Command : 0)
        | (k.HasFlag(KeyModifiers.Shift) ? PhotoSelection.Modifiers.Shift : 0);
}

/// Embedded previews through the facade (orion_read_thumbnail via the shim).
static class Thumbnails {
    // ponytail: one decode size serves the strip and the gallery's largest
    // cell (400 pt at 2x); per-view sizes when folders reach thousands.
    const int DecodeWidth = 800;
    static readonly byte[] Buffer = new byte[8 << 20];

    public static unsafe Bitmap? Load(string path) {
        int size, turns;
        fixed (byte* p = Buffer) size = Native.shim_thumbnail(path, p, Buffer.Length, out turns);
        if (size <= 0) return null;
        using var stream = new MemoryStream(Buffer, 0, size);
        return Upright(Bitmap.DecodeToWidth(stream, DecodeWidth), turns);
    }

    /// The JPEG carries no orientation; `turns` clockwise quarter turns stand
    /// it up (orion.h). Done once on the pixels, so every view sees the true
    /// aspect instead of a landscape box with a rotated picture in it.
    static Bitmap Upright(Bitmap bitmap, int turns) {
        turns = ((turns % 4) + 4) % 4;
        if (turns == 0) return bitmap;
        var src = bitmap.PixelSize;
        var size = turns % 2 == 1 ? new PixelSize(src.Height, src.Width) : src;
        var upright = new RenderTargetBitmap(size);
        using (var c = upright.CreateDrawingContext())
        using (c.PushTransform(Matrix.CreateTranslation(-src.Width / 2.0, -src.Height / 2.0) *
                               Matrix.CreateRotation(turns * Math.PI / 2) *
                               Matrix.CreateTranslation(size.Width / 2.0, size.Height / 2.0)))
            c.DrawImage(bitmap, new Rect(0, 0, src.Width, src.Height));
        bitmap.Dispose();
        return upright;
    }
}

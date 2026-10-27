using Avalonia;
using Avalonia.Media.Imaging;

namespace OrionShell.Library;

/// What the filmstrip, the gallery and the rating bar read: the folder's
/// photographs with their marks, the filter, and the one-line summary.
/// The names and rules are Library.swift's, so the three views can never
/// disagree about what a filter shows or what the count says.
///
/// Ratings and rejections live in memory here; Swift keeps them in XMP
/// sidecars, and its PhotoSelection (command/shift-click groups) is not ported,
/// so every cull acts on the one photo clicked.
// stub(#315): Library.swift owns this until the logic moves below the facade
public sealed class LibraryModel {
    public static LibraryModel Shared { get; } = new();

    public enum Filter { All, Rated, Unrated, Rejected }

    /// Library.Filter.title, in allCases order.
    public static readonly string[] FilterTitles = ["All", "Rated", "Unrated", "Rejected"];

    public sealed class Photo(string path, int index) {
        public string Path { get; } = path;
        /// Its position in EngineModel.Photos, which is what Open takes.
        public int Index { get; } = index;
        public string Name { get; } = System.IO.Path.GetFileName(path);
        public int Rating { get; internal set; }
        public bool Rejected { get; internal set; }
        // CaptureInfo.tooltip adds aperture, shutter and ISO from the index.
        // stub(#315): Library.swift owns this until the logic moves below the facade
        public string Tooltip => Name;

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
                _photos = source.Select((p, i) => _byPath.TryGetValue(p, out var known) && known.Index == i
                    ? known : _byPath[p] = new Photo(p, i)).ToList();
            }
            return _photos;
        }
    }

    public Filter Current {
        get => _filter;
        set { if (_filter != value) { _filter = value; Changed?.Invoke(); } }
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

    /// Library.summary, word for word ("1 photos" included).
    public string Summary {
        get {
            if (Photos.Count == 0) return "";
            var parts = new List<string> { $"{Photos.Count} photos" };
            if (RejectedCount > 0) parts.Add($"{RejectedCount} rejected");
            return string.Join(" · ", parts);
        }
    }

    public void SetRating(Photo photo, int stars) {
        photo.Rating = Math.Clamp(stars, 0, 5);
        Changed?.Invoke();
    }

    public void SetRejected(Photo photo, bool rejected) {
        photo.Rejected = rejected;
        Changed?.Invoke();
    }

    public void ToggleRejected(Photo photo) => SetRejected(photo, !photo.Rejected);
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

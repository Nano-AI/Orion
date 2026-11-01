using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace OrionShell.Library;

/// The two sizes a thumbnail is decoded at, as Lightroom keeps a small one for
/// every photo and a larger one only for what is in view.
public enum ThumbnailSize { Strip, Large }

/// Embedded previews through the facade (orion_read_thumbnail via the shim),
/// read on one background thread and shown as each lands: a folder appears at
/// once and its pictures fill in, as its marks do (ReadMarks). Read on the UI
/// thread, 336 raws on a spinning disk held the window blank for half a minute
/// before the first photo (#399).
///
/// Every photo gets the strip's size (256 px, ~0.2 MB); the gallery's cells
/// and its aside ask for the large one (800 px, its largest cell at 2x, ~1.7 MB)
/// only while on screen, and only the most recently seen are held. All at 800,
/// a 336-photo folder held 590 MB of thumbnails, a 3000-photo one would hold 5 GB.
static class Thumbnails {
    static readonly int[] Widths = [256, 800];   // by ThumbnailSize
    const int LargeKept = 64;   // held beyond what is on screen: a scroll back finds them
    static byte[] _buffer = new byte[8 << 20];   // the reader thread's alone; grows for a larger preview
    static readonly object Gate = new();
    static readonly HashSet<(LibraryModel.Photo Photo, ThumbnailSize Size)> Wanted = [];
    static readonly LinkedList<LibraryModel.Photo> Recent = new();   // UI thread: large ones held, most recently seen first
    static readonly Dictionary<LibraryModel.Photo, int> Showing = [];  // UI thread: views showing a photo's large one
    static bool _running;   // under Gate: the reader thread is up
    static int _owed;       // asked for and not yet landed: the harness waits for 0

    /// Thumbnails asked for and not yet shown. The screenshot harness waits on it.
    public static bool Busy => Volatile.Read(ref _owed) > 0;

    public static void Want(LibraryModel.Photo photo, ThumbnailSize size) {
        lock (Gate) {
            if (!Wanted.Add((photo, size))) return;
            Interlocked.Increment(ref _owed);
            if (!_running) { _running = true; Task.Run(Read); }
        }
    }

    /// A new listing: what was asked for and is no longer listed is never read,
    /// and a large one held for a photo no longer listed goes.
    public static void Keep(IReadOnlySet<LibraryModel.Photo> listed) {
        lock (Gate) {
            var gone = Wanted.Where(w => !listed.Contains(w.Photo)).ToList();
            foreach (var w in gone) { Wanted.Remove(w); w.Photo.Forget(w.Size); }
            Interlocked.Add(ref _owed, -gone.Count);
        }
        for (var node = Recent.First; node != null;) {
            var next = node.Next;
            if (!listed.Contains(node.Value) && !Showing.ContainsKey(node.Value)) {
                node.Value.Forget(ThumbnailSize.Large);
                Recent.Remove(node);
            }
            node = next;
        }
    }

    /// A view shows `photo`'s large thumbnail now: never let go while one does.
    public static void Show(LibraryModel.Photo photo) {
        Showing[photo] = Showing.GetValueOrDefault(photo) + 1;
        Held(photo);
    }

    /// A view stopped showing it: it may go, least recently seen first, and if
    /// it was never read, it is not read now (a fling past a hundred cells).
    public static void Hide(LibraryModel.Photo photo) {
        if (Showing.TryGetValue(photo, out int n) && n > 1) { Showing[photo] = n - 1; return; }
        Showing.Remove(photo);
        lock (Gate) {
            if (Wanted.Remove((photo, ThumbnailSize.Large))) {
                photo.Forget(ThumbnailSize.Large);
                Interlocked.Decrement(ref _owed);
            }
        }
        Trim();
    }

    static void Held(LibraryModel.Photo photo) {
        if (!photo.Holds(ThumbnailSize.Large)) return;
        Recent.Remove(photo);
        Recent.AddFirst(photo);
        Trim();
    }

    /// Down to LargeKept off screen, the least recently seen first: what is on
    /// screen is never counted, so a wall of cells does not push out the rest.
    static void Trim() {
        int offScreen = Recent.Count(p => !Showing.ContainsKey(p));
        for (var node = Recent.Last; node != null && offScreen > LargeKept;) {
            var previous = node.Previous;
            if (!Showing.ContainsKey(node.Value)) {
                node.Value.Forget(ThumbnailSize.Large);
                Recent.Remove(node);
                offScreen--;
            }
            node = previous;
        }
    }

    static void Read() {
        bool drained = false;   // Next said so under Gate, and cleared _running there
        try {
            while (Next() is (var photo, var size)) {
                Bitmap? picture = null;
                try {
                    picture = Load(photo.Path, Widths[(int)size], ref _buffer);
                } catch (Exception e) {
                    // One unreadable file is a raw with no preview, not the end of the strip.
                    Console.Error.WriteLine($"thumbnail {photo.Name}: {e.Message}");
                }
                Dispatcher.UIThread.Post(() => {
                    Interlocked.Decrement(ref _owed);
                    photo.Land(size, picture);
                    if (size == ThumbnailSize.Large) Held(photo);
                });
            }
            drained = true;
        } finally {
            // Thrown out of the loop: let the next Want start a reader. Never
            // after draining, when a new one may already be running.
            if (!drained) lock (Gate) _running = false;
        }
    }

    /// What to read next, once the photo on the canvas has the disk to itself
    /// (it decodes from it first, and the hand may move meanwhile): a large one,
    /// which is on screen, else the strip's nearest the open photo. Null when
    /// nothing is wanted.
    static (LibraryModel.Photo, ThumbnailSize)? Next() {
        for (int waited = 0; EngineModel.Shared.Opening && waited < 5000; waited += 20) Thread.Sleep(20);
        lock (Gate) {
            if (Wanted.Count == 0) { _running = false; return null; }
            int open = EngineModel.Shared.Current;   // ints, written on the UI thread: a stale one only reorders
            var next = Wanted.MinBy(w => (w.Size == ThumbnailSize.Large ? 0 : 1, Math.Abs(w.Photo.Index - open)));
            Wanted.Remove(next);
            return next;
        }
    }

    /// One preview read now, on the caller's thread, at the large size (the Stack
    /// sheet's split painter, #391): with its own buffer, so it never races the reader.
    public static Bitmap? Preview(string path) {
        var buffer = new byte[8 << 20];
        return Load(path, Widths[(int)ThumbnailSize.Large], ref buffer);
    }

    /// The preview, decoded `width` wide and turned upright, through `buffer`,
    /// which grows for a larger one.
    static unsafe Bitmap? Load(string path, int width, ref byte[] buffer) {
        int size, turns;
        fixed (byte* p = buffer) size = Native.shim_thumbnail(path, p, buffer.Length, out turns);
        if (size < 0) {
            buffer = new byte[-size];
            fixed (byte* p = buffer) size = Native.shim_thumbnail(path, p, buffer.Length, out turns);
        }
        if (size <= 0) return null;
        using var stream = new MemoryStream(buffer, 0, size);
        return Upright(Bitmap.DecodeToWidth(stream, width), turns);
    }

    /// The JPEG carries no orientation; `turns` clockwise quarter turns stand
    /// it up (orion.h). Done once on the pixels, so every view sees the true
    /// aspect instead of a landscape box with a rotated picture in it. Moved
    /// pixel by pixel straight into the new bitmap rather than drawn, so it
    /// needs no renderer and runs here.
    static Bitmap Upright(Bitmap bitmap, int turns) {
        turns = ((turns % 4) + 4) % 4;
        if (turns == 0) return bitmap;
        using var src = bitmap;
        int w = src.PixelSize.Width, h = src.PixelSize.Height;
        bool swap = turns % 2 == 1;
        var upright = new WriteableBitmap(swap ? new PixelSize(h, w) : src.PixelSize, src.Dpi,
                                          src.Format ?? Avalonia.Platform.PixelFormat.Bgra8888, src.AlphaFormat);
        var pixels = System.Buffers.ArrayPool<uint>.Shared.Rent(w * h);
        try {
            unsafe {
                fixed (uint* p = pixels) src.CopyPixels(new PixelRect(0, 0, w, h), (nint)p, w * h * 4, w * 4);
                using var fb = upright.Lock();
                byte* dst = (byte*)fb.Address;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        var (tx, ty) = turns switch { 1 => (h - 1 - y, x), 2 => (w - 1 - x, h - 1 - y), _ => (y, w - 1 - x) };
                        *(uint*)(dst + ty * fb.RowBytes + tx * 4) = pixels[y * w + x];
                    }
            }
        } finally {
            System.Buffers.ArrayPool<uint>.Shared.Return(pixels);
        }
        return upright;
    }
}

/// A photo's thumbnail wherever a view shows one: the picture once it is read,
/// nothing before (the cell's own ground is the placeholder). A large one shows
/// the strip's until its own lands, and goes back to it off screen, so what the
/// gallery holds is what is in view (Thumbnails.LargeKept).
sealed class ThumbnailImage : Image {
    readonly ThumbnailSize _size;
    LibraryModel.Photo? _photo;
    LibraryModel.Photo? _showing;   // whose large one this has told Thumbnails it shows
    bool _attached, _seen;

    public ThumbnailImage(LibraryModel.Photo? photo = null, ThumbnailSize size = ThumbnailSize.Strip) {
        _size = size;
        Stretch = Avalonia.Media.Stretch.Uniform;
        Photo = photo;
    }

    public LibraryModel.Photo? Photo {
        get => _photo;
        set {
            if (_attached && _photo != null) _photo.ThumbnailLanded -= Landed;
            _photo = value;
            if (_attached && _photo != null) _photo.ThumbnailLanded += Landed;
            Landed();
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        _attached = true;
        if (_photo != null) _photo.ThumbnailLanded += Landed;
        if (_size == ThumbnailSize.Large) EffectiveViewportChanged += Viewport;
        Landed();   // landed while this was off screen
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
        _attached = _seen = false;
        if (_photo != null) _photo.ThumbnailLanded -= Landed;
        EffectiveViewportChanged -= Viewport;
        Landed();   // tells Thumbnails it no longer shows a large one
        base.OnDetachedFromVisualTree(e);
    }

    void Viewport(object? sender, EffectiveViewportChangedEventArgs e) {
        bool seen = e.EffectiveViewport.Intersects(new Rect(Bounds.Size)) && Bounds.Width > 0;
        if (seen == _seen) return;
        _seen = seen;
        Landed();
    }

    void Landed() {
        var large = _size == ThumbnailSize.Large && _attached && _seen ? _photo : null;
        // The picture first: what Hide lets go is disposed, and must not be on screen then.
        Source = large?.Picture(ThumbnailSize.Large) ?? _photo?.Thumbnail;
        if (large != _showing) {
            if (large != null) Thumbnails.Show(large);
            if (_showing != null) Thumbnails.Hide(_showing);
            _showing = large;
        }
    }
}

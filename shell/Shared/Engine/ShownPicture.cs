using Avalonia;
using Avalonia.Media.Imaging;

namespace OrionShell;

/// Which graph made a picture: the shim's 1 and 2 (shim_render), 3 for only
/// part of the picture (shim_render_region, #22), 0 for none. Failed is a
/// region render that failed (out of memory, a lost device): no picture, and
/// not a reason to render the whole graph, which needs far more memory.
public enum Graph { Failed = -1, None = 0, Preview = 1, Full = 2, Region = 3 }

/// The one picture the canvas shows. Everything that wants a picture on screen
/// goes through Show or ShowLive; the canvas repaints on Changed. A kept picture
/// (the cache's) is a CPU bitmap. A render is either copied into one, or, while
/// the canvas can take it straight from the GPU (OnGpu, #317), left there: Live.
public sealed class ShownPicture {
    Bitmap? _bitmap;

    /// The picture as a CPU bitmap. A live render is copied off the GPU here,
    /// once, on first ask: the compare capture, the watermark preview, and the
    /// canvas itself while its shared surface does not hold this picture yet.
    public Bitmap? Bitmap => _bitmap ??= Live is { Handle: not 0 } s ? s.Read(Graph) : null;
    public Graph Graph { get; private set; }
    /// The part of the displayed picture this shows, normalized: all of it
    /// except for a Region render, which the canvas places by this.
    public Rect Uv { get; private set; } = Unit;
    static readonly Rect Unit = new(0, 0, 1, 1);
    /// The session whose last render of Graph this is, while it has not been copied off the GPU.
    public Session? Live { get; private set; }
    /// Something to show, found without copying a live render to find out.
    public bool Any => _bitmap != null || Live != null;
    /// Set by the canvas while it can show a render with no CPU copy (SharedPicture):
    /// EngineModel.Render then calls ShowLive instead of copying.
    public bool OnGpu { get; set; }
    /// After every Show, including the same bitmap with new pixels.
    public event Action? Changed;

    public void Show(Bitmap bitmap, Graph graph, Rect? uv = null) => Set(bitmap, null, graph, uv);

    /// `session`'s last render of `graph`, still on the GPU.
    public void ShowLive(Session session, Graph graph, Rect? uv = null) => Set(null, session, graph, uv);

    /// Nothing to show: a photo opening with no kept picture, so the canvas
    /// does not hold the previous photograph under the new one's name.
    public void Clear() => Set(null, null, Graph.None);

    void Set(Bitmap? bitmap, Session? live, Graph graph, Rect? uv = null) {
        (_bitmap, Live, Graph, Uv) = (bitmap, live, graph, uv ?? Unit);
        Changed?.Invoke();
    }
}

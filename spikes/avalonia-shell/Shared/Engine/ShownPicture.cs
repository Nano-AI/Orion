using Avalonia.Media.Imaging;

namespace OrionShell;

/// Which graph made a picture: the shim's 1 and 2 (shim_render), 0 for none.
public enum Graph { None = 0, Preview = 1, Full = 2 }

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
    /// The session whose last render of Graph this is, while it has not been copied off the GPU.
    public Session? Live { get; private set; }
    /// Something to show, found without copying a live render to find out.
    public bool Any => _bitmap != null || Live != null;
    /// Set by the canvas while it can show a render with no CPU copy (SharedPicture):
    /// EngineModel.Render then calls ShowLive instead of copying.
    public bool OnGpu { get; set; }
    /// After every Show, including the same bitmap with new pixels.
    public event Action? Changed;

    public void Show(Bitmap bitmap, Graph graph) => Set(bitmap, null, graph);

    /// `session`'s last render of `graph`, still on the GPU.
    public void ShowLive(Session session, Graph graph) => Set(null, session, graph);

    /// Nothing to show: a photo opening with no kept picture, so the canvas
    /// does not hold the previous photograph under the new one's name.
    public void Clear() => Set(null, null, Graph.None);

    void Set(Bitmap? bitmap, Session? live, Graph graph) {
        (_bitmap, Live, Graph) = (bitmap, live, graph);
        Changed?.Invoke();
    }
}

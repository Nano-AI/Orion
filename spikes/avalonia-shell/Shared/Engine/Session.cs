using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace OrionShell;

/// One open photo over one shim handle: its adjustments by C field name
/// (OrionAdjustments in orion.h), its renders, and the pixels of the last one.
/// No events and no UI state: EngineModel decides when to render and what to
/// show, and a cache may hold several of these at once (decision #318).
/// The calls stage 1b builds natively are in SessionSeams.cs.
///
/// One thread at a time: the shim is not thread-safe per handle. Separate
/// sessions are independent (each engine has its own command queue and LibRaw
/// instance; they share only the Metal device, which is thread-safe, and
/// read-only statics: engine/src/Engine.cpp, gpu/MetalDevice.mm), so a cache
/// may open and render one on a worker and then hand it to the UI thread.
public sealed partial class Session : IDisposable {
    nint _shim;
    /// One bitmap per graph, reused render after render: same bitmap, new pixels.
    readonly Dictionary<Graph, WriteableBitmap> _pictures = new();

    Session(string path, nint shim) {
        (Path, _shim) = (path, shim);
        Handle = Native.shim_engine(shim);
    }

    /// Decodes the RAW and applies the default look. Null when the shim cannot
    /// open it; its reason goes to stderr.
    public static Session? Open(string path) => Native.shim_open(path) is var shim and not 0 ? new Session(path, shim) : null;

    public string Path { get; }

    /// The OrionEngine*, for a slice's own orion_* imports. 0 once disposed.
    public nint Handle { get; private set; }

    nint Shim => _shim != 0 ? _shim : throw new ObjectDisposedException(nameof(Session), Path);

    /// Throws on a misspelt field or an index past an array (SLICES.md rule 8).
    public float Get(string field, int index = 0) {
        float v = Native.shim_get(Shim, field, index, out int ok);
        if (ok == 0) throw new ArgumentException($"no adjustment '{field}[{index}]' in OrionAdjustments", nameof(field));
        return v;
    }

    /// Writes one field. Does not render: the caller says which graph, and when.
    public void Set(string field, float value, int index = 0) {
        if (Native.shim_set(Shim, field, index, value) == 0)
            throw new ArgumentException($"no adjustment '{field}[{index}]' in OrionAdjustments", nameof(field));
    }

    /// Renders the quarter-resolution preview graph or the full one. The shim
    /// falls back to the full graph when there is no preview graph.
    public Graph Render(bool preview) {
        var graph = (Graph)Native.shim_render(Shim, preview ? 1 : 0, out _);
        if (graph == Graph.Full) RenderedFull = true;
        return graph;
    }

    /// Has rendered the full graph: its intermediates (1.35 GB, #318) stay
    /// allocated until ReleaseRenderMemory.
    public bool RenderedFull { get; private set; }

    /// The last preview read: the screen-sized picture a cache keeps for this photo.
    public WriteableBitmap? Picture => _pictures.GetValueOrDefault(Graph.Preview);

    /// The last render of `graph`, copied into this session's bitmap for it.
    /// Null when there is nothing to read. The copy is the spike's cost, not
    /// the design's: #317 replaces it with a shared texture.
    public unsafe WriteableBitmap? Read(Graph graph) {
        if (graph == Graph.None || Native.shim_read(Shim, (int)graph, null, 0, out int w, out int h) == 0) return null;
        if (!_pictures.TryGetValue(graph, out var bitmap) || bitmap.PixelSize != new PixelSize(w, h)) {
            bitmap = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
            _pictures[graph] = bitmap;
        }
        using var fb = bitmap.Lock();
        if (fb.RowBytes == w * 4) {
            Native.shim_read(_shim, (int)graph, (byte*)fb.Address, fb.RowBytes * h, out _, out _);
        } else {
            var tmp = new byte[w * h * 4];
            fixed (byte* p = tmp) Native.shim_read(_shim, (int)graph, p, tmp.Length, out _, out _);
            for (int y = 0; y < h; y++) Marshal.Copy(tmp, y * w * 4, fb.Address + y * fb.RowBytes, w * 4);
        }
        return bitmap;
    }

    /// The full-resolution output size (crop preview included).
    public PixelSize ImageSize =>
        Handle != 0 && Native.orion_engine_image_size(Handle, out uint w, out uint h) == 0 ? new PixelSize((int)w, (int)h) : default;

    /// The engine's reason for the last failure.
    public string LastError => Marshal.PtrToStringUTF8(Native.orion_last_error(Handle)) ?? "";

    public void Dispose() {
        if (_shim != 0) Native.shim_close(_shim);
        (_shim, Handle) = (0, 0);
        _pictures.Clear();
    }
}

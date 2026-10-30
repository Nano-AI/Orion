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
    /// open it; its reason goes to stderr. `headless`: no preview graph and no
    /// intermediates kept between renders, for a process that develops once and
    /// exits (`--agent`, `--batch-export`; decision #312), never the canvas.
    public static Session? Open(string path, bool headless = false) =>
        Native.shim_open_mode(path, headless ? 1 : 0) is var shim and not 0 ? new Session(path, shim) : null;

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
        Interlocked.Increment(ref _renders);
        var graph = (Graph)Native.shim_render(Shim, preview ? 1 : 0, out _);
        if (graph == Graph.Full) RenderedFull = true;
        return graph;
    }

    /// Only `uv` of the displayed picture, at `pixels` (the screen's): the
    /// canvas past fit (#22). Region, or None when the engine needs the whole
    /// graph for this edit, and the caller renders that instead; Failed when
    /// the render failed, and LastError says why.
    public Graph RenderRegion(Avalonia.Rect uv, PixelSize pixels, bool interacting = false) {
        Interlocked.Increment(ref _renders);
        return (Graph)Native.shim_render_region(Shim, (float)uv.X, (float)uv.Y, (float)uv.Width, (float)uv.Height,
                                                pixels.Width, pixels.Height, interacting ? 1 : 0, out _);
    }

    /// Every engine render in the process, on any session: what a gesture that
    /// must cost none (the compare divider's drag) is measured by.
    public static int Renders => _renders;
    static int _renders;

    /// Has rendered the full graph since it was last lightened: its
    /// intermediates (1.35 GB, #318) are allocated and the preview picture may
    /// be older than the last edit.
    public bool RenderedFull { get; private set; }

    /// The last preview read: the screen-sized picture a cache keeps for this photo.
    public WriteableBitmap? Picture => _pictures.GetValueOrDefault(Graph.Preview);

    /// What a cache does to a session it keeps but is not showing: the graphs'
    /// textures go (ReleaseRenderMemory) and so does the full-size bitmap
    /// (~97 MB at 24 MP); the decoded photo and the preview picture stay.
    public void Lighten() {
        ReleaseRenderMemory();
        _pictures.Remove(Graph.Full);
        RenderedFull = false;
    }

    /// The last render of `graph`, copied into this session's bitmap for it.
    /// Null when there is nothing to read. The fallback where the canvas has no
    /// GPU interop (headless, OpenGL, a failed import); SharedPicture is the rest.
    public unsafe WriteableBitmap? Read(Graph graph) {
        if (graph == Graph.None || Native.shim_read(Shim, (int)graph, null, 0, out int w, out int h) == 0) return null;
        if (!_pictures.TryGetValue(graph, out var bitmap) || bitmap.PixelSize != new PixelSize(w, h)) {
            bitmap = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
            _pictures[graph] = bitmap;
        }
        Fill(bitmap, graph, w, h);
        return bitmap;
    }

    /// The last render of `graph` in a bitmap of its own, which no later render
    /// overwrites: compare's held original.
    public unsafe WriteableBitmap? Copy(Graph graph) {
        if (graph == Graph.None || Native.shim_read(Shim, (int)graph, null, 0, out int w, out int h) == 0) return null;
        var bitmap = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
        Fill(bitmap, graph, w, h);
        return bitmap;
    }

    unsafe void Fill(WriteableBitmap bitmap, Graph graph, int w, int h) {
        using var fb = bitmap.Lock();
        if (fb.RowBytes == w * 4) {
            Native.shim_read(_shim, (int)graph, (byte*)fb.Address, fb.RowBytes * h, out _, out _);
        } else {
            var tmp = new byte[w * h * 4];
            fixed (byte* p = tmp) Native.shim_read(_shim, (int)graph, p, tmp.Length, out _, out _);
            for (int y = 0; y < h; y++) Marshal.Copy(tmp, y * w * 4, fb.Address + y * fb.RowBytes, w * 4);
        }
    }

    /// Queues the canvas's copy of the last render of `graph`, `level` halvings
    /// down, on the GPU, and does not wait for it (#386). Its ticket; 0 when there
    /// is nothing to copy.
    public ulong BeginCopy(Graph graph, int level) =>
        graph is Graph.None or Graph.Failed ? 0 : Native.shim_copy_begin(Shim, (int)graph, level, out _, out _);

    /// The newest copy that has landed, written straight into `bitmap`. A copy of
    /// another size goes into a new bitmap, which replaces (and disposes) the old
    /// one only once it holds the copy. `wait`: for the last one begun. Its
    /// ticket; 0 when none has landed; -1 when a copy failed (LastError).
    public unsafe long TakeCopy(bool wait, ref WriteableBitmap? bitmap) {
        long asked = Native.shim_copy_take(Shim, wait ? 1 : 0, null, 0, 0, out int w, out int h);
        if (asked <= 0) return asked;
        var into = bitmap?.PixelSize == new PixelSize(w, h) ? bitmap
                 : new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
        long ticket;
        using (var fb = into.Lock())
            ticket = Native.shim_copy_take(_shim, 0, (byte*)fb.Address, fb.RowBytes, fb.RowBytes * h, out _, out _);
        if (into == bitmap) return ticket;
        if (ticket <= 0) { into.Dispose(); return ticket; }
        bitmap?.Dispose();
        bitmap = into;
        return ticket;
    }

    /// Windows: the last render of `graph`, `level` halvings down, into the D3D11
    /// texture SharedPicture's ANGLE import reads (#386). 0 where there is none.
    public int Share(Graph graph, int level, out nint handle, out int generation, out int w, out int h) =>
        Native.shim_share(Shim, (int)graph, level, out handle, out generation, out w, out h);

    /// The last render of `graph` as tightly packed RGBA bytes, with no bitmap
    /// and so no Avalonia platform: what `Orion --agent` measures and crops.
    public unsafe byte[]? Rgba(Graph graph, out int w, out int h) {
        w = h = 0;
        if (graph == Graph.None || Native.shim_read(Shim, (int)graph, null, 0, out w, out h) == 0) return null;
        var bytes = new byte[w * h * 4];
        fixed (byte* p = bytes) return Native.shim_read(_shim, (int)graph, p, bytes.Length, out _, out _) != 0 ? bytes : null;
    }

    /// The size of the last render of `graph`, without copying it. Default when there is nothing to read.
    public unsafe PixelSize Size(Graph graph) =>
        graph != Graph.None && Native.shim_read(Shim, (int)graph, null, 0, out int w, out int h) != 0 ? new PixelSize(w, h) : default;

    /// Copies the last render of `graph` into `presenter`'s shared surface, on the
    /// GPU (native/present.mm, SharedPicture). 0 when there is nothing to hand over.
    public int Present(nint presenter, Graph graph, int level, out nint surface, out int generation, out int w, out int h,
                       out ulong value) =>
        Native.present_frame(presenter, Shim, (int)graph, level, out surface, out generation, out w, out h, out value);

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

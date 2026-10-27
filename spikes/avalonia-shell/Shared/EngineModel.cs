using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace OrionShell;

/// The one engine every panel talks to: the open photo, its adjustments by C
/// field name (OrionAdjustments in orion.h), and the rendered picture.
///
/// It stands in for app/Engine.swift. Everything Engine+*.swift does beyond
/// this — history, masks, spots, sidecars, presets — is not here yet; a slice
/// that needs it stubs it in its own folder (SLICES.md).
public sealed class EngineModel : INotifyPropertyChanged {
    public static EngineModel Shared { get; } = new();

    public IReadOnlyList<string> Photos { get; private set; } = [];
    public int Current { get; private set; } = -1;
    public string CurrentName => Current >= 0 ? Path.GetFileName(Photos[Current]) : "";
    public Bitmap? Picture { get; private set; }
    public string Timing { get; private set; } = "";
    /// Milliseconds the last render spent in the engine and in the copy.
    public (double Render, double Copy) Last { get; private set; }
    public bool Interacting { get; private set; }

    /// The armed canvas tool (Engine.tool in Swift). ToolButtons set it; the
    /// canvas slice reads it to decide what a click on the photo does.
    public CanvasTool Tool {
        get => _tool;
        set { if (_tool != value) { _tool = value; Changed(nameof(Tool)); } }
    }
    CanvasTool _tool;

    /// The OrionEngine*, for a slice's own orion_* imports. 0 when nothing is open.
    public nint Handle => _shim == 0 ? 0 : Native.shim_engine(_shim);

    /// A photo opened: panels re-read their values (and their detents).
    public event Action? Opened;
    /// Any render finished: canvas and readouts repaint.
    public event Action? Rendered;
    public event PropertyChangedEventHandler? PropertyChanged;

    /// Every adjustable C field and its length (1, or N for float[N]).
    public static IReadOnlyList<(string Name, int Length)> Fields { get; } = ReadFields();

    nint _shim;
    readonly Dictionary<int, WriteableBitmap> _bitmaps = new();

    public void Load(IEnumerable<string> photos) {
        Photos = photos.ToArray();
        Changed(nameof(Photos));
    }

    public bool Open(int index) {
        Close();
        Current = index;
        _shim = Native.shim_open(Photos[index]);
        Changed(nameof(Current), nameof(CurrentName));
        if (_shim == 0) { SetTiming("could not open"); return false; }
        Opened?.Invoke();
        Render(preview: false);
        return true;
    }

    public void Close() {
        if (_shim != 0) Native.shim_close(_shim);
        _shim = 0;
        _bitmaps.Clear();
    }

    public float Get(string field, int index = 0) {
        if (_shim == 0) return 0;
        float v = Native.shim_get(_shim, field, index, out int ok);
        if (ok == 0) throw new ArgumentException($"no adjustment '{field}[{index}]' in OrionAdjustments", nameof(field));
        return v;
    }

    /// Writes one field and renders: the preview graph while a drag is in
    /// progress, the full graph otherwise.
    public void Set(string field, float value, int index = 0) {
        if (_shim == 0) return;
        if (Native.shim_set(_shim, field, index, value) == 0)
            throw new ArgumentException($"no adjustment '{field}[{index}]' in OrionAdjustments", nameof(field));
        Render(preview: Interacting);
    }

    /// A drag started: renders go to the quarter-resolution preview graph.
    public void BeginInteraction() => Interacting = true;

    /// The drag ended: one full-resolution render.
    public void EndInteraction() {
        if (!Interacting) return;
        Interacting = false;
        if (_shim != 0) Render(preview: false);
    }

    /// Engine render, then a CPU copy into a bitmap. The copy is the spike's
    /// cost, not the design's: Windows would share a D3D texture instead.
    public unsafe (double Render, double Copy) Render(bool preview) {
        var sw = Stopwatch.StartNew();
        int which = Native.shim_render(_shim, preview ? 1 : 0, out _);
        double render = sw.Elapsed.TotalMilliseconds;
        if (which == 0 || Native.shim_read(_shim, which, null, 0, out int w, out int h) == 0) {
            SetTiming("render failed: " + Marshal.PtrToStringUTF8(Native.orion_last_error(Handle)));
            return (render, 0);
        }
        if (!_bitmaps.TryGetValue(which, out var bitmap) || bitmap.PixelSize != new PixelSize(w, h)) {
            bitmap = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
            _bitmaps[which] = bitmap;
        }
        sw.Restart();
        using (var fb = bitmap.Lock()) {
            if (fb.RowBytes == w * 4) {
                Native.shim_read(_shim, which, (byte*)fb.Address, fb.RowBytes * h, out _, out _);
            } else {
                var tmp = new byte[w * h * 4];
                fixed (byte* p = tmp) Native.shim_read(_shim, which, p, tmp.Length, out _, out _);
                for (int y = 0; y < h; y++) Marshal.Copy(tmp, y * w * 4, fb.Address + y * fb.RowBytes, w * 4);
            }
        }
        double copy = sw.Elapsed.TotalMilliseconds;
        Last = (render, copy);
        Picture = bitmap;
        SetTiming($"{(which == 1 ? "preview" : "full")} {w}×{h} · engine {render:0.0} ms · copy {copy:0.0} ms");
        Changed(nameof(Picture));
        Rendered?.Invoke();
        return (render, copy);
    }

    void SetTiming(string text) { Timing = text; Changed(nameof(Timing)); }

    void Changed(params string[] names) {
        foreach (var n in names) PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
    }

    static (string, int)[] ReadFields() =>
        Enumerable.Range(0, Native.shim_field_count()).Select(i => {
            nint name = Native.shim_field_name(i, out int length);
            return (Marshal.PtrToStringUTF8(name)!, length);
        }).ToArray();
}

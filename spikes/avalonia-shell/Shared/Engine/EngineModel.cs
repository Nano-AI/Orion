using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia.Media.Imaging;

namespace OrionShell;

/// The one engine every panel talks to: the open photo, its adjustments by C
/// field name (OrionAdjustments in orion.h), and the rendered picture.
///
/// It stands in for app/Engine.swift, as a facade over the pieces in this
/// folder: the PhotoList, the Session for the photo on screen, the source that
/// supplies sessions, and the ShownPicture the canvas draws. Everything
/// Engine+*.swift does beyond this (history, spots, sidecars, presets) is not
/// here yet; a slice that needs it stubs it in its own folder (SLICES.md).
public sealed class EngineModel : INotifyPropertyChanged {
    public static EngineModel Shared { get; } = new();

    readonly PhotoList _list = new();
    readonly SessionCache _source;
    Session? _session;

    public EngineModel() => _source = new SessionCache(_list);

    /// Where sessions come from: the two-tier cache of decision #318.
    public SessionCache Cache => _source;

    public IReadOnlyList<string> Photos => _list.Paths;
    public int Current => _list.Current;
    public string CurrentName => Current >= 0 ? Path.GetFileName(Photos[Current]) : "";
    /// The session for the photo on screen; null when nothing is open, and
    /// while Opening.
    public Session? Session => _session;
    /// From an open until its session arrives from a background decode. The
    /// photo's kept picture, if any, is on screen meanwhile; Opened fires after.
    public bool Opening { get; private set; }
    /// What the canvas shows, and which graph made it.
    public ShownPicture Shown { get; } = new();
    public Bitmap? Picture => Shown.Bitmap;
    public string Timing { get; private set; } = "";
    /// Milliseconds the last render spent in the engine and in the copy.
    public (double Render, double Copy) Last { get; private set; }
    public bool Interacting { get; private set; }

    /// Set by the canvas. Zoomed past fit, the photo on screen renders full
    /// size; at fit the preview graph's picture is already screen-sized, so
    /// opening a photo renders only that (#318).
    public bool ZoomedPastFit {
        get => _zoomed;
        set {
            if (_zoomed == value) return;
            _zoomed = value;
            if (value && Shown.Graph != Graph.Full) Render(preview: false);
        }
    }
    bool _zoomed;
    /// The status line's message: a stub saying it is not built, until the next open.
    public string Status { get; private set; } = "";

    /// The armed canvas tool (Engine.tool in Swift). ToolButtons set it; the
    /// canvas slice reads it to decide what a click on the photo does.
    public CanvasTool Tool {
        get => _tool;
        set { if (_tool != value) { _tool = value; Changed(nameof(Tool)); } }
    }
    CanvasTool _tool;

    /// The OrionEngine*, for a slice's own orion_* imports. 0 when nothing is open.
    public nint Handle => _session?.Handle ?? 0;

    /// A photo opened: panels re-read their values (and their detents).
    public event Action? Opened;
    /// Any render finished: canvas and readouts repaint.
    public event Action? Rendered;
    /// Adjustments changed under the panels rather than through their own Set
    /// (auto-enhance; later reset, undo, paste): re-read values, keep detents.
    public event Action? ValuesChanged;
    public event PropertyChangedEventHandler? PropertyChanged;

    /// Every adjustable C field and its length (1, or N for float[N]).
    public static IReadOnlyList<(string Name, int Length)> Fields { get; } = ReadFields();

    public void Load(IEnumerable<string> photos) {
        _list.Load(photos);
        _source.Clear();   // drops an unanswered Request too
        Opening = false;
        Changed(nameof(Photos), nameof(Current));
    }

    /// Takes photos out of the list (a move to the Trash). The photo on screen,
    /// if it stays, stays open: same session, same edits, at its new index. If
    /// it goes, it closes and nothing is current.
    public void Remove(IEnumerable<string> paths) {
        var gone = paths.ToHashSet();
        int at = Current;
        if (at >= 0 && gone.Contains(Photos[at])) {
            Close();
            Shown.Clear();
            at = -1;
        }
        var left = Photos.Select((path, i) => (path, i)).Where(p => !gone.Contains(p.path)).ToList();
        _list.Load(left.Select(p => p.path));
        _source.Clear();   // every index moved; the session on screen closes when it is left
        Opening = false;
        if (at >= 0 && _list.Go(left.FindIndex(p => p.i == at))) _source.Hint(Current, 0);
        Changed(nameof(Photos), nameof(Current), nameof(CurrentName), nameof(Picture));
    }

    public bool Open(int index) {
        int from = Current;
        if (!_list.Go(index)) throw new ArgumentOutOfRangeException(nameof(index), index, $"{Photos.Count} photos");
        return Show(from);
    }

    /// The arrow keys (Library.neighbor): false at either end, where nothing reopens.
    /// A held key coalesces: each press shows the kept picture, and only the
    /// photo it stops on waits for a session (ISessionSource.Request).
    public bool Next() { int from = Current; return _list.Next() && Show(from); }
    public bool Previous() { int from = Current; return _list.Previous() && Show(from); }

    /// Preview-first: the kept picture now, the session when it is ready,
    /// which for a kept session is before this returns. False when the photo
    /// will not open, as far as is known by then.
    bool Show(int from) {
        Close();
        int index = Current, step = index - from;
        _source.Hint(index, from >= 0 && Math.Abs(step) == 1 ? step : 0);   // a jump has no direction
        Status = "";
        if (_source.Picture(index) is { } picture) Shown.Show(picture, Graph.Preview);
        else Shown.Clear();
        Changed(nameof(Current), nameof(CurrentName), nameof(Status), nameof(Picture));
        Opening = true;
        bool opened = true;
        _source.Request(index, s => opened = Arrive(index, s));
        if (Opening) SetTiming("decoding…");
        return opened;
    }

    /// Photo `index`'s session is ready, or will not open.
    bool Arrive(int index, Session? s) {
        Opening = false;
        if (index != Current || _session != null) {   // not asked for: never happens, never leaks
            if (s != null) _source.Release(s);
            return false;
        }
        if ((_session = s) == null) { SetTiming("could not open"); return false; }
        Opened?.Invoke();
        Render(preview: !ZoomedPastFit);
        return true;
    }

    /// Hands the photo on screen back to the source.
    public void Close() {
        if (_session != null) _source.Release(_session);
        _session = null;
    }

    public float Get(string field, int index = 0) => _session?.Get(field, index) ?? 0;

    /// Writes one field and renders: the preview graph while a drag is in
    /// progress, the full graph otherwise.
    public void Set(string field, float value, int index = 0) {
        if (_session == null) return;
        _session.Set(field, value, index);
        Render(preview: Interacting);
    }

    /// A write through one of the session's own calls (curve, auto-enhance,
    /// the mask group), then the render Set would do. A call stage 1b has not
    /// built yet, or one that refuses its input (the mask group's
    /// ArgumentException and InvalidOperationException), goes to the status
    /// line, not up a pointer handler. `reread`
    /// tells the panels the values moved under them. False when nothing ran.
    public bool Apply(Action<Session> write, bool reread = false) {
        if (_session == null) return false;
        try {
            write(_session);
        } catch (Exception e) when (e is NotBuiltException or ArgumentException or InvalidOperationException) {
            Say(e.Message);
            return false;
        }
        Render(preview: Interacting);
        if (reread) ValuesChanged?.Invoke();
        return true;
    }

    /// Puts a message on the status line until the next open.
    public void Say(string message) {
        if (Status == message) return;
        Status = message;
        Changed(nameof(Status));
    }

    /// A drag started: renders go to the quarter-resolution preview graph.
    public void BeginInteraction() => Interacting = true;

    /// The drag ended: one full-resolution render.
    public void EndInteraction() {
        if (!Interacting) return;
        Interacting = false;
        if (_session != null) Render(preview: false);
    }

    /// Engine render, then a CPU copy into the session's bitmap, shown.
    public (double Render, double Copy) Render(bool preview) {
        if (_session is not { } s) return (0, 0);
        var sw = Stopwatch.StartNew();
        var graph = s.Render(preview);
        double render = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        if (s.Read(graph) is not { } bitmap) {
            SetTiming("render failed: " + s.LastError);
            return (render, 0);
        }
        double copy = sw.Elapsed.TotalMilliseconds;
        Last = (render, copy);
        Shown.Show(bitmap, graph);
        var size = bitmap.PixelSize;
        SetTiming($"{(graph == Graph.Preview ? "preview" : "full")} {size.Width}×{size.Height} · engine {render:0.0} ms · copy {copy:0.0} ms");
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

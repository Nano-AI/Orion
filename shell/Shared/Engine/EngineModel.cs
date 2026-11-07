using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace OrionShell;

/// The one engine every panel talks to: the open photo, its adjustments by C
/// field name (OrionAdjustments in orion.h), and the rendered picture.
///
/// It stands in for the Swift app's Engine.swift, as a facade over the pieces in this
/// folder: the PhotoList, the Session for the photo on screen, the source that
/// supplies sessions, and the ShownPicture the canvas draws. Everything
/// Engine+*.swift does beyond this (history, spots, sidecars, presets) is not
/// here yet; a slice that needs it stubs it in its own folder (SLICES.md).
public sealed class EngineModel : INotifyPropertyChanged {
    public static EngineModel Shared { get; } = new();

    readonly PhotoList _list = new();
    readonly SessionCache _source;
    Session? _session;

    public EngineModel() {
        _source = new SessionCache(_list);
        Rendered += ArmIdleTrim;
    }

    /// How long the photo on screen goes without a render before its engine gives back
    /// what its pools hold idle (Session.Trim): past the pause between two slider moves,
    /// whose next tick would reuse those textures, and well inside the time a photo is looked at.
    internal static readonly TimeSpan IdleTrimAfter = TimeSpan.FromSeconds(3);
    DispatcherTimer? _idleTrim;
    int _rendersSeen;

    /// Its next render, by anyone (a side-by-side pane too), puts the trim off another IdleTrimAfter.
    void ArmIdleTrim() {
        _rendersSeen = _session?.RenderCount ?? 0;
        _idleTrim ??= new DispatcherTimer(IdleTrimAfter, DispatcherPriority.Background, (t, _) => {
            if (_session is not { } s) { ((DispatcherTimer)t!).Stop(); return; }
            if (s.RenderCount != _rendersSeen) { _rendersSeen = s.RenderCount; return; }
            ((DispatcherTimer)t!).Stop();
            s.Trim(full: false);
        });
        _idleTrim.Stop();
        _idleTrim.Start();
    }

    /// A sidecar write that escaped to the full name says so on the status line (#405).
    /// Posted, so a write flushed on a photo switch is not cleared by the open after it.
    static EngineModel() => Sidecar.Noticed += message => Dispatcher.UIThread.Post(() => Shared.Say(message));

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
            else if (!value && Shown.Graph == Graph.Region) Render(preview: true);   // back at fit: the whole picture again
        }
    }
    bool _zoomed;

    /// What the canvas shows of the picture past fit: the part, normalized,
    /// and the device pixels it covers. Renders past fit ask for this and a
    /// margin round it (#22, ViewRegion). Set by the canvas on every change.
    /// A pan moves the pixels already rendered, over the preview where it
    /// uncovers more (#401); a view the shown region does not cover renders a
    /// region round it, which never holds the UI thread for the GPU: the copy
    /// lands behind it and the canvas draws the last one meanwhile. While a
    /// hand pans (Pan), a region the moment the last has landed, newest view
    /// winning; at rest, where a spring will stop (Land) and under a hand held
    /// still (Rested), the rest render, with its wider margin.
    public (Avalonia.Rect Uv, Avalonia.PixelSize Pixels)? View {
        get => _view;
        set {
            if (_view == value) return;
            _view = value;
            if (ViewHeld?.Invoke() != true) Rested();
        }
    }
    (Avalonia.Rect Uv, Avalonia.PixelSize Pixels)? _view, _region;   // _region: what the shown region was asked for
    /// Set by the canvas: a hand or a spring holds the view now.
    public Func<bool>? ViewHeld { get; set; }
    static readonly TimeSpan Rest = TimeSpan.FromMilliseconds(300);

    /// The canvas: a hand moved the view.
    public void Pan() {
        if (_view is { } now && ZoomedPastFit && _session != null && !_refused && !Covered(now, rest: false) && Panned(now)) Ask(now, rest: false);
    }

    /// The view is at rest, or a hand has held it still long enough to count as rest.
    public void Rested() {
        if (_view is { } now && ZoomedPastFit && _session != null && !Covered(now, rest: true)) Ask(now, rest: true);
    }

    /// The canvas: a spring will bring the view to rest at `view`. It renders now,
    /// so the picture is sharp when the glide ends.
    public void Land((Avalonia.Rect Uv, Avalonia.PixelSize Pixels) view) {
        if (ZoomedPastFit && _session != null && !Covered(view, rest: true)) Ask(view, rest: true);
    }

    /// Nothing a render would add: the whole picture at full size, or a region
    /// that holds this view at its detail; at `rest`, a region rendered at rest,
    /// whose margin is the wider one.
    bool Covered((Avalonia.Rect Uv, Avalonia.PixelSize Pixels) view, bool rest) =>
        Shown.Graph == Graph.Full || Shown.Graph == Graph.Region && _region is { } had && (!rest || _regionRested) && ViewRegion.Covers(had, view);

    /// The view moved at the shown region's scale: a pan, which renders under
    /// the hand. A zoom keeps the picture it has, scaled, until it rests (the
    /// region graph rebuilds at every new size and step).
    bool Panned((Avalonia.Rect Uv, Avalonia.PixelSize Pixels) view) =>
        Shown.Graph == Graph.Region && _region is { } had && ViewRegion.SameScale(had, view);

    /// The view a render waits for, the newest asked: submitted at Render
    /// priority, after the frame's own work, and once the region in flight has landed.
    (Avalonia.Rect Uv, Avalonia.PixelSize Pixels)? _asked;
    bool _askedRest, _posted, _regionRested, _refused;   // _refused: the region refused a hand's render; until the next shown
    DispatcherTimer? _landing;

    void Ask((Avalonia.Rect Uv, Avalonia.PixelSize Pixels) view, bool rest) {
        (_asked, _askedRest) = (view, rest);
        if (_posted) return;
        _posted = true;
        Dispatcher.UIThread.Post(Submit, DispatcherPriority.Render);
    }

    void Submit() {
        _posted = false;
        if (_asked is not { } view || _session is not { } s) return;
        if (!ZoomedPastFit || Interacting) { _asked = null; return; }   // the edit's own renders show the view
        if (s.Busy) { Watch(); return; }
        _asked = null;
        if (Covered(view, _askedRest)) return;
        if (_askedRest) Render(preview: false, ViewRegion.Around(view, ViewRegion.RestMargin));
        else Hand(s, ViewRegion.Around(view, ViewRegion.HandMargin));
    }

    /// A region round where a hand is panning to: never the whole graph, which
    /// would hold the hand. An edit the region refuses while a hand moves
    /// (denoise past 100%, Engine::renderRegion) waits for the rest render.
    void Hand(Session s, (Avalonia.Rect Uv, Avalonia.PixelSize Pixels) region) {
        HandRenders++;
        Rendering?.Invoke();
        var sw = Stopwatch.StartNew();
        if (s.RenderRegion(region.Uv, region.Pixels, interacting: true, wait: false) == Graph.Region) Present(s, Graph.Region, region, sw, rested: false);
        else _refused = true;
    }

    /// Regions rendered under a moving hand: what the selftest counts.
    public int HandRenders { get; private set; }

    /// Polls the region in flight every 2 ms until it lands, then submits what waited for it.
    void Watch() {
        _landing ??= new DispatcherTimer(TimeSpan.FromMilliseconds(2), DispatcherPriority.Render, (t, _) => {
            if (_session?.Busy == true) return;
            ((DispatcherTimer)t!).Stop();
            Landed?.Invoke();
            if (_asked != null) Submit();
        });
        _landing.Start();
    }

    /// The last region rendered without waiting is off the GPU: what must not
    /// wait behind it (the histogram's read drains the queue) runs now.
    public event Action? Landed;
    /// A region rendered without waiting is still on the GPU.
    public bool Busy => _session?.Busy == true;
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
    /// A render is starting; Rendered follows unless it fails.
    public event Action? Rendering;
    /// Any render finished: canvas and readouts repaint.
    public event Action? Rendered;
    /// Adjustments changed under the panels rather than through their own Set
    /// (auto-enhance; later reset, undo, paste): re-read values, keep detents.
    public event Action? ValuesChanged;
    public event PropertyChangedEventHandler? PropertyChanged;

    /// Every adjustable C field and its length (1, or N for float[N]).
    public static IReadOnlyList<(string Name, int Length)> Fields { get; } = ReadFields();

    /// False, with the reason on the status line, while the open photo's edit
    /// is owed to its sidecar and will not write: the list stays as it was.
    public bool Load(IEnumerable<string> photos) {
        if (!CanLeave()) return false;
        _list.Load(photos);
        _source.Clear();   // drops an unanswered Request too
        Opening = false;
        FolderLoaded?.Invoke();
        Changed(nameof(Photos), nameof(Current));
        return true;
    }

    /// A list was opened (Load), not merely reordered or trimmed (Relist): what
    /// was read about the old one, a photo's marks, is to be read again.
    public event Action? FolderLoaded;

    /// Editor.canLeavePhoto: an edit that will not write keeps its photo on
    /// screen, so the retry stays possible and no later state replaces it.
    bool CanLeave() {
        if (Autosave.Shared.FlushBeforeLeaving()) return true;
        Say(Autosave.Shared.LastFailure ?? "The open photo's edits could not be saved.");
        return false;
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
        Relist(Photos.Where(p => !gone.Contains(p)).ToList(), at);
    }

    /// The same photos in a new order (a sort). The photo on screen stays open,
    /// same session, same edits, at its new index.
    public void Reorder(IReadOnlyList<string> order) => Relist(order, Current);

    /// The list became `paths`, the same photos or fewer: the photo at `at`
    /// stays current at its new index, and the cache follows every photo to its
    /// new index (ISessionSource.Moved) rather than starting cold.
    void Relist(IReadOnlyList<string> paths, int at) {
        string? open = at >= 0 ? Photos[at] : null;
        var index = new Dictionary<string, int>();
        for (int i = 0; i < paths.Count; i++) index.TryAdd(paths[i], i);
        var to = Photos.Select(p => index.GetValueOrDefault(p, -1)).ToList();
        _list.Load(paths);
        _source.Moved(to);
        Opening = false;
        if (open != null && index.TryGetValue(open, out int now) && _list.Go(now)) _source.Hint(Current, 0);
        Changed(nameof(Photos), nameof(Current), nameof(CurrentName), nameof(Picture));
    }

    /// False without moving while the open photo cannot be left (CanLeave).
    public bool Open(int index) {
        if (index < 0 || index >= Photos.Count) throw new ArgumentOutOfRangeException(nameof(index), index, $"{Photos.Count} photos");
        if (!CanLeave()) return false;
        int from = Current;
        _list.Go(index);
        return Show(from);
    }

    /// The arrow keys (Library.neighbor): false at either end, where nothing reopens.
    /// A held key coalesces: each press shows the kept picture, and only the
    /// photo it stops on waits for a session (ISessionSource.Request).
    public bool Next() { int from = Current; return CanLeave() && _list.Next() && Show(from); }
    public bool Previous() { int from = Current; return CanLeave() && _list.Previous() && Show(from); }

    /// Preview-first: the kept picture now, the session when it is ready,
    /// which for a kept session is before this returns. A photo with neither
    /// shows the camera's embedded preview while it decodes, read once the
    /// decode has started beside it. False when the photo will not open, as
    /// far as is known by then.
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
        if (Opening) {
            if (!Shown.Any) Embed(index);
            SetTiming("decoding…");
        }
        return opened;
    }

    /// The embedded preview on screen while its photo decodes, until a render replaces it.
    Bitmap? _embedded;

    /// About a fit canvas's width on a 2x display.
    internal const int EmbeddedWidth = 2560;

    /// Read on a worker beside the decode, and shown ahead of anything else
    /// queued, so before the canvas's next frame: a jump waits there for what
    /// is left of the read, and a launch, whose window is built meanwhile, does not.
    void Embed(int index) {
        string path = Photos[index];
        var read = Task.Run(() => Embedded(path));
        Dispatcher.UIThread.Post(() => {
            if (Current == index && Opening && !Shown.Any && read.Result is { } embedded) Shown.Show(_embedded = embedded, Graph.Preview);
        }, DispatcherPriority.Send);
    }

    static Bitmap? Embedded(string path) {
        try {
            var buffer = new byte[2 << 20];   // grows for a larger one
            return EmbeddedPreview.ReadFramed(path, EmbeddedWidth, ref buffer);
        } catch (Exception e) {
            Console.Error.WriteLine($"embedded preview {Path.GetFileName(path)}: {e.Message}");   // the blank it was, not a failed open
            return null;
        }
    }

    /// Photo `index`'s session is ready, or will not open.
    bool Arrive(int index, Session? s) {
        Opening = false;
        if (index != Current || _session != null) {   // not asked for: never happens, never leaks
            if (s != null) _source.Release(s);
            return false;
        }
        if ((_session = s) == null) {
            if (_embedded != null && ReferenceEquals(Shown.Bitmap, _embedded)) Shown.Clear();   // the failure says why, not the camera's JPEG
            _embedded = null;
            SetTiming("could not open");
            return false;
        }
        _embedded = null;
        Opened?.Invoke();
        Render(preview: !ZoomedPastFit);
        // The histogram and eyedropper read the full graph (orion.h), which a swap
        // at fit skips: once the hand rests, render it, off the browse path.
        Task.Delay(Rest).ContinueWith(_ => Dispatcher.UIThread.Post(() => {
            if (Current == index && _session == s && !Interacting && Shown.Graph != Graph.Full) Render(preview: false);
        }));
        return true;
    }

    /// Hands the photo on screen back to the source.
    public void Close() {
        _asked = null;   // a view's render belongs to the photo it was for
        _landing?.Stop();
        _region = null;
        _previewRead = false;
        if (_session != null) {
            Leaving?.Invoke();
            _source.Release(_session);
        }
        _session = null;
    }

    /// The photo on screen is about to go back to the source: its session is
    /// still Session, for anything that must be put back on it first.
    public event Action? Leaving;

    /// With no session (nothing open, or the engine could not start) a field reads as DevelopState's
    /// Reset value, so a control shows its default, not a zero its range may not hold ("0 K") (#395).
    public float Get(string field, int index = 0) => _session?.Get(field, index) ?? Reset(field, index);

    static readonly Dictionary<string, System.Text.Json.Nodes.JsonNode?> Resets =
        DevelopJson.Defaults().ToDictionary(p => Session.Snake(p.Key), p => p.Value);

    static float Reset(string field, int index) => Resets.GetValueOrDefault(field) switch {
        System.Text.Json.Nodes.JsonArray a => index < a.Count ? DevelopJson.Float(a[index]) ?? 0 : 0,
        var v => DevelopJson.Float(v) ?? 0,
    };

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
            try {
                write(_session);
            } catch (InvalidOperationException e) when (e.Message.Contains(OutOfGpuMemory) && _source.Shed()) {
                write(_session);   // the decoded neighbours are a convenience: once more without them, as Render
            }
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

    /// Engine render, then shown live: the canvas takes it from the GPU with no
    /// copy (SharedPicture, #317) or copies it at the size it shows it, without
    /// waiting (CopiedPicture, #386). Copy is what showing it cost this thread,
    /// that copy's start included; a reader with no canvas copies on first ask.
    public (double Render, double Copy) Render(bool preview) =>
        // Past fit, only what is on screen, at the screen's resolution, drag or
        // rest (#22); the whole graph when the engine says this edit needs it.
        // At rest, with a margin round the view; a drag's ticks, the view alone, as fast as it can be.
        Render(preview, ZoomedPastFit && View is { } seen ? Interacting ? seen : ViewRegion.Around(seen, ViewRegion.RestMargin) : null);

    (double Render, double Copy) Render(bool preview, (Avalonia.Rect Uv, Avalonia.PixelSize Pixels)? view) {
        if (_session is not { } s) return (0, 0);
        Rendering?.Invoke();
        var sw = Stopwatch.StartNew();
        var graph = Graph.None;
        if (view is { } v) {
            // Before the region, whose submit this read would otherwise wait behind.
            if (!Interacting) Preview(s);
            graph = s.RenderRegion(v.Uv, v.Pixels, Interacting, wait: false);
        }
        if (graph == Graph.None) graph = s.Render(preview);
        // Out of GPU memory: the decoded neighbours are a convenience, the photo on
        // screen is not. Once more without them (#392).
        if (graph == Graph.None && s.LastError.Contains(OutOfGpuMemory) && _source.Shed()) graph = s.Render(preview);
        if (view == null || Interacting) _previewRead = false;
        return Present(s, graph, graph == Graph.Region ? view : null, sw, rested: !Interacting);
    }

    /// The small whole-frame pass: what the histogram reads while the full
    /// graph waits, and the picture under a pan until the next region. Read
    /// only when it rendered, or an edit since its last read went elsewhere.
    void Preview(Session s) {
        if (s.Render(preview: true, out double ms) == Graph.Preview && (ms > 0 || !_previewRead || s.Picture == null)) s.Read(Graph.Preview);
        _previewRead = true;
    }
    bool _previewRead;

    (double Render, double Copy) Present(Session s, Graph graph, (Avalonia.Rect Uv, Avalonia.PixelSize Pixels)? region, Stopwatch sw, bool rested) {
        double render = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        var size = s.Size(graph);
        if (size == default) {
            SetTiming("render failed: " + s.LastError);
            return (render, 0);
        }
        (_region, _regionRested, _refused) = (region, rested, false);
        Shown.ShowLive(s, graph, region?.Uv);
        double copy = sw.Elapsed.TotalMilliseconds;
        Last = (render, copy);
        SetTiming($"{graph.ToString().ToLowerInvariant()} {size.Width}×{size.Height} · engine {render:0.0} ms · " +
                  (Shown.OnGpu ? "on the GPU" : $"copy {copy:0.0} ms"));
        Changed(nameof(Picture));
        if (graph == Graph.Region) Watch();
        Rendered?.Invoke();
        return (render, copy);
    }

    /// What the engine's message says when the card had no room (Dawn.h's ErrorScope, Metal's allocation).
    internal const string OutOfGpuMemory = "out of GPU memory";

    internal void SetTiming(string text) { Timing = text; Changed(nameof(Timing)); }

    void Changed(params string[] names) {
        foreach (var n in names) PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
    }

    static (string, int)[] ReadFields() =>
        Enumerable.Range(0, Native.shim_field_count()).Select(i => {
            nint name = Native.shim_field_name(i, out int length);
            return (Marshal.PtrToStringUTF8(name)!, length);
        }).ToArray();
}

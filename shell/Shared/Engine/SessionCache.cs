using System.Diagnostics;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace OrionShell;

/// Decision #318: photo navigation caches in two tiers steered by a direction
/// vector. Around the photo on screen it keeps screen-sized pictures (the
/// preview graph's, ~6 MB) for `pictureSlots` neighbours and decoded sessions
/// (115 MB each) for 2-4. CachePlan decides what to decode and what to drop;
/// this holds the sessions and pictures it decides about.
///
/// Threading: a decode opens a session nothing else has seen, renders its
/// preview on a worker (Session.cs says why that is safe), and posts it to the
/// UI thread; a session dropped from both tiers closes on a worker too.
/// Every field here belongs to the UI thread.
public sealed class SessionCache : ISessionSource {
    readonly PhotoList _photos;
    readonly Func<CachePlan> _newPlan;
    CachePlan _plan;
    readonly Dictionary<int, Session> _kept = new();
    readonly Dictionary<int, Bitmap> _pictures = new();
    readonly Dictionary<Session, int> _lent = new();     // acquired: never closed here before Release
    readonly Dictionary<int, Task> _decoding = new();    // any generation; one decode per index at a time
    readonly HashSet<int> _failed = new();
    (int Index, Action<Session?> Ready)? _request;
    Task _closing = Task.CompletedTask;
    int _generation;
    bool _shed;   // out of GPU memory once: no decoded neighbours from then on (Shed)

    public SessionCache(PhotoList photos, int pictureSlots = 20, Eviction policy = Eviction.AheadRecent) {
        _photos = photos;
        _newPlan = () => new CachePlan(() => photos.Paths.Count, policy, pictureSlots);
        _plan = _newPlan();
    }

    /// Where the hand is going, in [-1, 1]: +1 forward.
    public double Bias => _plan.Bias;
    public int SessionSlots => _plan.SessionSlots;
    public int Sessions => _kept.Count;
    public int Pictures => _pictures.Count;
    public bool Busy => _decoding.Count > 0;

    public Session? Acquire(int index) {
        if (!_kept.Remove(index, out var s)) s = Session.OpenEdited(_photos.Paths[index]);
        if (s != null) _lent[s] = index;
        return s;
    }

    public void Request(int index, Action<Session?> ready) {
        _request = null;
        if (_kept.Remove(index, out var s)) {
            _lent[s] = index;
            ready(s);
            return;
        }
        _request = (index, ready);
        Pump();
    }

    /// A second photo on screen beside the first (the side-by-side compare, #400):
    /// kept, it is handed over at once; otherwise it decodes on a worker, after a
    /// prefetch of the same photo already running, so one photo never holds two
    /// sessions. Lent as Acquire lends: Release keeps it warm. `ready` gets null
    /// when it will not open (Session.OpenFailure says why) or the list changed meanwhile.
    public void Borrow(int index, Action<Session?> ready) {
        if (_kept.Remove(index, out var kept)) { _lent[kept] = index; ready(kept); return; }
        int generation = _generation;
        if (_decoding.TryGetValue(index, out var running)) {
            running.ContinueWith(_ => Dispatcher.UIThread.Post(() => {
                if (generation == _generation) Borrow(index, ready); else ready(null);
            }));
            return;
        }
        string path = _photos.Paths[index];
        // In _decoding while it runs: Drain waits for it, and no prefetch of the same photo starts beside it.
        var decode = Task.Run(() => {
            var s = Session.OpenEdited(path);
            try {
                if (s?.Render(preview: true) == Graph.Preview) s.Read(Graph.Preview);   // the picture, off the UI thread, as Start
            } catch (Exception e) {
                Console.Error.WriteLine($"cache: {path}: {e.Message}");
                s?.Dispose();
                s = null;
            }
            return s;
        });
        _decoding[index] = decode;
        decode.ContinueWith(t => Dispatcher.UIThread.Post(() => {
            if (_decoding.GetValueOrDefault(index) == decode) _decoding.Remove(index);
            var s = t.IsCompletedSuccessfully ? t.Result : null;
            if (s != null && generation != _generation) { Close(s); s = null; }
            if (s != null) _lent[s] = index;
            ready(s);
            Pump();
        }));
    }

    public Bitmap? Picture(int index) => _pictures.GetValueOrDefault(index);

    /// The photo on screen ran out of GPU memory: closes every decoded neighbour and
    /// decodes none from then on (#392). A kept one holds under 1 MB of the card
    /// (Engine::releaseRenderMemory); what this frees is a decode in flight and the
    /// graphs' compiled kernels. The pictures stay: they are CPU memory. False when
    /// there was nothing to give back.
    public bool Shed() {
        bool any = _kept.Count > 0 || _decoding.Count > 0;
        _shed = true;
        foreach (var s in _kept.Values) Close(s);
        _kept.Clear();
        Drain();   // the closes run on a worker, and the retry needs their memory now
        return any;
    }

    public void Release(Session s) {
        if (!_lent.Remove(s, out int index)) { Close(s); return; }   // lent before a Clear
        // Edited since its last preview: render one so the kept picture shows the edit.
        if (s.RenderedFull && s.Render(preview: true) == Graph.Preview) s.Read(Graph.Preview);
        if (s.Picture is { } p) _pictures[index] = p;
        if (!_shed && _kept.TryAdd(index, s)) s.Lighten();
        else Close(s);
        Evict();
        Pump();
    }

    public void Hint(int index, int direction) {
        _plan.Hint(index, direction, Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency);
        Evict();
        Pump();
    }

    public void Clear() {
        _generation++;
        foreach (var s in _kept.Values) Close(s);
        _kept.Clear();
        _pictures.Clear();
        _lent.Clear();
        _failed.Clear();
        _request = null;
        _plan = _newPlan();
    }

    /// Same photos, new indices: both tiers follow their photos, so a sort or a
    /// trash keeps the cache warm and a kept session's edits. Only a photo that
    /// left the list closes. A decode in flight was for an old index: dropped.
    public void Moved(IReadOnlyList<int> to) {
        _generation++;
        _request = null;
        CachePlan.Rekey(_kept, to, Close);
        CachePlan.Rekey(_pictures, to);
        foreach (var s in _lent.Keys.ToList())
            if (to[_lent[s]] is var n and >= 0) _lent[s] = n; else _lent.Remove(s);   // closes when it comes back
        var failed = _failed.Where(i => to[i] >= 0).Select(i => to[i]).ToList();
        _failed.Clear();
        _failed.UnionWith(failed);
        _plan.Moved(to);
    }

    /// Waits for the decodes and closes in flight, so none is inside the engine
    /// when the process exits. Never throws: it runs on the way out.
    public void Drain() => Task.WhenAll([.. _decoding.Values, _closing]).ContinueWith(_ => { }).Wait();

    /// One at a time, off the UI thread: shim_close takes 5-9 ms, a third of an arrow press.
    void Close(Session s) => _closing = _closing.ContinueWith(_ => s.Dispose());

    /// The decoded tier: kept, and lent to EngineModel.
    IEnumerable<int> Decoded => _kept.Keys.Concat(_lent.Values);

    void Evict() {
        foreach (int i in _plan.Over(Decoded.ToList(), _plan.SessionSlots))
            if (_kept.Remove(i, out var s)) Close(s);
        foreach (int i in _plan.Over(_pictures.Keys.ToList(), _plan.PictureSlots)) _pictures.Remove(i);
    }

    /// The photo asked for starts at once, beside at most one other decode; a
    /// prefetch starts only when nothing runs.
    void Pump() {
        if (_request is { Index: var r } && !_decoding.ContainsKey(r) && _decoding.Count < 2) Start(r);
        if (_decoding.Count > 0 || _plan.Current < 0 || _shed) return;
        if (_plan.Next(Decoded.ToList(), _pictures.Keys.ToList(), _failed.Contains) is int next) Start(next);
    }

    void Start(int index) {
        int generation = _generation;
        string path = _photos.Paths[index];
        _decoding[index] = Task.Run(() => {
            var s = Session.OpenEdited(path);
            try {
                if (s?.Render(preview: true) == Graph.Preview) s.Read(Graph.Preview);   // the picture, off the UI thread
            } catch (Exception e) {
                Console.Error.WriteLine($"cache: {path}: {e.Message}");
                s?.Dispose();
                s = null;
            }
            Dispatcher.UIThread.Post(() => Done(index, generation, s));
        });
    }

    void Done(int index, int generation, Session? s) {
        _decoding.Remove(index);
        if (generation != _generation) { if (s != null) Close(s); }   // the list changed under it
        else if (_request is { } r && r.Index == index) {
            _request = null;
            if (s == null) _failed.Add(index);
            else {
                _plan.Decoded(index);
                if (s.Picture is { } p) _pictures[index] = p;
                _lent[s] = index;
            }
            r.Ready(s);
        } else if (s == null) _failed.Add(index);
        else {
            _plan.Decoded(index);
            if (s.Picture is { } p && _plan.Wants(_pictures.Keys.ToList(), index, _plan.PictureSlots)) _pictures[index] = p;
            if (!_shed && _plan.Wants(Decoded.ToList(), index, _plan.SessionSlots)) {
                s.Lighten();
                _kept[index] = s;
            } else Close(s);
        }
        Evict();
        Pump();
    }
}

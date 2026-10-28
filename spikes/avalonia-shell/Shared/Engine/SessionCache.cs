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
        if (!_kept.Remove(index, out var s)) s = Session.Open(_photos.Paths[index]);
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

    public Bitmap? Picture(int index) => _pictures.GetValueOrDefault(index);

    public void Release(Session s) {
        if (!_lent.Remove(s, out int index)) { Close(s); return; }   // lent before a Clear
        // Edited since its last preview: render one so the kept picture shows the edit.
        if (s.RenderedFull && s.Render(preview: true) == Graph.Preview) s.Read(Graph.Preview);
        if (s.Picture is { } p) _pictures[index] = p;
        if (!_kept.ContainsKey(index) && Light(s)) _kept[index] = s;
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

    /// Drops the graphs' intermediates (stage 1b). Until that is built, a
    /// session that rendered full holds 1.35 GB more (#318): not worth keeping.
    static bool Light(Session s) {
        try {
            s.ReleaseRenderMemory();
            return true;
        } catch (NotBuiltException) {
            return !s.RenderedFull;
        }
    }

    /// The photo asked for starts at once, beside at most one other decode; a
    /// prefetch starts only when nothing runs.
    void Pump() {
        if (_request is { Index: var r } && !_decoding.ContainsKey(r) && _decoding.Count < 2) Start(r);
        if (_decoding.Count > 0 || _plan.Current < 0) return;
        if (_plan.Next(Decoded.ToList(), _pictures.Keys.ToList(), _failed.Contains) is int next) Start(next);
    }

    void Start(int index) {
        int generation = _generation;
        string path = _photos.Paths[index];
        _decoding[index] = Task.Run(() => {
            var s = Session.Open(path);
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
            if (_plan.Wants(Decoded.ToList(), index, _plan.SessionSlots) && Light(s)) _kept[index] = s;
            else Close(s);
        }
        Evict();
        Pump();
    }
}

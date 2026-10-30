using System.Text.Json.Nodes;
using Avalonia.Threading;
using OrionShell.CanvasSlice;
using OrionShell.Develop;
using OrionShell.Masks;

namespace OrionShell.Chrome;

/// A port of app/ProposalWatcher.swift: watches the open photo's folder for
/// `<stem>.proposed.json` (what the MCP server's propose_edit writes) appearing,
/// changing or going, and drives the engine, compare and Autosave through
/// Proposal's phase machine. While a proposal is up the edit is locked
/// (IsLive): autosave is off for the whole preview, so a manual edit would be
/// neither saved nor undoable, and Reject would discard it silently.
sealed class ProposalWatcher {
    public static ProposalWatcher Shared { get; } = new();

    Proposal.Phase _phase = Proposal.Phase.Idle;
    string? _photo;
    /// The state committed before the live proposal started, captured once, on
    /// Appeared: by a second Changed the engine holds the previous proposal.
    JsonObject? _committed;
    byte[]? _lastSeen;
    FileSystemWatcher? _watch;
    int _debounce;   // the latest pending check; an older one finds it moved and stands down

    static EngineModel E => EngineModel.Shared;

    public bool IsLive => _phase.Previewing;
    /// The keys the footer names while a proposal is up.
    public IReadOnlyList<string> Keys => _phase.Keys ?? [];
    public event Action? Changed;

    ProposalWatcher() {
        // Editor.load: the photo changed. A live preview is put back on the photo
        // it was on before that photo's session goes back to the cache.
        E.Leaving += () => Attach(null);
        E.Opened += () => Attach(E.Session?.Path);
    }

    /// Wired once, at the first open: from then on every photo is watched.
    public static void Install() { if (E.Session is { } s) Shared.Attach(s.Path); }

    /// The photo changed, or closed (null). Whatever was live is put back (the
    /// file stays on disk: reject without delete), then the new photo is watched.
    public void Attach(string? photo) {
        if (photo == _photo && _watch != null) return;
        if (IsLive) Fire(new(Proposal.EventKind.SwitchedAway));
        Stop();
        (_photo, _committed, _lastSeen) = (photo, null, null);
        if (photo == null) return;
        try {
            _watch = new FileSystemWatcher(RealPath(Path.GetDirectoryName(photo)!)) { NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size };
            FileSystemEventHandler seen = (_, _) => Dispatcher.UIThread.Post(Debounced);
            _watch.Created += seen;
            _watch.Changed += seen;
            _watch.Deleted += seen;
            _watch.Renamed += (_, _) => Dispatcher.UIThread.Post(Debounced);
            _watch.EnableRaisingEvents = true;
        } catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException or PlatformNotSupportedException) {
            Console.Error.WriteLine($"orion: proposals not watched in {Path.GetDirectoryName(photo)} ({e.Message})");
        }
        // One already on disk counts as just appeared: the agent may have written it while Orion was elsewhere.
        Check();
    }

    /// The folder with every symbolic link resolved. macOS reports file events under
    /// the real path (/private/var for /var), and .NET drops an event whose path is
    /// not under the one it was given to watch, so a linked folder never fired.
    static string RealPath(string path) {
        if (!OperatingSystem.IsMacOS() && !OperatingSystem.IsLinux()) return path;
        nint p = realpath(path, 0);
        if (p == 0) return path;
        try { return System.Runtime.InteropServices.Marshal.PtrToStringUTF8(p) ?? path; } finally { free(p); }
    }

    [System.Runtime.InteropServices.DllImport("libc", CharSet = System.Runtime.InteropServices.CharSet.Ansi)]
    static extern nint realpath(string path, nint resolved);
    [System.Runtime.InteropServices.DllImport("libc")]
    static extern void free(nint p);

    void Stop() {
        _watch?.Dispose();
        _watch = null;
        _debounce++;
    }

    public void Approve() => Fire(new(Proposal.EventKind.Approved));
    public void Reject() => Fire(new(Proposal.EventKind.Rejected));

    /// ~150 ms after the last event, so the several one atomic rename raises settle into one check.
    void Debounced() {
        // A delay and a post rather than a DispatcherTimer: the same in the app and headless.
        int mine = ++_debounce;
        Task.Delay(150).ContinueWith(_ => Dispatcher.UIThread.Post(() => { if (mine == _debounce) Check(); }));
    }

    /// Reads the proposed file, if any, and turns what changed into an event. A
    /// directory event with no bearing on it (the sidecar Autosave just wrote,
    /// which fires this same watch) changes nothing.
    public void Check() {
        if (_photo == null || E.Session is not { } s || s.Path != _photo) return;
        // A drag finishes its document transaction before the committed reference is taken.
        if (E.Interacting) { Debounced(); return; }
        byte[]? onDisk = null;
        try { onDisk = File.ReadAllBytes(Proposal.ProposedPath(_photo)); }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { }
        catch (IOException) { Debounced(); return; }   // mid-write: look again

        if (!IsLive && onDisk != null) {
            _committed = State(s);
            _lastSeen = onDisk;
            Fire(new(Proposal.EventKind.Appeared, Proposal.ChangedKeys(Normal(_committed), Normal(onDisk))), onDisk);
        } else if (IsLive && onDisk != null && !onDisk.AsSpan().SequenceEqual(_lastSeen)) {
            _lastSeen = onDisk;
            Fire(new(Proposal.EventKind.Changed, Proposal.ChangedKeys(Normal(_committed!), Normal(onDisk))), onDisk);
        } else if (IsLive && onDisk == null) {
            var develop = Sidecar.Read(_photo)?.Develop;
            bool changed = develop != null && !develop.AsSpan().SequenceEqual(DevelopJson.Encode(_committed!));
            Fire(new(Proposal.EventKind.Vanished, SidecarChanged: changed));
        }
    }

    /// engine.state: the edit as the sidecar would hold it, mask names included.
    static JsonObject State(Session s) => s.Capture(MaskGroup.Shared.Shapes);

    /// Both sides through the one encoder, so a number compares by what it is, not how it was spelt.
    static JsonObject? Normal(JsonObject state) => JsonNode.Parse(DevelopJson.Encode(state)) as JsonObject;
    static JsonObject? Normal(byte[] proposed) => DevelopJson.Decode(proposed) is { } d ? Normal(d) : null;

    void Fire(Proposal.Event e, byte[]? proposed = null) {
        var (next, actions) = Proposal.Transition(_phase, e);
        _phase = next;
        if (IsLive) {
            E.EndInteraction();
            E.Tool = CanvasTool.None;
        }
        // A history step names the act it records, whichever render records it (EditHistory).
        if (actions.FirstOrDefault(a => a.Kind == Proposal.Act.RecordHistory) is { Label: { } label }) EditHistory.Label(label);
        foreach (var a in actions) Perform(a, proposed, e.Kind == Proposal.EventKind.SwitchedAway);
        Changed?.Invoke();
        CanvasState.Touch();
    }

    /// One action, turned into the real call. `leaving`: the photo is going back
    /// to the cache, so nothing is rendered or written for it.
    void Perform(Proposal.Action action, byte[]? proposed, bool leaving) {
        if (_photo == null || E.Session is not { } session || session.Path != _photo) return;
        switch (action.Kind) {
            case Proposal.Act.CaptureOriginal:
                CompareState.Reference = _committed;   // "Current edit", at the displayed framing
                break;
            case Proposal.Act.StopAutosave:
                Autosave.Shared.Stop();
                break;
            case Proposal.Act.RestoreProposed:
                if (proposed != null && DevelopJson.Decode(proposed) is { } state) Restore(session, state, leaving);
                break;
            case Proposal.Act.SetCompare:
                CompareState.SetSplit(0.5);
                break;
            case Proposal.Act.RecordHistory:
                if (!leaving) E.Render(preview: false);   // the render that records it, labelled above
                break;
            case Proposal.Act.BeginAutosave:
                if (_committed != null && session.Unreadable == null && !leaving) Autosave.Shared.Begin(_photo, _committed);
                break;
            case Proposal.Act.NoteAndFlush:
                Autosave.Shared.Note(State(session));
                Autosave.Shared.Flush();
                break;
            case Proposal.Act.DeleteProposedFile:
                try { File.Delete(Proposal.ProposedPath(_photo)); }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { Console.Error.WriteLine($"orion: proposal not deleted ({ex.Message})"); }
                _lastSeen = null;
                break;
            case Proposal.Act.RestoreCommitted:
                if (_committed != null) Restore(session, _committed, leaving);
                break;
            case Proposal.Act.RestoreSidecar:
                if (Sidecar.Read(_photo)?.Develop is { } develop && DevelopJson.Decode(develop) is { } saved) Restore(session, saved, leaving);
                break;
            case Proposal.Act.ClearCompare:
                CompareState.Reference = null;
                if (!leaving) CompareState.Clear();
                break;
        }
    }

    /// engine.apply: a whole state, without touching history's past, then one
    /// render and the panels re-read. A sidecar from before frame anchoring is
    /// converted on the way in, as an open converts it.
    static void Restore(Session session, JsonObject state, bool leaving) {
        var copy = (JsonObject)state.DeepClone();
        if (leaving) {
            try {
                session.ToFrameSpace(copy);
                session.Apply(copy);
            } catch (Exception e) when (e is ArgumentException or InvalidOperationException) {
                Console.Error.WriteLine($"orion: the committed edit could not be put back on {Path.GetFileName(session.Path)} ({e.Message})");
            }
            return;
        }
        E.Apply(s => { s.ToFrameSpace(copy); s.Apply(copy); }, reread: true);
    }
}

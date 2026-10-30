using System.Text.Json.Nodes;
using Avalonia.Threading;
using OrionShell.Masks;

namespace OrionShell;

/// Writes the open photo's edit to its sidecar without being asked: a port of
/// app/Autosave.swift. Coalesced writes 900 ms after the first unsaved change
/// catch the crash; a flush on quit, on leaving the window, and on every photo
/// switch catches the rest.
///
/// The queued state travels with the photo it belongs to, so a write landing
/// mid-switch can never file one photo's edit under another's name. A write
/// that fails keeps the job, says so on the status line, and is tried again at
/// the next settle, switch or quit: the baseline moves only once it has landed.
public sealed class Autosave {
    /// How a queued write is deferred: a timer in the app, a hand-fired closure in the selftest.
    readonly Action<Action> _defer;
    readonly Func<string, JsonObject, bool> _write;

    string? _target;
    string? _saved;   // the target's sidecar, as Capture's JSON text
    (string Path, JsonObject State, string Text)? _pending;
    bool _scheduled;

    public Autosave(Action<Action>? defer = null, Func<string, JsonObject, bool>? write = null) {
        _defer = defer ?? (body => DispatcherTimer.RunOnce(body, Settle));
        _write = write ?? ToSidecar;
    }

    /// 900 ms, from the first unsaved change: a slider drag is one write, not sixty,
    /// and a long drag still checkpoints instead of waiting for the mouse to come up.
    public static readonly TimeSpan Settle = TimeSpan.FromMilliseconds(900);

    /// Why the last write did not land, or null if it did.
    public string? LastFailure { get; private set; }
    public event Action? Changed;
    public bool IsDirty => _pending != null;

    public static bool ToSidecar(string photo, JsonObject state) =>
        Sidecar.Merge(photo, s => s.Develop = DevelopJson.Encode(state));

    /// A photo is open and settled; `saved` is what its sidecar already holds,
    /// so opening a photo does not write straight back what it just read.
    /// A write still owed to this photo (it failed) means its sidecar is not
    /// `saved`, so the baseline is unknown and the next note re-queues it.
    public void Begin(string photo, JsonObject saved) {
        Flush();
        (_target, _saved) = (photo, _pending?.Path == photo ? null : saved.ToJsonString());
    }

    /// Nothing is open, or the open photo must not be written (an unreadable sidecar).
    public void Stop() {
        Flush();
        (_target, _saved) = (null, null);
    }

    /// The edit changed. Cheap enough for every render.
    public void Note(JsonObject state) {
        if (_target == null) return;
        string text = state.ToJsonString();
        if (text == _saved) {
            // Back to what is saved: an owed write for this photo is obsolete. Never another photo's.
            if (_pending?.Path == _target) { _pending = null; SetFailure(null); }
            return;
        }
        // A write still owed to another photo is never replaced: it is tried first, and while it
        // fails this photo's edit waits for the next note (the engine refuses to leave meanwhile).
        if (_pending is { } other && other.Path != _target) {
            Flush();
            if (_pending != null) return;
        }
        _pending = (_target, state, text);
        if (_scheduled) return;
        _scheduled = true;
        _defer(Flush);
    }

    /// Writes what is owed, now. A no-op when nothing is.
    public void Flush() {
        _scheduled = false;   // cleared either way, so something owed always has a deferral outstanding
        if (_pending is not { } job) return;
        if (!_write(job.Path, job.State)) {
            SetFailure($"The edits to {Path.GetFileName(job.Path)} could not be saved. Orion will keep trying; do not quit yet.");
            return;
        }
        _pending = null;
        SetFailure(null);
        if (job.Path == _target) _saved = job.Text;   // a late write for a photo already left teaches nothing about this one
    }

    /// Editor.canLeavePhoto's half: write what is owed, and say whether it landed.
    public bool FlushBeforeLeaving() {
        Flush();
        return !IsDirty;
    }

    void SetFailure(string? why) {
        if (LastFailure == why) return;
        LastFailure = why;
        Changed?.Invoke();
    }

    // ── The app's one instance, on the engine's lifecycle ────────────────────

    public static Autosave Shared { get; } = new();

    /// Arms Shared on the engine. Only the app at a window calls it: the
    /// headless modes read sidecars but never write the photographer's.
    public static void Install() {
        var e = EngineModel.Shared;
        e.Opened += Opened;
        e.Rendered += () => {
            if (e.Session is { Unreadable: null } s && s.Path == Shared._target) Shared.Note(s.Capture(MaskGroup.Shared.Shapes));
        };
        if (e.Session != null) Opened();
    }

    static void Opened() {
        if (EngineModel.Shared.Session is not { } s) return;
        if (s.Unreadable is { } why) {
            Shared.Stop();
            EngineModel.Shared.Say(why);
        } else Shared.Begin(s.Path, s.Document!);
    }
}

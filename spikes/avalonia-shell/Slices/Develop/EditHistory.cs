namespace OrionShell.Develop;

/// A port of app/EditHistory.swift over the session's field values: whole
/// snapshots rather than deltas, a new branch drops the redoable tail, fifty
/// deep, reset on every open. Swift records where a control calls
/// `engine.edit(label)`; here every settled render is a candidate, so a panel
/// cannot forget to record itself. A drag settles once, at its full render.
/// The label is the control's (CropEdit.Commit) when one said, otherwise the
/// fields that moved; edits under one label within 0.6 s are one step.
///
/// Also `resetEdits` (Engine+Document.swift): the defaults are the state a
/// photo first opened in, which is the shim's as-shot look.
static class EditHistory {
    sealed record Entry(string Label, float[] State, DateTime Time);

    const int Depth = 50;
    static readonly TimeSpan Coalesce = TimeSpan.FromSeconds(0.6);
    /// View state the crop tab writes, not edits: Swift keeps them out of DevelopState.
    static readonly HashSet<string> NotEdits = ["crop_preview", "preview_x", "preview_y", "preview_size"];
    static readonly (string Name, int Index)[] Slots = EngineModel.Fields
        .Where(f => !NotEdits.Contains(f.Name))
        .SelectMany(f => Enumerable.Range(0, f.Length).Select(i => (f.Name, i))).ToArray();

    static readonly List<Entry> _entries = new();
    static int _position = -1;
    static string? _label;
    static readonly Dictionary<string, float[]> _defaults = new();

    static EngineModel E => EngineModel.Shared;
    public static event Action? Changed;

    public static bool CanUndo => _position > 0;
    public static bool CanRedo => _position >= 0 && _position < _entries.Count - 1;
    public static string? UndoLabel => CanUndo ? _entries[_position].Label : null;
    public static string? RedoLabel => CanRedo ? _entries[_position + 1].Label : null;

    static EditHistory() {
        E.Opened += Opened;
        E.Rendered += Settle;
        // The shell opens its first photo before it builds the window that first asks.
        if (E.Handle != 0) Opened();
    }

    static void Opened() {
        var state = Snapshot();
        _defaults.TryAdd(E.Photos[E.Current], state);
        Reset(state);
    }

    /// Names the next settled edit, as `engine.edit("Crop") { … }` does.
    public static void Label(string label) => _label = label;

    public static float[] Snapshot() => Slots.Select(s => E.Get(s.Name, s.Index)).ToArray();

    public static void Undo() { if (CanUndo) Restore(_entries[--_position].State); }
    public static void Redo() { if (CanRedo) Restore(_entries[++_position].State); }

    /// Every adjustment back to the as-shot look: one write, one render, one step.
    public static void ResetEdits() {
        if (E.Current < 0 || !_defaults.TryGetValue(E.Photos[E.Current], out var defaults)) return;
        Label("Reset");
        Restore(defaults);
    }

    /// Writes a snapshot back without a render per field, then renders once and
    /// tells the panels their values moved.
    public static void Restore(float[] state) {
        E.Apply(s => {
            for (int i = 0; i < Slots.Length; i++)
                if (s.Get(Slots[i].Name, Slots[i].Index) != state[i]) s.Set(Slots[i].Name, state[i], Slots[i].Index);
        }, reread: true);
        Changed?.Invoke();
    }

    static void Reset(float[] state) {
        _entries.Clear();
        _entries.Add(new Entry("Opened", state, DateTime.Now));
        _position = 0;
        _label = null;
        Changed?.Invoke();
    }

    /// A render settled: if the state is not the one history is standing on, record it.
    static void Settle() {
        if (E.Interacting || E.Handle == 0) return;
        var state = Snapshot();
        if (_position < 0) { Reset(state); return; }
        var here = _entries[_position].State;
        var moved = Enumerable.Range(0, Slots.Length).Where(i => here[i] != state[i]).ToList();
        if (moved.Count == 0) return;
        string label = _label ?? string.Join(", ", moved.Select(i => Slots[i].Name).Distinct());
        _label = null;
        Record(state, label);
    }

    static void Record(float[] state, string label) {
        if (_position < _entries.Count - 1) _entries.RemoveRange(_position + 1, _entries.Count - _position - 1);
        var now = DateTime.Now;
        var last = _entries[^1];
        if (last.Label == label && now - last.Time < Coalesce) {
            _entries[^1] = new Entry(label, state, now);
        } else {
            _entries.Add(new Entry(label, state, now));
            if (_entries.Count > Depth) _entries.RemoveRange(0, _entries.Count - Depth);
            _position = _entries.Count - 1;
        }
        Changed?.Invoke();
    }
}

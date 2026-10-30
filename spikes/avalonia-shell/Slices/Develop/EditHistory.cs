using Avalonia;
using OrionShell.CanvasSlice;

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
static partial class EditHistory {
    /// The numbers, and the mask rows with their names and matte ids (EditHistory.Masks.cs).
    sealed record Entry(string Label, float[] State, MaskShape[] Masks, DateTime Time);

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
    static readonly Dictionary<string, Entry> _defaults = new();

    static EngineModel E => EngineModel.Shared;
    public static event Action? Changed;

    public static bool CanUndo => _position > 0;
    public static bool CanRedo => _position >= 0 && _position < _entries.Count - 1;
    public static string? UndoLabel => CanUndo ? _entries[_position].Label : null;
    public static string? RedoLabel => CanRedo ? _entries[_position + 1].Label : null;

    static EditHistory() {
        // The spots clear on open before the open is recorded: their handler first, or "Opened" holds the last photo's.
        System.Runtime.CompilerServices.RuntimeHelpers.RunClassConstructor(typeof(SpotModel).TypeHandle);
        _ = Masks.MaskGroup.Shared;   // and the mask rows reread on open first, for the same reason
        E.Opened += Opened;
        E.Rendered += Settle;
        // The shell opens its first photo before it builds the window that first asks.
        if (E.Handle != 0) Opened();
    }

    static void Opened() {
        var opened = new Entry("Opened", Snapshot(), MaskRows(), DateTime.Now);
        _defaults.TryAdd(E.Photos[E.Current], opened);
        Reset(opened);
    }

    /// Names the next settled edit, as `engine.edit("Crop") { … }` does.
    public static void Label(string label) => _label = label;

    /// Every field slot, then the tone curve: it is not an OrionAdjustments field
    /// the table reaches, so without it a curve edit, or a preset's, never undid.
    public static float[] Snapshot() => [.. Slots.Select(s => E.Get(s.Name, s.Index)), .. Curves(), .. Spots()];

    /// The spots, which no field reaches: their count, then up to ORION_MAX_SPOTS rows of
    /// destination, source, radius and heal. Without them a spot placed or moved never undid (commitSpotEdit).
    const int MaxSpots = 64, SpotRow = 6;

    static float[] Spots() {
        var row = new float[1 + MaxSpots * SpotRow];
        var spots = SpotModel.Spots;
        row[0] = Math.Min(spots.Count, MaxSpots);
        for (int i = 0; i < row[0]; i++) {
            var s = spots[i];
            float[] values = [(float)s.Destination.X, (float)s.Destination.Y, (float)s.Source.X, (float)s.Source.Y, (float)s.Radius, s.Heal ? 1 : 0];
            values.CopyTo(row, 1 + i * SpotRow);
        }
        return row;
    }

    static void RestoreSpots(float[] state) {
        int at = Slots.Length + CurveChannels * CurveRow;
        SpotModel.Replace(Enumerable.Range(0, (int)state[at]).Select(i => at + 1 + i * SpotRow).Select(r =>
            new SpotPlacement(new Point(state[r], state[r + 1]), new Point(state[r + 2], state[r + 3]), state[r + 4], state[r + 5] != 0)).ToList());
    }

    /// Four channels, each its point count then up to sixteen x, y pairs.
    const int CurveChannels = 4, CurveRow = 1 + 16 * 2;

    static float[] Curves() {
        var row = new float[CurveChannels * CurveRow];
        for (int c = 0; c < CurveChannels; c++) {
            IReadOnlyList<(float X, float Y)> points;
            try { points = E.Session?.Curve(c) ?? CurveMath.Identity; } catch (NotBuiltException) { points = CurveMath.Identity; }
            row[c * CurveRow] = points.Count;
            for (int i = 0; i < points.Count && i < 16; i++) (row[c * CurveRow + 1 + 2 * i], row[c * CurveRow + 2 + 2 * i]) = points[i];
        }
        return row;
    }

    static List<(float X, float Y)> Curve(float[] state, int channel) {
        int at = Slots.Length + channel * CurveRow;
        return Enumerable.Range(0, (int)state[at]).Select(i => (state[at + 1 + 2 * i], state[at + 2 + 2 * i])).ToList();
    }

    static string Name(int slot) => slot < Slots.Length ? Slots[slot].Name : slot < Slots.Length + CurveChannels * CurveRow ? "curve" : "Spot";

    public static void Undo() { if (CanUndo) Restore(_entries[--_position]); }
    public static void Redo() { if (CanRedo) Restore(_entries[++_position]); }

    /// Every adjustment back to the as-shot look: one write, one render, one step.
    public static void ResetEdits() {
        if (E.Current < 0 || !_defaults.TryGetValue(E.Photos[E.Current], out var defaults)) return;
        Label("Reset");
        Restore(defaults);
    }

    /// Writes a snapshot back without a render per field, then renders once and
    /// tells the panels their values moved.
    static void Restore(Entry entry) {
        var state = entry.State;
        RestoreSpots(state);   // before the render, whose settle must find history standing on this state
        var rows = RestoreMaskNames(entry.Masks);
        E.Apply(s => {
            for (int i = 0; i < Slots.Length; i++)
                if (s.Get(Slots[i].Name, Slots[i].Index) != state[i]) s.Set(Slots[i].Name, state[i], Slots[i].Index);
            for (int c = 0; c < CurveChannels; c++)
                if (!s.Curve(c).SequenceEqual(Curve(state, c))) s.SetCurve(c, Curve(state, c));
            s.SetMaskGroup(rows);
        }, reread: true);
        Changed?.Invoke();
    }

    static void Reset(Entry opened) {
        _entries.Clear();
        _entries.Add(opened);
        _position = 0;
        _label = null;
        Changed?.Invoke();
    }

    /// A render settled: if the state is not the one history is standing on, record it.
    static void Settle() {
        if (E.Interacting || E.Handle == 0) return;
        var state = Snapshot();
        var masks = MaskRows();
        if (_position < 0) { Reset(new Entry("Opened", state, masks, DateTime.Now)); return; }
        var here = _entries[_position].State;
        var moved = Enumerable.Range(0, state.Length).Where(i => here[i] != state[i]).Select(Name).ToList();
        if (!SameMasks(_entries[_position].Masks, masks)) moved.Add("mask");
        // Taken either way: a named act that changed nothing (a click on the crop, a
        // preset already in effect) must not name the next, unrelated, step.
        string? named = _label;
        _label = null;
        if (moved.Count == 0) return;
        Record(state, masks, named ?? string.Join(", ", moved.Distinct()));
    }

    static void Record(float[] state, MaskShape[] masks, string label) {
        if (_position < _entries.Count - 1) _entries.RemoveRange(_position + 1, _entries.Count - _position - 1);
        var now = DateTime.Now;
        var last = _entries[^1];
        if (last.Label == label && now - last.Time < Coalesce) {
            _entries[^1] = new Entry(label, state, masks, now);
        } else {
            _entries.Add(new Entry(label, state, masks, now));
            if (_entries.Count > Depth) _entries.RemoveRange(0, _entries.Count - Depth);
            _position = _entries.Count - 1;
        }
        Changed?.Invoke();
    }
}

using Avalonia.Threading;
using OrionShell.Develop;

namespace OrionShell.Masks;

/// The mask group the Mask tab and the canvas edit: the rows (MaskShape), which
/// one is selected, their grouping into masks (MaskLayers.swift, ported whole),
/// what the canvas draws next, and the overlay.
///
/// The engine's rows are the truth. Every edit goes out through
/// Session.SetMaskGroup, and the rows come back from Session.ReadMaskGroup
/// whenever the values move under the panel (undo, redo, reset) or a photo
/// opens, so undo needs nothing here: the shim puts the rows and each brush
/// revision in the field table EditHistory already snapshots.
public sealed class MaskGroup {
    public static MaskGroup Shared { get; } = new();
    public const int Max = 8;                       // ORION_MAX_MASK_COMPONENTS
    public static readonly MaskShape Defaults = new();

    public List<MaskShape> Shapes { get; } = [];
    /// Engine.brushErasing: how the next stroke lands, not part of the edit.
    public bool BrushErasing;
    /// A brush slider is moving: the canvas shows the nib mid-photo until the pointer comes back.
    public bool BrushPreview;
    /// A structural change: rows, selection, names, visibility, the armed kind,
    /// the overlay switch, a canvas gesture ending. Shape numbers moved by a
    /// slider or mid-drag do not raise it, or the panel would rebuild under the hand.
    public event Action? Changed;

    MaskGroup() {
        var engine = EngineModel.Shared;
        // A photo's rows come with its session (a cached one keeps its masks). The
        // overlay goes into it unrendered: the open renders next anyway, and a
        // render here would cost every arrow-key step (#318).
        engine.Opened += () => {
            ColorSwatch = null;
            Shapes.Clear();
            _selected = 0;
            Read();
            if (engine.Session is { } s) WriteOverlay(s);
            Changed?.Invoke();
        };
        engine.ValuesChanged += Reload;
        // One armed tool at a time (#211): a draw is CanvasTool.MaskDraw, so arming the color
        // picker or spots, or Escape clearing the tool, disarms it.
        engine.PropertyChanged += (_, e) => {
            if (e.PropertyName == nameof(EngineModel.Tool) && engine.Tool != CanvasTool.MaskDraw) Drawing = 0;
        };
    }

    /// Reads the rows back from the engine. Names and matte references never
    /// cross the facade (#207), so they stay with their position.
    public void Reload() {
        Read();
        var engine = EngineModel.Shared;
        // The rows moved under the panel (undo): each matte follows its row's id.
        if (engine.Session is { } session && MatteStore.Upload(session, Shapes)) engine.Render(preview: false);
        SyncOverlay();
        Changed?.Invoke();
    }

    void Read() {
        var session = EngineModel.Shared.Session;
        var rows = session?.ReadMaskGroup() ?? [];
        // A photo just opened has no rows here yet: its names come from its sidecar (Session.Document).
        var named = Shapes.Count > 0 ? Shapes : Session.Rows(session?.Document);
        for (int i = 0; i < rows.Count && i < named.Count; i++)
            (rows[i].Name, rows[i].MatteId, rows[i].MatteSource) = (named[i].Name, named[i].MatteId, named[i].MatteSource);
        Shapes.Clear();
        Shapes.AddRange(rows);
        _selected = Math.Clamp(_selected, 0, Math.Max(Shapes.Count - 1, 0));
    }

    public int Selected {
        get => _selected;
        set {
            if (_selected == value) return;
            _selected = value;
            SyncOverlay();
            Changed?.Invoke();
        }
    }
    int _selected;

    public MaskShape? Current => _selected >= 0 && _selected < Shapes.Count ? Shapes[_selected] : null;
    public int Kind => Current?.Kind ?? 0;
    public int SelectedLayer => Math.Min(LayerOf(_selected), Max - 1);
    public int LayerCount => Runs().Count;

    /// The kind a press on the photo draws next: 1 linear, 2 radial, 3 brush, 0 none.
    /// One-shot: the press that draws disarms it.
    public int Drawing {
        get => _drawing;
        set => Arm(value, null);
    }
    int _drawing;

    /// What the armed draw makes: a new mask (null), or a shape into the
    /// selected one with this op (0 add, 1 subtract): Lightroom's Add and Subtract.
    public int? DrawInto { get; private set; }

    public void Arm(int kind, int? into) {
        if (_drawing == kind && DrawInto == into) return;
        (_drawing, DrawInto) = (kind, kind == 0 || Shapes.Count == 0 ? null : into);
        var engine = EngineModel.Shared;
        if (kind != 0) engine.Tool = CanvasTool.MaskDraw;
        else if (engine.Tool == CanvasTool.MaskDraw) engine.Tool = CanvasTool.None;
        Changed?.Invoke();
    }

    /// The armed draw's row, placed by `place` before its first render.
    public bool Create(int kind, Action<MaskShape>? place) => DrawInto is { } op ? AddInto(kind, op, place) : Add(kind, place);

    // MARK: The overlay. A way of looking, not an edit (#57): never in history.

    /// "Show overlay": on unless the photographer turns it off, for the session.
    public bool Overlay {
        get => _overlay;
        set { if (_overlay != value) { _overlay = value; SyncOverlay(); Changed?.Invoke(); } }
    }
    bool _overlay = true;

    /// The Mask tab is on show; the canvas says. Off, there is no overlay and nothing armed.
    public bool Showing {
        get => _showing;
        set {
            if (_showing == value) return;
            _showing = value;
            SyncOverlay();
            if (!value && _drawing != 0) Arm(0, null);   // an armed tool belongs to the tab that armed it (#211)
        }
    }
    bool _showing;

    /// A mask under the pointer (its card, or its pin on the photo): its
    /// coverage paints until the pointer leaves, switch on or off (Lightroom's hover).
    public int? Hover {
        get => _hover;
        set { if (_hover != value) { _hover = value; SyncOverlay(); } }
    }
    int? _hover;

    /// What the overlay should be now: on, and which mask it paints.
    public (bool On, int Layer) OverlayWanted =>
        (_showing && Shapes.Count > 0 && (_hover != null || (_overlay && Current != null)), _hover ?? SelectedLayer);
    (bool, int)? _pushed;

    void WriteOverlay(Session s) {
        s.SetMaskOverlay(OverlayWanted.On, OverlayWanted.Layer);
        _pushed = OverlayWanted;
    }

    /// Renders only when what the overlay should be has changed, and only the
    /// screen-sized preview graph at fit (#318): pointing at a card is a way of
    /// looking, and a full 24 MP render per card would lag the pointer by ~300 ms.
    /// Once the overlay rests, the full graph follows, as an open's does
    /// (EngineModel.Arrive): the histogram and eyedropper read that one.
    void SyncOverlay() {
        var engine = EngineModel.Shared;
        if (_pushed == OverlayWanted || engine.Session is not { } s) return;
        WriteOverlay(s);
        var wanted = OverlayWanted;
        engine.Render(preview: !engine.ZoomedPastFit || engine.Interacting);
        Task.Delay(300).ContinueWith(_ => Dispatcher.UIThread.Post(() => {
            if (_pushed == wanted && engine.Session == s && !engine.Interacting && engine.Shown.Graph != Graph.Full)
                engine.Render(preview: false);
        }));
    }

    // MARK: MaskLayers.swift

    /// The stack as runs of row indices. Row 0 always begins one.
    public List<List<int>> Runs() => RunsOf(Shapes);

    static List<List<int>> RunsOf(IReadOnlyList<MaskShape> shapes) {
        var runs = new List<List<int>>();
        for (int i = 0; i < shapes.Count; i++)
            if (i == 0 || shapes[i].StartsLayer) runs.Add([i]);
            else runs[^1].Add(i);
        return runs;
    }

    /// Which layer a row belongs to; clamps, so a selection past the end names a real one.
    public int LayerOf(int index) {
        if (Shapes.Count == 0) return 0;
        int i = Math.Clamp(index, 0, Shapes.Count - 1);
        int layer = 0;
        for (int j = 1; j < i; j++) if (Shapes[j].StartsLayer) layer++;
        if (i > 0 && Shapes[i].StartsLayer) layer++;
        return layer;
    }

    public static string KindName(int kind) => kind switch {
        1 => "Linear", 2 => "Radial", 3 => "Brush", 4 => "Selection", 5 => "Range", 6 => "Color", _ => "Off",
    };

    /// The card's name: the first row's chosen name, else "Radial 2".
    public string DisplayName(int layer) {
        var runs = Runs();
        if (layer < 0 || layer >= runs.Count) return "";
        var chosen = Shapes[runs[layer][0]].Name?.Trim();
        if (!string.IsNullOrEmpty(chosen)) return chosen;
        string Word(int i) => Shapes[i].MatteSource ?? KindName(Shapes[i].Kind);
        string mine = Word(runs[layer][0]);
        int ordinal = runs.Take(layer + 1).Count(run => Word(run[0]) == mine);
        return $"{mine} {ordinal}";
    }

    public bool LayerHidden(int layer) {
        var runs = Runs();
        return layer >= 0 && layer < runs.Count && runs[layer].All(i => Shapes[i].Hidden);
    }

    // MARK: Engine+Mask.swift's row operations, each one undo step.

    /// A new mask starts its own layer (decision #197). `place` sets its geometry before the first render.
    public bool Add(int kind, Action<MaskShape>? place = null) {
        if (Shapes.Count >= Max) return false;
        var m = new MaskShape { Kind = kind, StartsLayer = Shapes.Count > 0 };
        place?.Invoke(m);
        Structural($"Add {KindName(kind).ToLowerInvariant()}", () => { Shapes.Add(m); _selected = Shapes.Count - 1; });
        return true;
    }

    /// A shape folded into the selected mask at the end of its run: Lightroom's
    /// Add (compose 0) and Subtract (1) on the selected mask.
    public bool AddInto(int kind, int compose = 0, Action<MaskShape>? place = null) {
        if (Shapes.Count >= Max || Shapes.Count == 0) return false;
        int at = Runs()[LayerOf(_selected)][^1] + 1;
        var m = new MaskShape { Kind = kind, Compose = compose };
        place?.Invoke(m);
        Structural(compose == 1 ? "Subtract shape" : "Add shape", () => { Shapes.Insert(at, m); _selected = at; });
        return true;
    }

    /// One row. A row that began its mask hands the start to the next row of the
    /// run, which folds from zero, so its op becomes add (a subtract from nothing is nothing).
    public void Remove(int index) {
        if (index < 0 || index >= Shapes.Count) return;
        Structural("Remove shape", () => {
            bool began = index == 0 || Shapes[index].StartsLayer;
            Shapes.RemoveAt(index);
            if (began && index < Shapes.Count && !Shapes[index].StartsLayer)
                (Shapes[index].StartsLayer, Shapes[index].Compose) = (index > 0, 0);
            _selected = Shapes.Count == 0 ? 0 : Math.Min(index, Shapes.Count - 1);
        });
    }

    /// A whole mask, every row of its run.
    public void RemoveLayer(int layer) {
        var runs = Runs();
        if (layer < 0 || layer >= runs.Count) return;
        Structural("Delete mask", () => {
            Shapes.RemoveRange(runs[layer][0], runs[layer].Count);
            if (Shapes.Count > 0) Shapes[0].Compose = 0;
            _selected = Shapes.Count == 0 ? 0 : Math.Min(runs[layer][0], Shapes.Count - 1);
        });
    }

    /// Every row, one step.
    public void Clear() {
        if (Shapes.Count > 0) Structural("Delete all masks", () => { Shapes.Clear(); _selected = 0; });
    }

    /// A whole mask up (−1) or down (+1) the list, its adjustments with it.
    public void MoveLayer(int layer, int by) {
        var runs = Runs();
        int to = layer + by;
        if (layer < 0 || layer >= runs.Count || to < 0 || to >= runs.Count) return;
        Structural("Move mask", () => {
            var rows = runs.Select(r => r.Select(i => Shapes[i]).ToList()).ToList();
            (rows[layer], rows[to]) = (rows[to], rows[layer]);
            Shapes.Clear();
            foreach (var run in rows) {
                run[0].StartsLayer = Shapes.Count > 0;
                Shapes.AddRange(run);
            }
            _selected = Shapes.IndexOf(rows[to][0]);
        });
    }

    /// What was under the last colour pick, display-referred, for the swatch:
    /// the stored target is peak-normalized and would draw saturated. Null after an open.
    public float[]? ColorSwatch { get; private set; }

    /// Engine+Mask.swift's pickMaskColor: the scene-linear colour into the
    /// selected row, one undo step. Oklab chromaticity ignores the scale, so the
    /// peak-normalized sample selects what was clicked (research/masking.md §4c).
    public bool PickColor(float[] scene, float[] display) {
        if (Current is not { } m) return false;
        (m.ColorR, m.ColorG, m.ColorB) = (scene[0], scene[1], scene[2]);
        ColorSwatch = display;
        Push("Mask color");
        Changed?.Invoke();
        return true;
    }

    /// Engine+Mask.swift's setMaskKind: what an existing row is. A shape is not a
    /// selection, so leaving kind 4 drops the matte reference with it.
    public void SetKind(int index, int kind) => Edit(index, "Mask kind", m => {
        m.Kind = kind;
        if (kind != 4) (m.MatteId, m.MatteSource) = (null, null);
    });

    public void SetCompose(int index, int compose) => Edit(index, "Mask op", m => m.Compose = compose);
    public void ToggleHidden(int index) => Edit(index, "Hide shape", m => m.Hidden = !m.Hidden);
    public void Rename(int start, string name) => Edit(start, "Rename mask", m => m.Name = name.Trim() is { Length: > 0 } t ? t : null);

    /// Row 0 always begins a layer.
    public void SetLayerBreak(bool starts, int index) {
        if (index > 0) Edit(index, starts ? "Make own mask" : "Join mask above", m => (m.StartsLayer, m.Compose) = (starts, starts ? 0 : m.Compose));
    }

    /// "Subtract from mask above": the break and the op in one act.
    public void MergeIntoLayerAbove(int start, int compose) {
        if (start > 0 && start < Shapes.Count && Shapes[start].StartsLayer)
            Edit(start, "Merge mask", m => (m.StartsLayer, m.Compose) = (false, compose));
    }

    public void ToggleLayerHidden(int layer) {
        var runs = Runs();
        if (layer < 0 || layer >= runs.Count) return;
        bool hide = !LayerHidden(layer);
        Structural(hide ? "Hide mask" : "Show mask", () => { foreach (int i in runs[layer]) Shapes[i].Hidden = hide; });
    }

    void Edit(int index, string label, Action<MaskShape> change) {
        if (index >= 0 && index < Shapes.Count) Structural(label, () => change(Shapes[index]));
    }

    /// A change to the rows: each mask's local adjustments follow its rows to
    /// wherever they now are (a removal shifts every later mask down a slot),
    /// then the rows go out with one render and one undo step.
    void Structural(string label, Action change) {
        var session = EngineModel.Shared.Session;
        var before = RunsOf(Shapes).Select(r => r.Select(i => Shapes[i]).ToList()).ToList();
        var locals = LocalFields.ToDictionary(f => f, f => Enumerable.Range(0, Max).Select(l => session?.Get(f, l) ?? 0).ToArray());
        change();
        var carried = Carried(before, RunsOf(Shapes).Select(r => r.Select(i => Shapes[i]).ToList()).ToList());
        Push(label, s => {
            foreach (var (field, old) in locals)
                for (int layer = 0; layer < Max; layer++) {
                    float v = layer < carried.Length && carried[layer] >= 0 ? old[carried[layer]] : 0;
                    if (s.Get(field, layer) != v) s.Set(field, v, layer);
                }
        });
        Changed?.Invoke();
    }

    /// Every per-mask adjustment array in OrionAdjustments (local_exposure_ev…).
    static readonly string[] LocalFields = EngineModel.Fields.Where(f => f.Name.StartsWith("local_") && f.Length == Max)
                                                             .Select(f => f.Name).ToArray();

    /// For each mask after a change, the mask before it whose adjustments it
    /// keeps: the one its first surviving row belonged to, each claimed once
    /// (a split's second half starts clean), or −1 for a mask that is all new.
    public static int[] Carried<T>(IReadOnlyList<IReadOnlyList<T>> before, IReadOnlyList<IReadOnlyList<T>> after) where T : class {
        var owner = new Dictionary<T, int>(ReferenceEqualityComparer.Instance);
        for (int l = 0; l < before.Count; l++) foreach (var row in before[l]) owner[row] = l;
        var claimed = new HashSet<int>();
        return after.Select(run => run.Select(r => owner.TryGetValue(r, out int l) ? l : -1).FirstOrDefault(l => l >= 0, -1))
                    .Select(l => l >= 0 && claimed.Add(l) ? l : -1).ToArray();
    }

    // MARK: Pushes

    /// The rows to the engine (and the overlay with them), one render. `label`
    /// names the undo step the render settles into (EditHistory.Label).
    public void Push(string label = "Mask", Action<Session>? also = null) {
        EditHistory.Label(label);
        EngineModel.Shared.Apply(s => { also?.Invoke(s); WriteOverlay(s); s.SetMaskGroup(Shapes); MatteStore.Upload(s, Shapes); });
    }

    /// Dabs into the selected brush row, painting or erasing, then pushed.
    public void Paint(IEnumerable<(double X, double Y)> dabs, bool erase) {
        if (Current is not { Kind: 3 } m) return;
        var e = m.BrushErase;
        while (e.Count < m.BrushStroke.Count / 2) e.Add(0);
        foreach (var (x, y) in dabs) {
            m.BrushStroke.Add((float)x);
            m.BrushStroke.Add((float)y);
            e.Add(erase ? 1 : 0);
        }
        Push(erase ? "Erase" : "Paint");
    }

    /// A canvas gesture or a slider finished: the panel may rebuild now.
    public void Settle() => Changed?.Invoke();
}

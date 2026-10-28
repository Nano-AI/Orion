namespace OrionShell.Masks;

/// The mask group the Mask tab edits: the row list (MaskShape, in Shared), which
/// row is selected, and the grouping into layers (MaskLayers.swift, ported
/// whole: pure functions).
///
/// Every edit pushes the rows through Session.SetMaskGroup, which stage 1b
/// builds: OrionAdjustments.mask_components is a struct array the shim's field
/// table skips. Until then the push puts "not built yet" on the status line.
public sealed class MaskGroup {
    public static MaskGroup Shared { get; } = new();
    public const int Max = 8;                       // ORION_MAX_MASK_COMPONENTS
    public static readonly MaskShape Defaults = new();

    public List<MaskShape> Shapes { get; } = [];
    /// Engine.brushErasing: how the next stroke lands, not part of the edit.
    public bool BrushErasing;
    /// A structural change: rows, selection, names, visibility. Shape numbers
    /// moved by a slider do not raise it, or the slider would be rebuilt under the hand.
    public event Action? Changed;

    /// A new photo starts with no rows; its saved ones are in its sidecar.
    // stub(#315): Engine+Mask.swift owns this until the logic moves below the facade
    MaskGroup() => EngineModel.Shared.Opened += () => { Shapes.Clear(); _selected = 0; Changed?.Invoke(); };

    public int Selected {
        get => _selected;
        set { if (_selected != value) { _selected = value; Changed?.Invoke(); } }
    }
    int _selected;

    public MaskShape? Current => _selected >= 0 && _selected < Shapes.Count ? Shapes[_selected] : null;
    public int Kind => Current?.Kind ?? 0;
    public int SelectedLayer => Math.Min(LayerOf(_selected), Max - 1);
    public int LayerCount => Runs().Count;

    // MARK: MaskLayers.swift

    /// The stack as runs of row indices. Row 0 always begins one.
    public List<List<int>> Runs() {
        var runs = new List<List<int>>();
        for (int i = 0; i < Shapes.Count; i++)
            if (i == 0 || Shapes[i].StartsLayer) runs.Add([i]);
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

    // MARK: Engine+Mask.swift's row operations. None records history (commitMaskGroupEdit).
    // stub(#315): EditHistory.swift owns this until the logic moves below the facade

    /// A new mask starts its own layer (decision #197).
    public bool Add(int kind) {
        if (Shapes.Count >= Max) return false;
        Shapes.Add(new MaskShape { Kind = kind, StartsLayer = Shapes.Count > 0 });
        _selected = Shapes.Count - 1;
        Commit();
        return true;
    }

    /// A shape folded into the selected mask: at the end of its run, composing with add.
    public bool AddInto(int kind) {
        if (Shapes.Count >= Max || Shapes.Count == 0) return false;
        int at = Runs()[LayerOf(_selected)][^1] + 1;
        Shapes.Insert(at, new MaskShape { Kind = kind });
        _selected = at;
        Commit();
        return true;
    }

    public void Remove(int index) {
        if (index < 0 || index >= Shapes.Count) return;
        Shapes.RemoveAt(index);
        _selected = Shapes.Count == 0 ? 0 : Math.Min(index, Shapes.Count - 1);
        Commit();
    }

    public void Move(int index, int by) {
        int to = index + by;
        if (index < 0 || index >= Shapes.Count || to < 0 || to >= Shapes.Count) return;
        (Shapes[index], Shapes[to]) = (Shapes[to], Shapes[index]);
        _selected = to;
        Commit();
    }

    /// "This row is a radial now": keeps everything else, drops a matte reference.
    public void SetKind(int index, int kind) => Edit(index, m => {
        m.Kind = kind;
        if (kind != 4) (m.MatteId, m.MatteSource) = (null, null);
    });

    public void SetCompose(int compose) => Edit(_selected, m => m.Compose = compose);
    public void ToggleHidden(int index) => Edit(index, m => m.Hidden = !m.Hidden);
    public void Rename(int start, string name) => Edit(start, m => m.Name = name.Trim() is { Length: > 0 } t ? t : null);

    /// Row 0 always begins a layer.
    public void SetLayerBreak(bool starts, int index) {
        if (index > 0) Edit(index, m => m.StartsLayer = starts);
    }

    /// "Subtract from mask above": the break and the op in one act.
    public void MergeIntoLayerAbove(int start, int compose) {
        if (start > 0 && start < Shapes.Count && Shapes[start].StartsLayer)
            Edit(start, m => (m.StartsLayer, m.Compose) = (false, compose));
    }

    public void ToggleLayerHidden(int layer) {
        var runs = Runs();
        if (layer < 0 || layer >= runs.Count) return;
        bool hide = !LayerHidden(layer);
        foreach (int i in runs[layer]) Shapes[i].Hidden = hide;
        Commit();
    }

    void Edit(int index, Action<MaskShape> change) {
        if (index < 0 || index >= Shapes.Count) return;
        change(Shapes[index]);
        Commit();
    }

    /// The rows to the engine, then the structural Changed.
    void Commit() {
        Push();
        Changed?.Invoke();
    }

    /// Engine+Mask.swift's write: every row, in order, through Session.SetMaskGroup
    /// (stage 1b's; until then the status line says so). The panel's shape
    /// sliders call this after moving a number.
    public void Push() => EngineModel.Shared.Apply(s => s.SetMaskGroup(Shapes));
}

namespace OrionShell.Library;

/// A port of app/PhotoSelection.swift: which photographs a batch acts on.
/// Pure value logic over paths, so the selftest pins it without a window.
///
/// One selected photo is not a selection: the photo on the canvas is always
/// chosen, so `Targets` falls back to everything in view until there are two
/// (Lightroom draws the same line).
public sealed class PhotoSelection {
    [Flags] public enum Modifiers { None = 0, Command = 1, Shift = 2 }

    readonly HashSet<string> _chosen = new();

    /// Where a shift-click measures from; moved by every click that is not one.
    public string? Anchor { get; private set; }
    /// The photo on the canvas: never deselected, and what a filter reseeds from.
    public string? Current { get; private set; }

    public int Count => _chosen.Count;
    public bool IsExplicit => _chosen.Count >= 2;
    public bool Contains(string path) => _chosen.Contains(path);

    /// The canvas moved, by any route: collapses onto it, so arrowing through a
    /// folder never gathers every frame passed over.
    public void Focus(string? path) {
        Current = Anchor = path;
        _chosen.Clear();
        if (path != null) _chosen.Add(path);
    }

    /// A click on a cell. Returns the photo the canvas should move to, or null
    /// when the click only changed the selection: a modified click never decodes.
    public string? Click(string path, Modifiers modifiers, IReadOnlyList<string> visible) {
        // Shift wins over command: the gesture is "extend", command decides replace or add.
        if (modifiers.HasFlag(Modifiers.Shift) && Anchor != null && IndexOf(visible, Anchor) is var a and >= 0
            && IndexOf(visible, path) is var b and >= 0) {
            var range = visible.Skip(Math.Min(a, b)).Take(Math.Abs(a - b) + 1);
            if (!modifiers.HasFlag(Modifiers.Command)) _chosen.Clear();
            _chosen.UnionWith(range);
            if (Current != null) _chosen.Add(Current);   // the anchor stays put: one range grows and shrinks
            return null;
        }
        if (modifiers.HasFlag(Modifiers.Command)) {
            if (_chosen.Contains(path) && path != Current) _chosen.Remove(path);
            else _chosen.Add(path);
            Anchor = path;
            return null;
        }
        Focus(path);
        return path;
    }

    public void SelectAll(IReadOnlyList<string> visible) {
        _chosen.Clear();
        _chosen.UnionWith(visible);
        if (Current != null) _chosen.Add(Current);
        Anchor ??= visible.FirstOrDefault();
    }

    /// Back to just the photo on the canvas.
    public void Collapse() => Focus(Current);

    /// Drops what the filter no longer shows, so a batch never acts on a photo nobody can see.
    public void Confine(IReadOnlyList<string> visible) {
        var live = visible.ToHashSet();
        _chosen.IntersectWith(live);
        if (Current != null && live.Contains(Current)) _chosen.Add(Current);
        if (Anchor != null && !live.Contains(Anchor)) Anchor = _chosen.Count == 0 ? null : visible.FirstOrDefault(_chosen.Contains);
    }

    /// What an operation touches, in view order: everything in view until the selection is real.
    public List<string> Targets(IReadOnlyList<string> visible) => IsExplicit ? visible.Where(_chosen.Contains).ToList() : visible.ToList();

    /// "12 selected", or empty for the resting state of one.
    public string Summary(IReadOnlyList<string> visible) => IsExplicit ? $"{Targets(visible).Count} selected" : "";

    public PhotoSelection Copy() {
        var c = new PhotoSelection { Anchor = Anchor, Current = Current };
        c._chosen.UnionWith(_chosen);
        return c;
    }

    static int IndexOf(IReadOnlyList<string> list, string path) {
        for (int i = 0; i < list.Count; i++) if (list[i] == path) return i;
        return -1;
    }
}

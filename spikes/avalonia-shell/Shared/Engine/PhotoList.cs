namespace OrionShell;

/// The photographs being browsed and which one is current. Pure state: opening
/// the photo is EngineModel's, deciding how is the session source's.
public sealed class PhotoList {
    public IReadOnlyList<string> Paths { get; private set; } = [];
    /// -1 when nothing is current: a new list, or before the first open.
    public int Current { get; private set; } = -1;

    public void Load(IEnumerable<string> paths) {
        Paths = paths.ToArray();
        Current = -1;
    }

    /// False, and nothing moves, for an index outside the list.
    public bool Go(int index) {
        if (index < 0 || index >= Paths.Count) return false;
        Current = index;
        return true;
    }

    /// Library.neighbor (Library.swift): clamps at both ends rather than
    /// wrapping, and starts at the first photo when none is current.
    public bool Next() => Go(Neighbor(1));
    public bool Previous() => Go(Neighbor(-1));

    int Neighbor(int offset) =>
        Current < 0 ? (Paths.Count > 0 ? 0 : -1)
                    : Current + offset is var i && i >= 0 && i < Paths.Count ? i : -1;
}

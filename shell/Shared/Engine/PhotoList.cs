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
}

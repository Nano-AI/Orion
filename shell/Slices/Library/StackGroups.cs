using OrionShell.Chrome;

namespace OrionShell.Library;

public sealed record Shot(string Path, string Camera, float Iso, float Shutter, float Aperture, float Focal, DateTime? Captured);
public sealed record StackGroup(IReadOnlyList<string> Paths, bool Astro);

/// Finding stacks in a folder from capture metadata alone (#391): contiguous runs, in capture
/// order, of identical settings at a steady cadence. The Library suggests them and the Stack
/// sheet falls back to them. Pure logic apart from `Of`, which reads each file's header.
public static class StackGroups {
    public const int MinFrames = 5;
    public const double CadenceTolerance = 2.0;   // seconds, on (gap − shutter); timestamps are whole seconds
    const double MinOverlap = -1;                 // a gap may undershoot the shutter by this much (rounding), no more
    const float AstroShutter = 5, AstroIso = 800; // research/UNSOURCED.md §22

    /// Contiguous runs (in capture order) of ≥ MinFrames with identical camera, ISO, shutter, aperture and focal
    /// length and a steady cadence. `dark` (optional) is asked once per candidate astro group, of its middle frame.
    /// ponytail: a run is judged against its first frame and its first cadence; a slowly drifting interval
    /// (an intervalometer ramp) splits into several groups. Compare to the previous gap if that proves too strict.
    public static List<StackGroup> Find(IReadOnlyList<Shot> shots, Func<string, bool>? dark = null) {
        // Unknown exposure (shutter or ISO unread) is no evidence of a stack: left out of every group.
        var timed = shots.Where(s => s.Captured != null && s.Shutter > 0 && s.Iso > 0).OrderBy(s => s.Captured).ThenBy(s => s.Path, StringComparer.Ordinal).ToList();
        var groups = new List<StackGroup>();
        var run = new List<Shot>();
        double c0 = 0;
        void Flush() {
            if (run.Count >= MinFrames) groups.Add(Group(run, dark));
            run = [];
        }
        foreach (var shot in timed) {
            if (run.Count > 0) {
                double c = (shot.Captured!.Value - run[^1].Captured!.Value).TotalSeconds - shot.Shutter;
                if (Same(run[0], shot) && c >= MinOverlap && (run.Count == 1 || Math.Abs(c - c0) <= CadenceTolerance)) {
                    if (run.Count == 1) c0 = c;
                } else Flush();
            }
            run.Add(shot);
        }
        Flush();
        return groups;
    }

    static StackGroup Group(List<Shot> run, Func<string, bool>? dark) {
        var paths = run.Select(s => s.Path).ToList();
        bool candidate = run[0].Shutter >= AstroShutter && run[0].Iso >= AstroIso;
        return new StackGroup(paths, candidate && (dark?.Invoke(paths[paths.Count / 2]) ?? true));
    }

    static bool Same(Shot a, Shot b) => a.Camera == b.Camera && Near(a.Iso, b.Iso) && Near(a.Shutter, b.Shutter)
        && Near(a.Aperture, b.Aperture) && Near(a.Focal, b.Focal);

    static bool Near(float a, float b) => Math.Abs(a - b) <= 0.001f * Math.Max(Math.Abs(a), Math.Abs(b));

    public static string Suggestion(StackGroup g) => $"{g.Paths.Count} frames look like a stack. Stack them?";

    // One scan shared by the Library and the Stack sheet. EngineModel.Photos is replaced, not mutated,
    // when a folder changes, so the list instance is the key.
    static readonly object Gate = new();
    static IReadOnlyList<string>? _key;
    static IReadOnlyList<StackGroup> _groups = [];

    /// The folder's groups, read from each file's header; unreadable files are skipped. `dark` as Find's;
    /// whichever scan of a list lands first is the one cached. Safe from a background task.
    public static IReadOnlyList<StackGroup> Of(IReadOnlyList<string> photos, Func<string, bool>? dark = null) {
        if (Cached(photos) is { } hit) return hit;
        var shots = new List<Shot>();
        foreach (var path in photos)
            if (CaptureInfo.ReadFile(path) is { } i)
                shots.Add(new Shot(path, i.Camera, i.Iso, i.Shutter, i.Aperture, i.FocalLength, i.Captured));
        var found = Find(shots, dark);
        lock (Gate) {
            if (ReferenceEquals(_key, photos)) return _groups;   // a concurrent scan finished first: one instance
            (_key, _groups) = (photos, found);
            return found;
        }
    }

    /// The cached groups when the cache is for this very list, else null. Cheap: for a command's CanRun.
    public static IReadOnlyList<StackGroup>? Cached(IReadOnlyList<string> photos) {
        lock (Gate) return ReferenceEquals(_key, photos) ? _groups : null;
    }
}

using System.Globalization;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using OrionShell.Develop;
using OrionShell.Masks;

namespace OrionShell.Sheets;

/// A port of app/Snapshots.swift: named versions of one photograph's whole
/// edit, kept until deleted. Not undo (fifty deep, gone at quit), not a preset
/// (a patch between photos that leaves out the crop and the masks).
///
/// They live beside the photo in `PHOTO.orion-snapshots.json`, not in the
/// sidecar (decision #99): Autosave rewrites the sidecar every settle, and one
/// bad merge must not take every version with it. The file is written when a
/// version is saved, renamed or deleted, and at no other time, in the shape
/// Swift's JSONEncoder writes: id, name, created (ISO 8601, whole seconds),
/// state (DevelopState), automatic. Either app reads the other's.
///
/// Three states, not two: absent reads as no versions; present and unreadable
/// shows nothing and writes nothing over it.
public sealed class SnapshotStore {
    public sealed record Snapshot(Guid Id, string Name, DateTimeOffset Created, JsonObject State, bool Automatic = false);

    /// A save, rename or delete refused, in words for the status line (SnapshotStore.Refusal).
    public sealed class Refusal(string message) : Exception(message);

    /// Refused, never evicted: dropping the oldest to make room deletes named work. The automatic slot does not count.
    public const int Limit = 100;

    /// The open photo's versions, following the canvas. Never between photos:
    /// showing the last photo's would invite restoring it onto this one.
    public static SnapshotStore Shared { get; } = Follow(new SnapshotStore());

    public string? Photo { get; private set; }
    /// Newest first, which puts the automatic version, always the latest written, on top.
    public IReadOnlyList<Snapshot> Snapshots { get; private set; } = [];
    public bool Unreadable { get; private set; }
    public event Action? Changed;

    static SnapshotStore Follow(SnapshotStore store) {
        // ORION_SCENE=versions poses the three rows Screenshot+Scenes.swift hands the Swift scene, off any disk.
        if (Environment.GetEnvironmentVariable("ORION_SCENE") == "versions") {
            var epoch = DateTimeOffset.FromUnixTimeSeconds(1_700_000_000);
            store.Snapshots = [
                new(Guid.NewGuid(), "Before restoring warmer", epoch, DevelopJson.Defaults(), Automatic: true),
                new(Guid.NewGuid(), "warmer", epoch.AddHours(-1), DevelopJson.Defaults()),
                new(Guid.NewGuid(), "with the sky darkened", epoch.AddDays(-1), Posed("Subject")),
            ];
            return store;
        }
        var e = EngineModel.Shared;
        void Opened() => store.Open(e.Current >= 0 ? e.Photos[e.Current] : null);
        e.Opened += Opened;
        if (e.Handle != 0) Opened();
        return store;
    }

    /// A state naming a raster mask no file backs: the posed "missing" row.
    static JsonObject Posed(string source) {
        var state = DevelopJson.Defaults();
        var mask = DevelopJson.DefaultMask();
        (mask["kind"], mask["matteId"], mask["matteSource"]) = (4, "posed-missing", source);
        state["maskComponents"] = new JsonArray(mask);
        return state;
    }

    // ── The file ────────────────────────────────────────────────────────────

    public static string PathFor(string photo) =>
        System.IO.Path.Combine(System.IO.Path.GetDirectoryName(photo) ?? "", System.IO.Path.GetFileNameWithoutExtension(photo) + ".orion-snapshots.json");

    /// Null means present but unreadable, which is not empty.
    public static List<Snapshot>? Read(string photo) {
        string text;
        try { text = File.ReadAllText(PathFor(photo)); }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { return []; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return null; }
        try {
            return ((JsonArray)JsonNode.Parse(text)!).Select(n => {
                var o = (JsonObject)n!;
                return new Snapshot(Guid.Parse((string)o["id"]!), (string)o["name"]!,
                                    DateTimeOffset.Parse((string)o["created"]!, CultureInfo.InvariantCulture),
                                    (JsonObject)o["state"]!.DeepClone(), (bool?)o["automatic"] ?? false);
            }).ToList();
        } catch (Exception e) when (e is JsonException or InvalidCastException or FormatException or InvalidOperationException
                                       or NullReferenceException or ArgumentException) {
            return null;
        }
    }

    public void Open(string? photo) {
        Photo = photo;
        var list = photo == null ? [] : Read(photo);
        (Snapshots, Unreadable) = list == null ? ([], true) : (list, false);
        Changed?.Invoke();
    }

    /// The text the panel shows in place of the list when the file will not read.
    public string? UnreadableText => Unreadable && Photo != null
        ? $"{System.IO.Path.GetFileName(PathFor(Photo))} could not be read, so nothing will be written over it. Move it aside to start again."
        : null;

    // ── Saving, renaming, deleting ──────────────────────────────────────────

    /// Replaces a version of the same name, as presets do; never the automatic one.
    public Snapshot Save(string name, JsonObject state) {
        string photo = Writable(), trimmed = Named(name);
        var kept = new Snapshot(Guid.NewGuid(), trimmed, Now(), Normal(state));
        var next = Snapshots.ToList();
        int at = next.FindIndex(s => s.Name == trimmed && !s.Automatic);
        if (at >= 0) next[at] = kept;
        else {
            if (next.Count(s => !s.Automatic) >= Limit)
                throw new Refusal($"This photo already has {Limit} versions. Delete one before saving another.");
            next.Insert(0, kept);
        }
        Commit(next, photo);
        return kept;
    }

    /// Renaming the automatic version keeps it: it becomes an ordinary one.
    public void Rename(Guid id, string name) {
        string photo = Writable(), trimmed = Named(name);
        var next = Snapshots.ToList();
        int at = next.FindIndex(s => s.Id == id);
        if (at < 0) throw new Refusal("That version is no longer in the list.");
        next[at] = next[at] with { Name = trimmed, Automatic = false };
        Commit(next, photo);
    }

    public void Remove(Guid id) {
        string photo = Writable();
        if (!Snapshots.Any(s => s.Id == id)) throw new Refusal("That version is no longer in the list.");
        Commit(Snapshots.Where(s => s.Id != id).ToList(), photo);
    }

    // ── Restoring ───────────────────────────────────────────────────────────

    /// Keeps the working edit first, then hands the version to `assign`. The
    /// order lives here, not at each caller: without the kept copy the only way
    /// back is ⌘Z, which does not survive a quit.
    public void Restore(Snapshot snapshot, JsonObject working, Action<Snapshot> assign) {
        KeepWorkingEdit(Normal(working), snapshot.Name);
        assign(snapshot);
    }

    /// One automatic slot, replaced each time; skipped when the working edit is
    /// already a named version. Best effort: a list that will not write is the next save's to report.
    void KeepWorkingEdit(JsonObject state, string name) {
        if (Photo is not { } photo || Unreadable) return;
        if (Snapshots.Any(s => !s.Automatic && JsonNode.DeepEquals(s.State, state))) return;
        var next = Snapshots.Where(s => !s.Automatic).ToList();
        next.Insert(0, new Snapshot(Guid.NewGuid(), $"Before restoring {name}", Now(), state, Automatic: true));
        try { Commit(next, photo); }
        catch (Refusal e) { Console.Error.WriteLine("orion: could not keep the working edit — " + e.Message); }
    }

    /// Engine.restore(snapshot:): the whole state in place of the working one,
    /// one render, and one history step, "Version NAME", so ⌘Z takes it back.
    public static void Assign(Snapshot snapshot) {
        var state = DevelopJson.Decode(DevelopJson.Encode(snapshot.State));
        if (state == null) { EngineModel.Shared.Say($"The version “{snapshot.Name}” could not be read."); return; }
        EditHistory.Label($"Version {snapshot.Name}");
        // The mask rows' names come with the state: the group drops the names it holds by position.
        EngineModel.Shared.Apply(s => { MaskGroup.Shared.Shapes.Clear(); s.Assign(state); }, reread: true);
    }

    /// The raster masks this version names whose matte file is no longer beside
    /// the photo, by the label the panel shows: said before it is pressed, not after.
    public static List<string> MissingMattes(Snapshot snapshot, string? photo) {
        if (snapshot.State["maskComponents"] is not JsonArray rows) return [];
        string dir = photo == null ? "" : System.IO.Path.GetDirectoryName(photo) ?? "", stem = photo == null ? "" : System.IO.Path.GetFileNameWithoutExtension(photo);
        return rows.OfType<JsonObject>()
            .Where(m => DevelopJson.Int(m["kind"]) == 4 && m["matteId"] is JsonValue id
                        && (photo == null || !File.Exists(System.IO.Path.Combine(dir, $"{stem}.orion-matte-{(string)id!}.png"))))
            .Select(m => (string?)m["matteSource"] ?? "a selection").ToList();
    }

    // ── Writing ─────────────────────────────────────────────────────────────

    string Writable() {
        if (Photo is not { } photo) throw new Refusal("Open a photo before saving a version of it.");
        if (Unreadable) throw new Refusal(UnreadableText!);
        return photo;
    }

    static string Named(string name) =>
        name.Trim() is { Length: > 0 } trimmed ? trimmed : throw new Refusal("Give the version a name.");

    /// Whole seconds: Swift's .iso8601 strategy refuses a fraction, which would make every file unreadable there.
    static DateTimeOffset Now() => DateTimeOffset.FromUnixTimeSeconds(DateTimeOffset.UtcNow.ToUnixTimeSeconds());

    /// The state as the sidecar's encoder writes it, so a version and a working edit compare by value.
    static JsonObject Normal(JsonObject state) => (JsonObject)JsonNode.Parse(DevelopJson.Encode(state))!;

    static readonly JsonSerializerOptions Options = new() { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    public static string Text(IEnumerable<Snapshot> list) => new JsonArray(list.Select(s => (JsonNode)new JsonObject {
        ["id"] = s.Id.ToString("D").ToUpperInvariant(),
        ["name"] = s.Name,
        ["created"] = s.Created.UtcDateTime.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", CultureInfo.InvariantCulture),
        ["state"] = s.State.DeepClone(),
        ["automatic"] = s.Automatic,
    }).ToArray()).ToJsonString(Options);

    /// Written first, published second: a list on the panel and not on disk is a version someone believes they have.
    void Commit(List<Snapshot> next, string photo) {
        string path = PathFor(photo), temp = path + ".orion-tmp";
        try {
            File.WriteAllText(temp, Text(next));
            File.Move(temp, path, overwrite: true);
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            try { File.Delete(temp); } catch (Exception) { /* the write already failed; that is the report */ }
            throw new Refusal($"The versions of {System.IO.Path.GetFileName(photo)} could not be saved ({e.Message}).");
        }
        Snapshots = next;
        Changed?.Invoke();
    }
}

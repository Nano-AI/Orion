using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using OrionShell.Chrome;
using OrionShell.Masks;
using OrionShell.Sheets;

namespace OrionShell.Agent;

/// `matte` (the agent's Subject, Person and Sky buttons) and `sync` (one
/// photo's settings onto a shoot): the Swift app's AgentMatte, AgentSync and
/// AgentSyncDriver.
static partial class AgentSync {
    // MARK: matte

    /// ⚠ A file and a row, and nothing else: the matte goes beside the photo
    /// through MatteStore.Write and the kind-4 row comes back as JSON for
    /// propose_edit's maskComponents; neither the sidecar nor a proposal is
    /// touched. "Found nothing" is an answer, not a failure.
    public static JsonObject Matte(string raw, string name, string? state) {
        var kind = KindOf(name);
        using var s = AgentCli.OpenEngine(raw, Sidecar.Read(raw)?.Develop, state);
        (float[] Alpha, int Width, int Height) m;
        try { m = SubjectMatte.Generate(s, kind); }
        catch (SubjectMatte.Failure f) { return new JsonObject { ["path"] = raw, ["found"] = false, ["kind"] = name, ["reason"] = f.Message }; }
        catch (InvalidOperationException e) { throw new AgentCli.Failure(e.Message); }
        string id = MatteStore.Write(m.Alpha, m.Width, m.Height, raw);
        return new JsonObject {
            ["path"] = raw, ["found"] = true, ["kind"] = name, ["matteId"] = id, ["matteSource"] = kind.ToString(),
            ["width"] = m.Width, ["height"] = m.Height, ["coverage"] = Coverage(m.Alpha), ["component"] = MatteRow(id, kind.ToString()),
        };
    }

    static SubjectMatte.Kind KindOf(string name) => name switch {
        "subject" => SubjectMatte.Kind.Subject, "person" => SubjectMatte.Kind.Person, "sky" => SubjectMatte.Kind.Sky,
        _ => throw new AgentCli.Usage("matte needs --kind " + string.Join("|", AgentCli.MatteKinds)),
    };

    /// The selected share: alpha above one half, the producer's own "nothing found" threshold.
    static double Coverage(float[] alpha) => alpha.Length == 0 ? 0 : (double)alpha.Count(a => a > 0.5f) / alpha.Length;

    /// A complete kind-4 row naming the matte, from the default row so it cannot drift; a layer of its own.
    static JsonObject MatteRow(string id, string label) {
        var row = DevelopJson.DefaultMask();
        (row["kind"], row["startsLayer"], row["matteId"], row["matteSource"]) = (4, true, id, label);
        return row;
    }

    // MARK: sync

    /// The six product groups are SyncSettings.Keys, not a copy of them, so a
    /// field added to a group reaches this verb the day it reaches the product's
    /// sync. `masks` is the one group the product has not: it carries its
    /// markers (maskSpace, localTintAxis) and the group's feather with it,
    /// because absent markers read as LEGACY on the target.
    static readonly string[] Groups = ["whiteBalance", "light", "color", "curve", "detail", "effects", "masks"];
    static readonly string[] Defaults = ["light", "color", "curve", "effects"];

    static string[] KeysOf(string group) => group == "masks"
        ? ["maskComponents", "layers", "maskRefine", "maskSpace", "localTintAxis"]
        : SyncSettings.Keys(System.Array.IndexOf(Groups, group));

    public static JsonObject Run(string source, Dictionary<string, string> o) {
        string targetsFile = o.GetValueOrDefault("targets")
            ?? throw new AgentCli.Usage("sync needs --targets <json-file> holding an array of absolute raw paths");
        var groups = o.TryGetValue("groups", out var g) ? ParseGroups(g) : Defaults.ToList();
        string mode = o.GetValueOrDefault("mode") ?? "propose";
        if (mode is not ("propose" or "commit")) throw new AgentCli.Usage($"sync --mode is propose or commit, not '{mode}'");
        var paths = ParseTargets(File.Exists(targetsFile) ? File.ReadAllBytes(targetsFile) : null);

        // Normalised before a key is copied, so a legacy source's conversions happen once, to the source.
        var (data, sourceState) = AgentCli.BaseState(source, o.GetValueOrDefault("state"));
        JsonObject sourceObject;
        try { sourceObject = AgentCli.Normal(data); }
        catch (Exception e) { throw new AgentCli.Failure($"the source's develop state could not be read: {e.Message}"); }

        var keys = groups.SelectMany(KeysOf).Distinct().ToList();
        // ⚠ Resolved once, before any target: a row nothing can re-detect fails every target alike.
        var mattes = new List<(int Row, SubjectMatte.Kind Kind)>();
        string? unmatchable = null;
        if (groups.Contains("masks") && sourceObject["maskComponents"] is JsonArray rows)
            for (int i = 0; i < rows.Count; i++) {
                if (DevelopJson.Int(rows[i]?["kind"]) != 4) continue;
                string? label = rows[i]?["matteSource"]?.GetValue<string>();
                if (label?.ToLowerInvariant() is { } lower && AgentCli.MatteKinds.Contains(lower)) mattes.Add((i, KindOf(lower)));
                else unmatchable ??= $"mask row {i} is a matte with matteSource '{label ?? "none"}', and only Subject, Person or Sky can be "
                                     + "re-detected on another photograph";
            }

        var results = new JsonArray();
        int written = 0, failed = 0, skipped = 0;
        foreach (var path in paths) {
            if (Canonical(path) == Canonical(source)) {
                results.Add(new JsonObject { ["path"] = path, ["skipped"] = "source" });
                skipped++;
                continue;
            }
            try {
                if (unmatchable != null) throw new AgentCli.Failure(unmatchable);
                if (!File.Exists(path)) throw new AgentCli.Failure($"no such file: {path}");
                var target = (JsonObject)sourceObject.DeepClone();
                var found = new JsonArray();
                foreach (var m in Redetect(mattes, path)) {
                    var row = target["maskComponents"]![m.Row]!;
                    (row["matteId"], row["matteSource"]) = (m.Id, m.Label);
                    found.Add(new JsonObject { ["row"] = m.Row, ["matteId"] = m.Id, ["matteSource"] = m.Label, ["coverage"] = m.Coverage });
                }
                string file = Write(target, keys, path, mode);
                results.Add(new JsonObject { ["path"] = path, ["wrote"] = mode == "propose" ? "proposed" : "sidecar", ["file"] = file, ["mattes"] = found });
                written++;
            } catch (Exception e) {
                // Per target: one card that will not take a write must not cost the other two hundred.
                results.Add(new JsonObject { ["path"] = path, ["error"] = e.Message });
                failed++;
            }
        }
        return new JsonObject {
            ["source"] = source, ["sourceState"] = sourceState, ["mode"] = mode,
            ["groups"] = new JsonArray(groups.Select(x => (JsonNode)x).ToArray()), ["keys"] = new JsonArray(keys.Select(x => (JsonNode)x).ToArray()),
            ["results"] = results, ["written"] = written, ["failed"] = failed, ["skipped"] = skipped,
        };
    }

    /// `--groups light,color`: order kept, repeats dropped, an unknown name refused with the vocabulary.
    static List<string> ParseGroups(string text) {
        var out_ = new List<string>();
        foreach (var part in text.Split(',')) {
            string name = part.Trim();
            if (!Groups.Contains(name)) throw new AgentCli.Usage($"sync --groups has no group '{name}'; groups are {string.Join(", ", Groups)}");
            if (!out_.Contains(name)) out_.Add(name);
        }
        return out_;
    }

    /// A JSON array of absolute paths, in a file: a shoot is three hundred of them.
    static List<string> ParseTargets(byte[]? data) {
        JsonArray? list = null;
        try { if (data != null) list = JsonNode.Parse(data) as JsonArray; } catch (JsonException) { }
        if (list == null) throw new AgentCli.Usage("sync --targets must be a JSON array of raw paths");
        var paths = list.Select(n => n is JsonValue v && v.GetValueKind() == JsonValueKind.String ? v.GetValue<string>() : null).ToList();
        if (paths.Count == 0 || paths.Any(p => p == null))
            throw new AgentCli.Usage("sync --targets must be a non-empty JSON array of raw paths (strings)");
        if (paths.FirstOrDefault(p => !p!.StartsWith('/')) is { } relative) throw new AgentCli.Usage($"sync --targets paths must be absolute: '{relative}'");
        return paths!;
    }

    /// Each target's mattes, made on the TARGET's own picture and saved beside it: a matte is the source's pixels and is never copied.
    static List<(int Row, string Id, string Label, double Coverage)> Redetect(List<(int Row, SubjectMatte.Kind Kind)> rows, string target) {
        var out_ = new List<(int, string, string, double)>();
        if (rows.Count == 0) return out_;
        using var s = AgentCli.OpenEngine(target, Sidecar.Read(target)?.Develop, null);
        var cache = new Dictionary<SubjectMatte.Kind, (float[] Alpha, int Width, int Height)>();
        foreach (var (row, kind) in rows) {
            if (!cache.TryGetValue(kind, out var m)) {
                try { m = SubjectMatte.Generate(s, kind); }
                catch (Exception e) when (e is SubjectMatte.Failure or InvalidOperationException) {
                    throw new AgentCli.Failure($"mask row {row} ({kind}): {e.Message}");
                }
                cache[kind] = m;
            }
            out_.Add((row, MatteStore.Write(m.Alpha, m.Width, m.Height, target), kind.ToString(), Coverage(m.Alpha)));
        }
        return out_;
    }

    /// `propose` is exactly `apply` on the target's own base (never 5500 K
    /// nobody chose). `commit` patches the sidecar at the key level, the
    /// product's own sync, so a key the target never had stays absent.
    static string Write(JsonObject source, List<string> keys, string target, string mode) {
        if (mode == "propose") {
            var patch = new JsonObject();
            foreach (var key in keys) if (source[key] is { } v) patch[key] = v.DeepClone();
            var (merged, _) = AgentComposite.MergeEdits(AgentCli.BaseState(target, null).Data, JsonSerializer.SerializeToUtf8Bytes(patch), target);
            string file = Proposal.ProposedPath(target);
            AgentCli.WriteAtomically(file, DevelopJson.Encode(merged));
            return file;
        }
        var data = SyncSettings.Patched(Sidecar.Read(target)?.Develop, source, keys)
                   ?? throw new AgentCli.Failure($"could not build the sidecar patch for {target}");
        if (!Sidecar.Merge(target, s => s.Develop = data)) throw new AgentCli.Failure($"could not write the sidecar for {target}");
        return Sidecar.PathFor(target);
    }

    /// The same photo however it is spelled: symlinks and /var against /private/var resolved.
    static string Canonical(string path) {
        var buffer = new byte[4096];
        return realpath(Path.GetFullPath(path), buffer) != 0
            ? System.Text.Encoding.UTF8.GetString(buffer, 0, System.Array.IndexOf(buffer, (byte)0)) : Path.GetFullPath(path);
    }

    [LibraryImport("libc", StringMarshalling = StringMarshalling.Utf8)]
    private static partial nint realpath(string path, [Out] byte[] resolved);
}

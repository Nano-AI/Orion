using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using OrionShell.Chrome;
using OrionShell.Sheets;

namespace OrionShell.Agent;

/// `Orion --agent <verb> <raw> …`: the surface an MCP client drives
/// (mcp/server.ts shells out to it, one process a call). A port of the Swift
/// app's AgentCLI and AgentCLIDriver: the verbs, their options, the field
/// names and the exit codes are the contract the server is built against, so
/// none is renamed casually. UTF-8 JSON, one object, on stdout. Exit 2 is a
/// usage error, 1 a failure, 0 the only success. The verb contract is
/// docs/superpowers/specs/2026-09-13-agent-mcp-design.md.
///
/// Every engine here is headless (Session.Open's `headless`, decision #312):
/// no preview graph and no intermediates kept, so one call on a 42 MP frame
/// peaks at one render's worth of textures.
static partial class AgentCli {
    public sealed class Usage(string message) : Exception(message);
    public sealed class UnknownKeys(List<string> keys) : Exception("unknown keys " + string.Join(", ", keys)) {
        public List<string> Keys { get; } = keys;
    }
    public sealed class Failure(string message) : Exception(message);

    /// A rectangle of the DISPLAYED picture, fractions from its top-left: what
    /// `--region x,y,w,h` parses to. ⚠ Not the frame space a mask lives in; a
    /// crop or a turn separates the two, which is why `stats --region` also
    /// answers with the rectangle carried into frame space.
    public sealed record Region(double X, double Y, double W, double H);

    const string UsageLine = "usage: Orion --agent <stats|proxy|apply|commit|flag|faces|state|matte|sync> <raw> [options]"
                             + "  |  Orion --agent keys";

    /// Every verb's own options: one outside it is very likely a typo (`--sate`) that would otherwise be ignored.
    static readonly Dictionary<string, string[]> Allowed = new() {
        ["stats"] = ["state", "region"], ["proxy"] = ["max", "out", "state", "region"], ["apply"] = ["edits", "out", "state"],
        ["commit"] = ["state"], ["flag"] = ["rating", "reject"], ["faces"] = ["state"], ["state"] = ["state"],
        ["matte"] = ["kind", "state"], ["sync"] = ["targets", "groups", "state", "mode"],
    };

    /// `matte --kind`: SubjectMatte.Kind by its lower-cased name.
    public static readonly string[] MatteKinds = ["subject", "person", "sky"];

    static readonly JsonSerializerOptions Wire = new() { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    public static int Execute(string[] args) {
        try {
            Console.Out.Write(Dispatch(args).ToJsonString(Wire) + "\n");
            Console.Out.Flush();
            return 0;
        } catch (Usage e) {
            Console.Error.WriteLine("orion: " + e.Message);
            return 2;
        } catch (UnknownKeys e) {
            Console.Error.WriteLine($"orion: unknown keys {string.Join(", ", e.Keys)}; allowed: {string.Join(", ", AgentKeys.Editable)}");
            return 2;
        } catch (Exception e) {
            Console.Error.WriteLine("orion: " + e.Message);
            return 1;
        }
    }

    internal static JsonObject Dispatch(string[] args) {
        int i = Array.IndexOf(args, "--agent");
        if (i < 0 || i + 1 >= args.Length) throw new Usage(UsageLine);
        string verb = args[i + 1];
        if (verb == "keys") {
            if (i + 2 != args.Length) throw new Usage("keys takes no arguments");
            return new JsonObject { ["keys"] = AgentKeys.Json() };
        }
        if (i + 2 >= args.Length) throw new Usage(UsageLine);
        string raw = args[i + 2];
        var o = new Dictionary<string, string>();
        for (int j = i + 3; j < args.Length; j += 2) {
            if (!args[j].StartsWith("--") || j + 1 >= args.Length) throw new Usage($"bad option near '{args[j]}'");
            o[args[j][2..]] = args[j + 1];
        }
        if (Allowed.TryGetValue(verb, out var allowed) && o.Keys.FirstOrDefault(k => !allowed.Contains(k)) is { } bad)
            throw new Usage($"{verb} does not take --{bad}");
        var region = o.TryGetValue("region", out var r) ? ParseRegion(r) : null;
        string? state = o.GetValueOrDefault("state");
        string Need(string key, string why) => o.GetValueOrDefault(key) ?? throw new Usage(why);
        return verb switch {
            "stats" => Stats(raw, state, region),
            "proxy" => Proxy(raw, uint.TryParse(o.GetValueOrDefault("max"), out uint max) ? max : throw new Usage("proxy needs --max <px>"),
                             Need("out", "proxy needs --out <jpg>"), state, region),
            "faces" => AgentInspect.Faces(raw, state),
            "state" => StateOf(raw, state),
            "matte" => AgentSync.Matte(raw, o.TryGetValue("kind", out var k) && MatteKinds.Contains(k) ? k
                                            : throw new Usage("matte needs --kind " + string.Join("|", MatteKinds)), state),
            "sync" => AgentSync.Run(raw, o),
            "apply" => Apply(raw, Need("edits", "apply needs --edits <json-file>"), Need("out", "apply needs --out <json-file>"), state),
            "commit" => Commit(raw, Need("state", "commit needs --state <json-file>")),
            "flag" => Flag(raw, int.TryParse(o.GetValueOrDefault("rating"), out int rating) ? rating : null,
                           int.TryParse(o.GetValueOrDefault("reject"), out int reject) ? reject != 0 : null),
            _ => throw new Usage($"unknown verb '{verb}'"),
        };
    }

    /// `--region x,y,w,h`. A rectangle outside 0…1 or with no area is refused
    /// (exit 2), never clamped: a clamped answer reads as the region it named.
    public static Region ParseRegion(string text) {
        var parts = text.Split(',').Select(p => double.TryParse(p.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double d)
            ? d : (double?)null).ToArray();
        if (parts.Length != 4 || parts.Any(p => p == null))
            throw new Usage("--region takes x,y,w,h as four numbers, fractions of the displayed picture from its top-left corner");
        double x = parts[0]!.Value, y = parts[1]!.Value, w = parts[2]!.Value, h = parts[3]!.Value;
        if (!(w > 0 && h > 0)) throw new Usage($"--region {text} has no area - w and h must be above 0");
        if (!(x >= 0 && y >= 0 && x + w <= 1.000001 && y + h <= 1.000001))
            throw new Usage($"--region {text} is outside 0…1 - x, y, x+w and y+h must all land inside the picture");
        return new Region(x, y, Math.Min(w, 1 - x), Math.Min(h, 1 - y));
    }

    // MARK: the verbs

    static JsonObject Stats(string raw, string? state, Region? region) {
        var info = CaptureInfo.ReadFile(raw) ?? throw new Failure($"could not read {raw}");
        var sidecar = Sidecar.Read(raw);
        using var s = OpenEngine(raw, sidecar?.Develop, state);
        const int Bins = 128;
        var bins = new uint[Bins * 3];
        if (orion_engine_histogram(s.Handle, bins, Bins) != 0) throw new Failure("no histogram — is a photo actually open");
        var (low, high, mean) = Histogram(bins, Bins);
        var o = new JsonObject {
            ["path"] = raw, ["width"] = (int)info.Width, ["height"] = (int)info.Height, ["camera"] = info.Camera,
            ["rating"] = sidecar?.Rating ?? 0, ["rejected"] = sidecar?.Rejected ?? false,
            ["clipLow"] = Numbers(low), ["clipHigh"] = Numbers(high), ["mean"] = Numbers(mean),
            // After the restore, so a photo with no sidecar answers with the camera's as-shot reading, not 5500 K.
            ["temperatureK"] = s.Get("temperature_k"), ["tint"] = s.Get("tint"),
        };
        // The block current.json carries, through the same facade call, so the two cannot disagree.
        if (CaptureInfo.Read(s.Handle) is { } capture) o["capture"] = CurrentPhoto.Wire(capture);
        if (region != null) o["region"] = AgentInspect.RegionJson(s, region);
        return o;
    }

    static JsonObject Proxy(string raw, uint max, string path, string? state, Region? region) {
        using var s = OpenEngine(raw, Sidecar.Read(raw)?.Develop, state);
        // A region renders at full size and is cropped, so --max caps the crop rather than the frame.
        if (region != null) return AgentInspect.ProxyRegion(s, region, max, path);
        var options = new SheetsNative.ExportOptions {
            Format = -1, Quality = 0.85f, MaxDimension = max, Space = 0, Rating = -1, Metadata = 1, BitDepth = 8, Sharpen = 0,
        };
        if (orion_engine_export(s.Handle, path, options) != 0) throw new Failure($"could not export {path}: {s.LastError}");
        using var codec = SkiaSharp.SKCodec.Create(path) ?? throw new Failure($"wrote {path} but could not read it back");
        return new JsonObject { ["path"] = path, ["width"] = codec.Info.Width, ["height"] = codec.Info.Height,
                                ["bytes"] = new FileInfo(path).Length };
    }

    static JsonObject Apply(string raw, string edits, string path, string? state) {
        var editsData = File.ReadAllBytes(edits);
        var (merged, changed) = AgentComposite.MergeEdits(BaseState(raw, state).Data, editsData, raw);
        WriteAtomically(path, DevelopJson.Encode(merged));
        // The FULL state the file holds, so a caller can read-modify-write a composite without re-reading it.
        return new JsonObject { ["path"] = path, ["changed"] = new JsonArray(changed.Select(c => (JsonNode)c).ToArray()),
                                ["state"] = merged.DeepClone() };
    }

    /// The state `apply` would merge onto, returned and nothing written: before
    /// it, reading the settings meant an empty propose_edit, which wrote a proposal.
    static JsonObject StateOf(string raw, string? state) {
        var (data, source) = BaseState(raw, state);
        return new JsonObject { ["path"] = raw, ["source"] = source, ["state"] = Normal(data) };
    }

    internal static JsonObject Commit(string raw, string state) {
        var decoded = Normal(File.ReadAllBytes(state));
        if (!Autosave.ToSidecar(raw, decoded)) throw new Failure($"could not write the sidecar for {raw}");
        return new JsonObject { ["sidecar"] = Sidecar.PathFor(raw) };
    }

    /// The spec's rule (Part A): a rating above zero clears reject, and reject
    /// zeroes the rating. ⚠ Not the library's rule, which clears reject on every
    /// rating including 0: `flag --rating 0` must not un-reject a photo.
    static JsonObject Flag(string raw, int? rating, bool? reject) {
        if (rating == null && reject == null) throw new Usage("flag needs --rating or --reject");
        bool wrote = Sidecar.Merge(raw, s => {
            if (rating is { } r) { s.Rating = Math.Clamp(r, 0, 5); if (r > 0) s.Rejected = false; }
            if (reject is { } x) { s.Rejected = x; if (x) s.Rating = 0; }
        });
        if (!wrote) throw new Failure($"could not write the sidecar for {raw}");
        var after = Sidecar.Read(raw);
        return new JsonObject { ["rating"] = after?.Rating ?? 0, ["rejected"] = after?.Rejected ?? false };
    }

    // MARK: shared

    /// The state an edit starts from and where it came from: `--state`
    /// ("proposed"), else the sidecar's ("sidecar"), else as shot ("as-shot").
    /// `apply`, `state` and `sync` all go through this one function, so they
    /// cannot disagree about what the current settings are. Only the as-shot
    /// branch opens the photo, and it never renders.
    public static (byte[] Data, string Source) BaseState(string raw, string? state) {
        if (state != null) return (File.ReadAllBytes(state), "proposed");
        if (Sidecar.Read(raw)?.Develop is { } saved) return (saved, "sidecar");
        using var s = Session.Open(raw, headless: true) ?? throw new Failure($"could not open {raw}");
        return (DevelopJson.Encode(s.Capture()), "as-shot");
    }

    /// Opens the photo, restores exactly one state (`--state`, else the
    /// sidecar's, else none) and renders once. ⚠ A state that will not decode
    /// is refused by name, never rendered as whatever the engine last held,
    /// and so is a mask row whose matte file will not read: rendered, it
    /// would be a mask selecting nothing, with no error anywhere.
    public static Session OpenEngine(string raw, byte[]? sidecarDevelop, string? state) {
        (byte[] Data, string File)? blob = state != null ? (File.ReadAllBytes(state), state)
            : sidecarDevelop != null ? (sidecarDevelop, Sidecar.PathFor(raw)) : null;
        var s = Session.Open(raw, headless: true) ?? throw new Failure($"could not open {raw}");
        try {
            if (blob is { } b) {
                try { s.Restore(b.Data); }
                catch (Exception e) { throw new Failure($"the saved edits could not be read: {b.File} ({e.Message})"); }
                var rows = Session.Rows(s.Document);
                for (int i = 0; i < rows.Count; i++)
                    if (rows[i].Kind == 4 && MatteStore.IsMissing(s, rows[i].MatteId))
                        throw new Failure($"mask row {i} names a matte that could not be read: {MatteStore.PathFor(raw, rows[i].MatteId!)}");
            }
            if (s.Render(preview: false) != Graph.Full) throw new Failure($"could not render {raw}: {s.LastError}");
            return s;
        } catch {
            s.Dispose();
            throw;
        }
    }

    /// A develop blob in the current convention, as Swift's decode and
    /// re-encode leaves it: every legacy conversion done, every current key
    /// present, and a key no DevelopState field names dropped.
    public static JsonObject Normal(byte[] data) {
        var state = DevelopJson.Decode(data);
        if (state == null || JsonNode.Parse(data) is not JsonObject) throw new Failure("the state is not a JSON object");
        foreach (var key in state.Select(p => p.Key).Where(k => !DevelopJson.Known.Contains(k)).ToList()) state.Remove(key);
        return state;
    }

    /// Per-channel clip shares and mean of a channel-major histogram, each 0…1.
    internal static (double[] Low, double[] High, double[] Mean) Histogram(uint[] h, int bins) {
        double[] low = new double[3], high = new double[3], mean = new double[3];
        for (int c = 0; c < 3; c++) {
            var slice = h.AsSpan(c * bins, bins);
            double total = 0, weighted = 0;
            for (int i = 0; i < bins; i++) { total += slice[i]; weighted += (double)i * slice[i]; }
            if (total <= 0) continue;
            (low[c], high[c], mean[c]) = (slice[0] / total, slice[bins - 1] / total, weighted / total / (bins - 1));
        }
        return (low, high, mean);
    }

    /// A temporary file beside it, then a rename: a reader sees the old file or the new one, never half.
    public static void WriteAtomically(string path, byte[] bytes) {
        string temp = path + ".orion-tmp";
        File.WriteAllBytes(temp, bytes);
        File.Move(temp, path, overwrite: true);
    }

    public static JsonArray Numbers(IEnumerable<double> values) => new(values.Select(v => (JsonNode)v).ToArray());

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_histogram(nint engine, [Out] uint[] bins, uint count);

    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    private static partial int orion_engine_export(nint engine, string path, in SheetsNative.ExportOptions options);
}

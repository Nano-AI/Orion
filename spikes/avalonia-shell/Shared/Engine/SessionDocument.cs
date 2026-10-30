using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace OrionShell;

/// The photo's edit as its sidecar holds it, both ways: Engine+Document.swift's
/// `restore(encoded:)` (Restore, on open) and `Engine.state` (Capture, for
/// Autosave). DevelopJson keys map onto OrionAdjustments by name, camelCase to
/// snake_case (exposureEv is exposure_ev); the selftest holds every one to a
/// real field.
public sealed partial class Session {
    /// The edit as of the open, kept current by every capture Autosave takes.
    /// What the shell does not model rides here untouched: the spots (no
    /// OrionSpot crosses the shim yet), a mask's name and matte id (#207).
    public JsonObject? Document { get; private set; }

    /// Why the sidecar's edit could not be read, or null. Nothing may write to
    /// a photo's sidecar while this is set (decision #87): its baseline would
    /// be the defaults, and the first slider tick would overwrite the edit.
    public string? Unreadable { get; private set; }

    /// Session.Open, then the sidecar's edit: what the canvas, the cache and an
    /// export of a photo not on screen want. Compare's as-shot original wants Open.
    public static Session? OpenEdited(string path) {
        var s = Open(path);
        s?.Restore();
        return s;
    }

    void Restore() {
        var sidecar = Sidecar.Read(Path);
        float asShotK = Get("temperature_k"), asShotTint = Get("tint");   // the shim opened at the camera's
        try {
            if (sidecar?.Unreadable is { } why) throw new FormatException(why);
            if (sidecar?.Develop is not { } bytes) { Document = Capture(); return; }
            var state = DevelopJson.Decode(bytes) ?? throw new FormatException("orion:Develop is not JSON");
            // A sidecar Sync wrote onto an untouched photo carries no white balance: the camera's stands.
            var stored = JsonNode.Parse(bytes) as JsonObject;
            foreach (var (key, field) in new[] { ("temperatureK", "temperature_k"), ("tint", "tint") })
                if (stored != null && stored[key] is null) state[key] = Get(field);
            ToFrameSpace(state);
            Apply(state);
            MatteStore.Upload(this, Rows(state));   // restoreMattes: a file that will not read is recorded, not swallowed
            Document = state;          // the carry for the capture: names, matte ids, spots
            Document = Capture();
        } catch (Exception e) {
            // Anything at all: this runs on the cache's prefetch task, and a sidecar must never fault it.
            // A half-applied edit is put back to the as-shot look, so what shows is a photo and not a guess.
            Unreadable = $"Orion could not read the saved edits for {System.IO.Path.GetFileName(Path)} ({e.Message}). " +
                         "They have been left on disk untouched; editing now will not save.";
            Console.Error.WriteLine($"orion: restore failed ({e.GetType().Name}) — " + Unreadable);
            Document = null;
            var asShot = DevelopJson.Defaults();
            (asShot["temperatureK"], asShot["tint"]) = (asShotK, asShotTint);
            try { Apply(asShot); } catch (Exception reset) { Console.Error.WriteLine("orion: the as-shot reset failed too — " + reset.Message); }
        }
    }

    /// Every field of `state` into this session's adjustments. Does not render.
    public void Apply(JsonObject state) {
        foreach (var (key, value) in state) {
            if (!DevelopJson.Known.Contains(key)) continue;   // a newer build's key: carried, not applied
            if (Scalar(key, value)) Set(Snake(key), DevelopJson.Float(value)!.Value);
            else if (DevelopJson.Floats(value) is { } band && NumberArrays.Contains(key))
                for (int i = 0; i < band.Count; i++) Set(Snake(key), band[i], i);
        }
        var layers = ((JsonArray)state["layers"]!).Select(l => (JsonObject)l!).ToList();
        for (int i = 0; i < MaxMaskComponents; i++)
            foreach (var (key, value) in i < layers.Count ? layers[i] : DevelopJson.DefaultLayer())
                Set("local_" + Snake(key), DevelopJson.Float(value)!.Value, i);
        for (int c = 0; c < 4; c++)
            SetCurve(c, ((JsonArray)state["curve"]![Channels[c]]!)
                .Select(p => (DevelopJson.Float(p!["x"])!.Value, DevelopJson.Float(p["y"])!.Value)).ToList());
        SetMaskGroup(((JsonArray)state["maskComponents"]!).Take(MaxMaskComponents).Select(m => Shape((JsonObject)m!)).ToList());
        // A name the lens database no longer has leaves the engine's own; Capture reads back what it settled on.
        Native.orion_engine_set_lens_choice(Handle, state["lensChoice"]?.GetValue<string>() ?? "");
    }

    /// The edit as DevelopState encodes it. `named`: the mask group's rows,
    /// for the names and matte ids the engine never holds; a row it lacks
    /// takes them from Document, by position, as MaskGroup.Read does.
    public JsonObject Capture(IReadOnlyList<MaskShape>? named = null) {
        var carry = Document ?? DevelopJson.Defaults();
        var s = DevelopJson.Defaults();
        foreach (var (key, value) in s.ToList()) {
            if (key is "maskSpace" or "localTintAxis") continue;   // the live state is always frame-space, current axis
            if (Scalar(key, value)) s[key] = DevelopJson.IntKeys.Contains(key) ? (int)Get(Snake(key)) : Get(Snake(key));
            else if (NumberArrays.Contains(key))
                s[key] = new JsonArray(Enumerable.Range(0, ((JsonArray)value!).Count).Select(i => (JsonNode)Get(Snake(key), i)).ToArray());
        }
        s["lensChoice"] = Marshal.PtrToStringUTF8(Native.orion_engine_lens_choice(Handle)) ?? "";

        var rows = ReadMaskGroup();
        var saved = (JsonArray)carry["maskComponents"]!;
        var masks = new JsonArray();
        for (int i = 0; i < rows.Count; i++) {
            var m = DevelopJson.DefaultMask();
            foreach (var (name, get, _) in MaskFields) {
                string key = Camel(name);
                m[key] = m[key]!.GetValueKind() is System.Text.Json.JsonValueKind.True or System.Text.Json.JsonValueKind.False ? get(rows[i]) != 0
                       : key is "kind" or "compose" ? (int)get(rows[i]) : get(rows[i]);
            }
            m["brushStroke"] = new JsonArray(rows[i].BrushStroke.Select(v => (JsonNode)v).ToArray());
            m["brushErase"] = new JsonArray(rows[i].BrushErase.Select(v => (JsonNode)v).ToArray());
            var from = named != null && i < named.Count ? named[i] : i < saved.Count ? Shape((JsonObject)saved[i]!) : null;
            if (from?.MatteId is { } id) m["matteId"] = id;
            if (from?.MatteSource is { } source) m["matteSource"] = source;
            if (from?.Name is { } title) m["name"] = title;
            masks.Add(m);
        }
        s["maskComponents"] = masks;

        // A layer per run of rows (row 0 always starts one), and never fewer than the sidecar had:
        // Swift's `layers` only grows, and a layer past the eighth lives in the sidecar alone.
        var kept = (JsonArray)carry["layers"]!;
        int runs = rows.Count == 0 ? 0 : 1 + rows.Skip(1).Count(r => r.StartsLayer);
        var layers = new JsonArray();
        for (int i = 0; i < Math.Max(1, Math.Max(runs, kept.Count)); i++) {
            if (i >= MaxMaskComponents) { layers.Add(kept[i]!.DeepClone()); continue; }
            var l = DevelopJson.DefaultLayer();
            foreach (var (key, _) in l.ToList()) l[key] = Get("local_" + Snake(key), i);
            layers.Add(l);
        }
        s["layers"] = layers;
        // Engine.assign keeps ORION_MAX_SPOTS, as it keeps eight mask rows (ReadMaskGroup's cap).
        s["spots"] = new JsonArray(((JsonArray)carry["spots"]!).Take(MaxSpots).Select(p => p!.DeepClone()).ToArray());
        // A newer build's keys ride through: Swift's own encoder would drop them, this does not.
        foreach (var (key, value) in carry) if (!DevelopJson.Known.Contains(key)) s[key] = value?.DeepClone();

        var curve = new JsonObject();
        for (int c = 0; c < 4; c++)
            curve[Channels[c]] = new JsonArray(Curve(c).Select(p => (JsonNode)new JsonObject { ["x"] = p.X, ["y"] = p.Y }).ToArray());
        s["curve"] = curve;
        Document = s;
        return s;
    }

    const int MaxSpots = 64;   // ORION_MAX_SPOTS
    static readonly string[] Channels = ["master", "red", "green", "blue"];
    static readonly HashSet<string> NumberArrays = ["gradeShadow", "gradeMidtone", "gradeHighlight", "hueShift", "satShift", "lumShift"];

    /// A number DevelopState holds that is an OrionAdjustments field of the same name.
    static bool Scalar(string key, JsonNode? value) =>
        value?.GetValueKind() == System.Text.Json.JsonValueKind.Number && key is not ("maskSpace" or "localTintAxis");

    public static string Snake(string key) => Regex.Replace(key, "([A-Z])", "_$1").ToLowerInvariant();
    static string Camel(string name) => Regex.Replace(name, "_([a-z])", m => m.Groups[1].Value.ToUpperInvariant());

    static MaskShape Shape(JsonObject o) {
        var m = new MaskShape();
        foreach (var (name, _, put) in MaskFields) {
            var v = o[Camel(name)];
            put(m, v?.GetValueKind() switch {
                System.Text.Json.JsonValueKind.True => 1, System.Text.Json.JsonValueKind.False => 0, _ => DevelopJson.Float(v) ?? 0 });
        }
        m.BrushStroke.AddRange(DevelopJson.Floats(o["brushStroke"]) ?? []);
        m.BrushErase.AddRange(DevelopJson.Floats(o["brushErase"]) ?? []);
        (m.MatteId, m.MatteSource, m.Name) = (Text(o["matteId"]), Text(o["matteSource"]), Text(o["name"]));
        return m;
    }

    static string? Text(JsonNode? n) => n?.GetValueKind() == System.Text.Json.JsonValueKind.String ? n.GetValue<string>() : null;

    /// The mask names and matte ids `state` carries, by row: what MaskGroup puts back on the engine's rows.
    public static List<MaskShape> Rows(JsonObject? state) =>
        state?["maskComponents"] is JsonArray a ? a.Select(m => Shape((JsonObject)m!)).ToList() : [];

    /// Engine+MaskMigration.swift: a sidecar from before frame anchoring holds
    /// display-space mask numbers beside the geometry they were relative to,
    /// and converts on the way in (#203). One refusal keeps every row as written.
    internal unsafe void ToFrameSpace(JsonObject state) {
        if (DevelopJson.Int(state["maskSpace"]) != 0) return;
        float F(string k) => DevelopJson.Float(state[k])!.Value;
        var g = new Native.MaskLegacyGeometry {
            CropX = F("cropX"), CropY = F("cropY"), CropW = F("cropW"), CropH = F("cropH"),
            RotateQuarters = DevelopJson.Int(state["rotateQuarters"])!.Value, StraightenDeg = F("straightenDeg"),
            PerspectiveVertical = F("perspectiveVertical"), PerspectiveHorizontal = F("perspectiveHorizontal"),
            PerspectiveAspect = F("perspectiveAspect"),
        };
        var rows = (JsonArray)state["maskComponents"]!;
        var converted = new List<(Native.MaskPlace Shape, float[] Dabs)>();
        foreach (JsonObject m in rows.Cast<JsonObject>()) {
            float G(string k) => DevelopJson.Float(m[k])!.Value;
            var shape = new Native.MaskPlace {
                CenterX = G("centerX"), CenterY = G("centerY"), Angle = G("angle"), Length = G("length"),
                RadiusX = G("radiusX"), RadiusY = G("radiusY"), BrushRadius = G("brushRadius"),
            };
            var dabs = (DevelopJson.Floats(m["brushStroke"]) ?? []).ToArray();
            int status;
            fixed (float* d = dabs)
                status = Native.orion_engine_mask_place_to_frame(Handle, &g, DevelopJson.Int(m["kind"])!.Value, &shape,
                                                                  dabs.Length >= 2 ? d : null, dabs.Length / 2);
            if (status != 0) {
                Console.Error.WriteLine($"orion: mask {converted.Count} could not be converted to frame coordinates and keeps its legacy reading");
                return;
            }
            converted.Add((shape, dabs));
        }
        for (int i = 0; i < rows.Count; i++) {
            var (p, dabs) = converted[i];
            var m = rows[i]!;
            (m["centerX"], m["centerY"], m["angle"], m["length"]) = (p.CenterX, p.CenterY, p.Angle, p.Length);
            (m["radiusX"], m["radiusY"], m["brushRadius"]) = (p.RadiusX, p.RadiusY, p.BrushRadius);
            m["brushStroke"] = new JsonArray(dabs.Select(v => (JsonNode)v).ToArray());
        }
        state["maskSpace"] = 1;
    }
}

public static unsafe partial class Native {
    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int orion_engine_set_lens_choice(nint engine, string name);

    [LibraryImport(Lib)]
    public static partial nint orion_engine_lens_choice(nint engine);

    /// OrionMaskLegacyGeometry and OrionMaskShape (orion.h).
    [StructLayout(LayoutKind.Sequential)]
    public struct MaskLegacyGeometry {
        public float CropX, CropY, CropW, CropH;
        public int RotateQuarters;
        public float StraightenDeg, PerspectiveVertical, PerspectiveHorizontal, PerspectiveAspect;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MaskPlace { public float CenterX, CenterY, Angle, Length, RadiusX, RadiusY, BrushRadius; }

    [LibraryImport(Lib)]
    public static partial int orion_engine_mask_place_to_frame(nint engine, MaskLegacyGeometry* geometry, int kind,
                                                               MaskPlace* shape, float* dabXy, int dabCount);
}

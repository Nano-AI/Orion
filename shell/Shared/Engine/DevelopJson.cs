using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace OrionShell;

/// The develop state inside a sidecar's `orion:Develop`: the JSON Swift's
/// DevelopState encodes (the Swift app's EditHistory.swift), read the way its forgiving
/// decoder reads it. A JsonObject with every key, in DevelopState's order,
/// rather than a 57-field class: SessionDocument maps it onto the engine by
/// name, and a key nobody maps (spots, a mask's matte id) rides through untouched.
///
/// Decode is the port of `init(from:)`, legacy keys and all: a key that is
/// absent or the wrong type keeps its default, and the three markers
/// (process, maskSpace, localTintAxis) read "absent" as the older meaning
/// wherever there is something for them to gate (decisions #203, #276, #290).
public static class DevelopJson {
    /// DevelopState() — every control at its Reset value, in declaration order.
    const string DefaultsText = """
        {"temperatureK":5500,"tint":0,"exposureEv":0,"highlights":0,"shadows":0,"whites":0,"blacks":0,
         "vibrance":0,"saturation":0,"contrast":1.45,"process":2,"rotateQuarters":0,"straightenDeg":0,
         "perspectiveVertical":0,"perspectiveHorizontal":0,"perspectiveAspect":0,"cropX":0,"cropY":0,"cropW":1,"cropH":1,
         "lensDistortion":0,"lensVignette":0,"lensCaRed":0,"lensCaBlue":0,"lensChoice":"","highlightRecovery":0,
         "gradeShadow":[0,0,0],"gradeMidtone":[0,0,0],"gradeHighlight":[0,0,0],"gradeBalance":0,
         "denoiseLuma":0,"denoiseColor":0,"denoiseDetail":0,"denoiseNlm":0,"denoiseSmoothness":0,"texture":0,
         "lutStrength":1,"maskSpace":1,"maskComponents":[],"maskRefine":0,"spots":[],"layers":[{}],"localTintAxis":1,
         "fusion":0,"dehaze":0,"clarity":0,"grainAmount":0,"grainSize":1.5,"vignetteAmount":0,"vignetteFieldAngle":45,
         "sharpenAmount":0,"sharpenRadius":1,"sharpenMasking":0,
         "curve":{"master":[{"x":0,"y":0},{"x":1,"y":1}],"red":[{"x":0,"y":0},{"x":1,"y":1}],
                  "green":[{"x":0,"y":0},{"x":1,"y":1}],"blue":[{"x":0,"y":0},{"x":1,"y":1}]},
         "hueShift":[0,0,0,0,0,0,0,0],"satShift":[0,0,0,0,0,0,0,0],"lumShift":[0,0,0,0,0,0,0,0]}
        """;

    /// LocalAdjustState() and MaskComponentState(), in declaration order.
    const string LayerText = """
        {"exposureEv":0,"contrast":0,"saturation":0,"warmth":0,"tint":0,"highlights":0,"shadows":0,"whites":0,"blacks":0,
         "denoiseLuma":0,"denoiseColor":0}
        """;
    const string MaskText = """
        {"kind":0,"compose":0,"invert":false,"hidden":false,"startsLayer":false,"centerX":0.5,"centerY":0.5,"angle":0,"length":0.5,
         "radiusX":0.3,"radiusY":0.3,"feather":0.5,"roundness":2,"rangeLo":-2,"rangeHi":2,"rangeSoft":1,
         "colorR":0.18,"colorG":0.18,"colorB":0.18,"colorTol":0.1,"colorSoft":0.05,
         "brushRadius":0.08,"brushFlow":0.5,"brushHardness":0.5,"brushStroke":[],"brushErase":[]}
        """;
    static readonly string[] SpotKeys = ["destX", "destY", "srcX", "srcY", "radius", "feather", "heal"];
    static readonly string[] MaskStrings = ["matteId", "matteSource", "name"];   // String?, omitted when nil
    /// DevelopState's Int32 fields; every other number is a Float.
    public static readonly HashSet<string> IntKeys = ["process", "rotateQuarters", "maskSpace", "localTintAxis"];
    /// DevelopState's keys. Anything else in a sidecar is a newer build's, carried through a save untouched.
    public static readonly HashSet<string> Known = Defaults().Select(p => p.Key).ToHashSet();
    /// Read, never written (the decoder's legacy cases): these are not a newer build's and are dropped on save, as Swift drops them.
    static readonly HashSet<string> LegacyKeys = [
        "denoiseColour", "localExposureEv", "localContrast", "localSaturation", "localWarmth", "localTint",
        "maskKind", "maskInvert", "maskCentreX", "maskCentreY", "maskAngle", "maskLength", "maskRadiusX", "maskRadiusY",
        "maskFeather", "maskRoundness", "brushRadius", "brushFlow", "brushHardness", "brushStroke"];

    public static JsonObject Defaults() => Parse(DefaultsText, LayerText);
    public static JsonObject DefaultLayer() => (JsonObject)JsonNode.Parse(LayerText)!;
    public static JsonObject DefaultMask() => (JsonObject)JsonNode.Parse(MaskText)!;

    static JsonObject Parse(string text, string layer) {
        var o = (JsonObject)JsonNode.Parse(text)!;
        o["layers"] = new JsonArray(JsonNode.Parse(layer));
        return o;
    }

    /// Null only when the bytes are not JSON at all, which is when Swift's
    /// decode throws and the photograph opens unedited with autosave disarmed.
    public static JsonObject? Decode(byte[] json) {
        JsonNode? root;
        try { root = JsonNode.Parse(json); } catch (JsonException) { return null; }
        var s = Defaults();
        if (root is not JsonObject c) return s;   // valid JSON, not an object: every default, as Swift's container guard gives

        foreach (var (key, value) in s.ToList()) {
            if (value?.GetValueKind() != JsonValueKind.Number) continue;
            if (IntKeys.Contains(key)) { if (Int(c[key]) is { } i) s[key] = i; }
            else if (Float(c[key]) is { } f) s[key] = f;
        }
        // Read, never written: the British spelling before decision #33.
        if (Float(c["denoiseColor"]) == null && Float(c["denoiseColour"]) is { } colour) s["denoiseColor"] = colour;
        if (c["lensChoice"] is JsonValue lens && lens.GetValueKind() == JsonValueKind.String) s["lensChoice"] = lens.GetValue<string>();
        foreach (var (key, n) in new[] { ("gradeShadow", 3), ("gradeMidtone", 3), ("gradeHighlight", 3), ("hueShift", 8), ("satShift", 8), ("lumShift", 8) })
            if (Floats(c[key]) is { Count: var count } band && count == n) s[key] = Array(band);

        // The new key wins; the legacy scalars fill layer 1 when it is absent.
        if (c["layers"] is JsonArray ls && ls.Count > 0) {
            s["layers"] = new JsonArray(ls.Select(l => (JsonNode)Fill(DefaultLayer(), l as JsonObject)).ToArray());
        } else {
            var first = DefaultLayer();
            foreach (var k in new[] { "ExposureEv", "Contrast", "Saturation", "Warmth", "Tint" })
                if (Float(c["local" + k]) is { } f) first[char.ToLowerInvariant(k[0]) + k[1..]] = f;
            s["layers"] = new JsonArray(first);
        }
        if (Spots(c["spots"]) is { } spots) s["spots"] = spots;

        // The group, or the single mask a pre-group sidecar carried, lifted into one.
        if (c["maskComponents"] is JsonArray list) {
            s["maskComponents"] = new JsonArray(list.Select(m => Mask(m as JsonObject)).Where(m => Int(m["kind"]) != 0).Select(m => (JsonNode)m).ToArray());
        } else if (Int(c["maskKind"]) is { } kind and not 0) {
            var m = DefaultMask();
            m["kind"] = kind;
            if (Bool(c["maskInvert"]) is { } inv) m["invert"] = inv;
            foreach (var (from, to) in new[] { ("maskCentreX", "centerX"), ("maskCentreY", "centerY"), ("maskAngle", "angle"), ("maskLength", "length"),
                                               ("maskRadiusX", "radiusX"), ("maskRadiusY", "radiusY"), ("maskFeather", "feather"),
                                               ("maskRoundness", "roundness"), ("brushRadius", "brushRadius"), ("brushFlow", "brushFlow"),
                                               ("brushHardness", "brushHardness") })
                if (Float(c[from]) is { } f) m[to] = f;
            if (Floats(c["brushStroke"]) is { } stroke) m["brushStroke"] = Array(stroke);
            s["maskComponents"] = new JsonArray(m);
        }

        var layers = ((JsonArray)s["layers"]!).Cast<JsonObject>().ToList();
        if (Int(c["maskSpace"]) == null && ((JsonArray)s["maskComponents"]!).Count > 0) s["maskSpace"] = 0;
        bool bands = new[] { "highlights", "shadows", "whites", "blacks" }.Any(k => Float(s[k]) != 0 || layers.Any(l => Float(l[k]) != 0));
        if (Int(c["process"]) == null && bands) s["process"] = 1;
        if (Int(c["localTintAxis"]) == null && layers.Any(l => Float(l["tint"]) != 0)) s["localTintAxis"] = 0;
        if (Int(s["localTintAxis"]) == 0) {
            // The shader once read green the wrong way: legacy tints flip once, here (#290).
            foreach (var l in layers.Where(l => Float(l["tint"]) != 0)) l["tint"] = -Float(l["tint"])!.Value;
            s["localTintAxis"] = 1;
        }
        if (Curve(c["curve"]) is { } curve) s["curve"] = curve;
        // Top-level keys only: a newer field inside a mask row or a layer is dropped, as Swift drops it.
        foreach (var (key, value) in c) if (!Known.Contains(key) && !LegacyKeys.Contains(key)) s[key] = value?.DeepClone();
        return s;
    }

    /// JSON bytes for `orion:Develop`, keys in DevelopState's order.
    public static byte[] Encode(JsonObject state) => JsonSerializer.SerializeToUtf8Bytes(state);

    static JsonObject Fill(JsonObject into, JsonObject? from) {
        if (from == null) return into;
        foreach (var (key, _) in into.ToList()) if (Float(from[key]) is { } f) into[key] = f;
        return into;
    }

    /// MaskComponentState's decoder: American keys win, British ones are read in their absence (#112).
    static JsonObject Mask(JsonObject? from) {
        var m = DefaultMask();
        if (from == null) return m;
        foreach (var (key, value) in m.ToList()) {
            var kind = value!.GetValueKind();
            if (key is "kind" or "compose") { if (Int(from[key]) is { } i) m[key] = i; }
            else if (kind is JsonValueKind.True or JsonValueKind.False) { if (Bool(from[key]) is { } b) m[key] = b; }
            else if (kind == JsonValueKind.Array) { if (Floats(from[key]) is { } a) m[key] = Array(a); }
            else if ((Float(from[key]) ?? Float(from[Legacy(key)])) is { } f) m[key] = f;
        }
        foreach (var key in MaskStrings)
            if (from[key] is JsonValue v && v.GetValueKind() == JsonValueKind.String) m[key] = v.GetValue<string>();
        return m;
    }

    static string Legacy(string key) => key.Replace("center", "centre").Replace("color", "colour");

    /// SpotState and ToneCurve decode synthesised: one missing or mistyped key
    /// and the whole value keeps its default.
    static JsonArray? Spots(JsonNode? node) {
        if (node is not JsonArray a) return null;
        var spots = new JsonArray();
        foreach (var e in a) {
            if (e is not JsonObject o) return null;
            var spot = new JsonObject();
            foreach (var k in SpotKeys[..^1]) {
                if (Float(o[k]) is not { } f) return null;
                spot[k] = f;
            }
            if (Bool(o["heal"]) is not { } heal) return null;
            spot["heal"] = heal;
            spots.Add(spot);
        }
        return spots;
    }

    static JsonObject? Curve(JsonNode? node) {
        if (node is not JsonObject o) return null;
        var curve = new JsonObject();
        foreach (var channel in new[] { "master", "red", "green", "blue" }) {
            if (o[channel] is not JsonArray points) return null;
            var copy = new JsonArray();
            foreach (var p in points) {
                if (p is not JsonObject xy || Float(xy["x"]) is not { } x || Float(xy["y"]) is not { } y) return null;
                copy.Add(new JsonObject { ["x"] = x, ["y"] = y });
            }
            curve[channel] = copy;
        }
        return curve;
    }

    static JsonArray Array(IEnumerable<float> values) => new(values.Select(v => (JsonNode)v).ToArray());

    /// Swift's Float decode: a number that fits, else nothing.
    public static float? Float(JsonNode? n) => Number(n) is { } d && Math.Abs(d) <= float.MaxValue ? (float)d : null;

    /// Int32: an integral number in range, else nothing.
    public static int? Int(JsonNode? n) => Number(n) is { } d && d == Math.Floor(d) && d >= int.MinValue && d <= int.MaxValue ? (int)d : null;

    /// Through the text, so a number reads the same whether it was parsed or
    /// put there as an int or a float: JsonValue will not convert between CLR types.
    static double? Number(JsonNode? n) =>
        n is JsonValue v && v.GetValueKind() == JsonValueKind.Number
        && double.TryParse(v.ToJsonString(), NumberStyles.Float, CultureInfo.InvariantCulture, out double d) ? d : null;

    static bool? Bool(JsonNode? n) => n?.GetValueKind() switch { JsonValueKind.True => true, JsonValueKind.False => false, _ => null };

    /// [Float]: every element a number, else nothing.
    public static List<float>? Floats(JsonNode? n) {
        if (n is not JsonArray a) return null;
        var list = new List<float>(a.Count);
        foreach (var e in a) { if (Float(e) is not { } f) return null; list.Add(f); }
        return list;
    }
}

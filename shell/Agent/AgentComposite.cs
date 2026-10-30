using System.Text.Json;
using System.Text.Json.Nodes;
using OrionShell.Masks;

namespace OrionShell.Agent;

/// `apply`'s merge, and the strict check every composite field gets on the
/// way in (the Swift app's AgentCLI.mergeEdits and AgentComposite).
///
/// **A composite edit REPLACES the whole value**: the model reads the current
/// one from `apply`'s `state` and sends back the complete new one. That is what
/// makes the strict check safe: every element must carry every field its kind
/// has, because the develop decoder is lenient per field (DevelopJson) and a
/// missing one would silently fall back to its default — which is how a partial
/// layer once zeroed its siblings. Every range is one the panel's own control
/// can produce.
static class AgentComposite {
    public static readonly string[] Fields =
        ["curve", "gradeShadow", "gradeMidtone", "gradeHighlight", "hueShift", "satShift", "lumShift", "layers", "spots", "maskComponents"];

    static readonly string[] Bands = ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"];
    static readonly string[] SpotKeys = ["destX", "destY", "srcX", "srcY", "radius", "feather", "heal"];
    static readonly string[] MaskOptional = ["matteId", "matteSource", "name"];

    /// Overlays `edits` onto `base` and proves the result still decodes. ⚠ The
    /// base is normalised first (AgentCli.Normal), so every legacy conversion
    /// happens to the BASE's values only: overlaying raw JSON onto an old
    /// sidecar with no `localTintAxis` once negated an agent's own layer tint.
    /// A key the base does not have is the model inventing vocabulary (exit 2).
    public static (JsonObject State, List<string> Changed) MergeEdits(byte[] baseBytes, byte[] edits, string? photo) {
        var merged = AgentCli.Normal(baseBytes);
        JsonObject e;
        try { e = JsonNode.Parse(edits) as JsonObject ?? throw new AgentCli.Failure("edits are not a JSON object"); }
        catch (JsonException) { throw new AgentCli.Failure("edits are not a JSON object"); }
        var unknown = e.Select(p => p.Key).Where(k => !merged.ContainsKey(k)).Order(StringComparer.Ordinal).ToList();
        if (unknown.Count > 0) throw new AgentCli.UnknownKeys(unknown);
        foreach (var (key, value) in e) {
            if (Fields.Contains(key)) Validate(key, value, photo);
            // Absolute, never a delta: the guard that was missing when 150 K was taken literally.
            else if (AgentKeys.Specs.GetValueOrDefault(key) is { Min: { } lo, Max: { } hi } spec
                     && value?.GetValueKind() == JsonValueKind.Number && Number(value) is var got && !(got >= lo && got <= hi))
                throw new AgentCli.Usage($"{key} {AgentKeys.Format(got)} is outside {AgentKeys.Format(lo)}…{AgentKeys.Format(hi)} "
                                         + $"({spec.Unit}, absolute not a delta)");
        }
        foreach (var (key, value) in e) merged[key] = value?.DeepClone();
        return (AgentCli.Normal(DevelopJson.Encode(merged)), e.Select(p => p.Key).Order(StringComparer.Ordinal).ToList());
    }

    /// Through the text, so a number reads the same however it was put there.
    static double Number(JsonNode? n) =>
        n is JsonValue v && v.GetValueKind() == JsonValueKind.Number
        && double.TryParse(v.ToJsonString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double d)
            ? d : double.NaN;

    static string Camel(string snake) => System.Text.RegularExpressions.Regex.Replace(snake, "_([a-z])", m => m.Groups[1].Value.ToUpperInvariant());

    static Exception Rejected(string message) => new AgentCli.Usage(message);

    static void Validate(string key, JsonNode? value, string? photo) {
        switch (key) {
            case "curve": Curve(value); break;
            case "gradeShadow" or "gradeMidtone" or "gradeHighlight": Grade(value, key); break;
            case "hueShift" or "satShift" or "lumShift": Band(value, key); break;
            case "layers": Elements(value, key, (o, label) => Layer(o, label)); break;
            case "spots": Elements(value, key, (o, label) => Spot(o, label)); break;
            case "maskComponents": Elements(value, key, (o, label) => Mask(o, label, photo)); break;
        }
    }

    static void Curve(JsonNode? value) {
        if (value is not JsonObject o) throw Rejected("curve must be an object");
        string[] channels = ["master", "red", "green", "blue"];
        Complete(o, channels, channels, "curve");
        foreach (var c in channels) {
            if (o[c] is not JsonArray points) throw Rejected($"curve.{c} must be an array of points");
            if (points.Count < 2) throw Rejected($"curve.{c} needs at least 2 points");
            double? last = null;
            for (int i = 0; i < points.Count; i++) {
                string label = $"curve.{c}[{i}]";
                if (points[i] is not JsonObject p) throw Rejected($"{label} must be an object");
                Complete(p, ["x", "y"], ["x", "y"], label);
                Range(p, "x", 0, 1, "unitless", "CurveEditor.cs", label);
                Range(p, "y", 0, 1, "unitless", "CurveEditor.cs", label);
                double x = Number(p["x"]);
                // A non-ascending curve is malformed to the engine and falls back to the identity: refused here instead.
                if (last is { } l && x <= l) throw Rejected($"curve.{c} points must be sorted by x, strictly increasing");
                last = x;
            }
        }
    }

    static void Grade(JsonNode? value, string name) {
        if (value is not JsonArray { Count: 3 } a || a.Any(n => n?.GetValueKind() != JsonValueKind.Number))
            throw Rejected($"{name} must be an array of exactly 3 numbers [x, y, luminance]");
        double x = Number(a[0]), y = Number(a[1]);
        if (x * x + y * y > 1)
            throw Rejected($"{name} [x={AgentKeys.Format(x)}, y={AgentKeys.Format(y)}] is outside the unit disc, x²+y² ≤ 1 (ColorWheel.cs)");
        Scalar(Number(a[2]), -0.5, 0.5, "unitless", "ColorWheel.cs", $"{name}[2] (luminance)");
    }

    static void Band(JsonNode? value, string name) {
        if (value is not JsonArray { Count: 8 } a) throw Rejected($"{name} must be an array of exactly 8 numbers");
        for (int i = 0; i < 8; i++) {
            if (a[i]?.GetValueKind() != JsonValueKind.Number) throw Rejected($"{name}[{i}] must be a number");
            Scalar(Number(a[i]), -1, 1, "unitless", "ColorPanel.cs", $"{name}[{i}]");
        }
    }

    static void Elements(JsonNode? value, string name, Action<JsonObject, string> check) {
        if (value is not JsonArray a) throw Rejected($"{name} must be an array");
        for (int i = 0; i < a.Count; i++)
            check(a[i] as JsonObject ?? throw Rejected($"{name}[{i}] must be an object"), $"{name}[{i}]");
    }

    /// The LOCAL ranges (AdjustmentGroup.LocalSet), not the global ones: local exposure is -3…3 EV where global is -5…5.
    static void Layer(JsonObject o, string label) {
        var keys = DevelopJson.DefaultLayer().Select(p => p.Key).ToArray();
        Complete(o, keys, keys, label);
        foreach (var (_, field, min, max, unit, _) in AdjustmentGroup.LocalSet) {
            string key = Camel(field["local_".Length..]);
            Range(o, key, min, max, unit.Trim() is { Length: > 0 } u ? u : "unitless", "AdjustmentGroup.cs", label);
        }
    }

    static void Spot(JsonObject o, string label) {
        Complete(o, SpotKeys, SpotKeys, label);
        foreach (var f in new[] { "destX", "destY", "srcX", "srcY" }) Range(o, f, 0, 1, "fraction", "SpotOverlay.cs", label);
        Range(o, "radius", 0.004, 0.12, "fraction", "DetailPanel.cs", label);
        Range(o, "feather", 0, 1, "unitless", "DetailPanel.cs", label);
        if (o["heal"]?.GetValueKind() is not (JsonValueKind.True or JsonValueKind.False)) throw Rejected($"{label}.heal must be a boolean");
    }

    static readonly (string Name, double Min, double Max, string Unit)[] MaskRanges = [
        ("centerX", 0, 1, "unitless"), ("centerY", 0, 1, "unitless"), ("angle", -3.15, 3.15, "radians"), ("length", 0.05, 1.5, "unitless"),
        ("radiusX", 0.02, 1, "unitless"), ("radiusY", 0.02, 1, "unitless"), ("feather", 0, 1, "unitless"), ("roundness", 2, 8, "unitless"),
        ("rangeLo", -8, 8, "EV"), ("rangeHi", -8, 8, "EV"), ("rangeSoft", 0.05, 4, "EV"), ("colorTol", 0.01, 0.8, "unitless"),
        ("colorSoft", 0.002, 0.4, "unitless"), ("brushRadius", 0.01, 0.4, "unitless"), ("brushFlow", 0.01, 1, "unitless"),
        ("brushHardness", 0, 1, "unitless"), ("compose", 0, 2, "unitless"),
    ];

    static void Mask(JsonObject o, string label, string? photo) {
        var required = DevelopJson.DefaultMask().Select(p => p.Key).ToArray();
        Complete(o, required, required.Concat(MaskOptional).ToArray(), label);
        if (o["kind"]?.GetValueKind() != JsonValueKind.Number) throw Rejected($"{label}.kind must be a number");
        double kind = Number(o["kind"]);
        // 0 is the zero value, "no mask", never a kind a caller may name.
        if (kind != Math.Round(kind) || kind < 1 || kind > 6)
            throw Rejected($"{label}.kind {AgentKeys.Format(kind)} is outside 1…6 — 1 linear, 2 radial, 3 brush, 4 matte, 5 range, 6 color");
        foreach (var (name, min, max, unit) in MaskRanges) Range(o, name, min, max, unit, "MaskControls.cs", label);
        foreach (var f in new[] { "invert", "hidden", "startsLayer" })
            if (o[f]?.GetValueKind() is not (JsonValueKind.True or JsonValueKind.False)) throw Rejected($"{label}.{f} must be a boolean");
        // colorR/G/B: set by an eyedropper click, not a slider: a type and no range.
        foreach (var f in new[] { "colorR", "colorG", "colorB" })
            if (o[f]?.GetValueKind() != JsonValueKind.Number) throw Rejected($"{label}.{f} must be a number");
        foreach (var f in new[] { "brushStroke", "brushErase" })
            if (o[f] is not JsonArray a || a.Any(n => n?.GetValueKind() != JsonValueKind.Number))
                throw Rejected($"{label}.{f} must be an array of numbers");
        if (kind != 4) return;
        // A model cannot paint a matte, only name one a producer already saved beside the photo.
        if (o["matteId"] is not JsonValue v || v.GetValueKind() != JsonValueKind.String || v.GetValue<string>() is not { Length: > 0 } id)
            throw Rejected($"{label} kind 4 (raster matte) needs matteId — a model cannot paint one, only reference a matte already saved beside the photo");
        if (photo == null) throw Rejected($"{label} kind 4 needs a photograph to check the matte against");
        string path;
        try { path = MatteStore.PathFor(photo, id); } catch (ArgumentException) { throw Rejected($"{label} kind 4 names '{id}', which is not a matte id"); }
        if (!File.Exists(path))
            throw Rejected($"{label} kind 4 names matte '{id}' but {Path.GetFileName(path)} does not exist beside {Path.GetFileName(photo)}");
    }

    static void Complete(JsonObject o, string[] required, string[] allowed, string label) {
        if (required.Where(k => !o.ContainsKey(k)).Order(StringComparer.Ordinal).FirstOrDefault() is { } missing)
            throw Rejected($"{label} is missing '{missing}'");
        if (o.Select(p => p.Key).Where(k => !allowed.Contains(k)).Order(StringComparer.Ordinal).FirstOrDefault() is { } unknown)
            throw Rejected($"{label} has an unknown field '{unknown}'");
    }

    static void Range(JsonObject o, string field, double min, double max, string unit, string source, string label) {
        if (o[field]?.GetValueKind() != JsonValueKind.Number) throw Rejected($"{label}.{field} must be a number");
        Scalar(Number(o[field]), min, max, unit, source, $"{label}.{field}");
    }

    static void Scalar(double v, double min, double max, string unit, string source, string label) {
        if (!(v >= min && v <= max))
            throw Rejected($"{label} {AgentKeys.Format(v)} is outside {AgentKeys.Format(min)}…{AgentKeys.Format(max)} ({unit}, {source})");
    }

    // MARK: the `keys` entries, each with a complete example built from the defaults so it cannot drift

    const string SendComplete = " Send the complete value; read the current one from apply.state first.";

    public static IEnumerable<JsonObject> Entries() {
        var d = DevelopJson.Defaults();
        var radial = DevelopJson.DefaultMask();
        radial["kind"] = 2;   // 0 is "no mask", never a kind to send
        string bands = $"Eight bands in HueBand order - {string.Join(", ", Bands)} - each -1…1.";
        var spot = new JsonObject { ["destX"] = 0.5, ["destY"] = 0.5, ["srcX"] = 0.5, ["srcY"] = 0.5, ["radius"] = 0.02, ["feather"] = 0.5, ["heal"] = true };
        (string, string, JsonNode, string)[] all = [
            ("curve", "object", d["curve"]!.DeepClone(),
             "Each channel (master, red, green, blue) needs at least 2 points, x and y each 0…1, sorted strictly ascending by x." + SendComplete),
            ("gradeShadow", "array", Zeros(3), GradeNote), ("gradeMidtone", "array", Zeros(3), GradeNote),
            ("gradeHighlight", "array", Zeros(3), GradeNote),
            ("hueShift", "array", Zeros(8), bands + SendComplete), ("satShift", "array", Zeros(8), bands + SendComplete),
            ("lumShift", "array", Zeros(8), bands + SendComplete),
            ("layers", "array", new JsonArray(DevelopJson.DefaultLayer()), LayersNote + SendComplete),
            ("spots", "array", new JsonArray(spot),
             "Every element needs every SpotState field (destX, destY, srcX, srcY, radius, feather, heal); positions 0…1, radius and "
             + "feather in the product's ranges." + SendComplete),
            ("maskComponents", "array", new JsonArray(radial), MaskNote + SendComplete),
        ];
        return all.Select(e => new JsonObject { ["name"] = e.Item1, ["type"] = e.Item2, ["absolute"] = true, ["example"] = e.Item3, ["note"] = e.Item4 });
    }

    static JsonArray Zeros(int n) => new(Enumerable.Range(0, n).Select(_ => (JsonNode)0).ToArray());

    /// The axes are what DevelopPipeline::gradeOffsets computes (engine/src/pipe/DevelopPipeline.cpp:158-191), not a guess from renders.
    const string GradeNote =
        "[x, y, luminance]. x²+y² ≤ 1 (the puck stays in the wheel); luminance -0.5…0.5. The angle picks a hue the way the wheel is "
        + "wound: red at 0°, green at 120°, blue at 240°, counter-clockwise with y upward (DevelopPipeline.cpp:158-191). So +x is red and "
        + "-x cyan; +y is yellow-to-green and -y blue-to-magenta; the radius is how far, 0 is neutral. The offset is zero-sum, so a wheel "
        + "tints without lifting; luminance is the separate track under it. Send the complete value; read the current one from apply.state first.";

    const string LayersNote =
        "One entry per mask LAYER, not per component: layers[0] is the run that starts at maskComponents[0] (row 0 always begins a layer "
        + "whatever its flag says), layers[1] the run beginning at the next component with startsLayer true, and so on. Every element needs "
        + "every LocalAdjustState field (exposureEv, contrast, saturation, warmth, tint, highlights, shadows, whites, blacks, denoiseLuma, "
        + "denoiseColor), each checked against the *local* panel's own range, not the global scalar one - local exposure is -3…3 EV where "
        + "global is -5…5. tint runs the same way as the global tint: positive is magenta, negative is green; warmth positive is warmer. "
        + "A layer's denoiseLuma and denoiseColor (0…4, the global sliders' units) run on top of the global chain through the coverage; a "
        + "layer cannot un-denoise, so to clean the wall and keep the climber put the denoise on a layer with the subject subtracted "
        + "(compose 1) or inverted, and leave the global low.";

    /// The per-kind field map, checked against engine/shaders/mask_component.slang's kind branches.
    /// ⚠ Two traps it closes: a linear gradient ignores `feather` (its length is the feather), and a radial reads `angle`.
    const string MaskNote =
        "Every element needs every MaskComponentState field except the optional matteId/matteSource/name. ALL coordinates, radii and "
        + "lengths are in FRAME space - the sensor's un-turned frame, x and y each 0…1 from the top-left, before crop, straighten, "
        + "rotation and perspective (maskSpace 1, decision #112) - NOT the display-space fractions get_proxy and get_stats' region use. "
        + "detect_faces returns both. kind is 1…6 and decides which fields are read: "
        + "1 linear gradient - centerX, centerY, angle, length; it ignores feather, because the ramp runs the whole length. The ramp is "
        + "centred on (centerX, centerY) and covers the side the angle points to: at angle 0 the right, at +1.571 rad (+90°) the BOTTOM, "
        + "at -1.571 rad (-90°) the TOP, y running downward, and length is how far the transition runs. "
        + "⚠ FRAME top is not display top on a turned photograph: a portrait frame off a landscape sensor puts frame-top down the "
        + "display's LEFT edge (measured), so aim a gradient by carrying a point across with detect_faces, or propose it and probe both "
        + "edges with get_stats' region before believing it. "
        + "2 radial - centerX, centerY, radiusX, radiusY, feather, roundness, and angle, which rotates the ellipse; roundness 2 is an "
        + "ellipse and higher walks toward a rounded rectangle; feather is the share of the radius the ramp takes, measured inward from "
        + "the boundary: coverage is full inside (1 - feather) of the radius and zero at the edge, so at feather 0.8 the ellipse is fully "
        + "on only across its inner 20%, and a compose-1 subtract row at 0.8 protects far less than its outline suggests; size the "
        + "ellipse to the subject plus the feather, or keep feather near 0.5 when the outline is the subject's own edge. "
        + "3 brush - brushStroke (interleaved x,y dabs), brushErase, brushRadius, brushFlow, brushHardness. A model cannot paint one: "
        + "send no dabs and it covers nothing, so use 2, 5 or 6 instead. "
        + "4 raster matte - matteId, which must name a PNG already saved beside the photograph by a producer (Subject, Sky); a model "
        + "cannot make one. "
        + "5 luminance range - rangeLo, rangeHi (the band's edges in EV relative to middle grey, as displayed, so they follow the "
        + "exposure slider) and rangeSoft (how many EV each edge takes to ramp). "
        + "6 colour range - colorR, colorG, colorB (the target shade in scene-linear Rec.2020), colorTol and colorSoft, measured in "
        + "Oklab chromaticity, so lightness does not enter: use kind 5 for that axis. "
        + "invert flips THIS component's own coverage before it folds into the group, so an inverted radial covers everything outside "
        + "its ellipse. compose (0 add, 1 subtract, 2 intersect) is how it folds; startsLayer begins a new layer, and layers[i] holds "
        + "the adjustments for the i-th layer in this list's order.";
}

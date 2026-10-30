using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace OrionShell.Agent;

/// The scalar develop fields `apply` may write, the range each is checked
/// against, and the `keys` payload describe_edits (mcp/) reads before a model
/// proposes anything. Ported from the Swift app's AgentKeys and AgentVocabulary
/// with every range and note unchanged: they are the numbers the sliders draw
/// (Slices/Develop/*Panel.cs), and the reason this exists is #244/#245, where
/// `temperatureK: 150` — meant as +150 K — was taken as absolute, rendered a
/// flat black proxy and was committed anyway.
static class AgentKeys {
    public sealed record Spec(string Unit, double? Min = null, double? Max = null, string? Note = null);

    const string AsShot = "not the camera's as-shot reading — read the real one from `stats` or a proposed state before changing it";

    public static readonly Dictionary<string, Spec> Specs = new() {
        ["temperatureK"] = new("kelvin", 2000, 12000, "default is DevelopState()'s 5500 K, " + AsShot),
        ["tint"] = new("unitless", -1, 1, "default is DevelopState()'s 0, " + AsShot),
        ["exposureEv"] = new("EV", -5, 5), ["contrast"] = new("unitless", 0.5, 2),
        ["highlights"] = new("unitless", -1, 1), ["shadows"] = new("unitless", -1, 1),
        ["whites"] = new("unitless", -1, 1), ["blacks"] = new("unitless", -1, 1), ["highlightRecovery"] = new("unitless", 0, 1),
        ["process"] = new("generation", 1, 2,
            "which generation of tone bands renders highlights, shadows, whites and blacks: 2 is the current one - an identity band at "
            + "middle grey, so a face survives a highlights pull - and 1 is the bands as shipped. A fresh photograph is 2; a sidecar "
            + "written before the field existed reopens at 1 so it renders as it was finished. Leave it alone unless you mean to "
            + "re-render an old edit under the new bands"),
        ["vibrance"] = new("unitless", -1, 1), ["saturation"] = new("unitless", -1, 1), ["gradeBalance"] = new("unitless", -1, 1),
        ["denoiseLuma"] = new("unitless", 0, 4), ["denoiseColor"] = new("unitless", 0, 4),
        ["denoiseDetail"] = new("unitless", 0, 1,
            "how far the luminance threshold backs off where the picture has more texture than noise: 0 is the fixed threshold every "
            + "photograph before it was rendered with, 1 is BayesShrink's shape under the same denoiseLuma strength. It only ever smooths "
            + "less, never more. Try 0.5 on rock and fabric, then re-read noise on a flat patch"),
        ["denoiseNlm"] = new("unitless", 0, 1,
            "non-local means on luminance, run before the wavelet chain: averages pixels whose 5x5 neighbourhoods match, so fine periodic "
            + "texture survives a removal that denoiseLuma would smooth. k in h = k*sigma; 0.4 is the published value. Pair it with "
            + "denoiseLuma 1-1.5: alone it leaves 1.6-1.8x the grain of denoiseLuma 2.5 at ISO 8000-16000 and reads wormy, paired it "
            + "measured half. denoiseColor still takes the colour blotches. A frame above ISO 3200 (capture.iso) is the reason to reach for it"),
        ["denoiseSmoothness"] = new("unitless", 0, 1,
            "coarse-scale colour noise: two more wavelet scales on the chroma, at strength denoiseColor * denoiseSmoothness, so it does "
            + "nothing while denoiseColor is 0. For the 20-60 px colour blotches in the shadows of a high-ISO frame that denoiseColor alone "
            + "leaves; 0.5-1. Strong values desaturate small colourful objects, so check one"),
        ["texture"] = new("unitless", -1, 1,
            "micro-contrast on the two finest scales, gated by the frame's measured noise so grain under three deviations is never boosted. "
            + "Positive lifts texture that stands above the noise (+0.5 and up to see it; where the fine scales are all noise, as on ISO 8000+ "
            + "walls, it has nothing to lift), negative smooths weak texture such as skin and leaves edges. Different from sharpenAmount, "
            + "which is an unsharp mask on edges"),
        ["lutStrength"] = new("unitless", 0, 1), ["fusion"] = new("unitless", 0, 1),
        ["grainAmount"] = new("unitless", 0, 0.06), ["grainSize"] = new("pixels", 1.2, 8),
        ["vignetteAmount"] = new("EV", -3, 3,
            "the exposure change at the corners of the COMPOSITION, not the sensor frame: the circle is the crop's (decision #103), so it "
            + "follows a crop or a straighten and its centre is the displayed picture's centre. Negative darkens the corners, positive "
            + "brightens them; the centre is never touched"),
        ["vignetteFieldAngle"] = new("degrees", 10, 70), ["dehaze"] = new("unitless", 0, 1), ["clarity"] = new("unitless", -1, 1),
        ["sharpenAmount"] = new("unitless", 0, 2), ["sharpenRadius"] = new("pixels", 0.5, 3), ["sharpenMasking"] = new("unitless", 0, 1),
        ["maskRefine"] = new("unitless", 0, 1),
        ["lensDistortion"] = new("unitless", -1, 1), ["lensVignette"] = new("unitless", -1, 1),
        ["lensCaRed"] = new("unitless", -1, 1), ["lensCaBlue"] = new("unitless", -1, 1),
        ["straightenDeg"] = new("degrees", -90, 90), ["perspectiveVertical"] = new("unitless", -1, 1),
        ["perspectiveHorizontal"] = new("unitless", -1, 1), ["perspectiveAspect"] = new("unitless", -1, 1),
        // No slider: the crop is a drag rectangle, so the range is the engine's own clamp.
        ["cropX"] = new("fraction", 0, 1), ["cropY"] = new("fraction", 0, 1), ["cropW"] = new("fraction", 0, 1), ["cropH"] = new("fraction", 0, 1),
        // No product range: the type is the only limit, and none is invented.
        ["rotateQuarters"] = new("quarter turns", Note: "no slider — rotates 90° per quarter turn (read mod 4); any integer the JSON type allows is accepted"),
        ["lensChoice"] = new("", Note: "a lens profile name from the lens database, not an index; empty means the profile came from the "
            + "file's own EXIF; any string is accepted here and simply fails to resolve to a profile if it names none"),
        ["maskSpace"] = new("", Note: "an internal marker, not a photographer-facing control — 0 is legacy display-space, 1 is frame-space "
            + "(decision #112); apply accepts any integer but only 0 and 1 mean anything to the engine"),
        ["localTintAxis"] = new("", Note: "an internal marker, not a photographer-facing control - 0 is legacy (a layer's positive tint "
            + "was green), 1 is current (positive is magenta, the global tint's direction), decision #290; a state is converted to 1 as it "
            + "is read, so every state apply returns already says 1. apply should never send it"),
    };

    /// The top-level fields that are numbers or strings, from DevelopState's own defaults so it cannot drift.
    public static readonly HashSet<string> Scalars =
        DevelopJson.Defaults().Where(p => p.Value is JsonValue).Select(p => p.Key).ToHashSet();

    /// What `apply` accepts, sorted: the scalars and the composites.
    public static IEnumerable<string> Editable => Scalars.Concat(AgentComposite.Fields).Order(StringComparer.Ordinal);

    /// `2000`, not `2000.0`; `0.06`, not `0.059999…`: as the ranges above were written.
    public static string Format(double v) =>
        v == Math.Round(v) && Math.Abs(v) < 1e15 ? ((long)v).ToString(CultureInfo.InvariantCulture) : v.ToString("R", CultureInfo.InvariantCulture);

    /// The `keys` payload. A scalar entry is `name, type, unit, min, max,
    /// default, absolute, note`; a composite one `name, type, absolute,
    /// example, note`. Both shapes are fixed: mcp/server.ts and
    /// tools/check-agent.py read them.
    public static JsonArray Json() {
        var defaults = DevelopJson.Defaults();
        var entries = Scalars.Select(name => {
            var e = new JsonObject { ["name"] = name, ["absolute"] = true };
            var value = (JsonValue)defaults[name]!;
            if (value.GetValueKind() == JsonValueKind.String) (e["type"], e["default"]) = ("string", value.GetValue<string>());
            else (e["type"], e["default"]) = ("number", DevelopJson.Float(value) is { } f ? double.Parse(f.ToString("R", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture) : 0);
            var spec = Specs.GetValueOrDefault(name);
            e["unit"] = spec?.Unit ?? "unitless";
            if (spec?.Min is { } min) (e["min"], e["max"]) = (min, spec.Max);
            if (spec?.Note is { } note) e["note"] = note;
            return e;
        }).Concat(AgentComposite.Entries());
        return new JsonArray(entries.OrderBy(e => e["name"]!.GetValue<string>(), StringComparer.Ordinal).Select(e => (JsonNode)e).ToArray());
    }
}

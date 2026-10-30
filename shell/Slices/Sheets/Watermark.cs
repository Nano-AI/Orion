using System.Text.Json.Nodes;

namespace OrionShell.Sheets;

/// A port of app/Watermark.swift: what the mark is, where it goes, how big and
/// how present, and its one saved copy. One gray, always (#284): the engine
/// only ever sees a coverage mask (WatermarkRaster).
sealed class Watermark {
    public static Watermark Shared { get; } = new(DefaultPath(), AppFolder.Writable);

    /// The 3x3 grid, row-major, the order the panel draws it in.
    public static readonly string[] Anchors =
        ["top left", "top", "top right", "left", "center", "right", "bottom left", "bottom", "bottom right"];
    /// Watermark.Anchor's raw values, the spelling watermark.json holds.
    static readonly string[] AnchorKeys =
        ["topLeft", "top", "topRight", "left", "center", "right", "bottomLeft", "bottom", "bottomRight"];

    /// The only color a mark can be: neutral gray, in sRGB.
    public const float Gray = 0.5f;

    public bool Enabled;
    // Content: text (Words, Family, Bold) or an SVG's bytes, copied in, so moving
    // the original cannot break the mark. Both kinds are kept while the panel is
    // open, so flipping Text/SVG and back loses nothing; the save keeps the one shown.
    public bool IsText = true;
    public string Words = "", Family = "Helvetica Neue", SvgName = "";
    public byte[] Svg = [];
    public bool Bold;
    /// An index into Anchors, or -1 for the diagonal.
    public int Anchor = 8;
    public double Size = 0.04, Span = 0.6, Margin = 0.03, Opacity = 0.35;
    /// Why the last save did not land, or null if it did.
    public string? LastFailure { get; private set; }

    readonly string? _path;
    readonly bool _writes;

    /// Reads `path` if it holds a mark; anything unreadable leaves the defaults, as Swift's init does.
    /// `writes` false (headless) reads it and keeps every change in memory.
    public Watermark(string? path, bool writes = true) {
        (_path, _writes) = (path, writes);
        try {
            if (path == null || !File.Exists(path) || JsonNode.Parse(File.ReadAllText(path)) is not JsonObject o) return;
            Read(o);
        } catch (Exception e) {   // any shape Swift's decoder would refuse: a missing field, a wrong type, not JSON
            Console.Error.WriteLine($"orion: watermark.json not read ({e.Message})");
        }
    }

    public bool Diagonal => Anchor < 0;
    /// The anchor's place in its row and column: 0 is the left or top edge, 1 the right or bottom.
    public (double X, double Y) Fraction => (Anchor % 3 / 2.0, Anchor / 3 / 2.0);

    /// A mark switched on with no text and no file writes nothing.
    public bool IsDrawable => IsText ? Words.Trim().Length > 0 : Svg.Length > 0;

    /// One line for the Export sheet's row: what the mark is and where it goes.
    public string Summary {
        get {
            if (!IsDrawable) return "Nothing to draw yet";
            string what = IsText ? $"“{Words}”" : SvgName.Length == 0 ? "SVG" : SvgName;
            return $"{what}, {(Diagonal ? "diagonal" : Anchors[Anchor])}";
        }
    }

    public static string? DefaultPath() => AppFolder.File("watermark.json");

    /// Swift's synthesized Codable for `Saved`, enums and all, so either app reads the other's file.
    public JsonObject Encode() => new() {
        ["enabled"] = Enabled,
        ["content"] = IsText
            ? new JsonObject { ["text"] = new JsonObject { ["_0"] = Words, ["fontFamily"] = Family, ["bold"] = Bold } }
            : new JsonObject { ["svg"] = new JsonObject { ["_0"] = Convert.ToBase64String(Svg), ["name"] = SvgName } },
        ["placement"] = Diagonal ? new JsonObject { ["diagonal"] = new JsonObject() }
                                 : new JsonObject { ["anchor"] = new JsonObject { ["_0"] = AnchorKeys[Anchor] } },
        ["size"] = Size, ["span"] = Span, ["margin"] = Margin, ["opacity"] = Opacity,
    };

    void Read(JsonObject o) {
        // Every field is required, as Swift's decoder requires it: a partial file changes nothing.
        bool enabled = o["enabled"]!.GetValue<bool>();
        double size = o["size"]!.GetValue<double>(), span = o["span"]!.GetValue<double>(),
               margin = o["margin"]!.GetValue<double>(), opacity = o["opacity"]!.GetValue<double>();
        var content = (JsonObject)o["content"]!;
        var placement = (JsonObject)o["placement"]!;
        int anchor = placement["diagonal"] != null ? -1
            : Array.IndexOf(AnchorKeys, placement["anchor"]!["_0"]!.GetValue<string>()) is var a and >= 0 ? a
            : throw new FormatException("unknown anchor");
        if (content["text"] is JsonObject text) {
            (Words, Family, Bold, IsText) = (text["_0"]!.GetValue<string>(), text["fontFamily"]!.GetValue<string>(), text["bold"]!.GetValue<bool>(), true);
        } else if (content["svg"] is JsonObject svg) {
            (Svg, SvgName, IsText) = (Convert.FromBase64String(svg["_0"]!.GetValue<string>()), svg["name"]!.GetValue<string>(), false);
        } else throw new FormatException("unknown content");
        (Enabled, Anchor, Size, Span, Margin, Opacity) = (enabled, anchor, size, span, margin, opacity);
    }

    /// Written atomically, with the reason a save failed kept rather than swallowed.
    public bool Save() {
        if (_path == null) {
            LastFailure = "Orion has nowhere to keep the watermark on this machine.";
            return false;
        }
        if (!_writes) return true;
        try {
            AppFolder.WriteAtomically(_path, Encode().ToJsonString());
            LastFailure = null;
            return true;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            LastFailure = "The watermark could not be saved. " + e.Message;
            Console.Error.WriteLine($"orion: watermark could not be saved - {e.Message}");
            return false;
        }
    }
}

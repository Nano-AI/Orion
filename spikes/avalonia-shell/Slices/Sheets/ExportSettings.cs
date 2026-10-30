using System.Globalization;

namespace OrionShell.Sheets;

/// A port of the parts of app/ExportSettings.swift the export sheet reads:
/// the choices with their titles and notes, the output dimensions, and the
/// size estimate that fills the moment before the measured size lands.
/// `Code` is the orion.h value each choice crosses the facade as.
sealed class ExportSettings {
    /// Per session, as the editor's `exportSettings` is: the sheet reopens as it was left.
    public static ExportSettings Shared { get; } = new();

    public sealed record Choice(string Title, int Code, string Note = "");

    public static readonly Choice[] Formats = [new("JPEG", 1), new("PNG", 0), new("TIFF", 2), new("JPEG XL", 3)];

    public static readonly Choice[] Spaces = [
        new("sRGB", 0, "The safe choice. What the web and most screens expect."),
        new("Display P3", 1, "For Apple displays and print. Converted and tagged, though nothing Orion renders yet reaches past sRGB."),
        new("Adobe RGB", 2, "What some print shops ask for. Converted and tagged, though nothing Orion renders yet reaches past sRGB."),
    ];

    public static readonly Choice[] Depths = [
        new("8-bit", 8, "What every browser, viewer and print lab reads."),
        new("16-bit", 16, "Twice the file. Worth it only if the export is going back into an editor for more work."),
    ];

    public static readonly Choice[] Metadatas = [
        new("Keep all", 0, "Camera, lens, exposure, date — and where the photo was taken. Anyone who downloads the file can read it."),
        new("Strip location", 1, "Camera, lens, exposure and date. GPS coordinates and IPTC place names are both removed."),
        new("Strip everything", 2, "Nothing but the star rating and that Orion developed it."),
    ];

    /// Code is the longest edge; 0 is full size, -1 custom.
    public static readonly Choice[] Sizes = [
        new("Full size", 0), new("4096 px", 4096), new("2048 px", 2048), new("1024 px", 1024), new("Custom…", -1),
    ];

    public static readonly Choice[] Sharpenings = [
        new("None", 0, "The pixels as rendered. Right when the export is going into another editor."),
        new("Screen", 1, "A light pass, for a file that will be looked at on a display."),
        new("Print", 2, "A stronger pass. Ink spreads on paper, so a print takes more than a screen does."),
    ];

    // Indexes into the tables above; the defaults are ExportSettings.swift's.
    public int Format, Size, Space, Depth, Sharpening;
    public int Metadata = 1;   // strip location
    public double Quality = 0.9;
    public uint CustomWidth, CustomHeight;
    /// Measured by a real encode, not estimated. null until the first one lands.
    public long? MeasuredBytes;

    int Code => Formats[Format].Code;
    /// JPEG and JPEG XL take a quality; JPEG XL at 100 is lossless (DECISIONS #363).
    public bool IsLossy => Code is 1 or 3;
    public bool IsJxl => Code == 3;
    /// JPEG holds eight bits; the control keeps its value but the file does not.
    public bool CarriesDepth => Code != 1;
    /// The file's extension, for the save panel and the batch plan.
    public string Extension => Code switch { 1 => "jpg", 0 => "png", 3 => "jxl", _ => "tiff" };
    public int EffectiveDepth => CarriesDepth ? Depths[Depth].Code : 8;
    bool IsCustom => Sizes[Size].Code < 0;

    public (uint W, uint H) Dimensions(uint sw, uint sh) {
        if (IsCustom) return (Math.Max(1, CustomWidth), Math.Max(1, CustomHeight));
        uint longest = Math.Max(sw, sh);
        int limit = Sizes[Size].Code;
        if (limit <= 0 || longest <= limit) return (sw, sh);
        double scale = (double)limit / longest;
        return ((uint)Math.Max(1, Math.Round(sw * scale)), (uint)Math.Max(1, Math.Round(sh * scale)));
    }

    /// What the engine caps the longest edge at; 0 keeps the source.
    public uint LongestEdge(uint sw, uint sh) {
        var (w, h) = Dimensions(sw, sh);
        uint longest = Math.Max(w, h);
        return longest >= Math.Max(sw, sh) ? 0 : longest;
    }

    public void SetCustomWidth(uint width, uint sw, uint sh) {
        if (sw == 0 || sh == 0) return;
        CustomWidth = Math.Max(1, width);
        CustomHeight = (uint)Math.Max(1, Math.Round(CustomWidth * (double)sh / sw));
    }

    public void SetCustomHeight(uint height, uint sw, uint sh) {
        if (sw == 0 || sh == 0) return;
        CustomHeight = Math.Max(1, height);
        CustomWidth = (uint)Math.Max(1, Math.Round(CustomHeight * (double)sw / sh));
    }

    /// Rough bytes per pixel. Empirical, replaced by a measurement within a fifth of a second.
    double BytesPerPixel() => Formats[Format].Code switch {
        1 => 0.08 + 0.72 * Math.Pow(Quality, 2.4),
        3 => Quality >= 1 ? (EffectiveDepth == 16 ? 3.0 : 1.5) : 0.05 + 0.6 * Math.Pow(Quality, 3),
        0 => EffectiveDepth == 16 ? 4.2 : 2.1,
        _ => EffectiveDepth == 16 ? 6.0 : 3.0,
    };

    /// The measured size once one has come back, the estimate until then.
    public string SizeText(uint sw, uint sh) {
        var (w, h) = Dimensions(sw, sh);
        long bytes = MeasuredBytes ?? (long)((double)w * h * BytesPerPixel());
        // ByteCountFormatter, .file style, MB and KB only: decimal units.
        return bytes >= 1_000_000
            ? (bytes / 1e6).ToString("0.#", CultureInfo.InvariantCulture) + " MB"
            : (bytes / 1e3).ToString("0", CultureInfo.InvariantCulture) + " KB";
    }

    /// What `Engine.exportedSize` sends: no rating, metadata stripped of location.
    public SheetsNative.ExportOptions Options(uint sw, uint sh) => new() {
        Format = Formats[Format].Code, Quality = (float)Quality, MaxDimension = LongestEdge(sw, sh),
        Space = Spaces[Space].Code, Rating = -1, Metadata = 1, BitDepth = EffectiveDepth,
        Sharpen = Sharpenings[Sharpening].Code,
    };
}

using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace OrionShell;

/// A port of app/CaptureInfo.swift: what the camera recorded, in the spellings
/// a photographer reads. Zero means "the file does not say", and every
/// formatter answers null for it rather than "ISO 0".
public sealed partial record CaptureInfo(string Camera, string Lens, float FocalLength, float Aperture,
                                         float Shutter, float Iso, DateTime? Captured, uint Width, uint Height) {
    public const string Separator = " · ";

    static float Round(float x) => MathF.Round(x, MidpointRounding.AwayFromZero);   // Swift's .rounded()
    static readonly CultureInfo C = CultureInfo.InvariantCulture;

    /// 1/250, 1/3, 0.5 s, 1.3 s, 2 s: a fraction up to a third of a second.
    public static string? ShutterText(float s) {
        if (!(s > 0) || !float.IsFinite(s)) return null;
        if (s < 0.35f) return $"1/{(int)Round(1 / s)}";
        float tenths = Round(s * 10) / 10;
        return tenths == Round(tenths) ? $"{(int)tenths} s" : tenths.ToString("0.0", C) + " s";
    }

    /// f/1.8, f/4.0, f/11.
    public static string? ApertureText(float f) =>
        !(f > 0) || !float.IsFinite(f) ? null : "f/" + f.ToString(f < 9.95f ? "0.0" : "0", C);

    /// 35 mm; a compact's 4.3 mm keeps its decimal.
    public static string? FocalText(float mm) {
        if (!(mm > 0) || !float.IsFinite(mm)) return null;
        float tenths = Round(mm * 10) / 10;
        return (mm < 10 && tenths != Round(tenths) ? tenths.ToString("0.0", C) : mm.ToString("0", C)) + " mm";
    }

    public static string? IsoText(float iso) => !(iso > 0) || !float.IsFinite(iso) ? null : "ISO " + iso.ToString("0", C);

    string?[] ExposureParts(bool focal) => [focal ? FocalText(FocalLength) : null, ApertureText(Aperture), ShutterText(Shutter), IsoText(Iso)];
    static string Join(IEnumerable<string?> parts) => string.Join(Separator, parts.Where(p => !string.IsNullOrEmpty(p)));

    /// Camera first, then the exposure in the order a viewfinder reads it.
    public string Line => Join(ExposureParts(true).Prepend(Camera));
    public string CompactLine => Join(ExposureParts(true));
    public string ExposureLine => Join(ExposureParts(false));

    /// The popover: label and value, a row the file does not record left out.
    public IEnumerable<(string, string)> Rows {
        get {
            (string, string?)[] all = [
                ("Camera", Camera), ("Lens", Lens), ("Focal length", FocalText(FocalLength)),
                ("Aperture", ApertureText(Aperture)), ("Shutter", ShutterText(Shutter)),
                ("ISO", Iso > 0 ? Iso.ToString("0", C) : null),
                ("Captured", Captured is { } d ? $"{d:MMM d, yyyy} at {d:h:mm tt}" : null),
                ("Dimensions", Width > 0 && Height > 0
                    ? $"{Width} × {Height}{Separator}{Width * (double)Height / 1e6:0.0} MP" : null),
            ];
            return all.Where(r => !string.IsNullOrEmpty(r.Item2)).Select(r => (r.Item1, r.Item2!));
        }
    }

    /// The open photo's, or null with nothing open (Engine+Document.swift).
    public static CaptureInfo? Read(nint engine) {
        if (engine == 0 || orion_engine_info(engine, out var c) != 0) return null;
        return new(Text(c.Camera), Text(c.Lens), c.FocalLength, c.Aperture, c.Shutter, c.Iso,
                   c.Timestamp > 0 ? DateTimeOffset.FromUnixTimeSeconds(c.Timestamp).LocalDateTime : null,
                   c.Width, c.Height);
    }

    static string Text(ReadOnlySpan<byte> s) {
        int end = s.IndexOf((byte)0);
        return Encoding.UTF8.GetString(end < 0 ? s : s[..end]).Trim();
    }

    [InlineArray(128)] struct Chars128 { byte _first; }

    /// OrionRawInfo in orion.h, field for field.
    [StructLayout(LayoutKind.Sequential)]
    struct OrionRawInfo {
        public uint Width, Height;
        public Chars128 Camera, Lens;
        public float Iso, Shutter, Aperture, FocalLength;
        public long Timestamp;
    }

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_info(nint engine, out OrionRawInfo info);
}

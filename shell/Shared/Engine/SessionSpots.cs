using System.Runtime.InteropServices;
using System.Text.Json.Nodes;

namespace OrionShell;

/// The photo's spots (Engine+Spots.swift, research/spot-removal.md), in frame
/// coordinates: what the engine heals and what the sidecar's `spots` holds.
public sealed partial class Session {
    /// OrionSpot (orion.h), field for field.
    [StructLayout(LayoutKind.Sequential)]
    public record struct Spot(float DestX, float DestY, float SrcX, float SrcY, float Radius, float Feather, int Heal);

    public unsafe IReadOnlyList<Spot> Spots() {
        var spots = new Spot[MaxSpots];
        int n;
        fixed (Spot* p = spots) n = Native.shim_spots(Shim, p, MaxSpots);
        return spots[..Math.Min(n, MaxSpots)];
    }

    /// Writes them all, at most ORION_MAX_SPOTS (Engine.assign's cap). Does not render.
    public unsafe void SetSpots(IReadOnlyList<Spot> spots) {
        var held = spots.Take(MaxSpots).ToArray();
        fixed (Spot* p = held)
            if (Native.shim_set_spots(Shim, p, held.Length) == 0) throw new InvalidOperationException("the engine refused the spots");
    }

    /// DevelopState's SpotState, both ways: destX…feather numbers, heal a bool.
    static JsonArray EncodeSpots(IEnumerable<Spot> spots) => new(spots.Select(s => (JsonNode)new JsonObject {
        ["destX"] = s.DestX, ["destY"] = s.DestY, ["srcX"] = s.SrcX, ["srcY"] = s.SrcY,
        ["radius"] = s.Radius, ["feather"] = s.Feather, ["heal"] = s.Heal != 0,
    }).ToArray());

    /// DevelopJson.Decode has already held every entry to SpotState's keys and types.
    static List<Spot> DecodeSpots(JsonNode? node) => (node as JsonArray ?? []).Select(n => {
        float F(string k) => DevelopJson.Float(n![k]) ?? 0;
        return new Spot(F("destX"), F("destY"), F("srcX"), F("srcY"), F("radius"), F("feather"), n!["heal"]?.GetValue<bool>() == true ? 1 : 0);
    }).ToList();
}

public static unsafe partial class Native {
    [LibraryImport(Lib)]
    public static partial int shim_set_spots(nint shim, Session.Spot* spots, int count);

    [LibraryImport(Lib)]
    public static partial int shim_spots(nint shim, Session.Spot* spots, int capacity);
}

using System.Runtime.InteropServices;
using Avalonia;

namespace OrionShell.Develop;

/// Lightroom's white balance menu and selector: As shot, Auto (grey world), the
/// engine's six fixed presets, and a click that makes what it lands on neutral.
/// Each is one write, one render and one undo step. research/white-balance.md.
static class WhiteBalanceTools {
    /// The selector's patch: this fraction of the frame's width either side of the click.
    public const float Radius = 0.004f;

    public sealed record Preset(string Name, float TemperatureK, float Tint);

    /// orion_white_balance_preset, in Lightroom's order.
    public static readonly IReadOnlyList<Preset> Fixed = ReadPresets();

    /// The menu: As shot, Auto, the fixed presets, and Custom for whatever the sliders or a pick made.
    public static readonly IReadOnlyList<string> Choices = ["As shot", "Auto", .. Fixed.Select(p => p.Name), "Custom"];

    static unsafe List<Preset> ReadPresets() {
        var list = new List<Preset>();
        byte* name = stackalloc byte[32];
        for (int i = 0; Native.orion_white_balance_preset(i, name, 32, out float k, out float t) == 0; i++)
            list.Add(new(Marshal.PtrToStringUTF8((nint)name)!, k, t));
        return list;
    }

    static EngineModel E => EngineModel.Shared;

    public static void Pick(Point uv) => Run("White balance", s => s.WhiteBalanceAt(uv, Radius));

    /// A menu row by its index in Choices; Custom changes nothing.
    public static void Choose(int choice) {
        if (choice == 0) Run("As shot", s => Put(s, s.AsShot()));
        else if (choice == 1) Run("Auto white balance", s => s.WhiteBalanceAt(null, Radius));
        else if (choice >= 2 && choice - 2 < Fixed.Count) {
            var p = Fixed[choice - 2];
            Run(p.Name, s => Put(s, (p.TemperatureK, p.Tint)));
        }
    }

    /// Which row of Choices the photo's white balance is: a preset only when it is exactly that preset.
    public static int Current() {
        if (E.Session is not { } s) return 0;
        (float k, float t) = (s.Get("temperature_k"), s.Get("tint"));
        if ((k, t) == s.AsShot()) return 0;
        int i = Fixed.ToList().FindIndex(p => p.TemperatureK == k && p.Tint == t);
        return i >= 0 ? i + 2 : Choices.Count - 1;
    }

    static void Put(Session s, (float TemperatureK, float Tint) wb) {
        s.Set("temperature_k", wb.TemperatureK);
        s.Set("tint", wb.Tint);
    }

    static void Run(string label, Action<Session> write) {
        EditHistory.Label(label);
        // The Temperature slider's range: the locus search reaches past it.
        if (!E.Apply(s => { write(s); s.Set("temperature_k", Math.Clamp(s.Get("temperature_k"), 2000, 12000)); }, reread: true))
            EditHistory.Label(null);   // nothing rendered: the name must not land on the next edit
    }
}

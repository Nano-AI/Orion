using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;
using OrionShell.Develop;

namespace OrionShell;

/// The Light tab's white balance: the menu's presets and W's selector, by pointer and key.
static partial class SelfTest {
    static partial void WhiteBalanceChecks() {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) { Check(false, "the white balance checks need an open photo"); return; }
        engine.Open(engine.Current);   // a clean history
        Arrived(engine);
        var window = Shell();
        Click(window, TabButton(window, "Light"));
        var host = window.FindControl<StackPanel>("ToolsHost")!;
        var menu = host.GetVisualDescendants().OfType<ComboBox>().First(c => c.Items.Contains("Daylight"));
        (float K, float Tint) WhiteBalance() => (engine.Get("temperature_k"), engine.Get("tint"));
        var asShot = engine.Session!.AsShot();

        // Lightroom's list. Each preset by its command, its value, and the menu naming it.
        var lightroom = new Dictionary<string, float> {
            ["Daylight"] = 5500, ["Cloudy"] = 6500, ["Shade"] = 7500, ["Tungsten"] = 2850, ["Fluorescent"] = 3800, ["Flash"] = 5500 };
        List<string> wrong = [];
        foreach (var p in WhiteBalanceTools.Fixed) {
            Commands.Run("white-balance-" + p.Name.ToLowerInvariant());
            Settle(window);
            if (WhiteBalance() != (lightroom.GetValueOrDefault(p.Name), p.Tint) || menu.SelectedItem as string != p.Name) wrong.Add(p.Name);
        }
        Commands.Run("white-balance-as-shot");
        Settle(window);
        Check(wrong.Count == 0 && WhiteBalanceTools.Fixed.Count == lightroom.Count && WhiteBalance() == asShot
              && menu.SelectedItem as string == "As shot",
              "each white balance preset sets Lightroom's temperature and the menu names it, and As shot puts the camera's back"
              + (wrong.Count > 0 ? $"; wrong: {string.Join(", ", wrong)}" : ""));

        // The selector, on the least coloured patch the photograph has that is neither dark nor blown,
        // under a cast it has to undo.
        Point? target = null;
        double least = double.MaxValue;
        for (double y = 0.1; y < 0.95; y += 0.05)
            for (double x = 0.05; x < 0.97; x += 0.03)
                if (Patch(new Point(x, y)) is { } c && c.Lit && c.Chroma < least) (least, target) = (c.Chroma, new Point(x, y));
        engine.Apply(s => { s.Set("temperature_k", asShot.TemperatureK + 1500); s.Set("tint", Math.Clamp(asShot.Tint + 0.3f, -1, 1)); });
        Settle(window);
        var cast = WhiteBalance();
        double before = target is { } t0 ? Patch(t0)?.Chroma ?? 1 : 1;
        Type(window, Key.W, PhysicalKey.W);
        bool armed = engine.Tool == CanvasTool.WhiteBalance;
        var canvas = window.GetVisualDescendants().OfType<CanvasPanel>().First();
        if (target is { } t) Click(window, canvas, CanvasState.Map(canvas.Bounds.Size).ToView(t));
        double after = target is { } t1 ? Patch(t1)?.Chroma ?? 1 : 1;
        string? label = EditHistory.UndoLabel;
        Check(target != null && armed && engine.Tool == CanvasTool.None && after < 0.02 && before > after && label == "White balance",
              $"W and a click make the patch neutral and disarm ({before:0.000} → {after:0.000} chroma at {target}, " +
              $"{cast.K:0} K → {engine.Get("temperature_k"):0} K, undo \"{label}\")");
        Commands.Run("undo");
        Check(WhiteBalance() == cast && menu.SelectedItem as string == "Custom", "one undo takes the pick back to the cast, and the menu says Custom");

        Commands.Run("white-balance-auto");
        Settle(window);
        Check(WhiteBalance() != cast && EditHistory.UndoLabel == "Auto white balance",
              $"Auto balances the frame to grey on average ({engine.Get("temperature_k"):0} K, tint {engine.Get("tint"):0.000})");
        Commands.Run("reset-adjustments");
        window.Close();
    }

    /// The scene colour's (max - min) / max over a 5x5 grid across the selector's patch, and whether
    /// the rendered patch is neither dark nor blown, so a balance can be read on it.
    static (double Chroma, bool Lit)? Patch(Point uv) {
        var size = CanvasNative.ImageSize;
        double ru = WhiteBalanceTools.Radius / 2, rv = ru * size.Width / Math.Max(size.Height, 1);
        double[] scene = new double[3];
        double lo = 1, hi = 0;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                if (CanvasNative.Sample(new Point(uv.X + dx * ru, uv.Y + dy * rv)) is not { } s) return null;
                for (int c = 0; c < 3; c++) scene[c] += s.Scene[c];
                (lo, hi) = (Math.Min(lo, s.Display.Min()), Math.Max(hi, s.Display.Max()));
            }
        double peak = scene.Max();
        return peak <= 0 ? null : ((peak - scene.Min()) / peak, lo > 0.1 && hi < 0.9);
    }
}

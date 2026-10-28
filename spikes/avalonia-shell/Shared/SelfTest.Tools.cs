using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;

namespace OrionShell;

/// Round 2, u3 tools: the crop flow, the presets and the controls the audit
/// found dead, each driven the way a person drives it: a real pointer and real
/// keys through the headless window, so focus and routing are the window's own.
static partial class SelfTest {
    static partial void ToolsChecks() {
        var engine = EngineModel.Shared;
        Arrived(engine);   // an earlier check may have left a background decode in flight
        if (engine.Handle == 0) { Check(false, "the tools checks need an open photo"); return; }
        engine.Open(engine.Current);   // a clean history
        Arrived(engine);
        CropChecks(engine);
    }

    static void Arrived(EngineModel engine) {
        for (var sw = System.Diagnostics.Stopwatch.StartNew(); engine.Opening && sw.Elapsed.TotalSeconds < 60; Thread.Sleep(1))
            Dispatcher.UIThread.RunJobs();
    }

    /// The developer's report: "I try to hit enter and it doesn't work." The whole
    /// flow, by pointer and keyboard: drag, Enter, undo, a tab switch, a photo swap.
    static void CropChecks(EngineModel engine) {
        var window = Shell();
        var full = CanvasNative.ImageSize;

        Click(window, TabButton(window, "Crop"));   // the tab button now has focus, as it does for a person
        var overlay = window.GetVisualDescendants().OfType<CropOverlay>().First();
        var r = overlay.CropRect;
        Drag(window, overlay, r.TopLeft, r.TopLeft + new Vector(r.Width * 0.3, r.Height * 0.3));
        bool dragged = CropEdit.Crop.Width < 0.9;
        Type(window, Key.Enter, PhysicalKey.Enter);
        var cropped = CanvasNative.ImageSize;
        var crop = CropEdit.Crop;
        Check(dragged && window.Tab == "Light" && !CropEdit.Previewing && cropped.Width < full.Width * 0.9,
              $"a crop drag and Enter, with the tab button focused, applies the crop ({full} → {cropped}, tab {window.Tab})");

        Commands.Run("undo");
        bool undone = CanvasNative.ImageSize == full;
        Commands.Run("redo");
        Check(undone && CanvasNative.ImageSize == cropped, $"undo takes the crop back and redo puts it back ({Develop.EditHistory.UndoLabel})");

        Click(window, TabButton(window, "Color"));
        Click(window, TabButton(window, "Light"));
        Check(CanvasNative.ImageSize == cropped, "the applied crop survives a tab switch");

        if (engine.Photos.Count >= 2) {
            Commands.Run("next-photo");
            Arrived(engine);
            var other = CanvasNative.ImageSize;
            Commands.Run("previous-photo");
            Arrived(engine);
            Check(other != cropped && CanvasNative.ImageSize == cropped && CropEdit.Crop == crop,
                  $"the applied crop survives a photo swap and return ({other}, back to {CanvasNative.ImageSize})");

            // Swapping while the tool is open: the next photo opens in the crop preview
            // too, and the one left behind does not keep a preview nobody can see.
            Click(window, TabButton(window, "Crop"));
            Commands.Run("next-photo");
            Arrived(engine);
            bool previewing = CropEdit.Previewing;
            Click(window, TabButton(window, "Light"));
            Commands.Run("previous-photo");
            Arrived(engine);
            Check(previewing && !CropEdit.Previewing && CanvasNative.ImageSize == cropped,
                  $"a photo opened on the Crop tab previews, and the one left there comes back cropped ({CanvasNative.ImageSize})");
        }

        // Enter reaches the crop from a focused aspect button too; 1:1 is square in pixels.
        Click(window, TabButton(window, "Crop"));
        Click(window, Labelled(window, "1:1"));
        Type(window, Key.Enter, PhysicalKey.Enter);
        var square = CanvasNative.ImageSize;
        Check(window.Tab == "Light" && Math.Abs(square.Width - square.Height) <= 2, $"1:1 and Enter crop a square ({square})");

        // Rotate turns the frame; straighten by the track; Escape cancels to the whole, upright frame.
        Click(window, TabButton(window, "Crop"));
        Click(window, Labelled(window, "Rotate right"));
        bool turned = (int)engine.Get("rotate_quarters") == 1;
        var angle = window.GetVisualDescendants().OfType<AdjustmentSlider>().First(s => Named(s, "Angle"));
        var track = angle.GetVisualDescendants().OfType<AnalogTrack>().First();
        var mid = new Point(track.Bounds.Width / 2, 9.5);   // on the groove: the track takes a press only where it draws
        Drag(window, track, mid, mid + new Vector(track.Bounds.Width * 0.1, 0));
        bool straightened = engine.Get("straighten_deg") != 0;
        Type(window, Key.Escape, PhysicalKey.Escape);
        Click(window, TabButton(window, "Crop"));
        Click(window, Labelled(window, "Rotate left"));
        Type(window, Key.Enter, PhysicalKey.Enter);
        Check(turned && straightened && engine.Get("straighten_deg") == 0 && (int)engine.Get("rotate_quarters") == 0
              && CanvasNative.ImageSize == full, $"rotate and straighten from the panel, Escape to the whole frame ({CanvasNative.ImageSize})");
        window.Close();
    }

    static bool Named(Control c, string name) => c.GetVisualDescendants().OfType<TextBlock>().Any(t => t.Text == name);

    static StateButton Labelled(Window window, string label) =>
        window.GetVisualDescendants().OfType<StateButton>().First(b => b.Label == label && b.IsEffectivelyVisible);

    /// The window as the app has it: App.Window, which the commands reach the tab through.
    /// --selftest builds no window of its own, so this one stands in for MakeWindow's.
    static MainWindow Shell() {
        var window = new MainWindow();
        typeof(App).GetProperty(nameof(App.Window))!.SetValue(null, window);
        window.Show();
        Settle(window);
        return window;
    }

    static StateButton TabButton(MainWindow window, string tab) =>
        window.FindControl<Grid>("TabBar")!.Children.OfType<StateButton>().First(b => b.Content is TextBlock { Text: var t } && t == tab);

    static void Settle(Window window) {
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        window.UpdateLayout();
    }

    static Point At(Window window, Control c, Point? local = null) =>
        c.TranslatePoint(local ?? new Rect(c.Bounds.Size).Center, window) ?? default;

    /// A left click through the window's input: focus moves as it does under a mouse.
    static void Click(Window window, Control c, Point? local = null) {
        c.BringIntoView();   // the tool column scrolls
        Settle(window);
        var p = At(window, c, local);
        window.MouseMove(p);
        window.MouseDown(p, MouseButton.Left);
        window.MouseUp(p, MouseButton.Left);
        Settle(window);
    }

    static void Drag(Window window, Control c, Point from, Point to) {
        var (a, b) = (At(window, c, from), At(window, c, to));
        window.MouseMove(a);
        window.MouseDown(a, MouseButton.Left);
        for (int i = 1; i <= 4; i++) window.MouseMove(a + (b - a) * i / 4, RawInputModifiers.LeftMouseButton);
        window.MouseUp(b, MouseButton.Left);
        Settle(window);
    }

    /// A key as the keyboard sends it: to whatever has focus, tunnelling from the window.
    static void Type(Window window, Key key, PhysicalKey physical, RawInputModifiers modifiers = RawInputModifiers.None) {
        window.KeyPress(key, modifiers, physical, null);
        window.KeyRelease(key, modifiers, physical, null);
        Settle(window);
    }
}

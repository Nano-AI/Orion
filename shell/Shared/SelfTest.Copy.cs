using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.LogicalTree;
using Avalonia.Media.Imaging;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;
using OrionShell.Chrome;

namespace OrionShell;

/// The canvas's copy where the shared surface does not hold the picture (#386):
/// it shows the render on screen after the surface lets go of it, and a copy that
/// will not begin or fails is no picture held and no endless look again.
static partial class SelfTest {
    static partial void CopyChecks() {
        var engine = EngineModel.Shared;
        if (engine.Session == null) return;   // SelfTest.Run already failed for want of a photo
        var shown = engine.Shown;
        bool onGpu = shown.OnGpu;
        float start = engine.Get("exposure_ev");
        double Whole() => MeanLuma(shown.Bitmap);   // the render on screen, read whole and waited for
        Avalonia.Size Frame() { var r = engine.Session!.ImageSize; return new(r.Width, r.Height); }   // the picture, which the canvas draws a copy into
        // A reduced copy's mean, sampled every 16th pixel, drifts a few levels from the whole
        // render's on a busy picture (3.4 on CI's generated photo); a stale one is off by tens.
        static bool Same(double copy, double whole) => Math.Abs(copy - whole) < Math.Max(3, whole * 0.05);
        try {
            // A surface attached after a copy, edits made through it, then it failed.
            using (var copied = new CopiedPicture()) {
                shown.OnGpu = false;
                engine.Set("exposure_ev", -2);
                double before = MeanLuma(copied.For(shown, Frame(), 400, live: false));
                shown.OnGpu = true;
                for (int i = 1; i <= 4; i++) engine.Set("exposure_ev", -2 + i);
                shown.OnGpu = false;
                double after = MeanLuma(copied.For(shown, Frame(), 400, live: true)), want = Whole();
                Check(after > before + 10 && Same(after, want),
                      $"after the shared surface fails the canvas copies the latest edit, not the one before it ({before:0.0} → {after:0.0}, the render {want:0.0})");
            }

            // A copy that will not begin: nothing held, and the canvas reads the picture whole.
            using (var refused = new CopiedPicture { BeginCopy = (_, _, _) => 0 }) {
                int looks = 0;
                refused.Arrived += () => looks++;
                engine.Set("exposure_ev", 0.5f);
                var got = refused.For(shown, Frame(), 400, live: true);
                Pump(50);
                Check(got == null && !refused.Looking && looks == 0,
                      "a copy that will not begin holds no picture and does not look again");
            }

            // A copy that fails in flight (a refused map, a GPU reset): no stale picture, no endless
            // redraw, and the next copy that works is the render on screen.
            using (var lost = new CopiedPicture()) {
                int looks = 0;
                lost.Arrived += () => looks++;
                engine.Set("exposure_ev", -1);
                bool had = lost.For(shown, Frame(), 400, live: false) != null;
                var take = lost.TakeCopy;
                lost.TakeCopy = (Session _, bool _, ref WriteableBitmap? _) => -1;
                engine.Set("exposure_ev", 1);
                var stale = lost.For(shown, Frame(), 400, live: true);
                Pump(50);
                Check(had && stale == null && !lost.Looking && looks == 0,
                      $"a copy that fails holds no stale picture and does not look again ({looks} looks)");
                lost.TakeCopy = take;
                double back = MeanLuma(lost.For(shown, Frame(), 400, live: false)), want = Whole();
                Check(Same(back, want), $"and the next copy is the render on screen ({back:0.0}, the render {want:0.0})");
            }

            // Past fit a region renders without waiting for the GPU: the canvas draws the copy it
            // holds, where that copy's pixels belong, until the region's lands, then that.
            using (var held = new CopiedPicture()) {
                engine.Set("exposure_ev", -2);
                double whole = MeanLuma(held.For(shown, Frame(), 400, live: false));
                var part = new Avalonia.Rect(0.25, 0.25, 0.25, 0.25);
                engine.View = (part, new Avalonia.PixelSize(1600, (int)(1600 / Frame().AspectRatio)));
                CanvasState.Viewport.ZoomBy(4, new Avalonia.Point(0.5, 0.5));   // past fit, as the canvas's ZoomedPastFit follows it
                engine.Render(preview: false);   // the view's region
                var drawn = held.For(shown, Frame(), 400, live: false);
                Check(shown.Graph == Graph.Region && drawn != null && Same(MeanLuma(drawn), whole) && held.Uv == new Avalonia.Rect(0, 0, 1, 1) && held.Looking,
                      $"a region past fit returns before its copy lands: the canvas draws the whole picture it held, in its place, and looks again ({shown.Graph}, {held.Uv}, looking {held.Looking})");
                for (var sw = System.Diagnostics.Stopwatch.StartNew(); held.Uv != shown.Uv && sw.Elapsed.TotalSeconds < 5; Pump(5))
                    held.For(shown, Frame(), 400, live: false);
                Check(held.Uv == shown.Uv && CopiedPicture.Behind == 0,
                      $"and once it lands, the region's copy, at the region's place, no render behind ({held.Uv}, {CopiedPicture.Behind} behind)");
                CanvasState.Viewport.Reset();
                engine.View = null;
            }
        } finally {
            shown.OnGpu = onGpu;
            engine.Set("exposure_ev", start);
        }
        BatchChecks(engine);
    }

    /// Copy, paste and sync settings as commands, and the toolbar's Batch menu that reaches them,
    /// over two links to the open photo in a temporary folder: Sync writes other photos' sidecars.
    static void BatchChecks(EngineModel engine) {
        var list = engine.Photos.ToList();
        int current = engine.Current;
        string source = engine.Photos[current], dir = Directory.CreateTempSubdirectory("orion-batch-").FullName;
        string a = Path.Combine(dir, "a" + Path.GetExtension(source)), b = Path.Combine(dir, "b" + Path.GetExtension(source));
        var owner = App.Window;
        try {
            Uncopy();
            File.CreateSymbolicLink(a, source);
            File.CreateSymbolicLink(b, source);
            SetAside();
            engine.Load([a, b]);
            OpenAndWait(engine, 0);
            var window = Shell();
            Click(window, TabButton(window, "Presets"));
            bool Noted() => window.GetVisualDescendants().OfType<TextBlock>().Any(t => t.Text?.StartsWith("Settings copied.") == true && t.IsEffectivelyVisible);
            Check(Commands.CanRun("copy-settings") && Commands.Run("paste-settings").EndsWith("not available now")
                  && Commands.Run("sync-settings").EndsWith("not available now") && !Noted(),
                  "copy settings can run on the open photo; paste and sync refuse with nothing copied");

            var bar = window.GetVisualDescendants().OfType<ToolbarPanel>().Single();
            StateButton? Batch() => bar.GetVisualDescendants().OfType<StateButton>().FirstOrDefault(x => x.Label == "Batch" && x.IsEffectivelyVisible);
            var batch = Batch();
            var items = (batch?.Flyout as MenuFlyout)?.Items.OfType<MenuItem>().ToList() ?? [];
            string[] names = ["copy-settings", "paste-settings", "sync-settings", "select-all", "export-all"];
            Check(items.Select(i => i.Tag as string).SequenceEqual(names)
                  && items.All(i => Words(i).FirstOrDefault() == Commands.Find((string)i.Tag!)!.Title
                                    && Words(i).ElementAtOrDefault(1) == Commands.KeyLabel((string)i.Tag!))
                  && ToolTip.GetTip(batch!) is string { Length: > 0 },
                  $"the toolbar's Batch menu lists copy, paste, sync, select all and export all, each with its key ({string.Join(", ", items.Select(i => string.Join(" ", Words(i))))})");
            // Opened with nothing copied, so paste and sync are the ones to grey; Copy settings from it shows the tab's note at once.
            var greyed = new List<object?> { "no menu" };
            bool faint = false;
            if (batch?.Flyout is MenuFlyout menu) {
                Click(window, batch);
                greyed = items.Where(i => i.IsEnabled != Commands.CanRun((string)i.Tag!) || i.IsEnabled == (i.Tag as string is "paste-settings" or "sync-settings"))
                    .Select(i => i.Tag).ToList();
                faint = items.Where(i => !i.IsEnabled).SelectMany(i => ((Control)i.Header!).GetLogicalDescendants().OfType<TextBlock>())
                    .All(t => t.Foreground == Palette.Faint);
                items[0].RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(MenuItem.ClickEvent));
                menu.Hide();
                Settle(window);
            }
            Check(greyed.Count == 0 && faint && Labelled(window, "Paste").IsEnabled && Noted(),
                  $"the menu greys paste and sync until a copy, in a greyed button's ink, and its Copy settings is the Presets tab's copy (out of step: {string.Join(", ", greyed)})");

            // Lightroom's keys, through the window as a hand presses them.
            var cmd = (OperatingSystem.IsMacOS() ? RawInputModifiers.Meta : RawInputModifiers.Control) | RawInputModifiers.Shift;
            engine.Set("exposure_ev", 0.7f);
            Type(window, Key.C, PhysicalKey.C, cmd);
            engine.Set("exposure_ev", -1.3f);
            Type(window, Key.V, PhysicalKey.V, cmd);
            Check(engine.Get("exposure_ev") == 0.7f && Develop.EditHistory.UndoLabel == "Paste",
                  $"copy settings, a change, then paste settings by their keys puts the copied value back ({engine.Get("exposure_ev")} EV, undo {Develop.EditHistory.UndoLabel})");
            // Typed into the assistant's terminal, the same key is the terminal's: here one that never started a process.
            engine.Set("exposure_ev", -1.3f);
            var typed = new KeyEventArgs { Source = new Iciclecreek.Terminal.TerminalControl(), Key = Key.V,
                                           KeyModifiers = (OperatingSystem.IsMacOS() ? KeyModifiers.Meta : KeyModifiers.Control) | KeyModifiers.Shift };
            Commands.Key(typed);
            Check(!typed.Handled && engine.Get("exposure_ev") == -1.3f,
                  $"paste settings' key typed into the assistant's terminal is the terminal's, not a paste onto the photo ({engine.Get("exposure_ev")} EV)");

            string synced = Commands.Run("sync-settings");
            Pump(50);
            Check(synced == "ran sync-settings" && window.OwnedWindows.Any(w => w.Tag as string == "sync-confirmation") && !File.Exists(Sidecar.PathFor(b)),
                  $"sync settings asks before it writes another photo's sidecar ({synced})");
            foreach (var asked in window.OwnedWindows.ToList()) asked.Close();

            // The widest toolbar, Compare on: every word at the window's 1440, and still no overlap at 1100; then the Library face.
            Commands.Run("compare");
            Settle(window);
            string wide = Overflow(bar);
            bool worded = bar.GetVisualDescendants().OfType<StateButton>().Any(x => x.Label == "Compare");
            Check(wide == "" && worded, $"at 1440 wide with Compare on the toolbar fits with every word ({wide})");
            window.Width = 1100;
            Settle(window);
            Shot(window, "toolbar-1100-develop-compare");
            string develop = Overflow(bar);
            Commands.Run("compare");
            Commands.Run("gallery");
            Settle(window);
            Shot(window, "toolbar-1100-library");
            string library = Overflow(bar);
            bool inLibrary = Batch() != null && !Commands.CanRun("copy-settings") && !Commands.CanRun("paste-settings") && Commands.CanRun("sync-settings");
            Sheets.CopiedSettings.Copy();   // re-reads the tab's buttons while the Library is up: greyed there
            Commands.Run("gallery");
            Settle(window);
            Check(Labelled(window, "Copy").IsEnabled && Labelled(window, "Paste").IsEnabled,
                  "back from the Library, the Presets tab's Copy and Paste are live again");
            Check(develop == "" && library == "" && inLibrary,
                  $"at 1100 wide the toolbar's controls fit side by side, Compare on and in the Library, where Batch syncs but does not copy or paste ({develop} | {library})");
        } finally {
            if (App.Window != owner) App.Window?.Close();
            typeof(App).GetProperty(nameof(App.Window))!.SetValue(null, owner);   // a later check's dialog would sit over a closed window
            engine.Load(list);
            OpenAndWait(engine, current);
            Directory.Delete(dir, recursive: true);
            Uncopy();
        }
    }

    /// Nothing copied, as when Orion starts: the copy is one for the whole app, and a check that
    /// presses Copy would otherwise arm the next one's Paste and Sync before it presses its own.
    static void Uncopy() {
        typeof(Sheets.CopiedSettings).GetProperty(nameof(Sheets.CopiedSettings.Look))!.SetValue(null, null);
        typeof(Sheets.CopiedSettings).GetField("_state", BindingFlags.NonPublic | BindingFlags.Static)!.SetValue(null, null);
    }

    /// A menu item's words: its title, then its key.
    static List<string> Words(MenuItem item) => item.Header switch {
        string s => [s],
        Control c => c.GetLogicalDescendants().OfType<TextBlock>().Select(t => t.Text ?? "").ToList(),
        _ => [],
    };

    /// Empty when every button and switch in the toolbar has its full width, inside it, overlapping none; else what does not.
    static string Overflow(ToolbarPanel bar) {
        var controls = bar.GetVisualDescendants().OfType<Control>()
            .Where(c => (c is Segmented || c is StateButton && c.FindAncestorOfType<Segmented>() == null) && c.IsEffectivelyVisible)
            .Select(c => (c, At: new Rect(c.TranslatePoint(default, bar)!.Value, c.Bounds.Size)))
            .OrderBy(p => p.At.X).ToList();
        var wrong = controls.Where(p => p.At.X < -0.5 || p.At.Right > bar.Bounds.Width + 0.5
                                        || p.c.Bounds.Width < p.c.DesiredSize.Width - p.c.Margin.Left - p.c.Margin.Right - 0.5)
            .Select(p => $"{Name(p.c)} at {p.At.X:0}-{p.At.Right:0} of {bar.Bounds.Width:0}")
            .Concat(controls.Zip(controls.Skip(1)).Where(p => p.Second.At.X < p.First.At.Right - 0.5)
                .Select(p => $"{Name(p.First.c)} over {Name(p.Second.c)} ({p.First.At.Right:0} > {p.Second.At.X:0})"));
        return string.Join("; ", wrong);
        static string Name(Control c) => c is StateButton s ? s.Label ?? ToolTip.GetTip(s) as string ?? "glyph" : c.GetType().Name;
    }
}

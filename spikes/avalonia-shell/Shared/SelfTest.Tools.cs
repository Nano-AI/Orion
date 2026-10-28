using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;
using OrionShell.Develop;

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
        engine.Open(engine.Current);
        Arrived(engine);
        PresetChecks(engine);
        engine.Open(engine.Current);
        Arrived(engine);
        SliderSweep(engine);
        FixedControlChecks(engine);
        LensChecks(engine);
    }

    /// The developer: the lens correction "was literally working in the original run of orion".
    /// The bundled database has no 24mm F1.4 DG DN, here or in Swift; what worked was choosing
    /// a lens by hand, so the picker is driven as a person drives it: typed into, then clicked.
    static void LensChecks(EngineModel engine) {
        var window = Shell();
        Click(window, TabButton(window, "Optics"));
        var query = window.GetVisualDescendants().OfType<TextBox>().First(t => t.PlaceholderText == "Find a lens…");
        query.Text = "sigma 24mm 1.4 hsm";
        Dispatcher.UIThread.RunJobs();
        var pick = window.GetVisualDescendants().OfType<StateButton>().FirstOrDefault(b => b.Label?.StartsWith("Sigma 24mm F1.4 DG HSM") == true);
        if (pick != null) Click(window, pick);
        var (name, _) = DevelopFacade.LensProfile();
        Check(pick != null && name.Contains("24mm F1.4 DG HSM") && engine.Get("lens_profile") == 1,
              $"typing in Find a lens… lists the Sigma, and a click applies its profile ({DevelopFacade.LensCatalogue.Value.Length} lenses, " +
              $"chose {pick?.Label ?? "nothing"}, profile \"{name}\")");
        DevelopFacade.orion_engine_set_lens_choice(engine.Handle, "");
        window.Close();
    }

    /// The audit's sweep: every enabled slider on the five develop tabs, dragged by the
    /// pointer, writes its field. ORION_AUDIT=1 prints the ones whose render did not move,
    /// which is by design for a control that waits on another (Smoothness on Color).
    static void SliderSweep(EngineModel engine) {
        var window = Shell();
        var host = window.FindControl<StackPanel>("ToolsHost")!;
        List<string> dead = [], still = [], fine = [];
        foreach (var tab in new[] { "Light", "Color", "Detail", "Optics", "Crop" }) {
            Click(window, TabButton(window, tab));
            foreach (var slider in host.GetVisualDescendants().OfType<AdjustmentSlider>()
                         .Where(s => s.IsEffectivelyVisible && s.IsEffectivelyEnabled).ToList()) {
                var track = slider.GetVisualDescendants().OfType<AnalogTrack>().First();
                track.BringIntoView();
                Settle(window);
                string name = $"{tab}/{slider.GetVisualDescendants().OfType<TextBlock>().Last().Text}";
                float before = slider.Value;
                long picture = Fingerprint(engine.Picture);
                double toward = before - track.Min < track.Max - before ? 1 : -1;
                var on = new Point(track.Bounds.Width / 2, 9.5);   // the groove: the track takes a press only where it draws
                Drag(window, track, on, on + new Vector(toward * track.Bounds.Width * 0.15, 0));
                (slider.Value == before ? dead : Fingerprint(engine.Picture) == picture ? still : fine).Add(name);
                if (slider.Value != before) Commands.Run("undo");
            }
        }
        Click(window, TabButton(window, "Light"));
        window.Close();
        Check(dead.Count == 0 && fine.Count >= 30,
              $"every enabled slider on the develop tabs writes its field ({fine.Count} move the render, {still.Count} wait on another control)"
              + (dead.Count > 0 ? $"; dead: {string.Join(", ", dead)}" : ""));
        if (Environment.GetEnvironmentVariable("ORION_AUDIT") != null)
            Console.WriteLine($"audit: render moved: {string.Join(", ", fine)}\naudit: render still: {string.Join(", ", still)}\naudit: dead: {string.Join(", ", dead)}");
    }

    /// A cheap fingerprint of the picture: every 16th pixel's bytes.
    static unsafe long Fingerprint(Avalonia.Media.Imaging.Bitmap? picture) {
        if (picture is not Avalonia.Media.Imaging.WriteableBitmap bitmap) return 0;
        using var fb = bitmap.Lock();
        long hash = fb.Size.Width * 31 + fb.Size.Height;
        for (int y = 0; y < fb.Size.Height; y += 16) {
            byte* row = (byte*)fb.Address + (long)y * fb.RowBytes;
            for (int x = 0; x < fb.Size.Width * 4; x += 64) hash = hash * 1_000_003 + row[x] + (row[x + 1] << 8) + (row[x + 2] << 16);
        }
        return hash;
    }

    /// The controls the audit found dead or broken in u3's files, one check each, by pointer and key.
    static void FixedControlChecks(EngineModel engine) {
        var window = Shell();

        // Detail's spot controls follow the overlay's spots: count, Undo spot, Clear, Heal/Clone, Size.
        Click(window, TabButton(window, "Detail"));
        var fd = CanvasNative.DisplayMap();
        SpotModel.Add(new Point(0.3, 0.4), fd);
        SpotModel.Add(new Point(0.6, 0.5), fd);
        Settle(window);
        var undoSpot = Labelled(window, "Undo spot");
        bool counted = undoSpot.IsEnabled && window.GetVisualDescendants().OfType<TextBlock>().Any(t => t.Text == "2");
        Click(window, undoSpot);
        bool undone = SpotModel.Spots.Count == 1;
        Click(window, Labelled(window, "Clear"));
        Check(counted && undone && SpotModel.Spots.Count == 0 && !Labelled(window, "Clear").IsEnabled,
              "the spot count, Undo spot and Clear follow the overlay's spots");
        Click(window, Labelled(window, "Clone"));
        bool clone = !SpotModel.Heal;
        Click(window, Labelled(window, "Heal"));
        double radius = SpotModel.Radius;
        var size = window.GetVisualDescendants().OfType<AnalogTrack>().First(t => t.Max == 0.12f);
        var groove = new Point(size.Bounds.Width / 2, 9.5);
        Drag(window, size, groove, groove + new Vector(size.Bounds.Width * 0.2, 0));
        Check(clone && SpotModel.Heal && SpotModel.Radius > radius, $"Heal/Clone and Size set the next spot ({radius:0.000} → {SpotModel.Radius:0.000})");
        SpotModel.Radius = radius;

        // A LUT survives a photo swap in its kept session, so the panel names it and offers Remove.
        if (engine.Photos.Count >= 2) {
            string cube = Path.Combine(Path.GetTempPath(), $"orion-selftest-{Environment.ProcessId}.cube");
            File.WriteAllLines(cube, ["TITLE \"Inverse\"", "LUT_3D_SIZE 2",
                .. Enumerable.Range(0, 8).Select(i => $"{1 - (i & 1)} {1 - ((i >> 1) & 1)} {1 - ((i >> 2) & 1)}")]);
            try {
                engine.Render(preview: false);
                long plain = Fingerprint(engine.Picture);
                int loaded = DevelopFacade.orion_engine_load_lut(engine.Handle, cube);
                engine.Render(preview: false);
                Commands.Run("next-photo");
                Arrived(engine);
                Commands.Run("previous-photo");
                Arrived(engine);
                Settle(window);
                var named = window.GetVisualDescendants().OfType<StateButton>().FirstOrDefault(b => b.Label == "Inverse" && b.IsEffectivelyVisible);
                var remove = named == null ? null : Labelled(window, "Remove");
                if (remove != null) Click(window, remove);
                Check(loaded == 0 && remove != null && Fingerprint(engine.Picture) == plain && !remove.IsEffectivelyVisible,
                      "a LUT kept with its photo is named on return, and Remove takes it off");
            } finally {
                DevelopFacade.orion_engine_clear_lut(engine.Handle);
                File.Delete(cube);
            }
        }

        // Color's band swatches are buttons: the keyboard picks a band.
        Click(window, TabButton(window, "Color"));
        var red = window.GetVisualDescendants().OfType<StateButton>().First(b => ToolTip.GetTip(b) as string == TargetedAdjust.Name(HueBand.Red));
        red.Focus(NavigationMethod.Tab);
        Type(window, Key.Space, PhysicalKey.Space);
        Check(window.GetVisualDescendants().OfType<TextBlock>().Any(t => t.Text == "Red" && t.IsEffectivelyVisible) && red.IsFocused,
              "Tab and Space pick a mixer band, and the focus stays on it");

        // A crop drag whose pointer capture is taken mid-way (here the overlay leaves the tree)
        // still ends: a full render, not the preview graph until the next drag anywhere.
        Click(window, TabButton(window, "Crop"));
        var overlay = window.GetVisualDescendants().OfType<CropOverlay>().First();
        var middle = At(window, overlay, overlay.CropRect.Center);
        window.MouseMove(middle);
        window.MouseDown(middle, MouseButton.Left);
        window.MouseMove(middle + new Vector(20, 10), RawInputModifiers.LeftMouseButton);
        Settle(window);
        bool held = engine.Interacting;
        var canvas = (Panel)overlay.Parent!;
        int slot = canvas.Children.IndexOf(overlay);
        canvas.Children.Remove(overlay);
        Settle(window);
        bool ended = !engine.Interacting && engine.Shown.Graph == Graph.Full;
        canvas.Children.Insert(slot, overlay);
        window.MouseUp(middle, MouseButton.Left);
        Type(window, Key.Escape, PhysicalKey.Escape);
        Check(held && ended, "a crop drag that loses its pointer capture still ends with a full render");

        // A stub says so where a person looks, not on stderr.
        Click(window, TabButton(window, "Presets"));
        Click(window, Labelled(window, "Copy"));
        Click(window, Labelled(window, "Sync all…"));
        Check(engine.Status.StartsWith("Syncing") && engine.Status.EndsWith("isn't in this version yet."),
              $"Sync all… says it is not built, on the status line ('{engine.Status}')");
        Click(window, TabButton(window, "Light"));
        window.Close();
    }

    /// The developer's report: "I can't even click the monochrome preset and load it."
    static void PresetChecks(EngineModel engine) {
        var window = Shell();
        Click(window, TabButton(window, "Presets"));
        StateButton Row(string name) =>
            window.GetVisualDescendants().OfType<StateButton>().First(b => b.Tag is Sheets.Preset p && p.Name == name);
        var builtIns = Sheets.PresetStore.All.Where(p => p.BuiltIn).Select(p => p.Name).ToList();

        // A curve first, so the preset has one to take away and undo one to give back.
        List<(float X, float Y)> bent = [(0, 0), (0.5f, 0.62f), (1, 1)];
        engine.Apply(s => s.SetCurve(0, bent));
        double colour = Chroma(engine.Picture);
        Click(window, Row("Monochrome"));
        double grey = Chroma(engine.Picture);
        bool chosen = Row("Monochrome").On && builtIns.Where(n => n != "Monochrome").All(n => !Row(n).On);
        Check(grey < 2 && colour > grey + 5 && chosen && engine.Get("saturation") == -1 && engine.Session!.Curve(0).Count == 2,
              $"a click on Monochrome makes the render grey and shows it chosen (chroma {colour:0.0} → {grey:0.0})");
        Commands.Run("undo");
        Check(Chroma(engine.Picture) > colour - 1 && !Row("Monochrome").On && engine.Session!.Curve(0).SequenceEqual(bent),
              $"undo takes the preset back in one step, curve included ({Develop.EditHistory.RedoLabel})");

        // Every built-in applies on its click; the keyboard reaches them too.
        var missed = builtIns.Where(n => {
            Click(window, Row(n));
            bool on = Row(n).On && Sheets.PresetStore.InEffect(Sheets.PresetStore.All.First(p => p.Name == n));
            Commands.Run("undo");
            return !on;
        }).ToList();
        Check(missed.Count == 0, "every built-in preset applies on click" + (missed.Count > 0 ? $"; not {string.Join(", ", missed)}" : ""));
        Row("Punch").Focus(NavigationMethod.Tab);
        Type(window, Key.Space, PhysicalKey.Space);
        Check(Row("Punch").On, "Tab and Space apply a preset");
        Commands.Run("undo");

        // Copy, then Paste onto the photo with the ticked groups.
        Click(window, Row("Soft"));
        Click(window, Labelled(window, "Copy"));
        Commands.Run("undo");
        bool gone = !Row("Soft").On;
        Click(window, Labelled(window, "Paste"));
        Check(gone && Row("Soft").On && Develop.EditHistory.UndoLabel == "Paste", $"Copy and Paste carry a look ({Develop.EditHistory.UndoLabel})");
        Commands.Run("undo");

        // Save writes a preset of the photo's look; it applies; delete removes it; the file has it meanwhile.
        string saved = Sheets.PresetStore.File;
        Sheets.PresetStore.File = Path.Combine(Directory.CreateTempSubdirectory("orion-presets-").FullName, "presets.json");
        try {
            engine.Set("exposure_ev", engine.Get("exposure_ev") + 0.8f);
            var field = window.GetVisualDescendants().OfType<TextBox>().First(t => t.PlaceholderText == "New preset");
            field.Text = "Bright";
            Click(window, ((Panel)field.Parent!).Children.OfType<StateButton>().First());   // its own Save, not the versions'
            bool written = File.Exists(Sheets.PresetStore.File) && File.ReadAllText(Sheets.PresetStore.File).Contains("Bright");
            Commands.Run("reset-adjustments");
            Click(window, Row("Bright"));
            bool applied = Row("Bright").On;
            Click(window, window.GetVisualDescendants().OfType<StateButton>().First(b => ToolTip.GetTip(b) as string == "Delete Bright"));
            Check(written && applied && !Sheets.PresetStore.All.Any(p => p.Name == "Bright") && field.Text == "",
                  "Save keeps the look in the presets file, it applies, and delete removes it");
            Commands.Run("reset-adjustments");
        } finally {
            Directory.Delete(Path.GetDirectoryName(Sheets.PresetStore.File)!, true);
            Sheets.PresetStore.File = saved;
        }
        Check(Labelled(window, "Export all…").IsEnabled == Commands.CanRun("export-all"), "Export all… follows its command's guard");
        window.Close();
    }

    /// Mean of max − min over the channels, every sixteenth pixel: 0 is grey.
    static unsafe double Chroma(Avalonia.Media.Imaging.Bitmap? picture) {
        if (picture is not Avalonia.Media.Imaging.WriteableBitmap bitmap) return 0;
        using var fb = bitmap.Lock();
        double sum = 0;
        long n = 0;
        for (int y = 0; y < fb.Size.Height; y += 16) {
            byte* row = (byte*)fb.Address + (long)y * fb.RowBytes;
            for (int x = 0; x < fb.Size.Width; x += 16, n++)
                sum += Math.Max(row[x * 4], Math.Max(row[x * 4 + 1], row[x * 4 + 2])) - Math.Min(row[x * 4], Math.Min(row[x * 4 + 1], row[x * 4 + 2]));
        }
        return n == 0 ? 0 : sum / n;
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
              $"a crop drag and Enter, with the tab button focused, applies the crop ({full} → {cropped}, tab {window.Tab}, dragged {dragged}, canvas tab {CanvasState.Tab})");

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

        // An edge moves its one side; hovering a grip says so with the pointer.
        Click(window, TabButton(window, "Crop"));
        var before = CropEdit.Crop;
        r = overlay.CropRect;
        window.MouseMove(At(window, overlay, r.TopLeft));
        bool pointed = overlay.Cursor != null;
        Drag(window, overlay, new Point(r.Right, r.Center.Y), new Point(r.Right - r.Width * 0.2, r.Center.Y + 30));
        var after = CropEdit.Crop;
        Check(pointed && after.Width < before.Width - 0.1 && after.X == before.X && after.Y == before.Y && after.Height == before.Height,
              $"the right edge moves the right side only, and the pointer names the grip ({before} → {after})");
        Type(window, Key.Escape, PhysicalKey.Escape);

        // Enter reaches the crop from a focused aspect button too; 1:1 is square in pixels.
        Click(window, TabButton(window, "Crop"));
        Click(window, Labelled(window, "1:1"));
        Type(window, Key.Enter, PhysicalKey.Enter);
        var square = CanvasNative.ImageSize;
        Check(window.Tab == "Light" && Math.Abs(square.Width - square.Height) <= 2, $"1:1 and Enter crop a square ({square})");

        // And from a slider: the Angle readout, reached by Tab, takes Return to start typing anywhere but here.
        Click(window, TabButton(window, "Crop"));
        window.GetVisualDescendants().OfType<AdjustmentSlider>().First(s => Named(s, "Angle"))
              .GetVisualDescendants().OfType<Decorator>().First(d => d.Focusable).Focus(NavigationMethod.Tab);
        Type(window, Key.Enter, PhysicalKey.Enter);
        Check(window.Tab == "Light", "Enter with a slider focused applies the crop");

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
              && CanvasNative.ImageSize == full && angle.Value == 0,
              $"rotate and straighten from the panel, Escape to the whole frame, the Angle slider with it ({CanvasNative.ImageSize}, {angle.Value}°)");
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
        window.FindControl<Grid>("TabBar")!.Children.OfType<StateButton>().First(b => b.Label == tab);

    /// Layout, the queue, and a frame: a button's release hit-tests the last rendered
    /// scene, so a click before the first frame of a new layout focused the button and
    /// never clicked it (about one run in six, before the frame was forced here).
    static void Settle(Window window) {
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        window.UpdateLayout();
        AvaloniaHeadlessPlatform.ForceRenderTimerTick();
        Dispatcher.UIThread.RunJobs();
    }

    static Point At(Window window, Control c, Point? local = null) =>
        c.TranslatePoint(local ?? new Rect(c.Bounds.Size).Center, window) ?? default;

    /// A left click through the window's input: focus moves as it does under a mouse.
    static void Click(Window window, Control c, Point? local = null) {
        c.BringIntoView();   // the tool column scrolls
        Settle(window);
        var p = At(window, c, local);
        window.MouseMove(p);
        Settle(window);   // each step against a current frame: the release hit-tests the last one
        window.MouseDown(p, MouseButton.Left);
        Settle(window);
        window.MouseUp(p, MouseButton.Left);
        Settle(window);
    }

    static void Drag(Window window, Control c, Point from, Point to) {
        var (a, b) = (At(window, c, from), At(window, c, to));
        window.MouseMove(a);
        Settle(window);
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

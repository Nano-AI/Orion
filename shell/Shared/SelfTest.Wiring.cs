using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;
using OrionShell.Chrome;
using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Sheets;

namespace OrionShell;

/// Stage 1c (#315): the commands the toolbar and keys reach, compare's one
/// state, the gallery, undo, the trash's confirmation, and the drag picture.
static partial class SelfTest {
    static partial void WiringChecks() {
        Check(Application.Current!.TryGetResource("SystemAccentColor", ThemeVariant.Dark, out var accent)
              && accent is Color c && c == ((ISolidColorBrush)Palette.Accent).Color, "Fluent's SystemAccentColor is Palette.Accent");
        Check(GalleryMode.Moved(0, GalleryMode.Direction.Right, 3, 5) == 1 && GalleryMode.Moved(3, GalleryMode.Direction.Up, 3, 5) == 0
              && GalleryMode.Moved(2, GalleryMode.Direction.Down, 3, 5) == 4 && GalleryMode.Moved(4, GalleryMode.Direction.Down, 3, 5) == 4
              && GalleryMode.Moved(0, GalleryMode.Direction.Left, 3, 5) == 0, "gallery arrows follow GalleryLayout.move");
        var plan = ExportRun.Plan(["/a/x.arw", "/b/x.arw"], "/out", "jpg", p => p == Path.Combine("/out", "x.jpg"));
        Check(plan.Select(j => j.Destination).SequenceEqual([Path.Combine("/out", "x-2.jpg"), Path.Combine("/out", "x-3.jpg")]), "batch names never overwrite (BatchExport.plan)");
        if (!OperatingSystem.IsLinux())
            Check(ExportRun.Plan(["/a/X.arw", "/b/x.arw"], "/out", "jpg", _ => false).Select(j => j.Destination)
                      .SequenceEqual([Path.Combine("/out", "X.jpg"), Path.Combine("/out", "x-2.jpg")]),
                  "names that differ only in case do not overwrite where the disk ignores case");

        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        UndoChecks(engine);
        CompareChecks(engine);
        DragPictureChecks(engine);
        GalleryChecks(engine);
        TrashChecks(engine);
        ExportChecks(engine);
        CommandSweep();
    }

    static void UndoChecks(EngineModel engine) {
        engine.Open(engine.Current);   // a clean history: earlier checks' settled renders are steps too
        float ev = engine.Get("exposure_ev"), contrast = engine.Get("contrast");
        engine.Set("exposure_ev", ev + 1);
        engine.Set("exposure_ev", ev + 1.5f);   // the same control inside 0.6 s: one step
        Check(EditHistory.CanUndo && EditHistory.UndoLabel == "exposure_ev", $"an edit is one undo step named for its field ('{EditHistory.UndoLabel}')");
        engine.Set("contrast", contrast + 0.2f);
        Commands.Run("undo");
        Check(engine.Get("contrast") == contrast && engine.Get("exposure_ev") == ev + 1.5f, "undo restores the last control only");
        Commands.Run("undo");
        Check(engine.Get("exposure_ev") == ev && !EditHistory.CanUndo, "undo restores a value, and a drag's ticks were one step");
        Commands.Run("redo");
        Check(engine.Get("exposure_ev") == ev + 1.5f && EditHistory.CanRedo, "redo puts it back");
        Commands.Run("reset-adjustments");
        Check(engine.Get("exposure_ev") == ev && engine.Get("contrast") == contrast && EditHistory.UndoLabel == "Reset" && !EditHistory.CanRedo,
              "reset is the as-shot look, one step, and drops the redo tail");
        Commands.Run("undo");
        Check(engine.Get("exposure_ev") == ev + 1.5f, "a reset undoes");
        engine.Set("exposure_ev", ev);
    }

    /// The toolbar and the canvas read one compare state: toggling either way, both follow.
    static void CompareChecks(EngineModel engine) {
        float ev = engine.Get("exposure_ev");
        engine.Set("exposure_ev", ev + 2);   // edited far brighter than the original
        var toolbar = new ToolbarPanel();
        var canvas = new CanvasPanel();
        var overlay = new CompareOverlay();
        Grid.SetRow(canvas, 1);
        Grid.SetRow(overlay, 1);
        var window = Show(new Grid { RowDefinitions = new("Auto,*"), Children = { toolbar, canvas, overlay } });
        StateButton Compare() => toolbar.GetVisualDescendants().OfType<StateButton>().First(b => b.Label == "Compare");

        var edited = Halves(window, canvas);
        Commands.Run("compare");
        var (left, right) = Halves(window, canvas);
        Check(CompareState.Comparing && Compare().On && Commands.CanRun("swap-split")
              && left < edited.Left - 10 && Math.Abs(right - edited.Right) < 2,
              $"compare on: the toolbar says so and the canvas shows the original on the left ({edited.Left:0} → {left:0}, right {right:0})");
        for (int i = 0; i < 3; i++) CanvasState.Touch();   // repaints must not move the labels
        window.UpdateLayout();
        var frame = CanvasState.DrawnFrame(overlay.Bounds.Size);
        double Centre(string text) => overlay.GetVisualDescendants().OfType<TextBlock>().FirstOrDefault(t => t.Text == text)?.Parent is Border b
            ? b.Bounds.Center.X : double.NaN;
        Check(Math.Abs(Centre("Original") - (frame.X + frame.Width * 0.25)) < 2 && Math.Abs(Centre("Edited") - (frame.X + frame.Width * 0.75)) < 2,
              $"the compare labels stay centred in their halves ({Centre("Original"):0}, {Centre("Edited"):0} in {frame})");
        Commands.Run("compare");
        (left, right) = Halves(window, canvas);
        Check(!CompareState.Comparing && !Compare().On && Math.Abs(left - edited.Left) < 2,
              $"compare off: the toolbar and the canvas both say so (left {left:0})");
        CompareState.SetSplit(0.5);   // any other route in (the overlay's drag) reaches the toolbar too
        Dispatcher.UIThread.RunJobs();
        Check(Compare().On, "a split set without the button still lights the button");
        CompareState.Clear();
        window.Close();
        engine.Set("exposure_ev", ev);
    }

    /// The drag picture: the quarter-size preview lands on exactly the full
    /// picture's rectangle, and each is drawn faithfully from its own bitmap.
    static void DragPictureChecks(EngineModel engine) {
        var canvas = new CanvasPanel();
        var window = Show(canvas);
        float ev = engine.Get("exposure_ev");
        engine.BeginInteraction();
        engine.Set("exposure_ev", ev + 0.5f);
        var (previewRect, previewError) = Drawn(window, canvas, engine.Shown.Bitmap!);
        bool preview = engine.Shown.Graph == Graph.Preview;
        engine.EndInteraction();
        var (fullRect, fullError) = Drawn(window, canvas, engine.Shown.Bitmap!);
        engine.Set("exposure_ev", ev);
        window.Close();
        Check(preview && previewRect == fullRect && previewRect.Width > 100,
              $"the drag picture fits the full picture's rectangle ({previewRect} vs {fullRect})");
        Check(previewError < 3 && fullError < 3,
              $"the canvas draws each graph as its bitmap holds it (mean block error {previewError:0.0} preview, {fullError:0.0} full)");
    }

    static void GalleryChecks(EngineModel engine) {
        var window = new MainWindow();
        window.Show();
        Dispatcher.UIThread.RunJobs();
        var face = window.FindControl<Control>("DevelopFace")!;
        var gallery = window.FindControl<Grid>("GalleryHost")!;
        Commands.Run("gallery");
        Dispatcher.UIThread.RunJobs();
        Check(GalleryMode.On && gallery.IsVisible && !face.IsVisible && gallery.Children.Count == 1 && GalleryMode.Focus == LibraryModel.Shared.Open,
              "G shows the gallery in place of the canvas, focused on the open photo");
        Check(!Commands.CanRun("compare") && !Commands.CanRun("fit") && !Commands.CanRun("reset-adjustments"),
              "the canvas's commands grey out in the gallery");
        Check(Press(window, Key.D9) && CanvasState.Viewport.IsFit, "the gallery swallows the canvas keys");
        if (engine.Photos.Count >= 2) {
            var open = LibraryModel.Shared.Open!;
            Press(window, Key.Right);
            var focus = GalleryMode.Focus!;
            int stars = focus.Rating;
            Commands.Run("rate-3");
            Check(focus != open && focus.Rating == 3 && engine.Current == open.Index,
                  "an arrow moves the focus, not the canvas, and the keys rate the focus");
            LibraryModel.Shared.SetRating(focus, stars);
            Press(window, Key.Left);
        }
        Press(window, Key.Escape);
        Check(!GalleryMode.On && face.IsVisible && !gallery.IsVisible, "Escape leaves the gallery for the canvas, which never closed");
        window.Close();
    }

    static void TrashChecks(EngineModel engine) {
        var open = LibraryModel.Shared.Open!;
        var moved = new List<string>();
        var dialog = Trash.Dialog([open], p => { moved.Add(p); return null; });
        Button Pressing(string title) => dialog.GetLogicalDescendants().OfType<StateButton>().First(b => b.Label == title);
        Check(dialog.Title == $"Move {open.Name} to the Trash?" && Trash.Question([open, open]) == "Move 2 photos to the Trash?",
              "the trash asks the counted question");
        Pressing("Cancel").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Check(moved.Count == 0, "Cancel moves nothing");
        if (engine.Photos.Count < 2) return;
        var list = engine.Photos.ToList();
        int current = engine.Current;
        float ev = engine.Get("exposure_ev");
        var other = LibraryModel.Shared.Photos.First(p => p != open);
        engine.Set("exposure_ev", ev + 0.7f);
        dialog = Trash.Dialog([other], p => { moved.Add(p); return null; });
        Pressing("Move to Trash").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Check(moved.FirstOrDefault() == other.Path && !engine.Photos.Contains(other.Path) && engine.Photos[engine.Current] == open.Path
              && engine.Get("exposure_ev") == ev + 0.7f, "a confirmed trash moves that photo and keeps the open one with its edits");
        engine.Load(list);
        engine.Open(current);
    }

    static void ExportChecks(EngineModel engine) {
        var settings = ExportSettings.Shared;
        int size = settings.Size, format = settings.Format;
        settings.Size = Array.FindIndex(ExportSettings.Sizes, s => s.Code == 1024);
        string folder = Directory.CreateTempSubdirectory("orion-selftest-").FullName;
        try {
            var run = ExportRun.Run(ExportRun.Plan([engine.Photos[engine.Current]], folder, "jpg"));
            while (!run.IsCompleted) Dispatcher.UIThread.RunJobs();
            var written = Directory.GetFiles(folder);
            Check(run.Result == "Exported 1 photo." && written.Length == 1 && new FileInfo(written[0]).Length > 10_000 && ExportRun.Progress == null,
                  $"a batch writes through the engine and says so ('{run.Result}')");

            // JPEG XL (#6): the format choice reaches the engine. With metadata the
            // file is the ISO BMFF container, which opens with a 12-byte "JXL " box.
            settings.Format = Array.FindIndex(ExportSettings.Formats, f => f.Code == 3);
            var jxl = ExportRun.Run(ExportRun.Plan([engine.Photos[engine.Current]], folder, settings.Extension));
            while (!jxl.IsCompleted) Dispatcher.UIThread.RunJobs();
            var file = Directory.GetFiles(folder, "*.jxl");
            byte[] head = file.Length == 1 ? File.ReadAllBytes(file[0])[..12] : [];
            // Off macOS no metadata is written (#376), so the file is the bare codestream, FF 0A.
            bool signed = OperatingSystem.IsMacOS()
                ? head.AsSpan().SequenceEqual((byte[])[0, 0, 0, 12, (byte)'J', (byte)'X', (byte)'L', (byte)' ', 13, 10, 0x87, 10])
                : head.Length == 12 && head[0] == 0xFF && head[1] == 0x0A;
            Check(jxl.Result == "Exported 1 photo." && signed,
                  $"choosing JPEG XL writes a .jxl through the engine ('{jxl.Result}')");
        } finally {
            settings.Size = size;
            settings.Format = format;
            Directory.Delete(folder, true);
        }
    }

    /// Every built command runs headless without throwing. The ones that need a
    /// window or a person (a file dialog, a confirmation) do nothing here.
    static void CommandSweep() {
        var failed = new List<string>();
        foreach (var command in Commands.All.Where(c => c.Built && c.CanRun())) {
            try {
                if (!Commands.Run(command.Name).StartsWith("ran ")) failed.Add(command.Name);
            } catch (Exception e) {
                failed.Add($"{command.Name} ({e.GetType().Name}: {e.Message})");
            }
            Dispatcher.UIThread.RunJobs();
        }
        if (GalleryMode.On) GalleryMode.Leave();
        CompareState.Clear();
        Commands.Run("reset-adjustments");
        Check(failed.Count == 0, "every built command that can run, runs" + (failed.Count > 0 ? ": " + string.Join(", ", failed) : ""));
        int stubs = Commands.All.Count(c => !c.Built);
        Check(stubs <= 3, $"{stubs} commands left as stubs: {string.Join(", ", Commands.All.Where(c => !c.Built).Select(c => c.Name))}");
    }

    static Window Show(Control content) {
        var window = new Window { Width = 1140, Height = 720, Content = content };
        window.Show();
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        return window;
    }

    static bool Press(Window window, Key key) {
        var e = new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Key = key, Source = window };
        window.RaiseEvent(e);
        Dispatcher.UIThread.RunJobs();
        return e.Handled;
    }

    /// The window at 2× (Retina) as rows of four bytes, and the canvas's rectangle in it.
    static (byte[] Pixels, int Stride, Rect Canvas) Pixels(Window window, Control canvas) {
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        var size = new PixelSize((int)(window.Bounds.Width * 2), (int)(window.Bounds.Height * 2));
        using var rtb = new RenderTargetBitmap(size, new Vector(192, 192));
        rtb.Render(window);
        var pixels = new byte[size.Width * size.Height * 4];
        unsafe { fixed (byte* p = pixels) rtb.CopyPixels(new PixelRect(size), (nint)p, pixels.Length, size.Width * 4); }
        var at = canvas.TranslatePoint(default, window) ?? default;
        return (pixels, size.Width * 4, new Rect(at.X * 2, at.Y * 2, canvas.Bounds.Width * 2, canvas.Bounds.Height * 2));
    }

    /// Mean of R, G and B over a rectangle, every other pixel: blind to channel order.
    static double Mean(byte[] px, int stride, Rect r) {
        double sum = 0;
        long n = 0;
        for (int y = (int)r.Y; y < (int)r.Bottom; y += 2)
            for (int x = (int)r.X; x < (int)r.Right; x += 2, n++) {
                int i = y * stride + x * 4;
                sum += (px[i] + px[i + 1] + px[i + 2]) / 3.0;
            }
        return n == 0 ? 0 : sum / n;
    }

    /// The picture's left and right halves (the compare split sits at half).
    static (double Left, double Right) Halves(Window window, Control canvas) {
        var (px, stride, c) = Pixels(window, canvas);
        var frame = CanvasState.DrawnFrame(canvas.Bounds.Size);
        var r = new Rect(c.X + frame.X * 2 + 4, c.Y + frame.Y * 2 + 4, frame.Width * 2 - 8, frame.Height * 2 - 8);
        return (Mean(px, stride, r.WithWidth(r.Width / 2 - 8)), Mean(px, stride, new Rect(r.Center.X + 8, r.Y, r.Width / 2 - 8, r.Height)));
    }

    /// Where the canvas drew the picture (anything off the black behind it), and the
    /// mean difference between an 8 × 12 grid of its blocks and the same blocks of `bitmap`.
    static unsafe (Rect Drawn, double Error) Drawn(Window window, Control canvas, Bitmap bitmap) {
        var (px, stride, c) = Pixels(window, canvas);
        int x0 = int.MaxValue, y0 = int.MaxValue, x1 = -1, y1 = -1;
        for (int y = (int)c.Y; y < (int)c.Bottom; y++)
            for (int x = (int)c.X; x < (int)c.Right; x++) {
                int i = y * stride + x * 4;
                if (px[i] + px[i + 1] + px[i + 2] <= 6) continue;
                (x0, y0, x1, y1) = (Math.Min(x0, x), Math.Min(y0, y), Math.Max(x1, x), Math.Max(y1, y));
            }
        if (x1 < 0 || bitmap is not WriteableBitmap wb) return (default, double.MaxValue);
        var drawn = new Rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        using var fb = wb.Lock();
        int bw = fb.Size.Width, bh = fb.Size.Height;
        const int gx = 8, gy = 12;
        double error = 0;
        for (int j = 0; j < gy; j++)
            for (int i = 0; i < gx; i++) {
                double onCanvas = Mean(px, stride, new Rect(drawn.X + drawn.Width * i / gx, drawn.Y + drawn.Height * j / gy, drawn.Width / gx, drawn.Height / gy));
                double sum = 0;
                long n = 0;
                for (int y = bh * j / gy; y < bh * (j + 1) / gy; y += 2) {
                    byte* row = (byte*)fb.Address + (long)y * fb.RowBytes;
                    for (int x = bw * i / gx; x < bw * (i + 1) / gx; x += 2, n++) sum += (row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2]) / 3.0;
                }
                error += Math.Abs(onCanvas - sum / Math.Max(n, 1));
            }
        return (drawn, error / (gx * gy));
    }
}

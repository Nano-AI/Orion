using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.LogicalTree;
using OrionShell.Chrome;

namespace OrionShell;

/// The feedback form and its reminder: when the toast may ask, and what the
/// form's URL carries. The browser never opens: Feedback.Launcher records the URL.
static partial class SelfTest {
    static void FeedbackChecks() {
        // The clock's now, to the second the settings keep: DueNow reads the clock.
        var now = DateTimeOffset.FromUnixTimeSeconds(DateTimeOffset.UtcNow.ToUnixTimeSeconds());
        const string v = "0.6.0-alpha.2";
        bool Due(double minutes = 10, bool once = false, DateTimeOffset[]? shown = null, bool never = false, string? given = null, bool busy = false) =>
            Feedback.Due(now, minutes, once, shown ?? [], never, given, v, busy);
        Check(Due() && !Due(minutes: 9.75), "feedback: the reminder waits for ten minutes of use");
        Check(!Due(once: true), "feedback: the reminder shows at most once a launch");
        Check(!Due(shown: [now.AddDays(-2.9), now.AddHours(-1)]) && Due(shown: [now.AddDays(-3.1), now.AddHours(-1)]),
              "feedback: a third reminder inside three days is refused, and allowed once the oldest is three days old");
        Check(!Due(never: true), "feedback: Never show again stops the reminder");
        Check(!Due(given: v) && Due(given: "0.6.0-alpha.1"), "feedback: given for this version quiets it; a new version asks again");
        Check(!Due(busy: true), "feedback: a drag, export, stack or merge in flight defers it");

        var full = new FeedbackFacts("Mac", "0.6.0+a b", "macOS 15.4", "Apple M4", "Apple M4 GPU", 24, "3024x1964@2x", "d8fc44a", 12, 3,
                                     "Sony ILCE-7RM3", "ARW");
        Check(Feedback.Url(full) == Feedback.Form + "?os=Mac&version=0.6.0%2Ba%20b&os_version=macOS%2015.4&chip=Apple%20M4&gpu=Apple%20M4%20GPU"
                  + "&ram_gb=24&display=3024x1964%402x&build=d8fc44a&minutes=12&photos=3&camera=Sony%20ILCE-7RM3&format=ARW"
              && Feedback.Os == (OperatingSystem.IsWindows() ? "Windows" : OperatingSystem.IsMacOS() ? "Mac" : "Linux"),
              $"feedback: the URL carries the form's twelve fields in order, encoded, and the OS as the form spells it ({Feedback.Os})");
        Check(Feedback.Url(new FeedbackFacts("Linux", "dev", OsVersion: "", Chip: " ", Gpu: @"C:\Users\x", Camera: "/Users/x/DSC1.ARW", Format: "~/x"))
                  == Feedback.Form + "?os=Linux&version=dev",
              "feedback: an unknown value is left out, and so is one that names a path");

        var dir = Directory.CreateTempSubdirectory("orion-feedback-").FullName;
        var opened = new List<Uri>();
        ShellSettings.File = Path.Combine(dir, "settings.json");
        Feedback.Launcher = uri => { opened.Add(uri); return Task.CompletedTask; };
        try {
            ShellSettings.Set("gallery-cell", 260);
            Feedback.RecordShown(now.AddDays(-5));
            Feedback.RecordShown(now.AddDays(-1));
            bool room = Feedback.DueNow(10, false, false);
            Feedback.RecordShown(now);
            Check(room && !Feedback.DueNow(10, false, false) && Feedback.Shown.SequenceEqual([now.AddDays(-1), now]),
                  "feedback: the last two reminders are kept; one in three days leaves room, two refuse a third");

            var toolbar = new ToolbarPanel().GetLogicalDescendants().OfType<StateButton>().FirstOrDefault(b => b.Label == "Feedback");
            toolbar?.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            // Gathered for real, with a photo open from a folder under the user's home.
            string sent = opened.Count == 1 ? Uri.UnescapeDataString(opened[0].AbsoluteUri) : "";
            Console.WriteLine($"selftest: feedback link {sent}");
            string[] fields = [.. sent.Split('?').Last().Split('&').Select(p => p.Split('=')[0])];
            string[] known = ["os", "version", "os_version", "chip", "gpu", "ram_gb", "display", "build", "minutes", "photos", "camera", "format"];
            var e = EngineModel.Shared;
            string stem = e.Current >= 0 ? Path.GetFileNameWithoutExtension(e.Photos[e.Current]) : "\0";
            Check(sent.StartsWith($"{Feedback.Form}?os={Feedback.Os}&version={Updates.Version}&") && fields.All(known.Contains)
                  && fields.Contains("ram_gb") && !sent.Contains(stem) && !sent.Contains(Environment.UserName)
                  && !sent.Contains(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)),
                  $"feedback: the toolbar's Feedback opens the form with this machine's facts and no path, name or file ({sent})");
            Check(ShellSettings.Get("feedback-given-version") == Updates.Version && ShellSettings.Get("gallery-cell", 0) == 260,
                  $"feedback: opening it records feedback given for {Updates.Version}, keeping the other settings");

            var toast = new FeedbackToast();
            var buttons = toast.GetLogicalDescendants().OfType<StateButton>().ToList();
            void Press(string help) => buttons.First(b => b.Label == help || ToolTip.GetTip(b) as string == help).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            toast.Show();
            Press("Close");
            bool closed = !toast.IsVisible && ShellSettings.Get("feedback-never", 0) == 0;
            toast.Show();
            Press("Never show again");
            Check(closed && !toast.IsVisible && buttons.Count == 4 && buttons.All(b => !b.Focusable) && ShellSettings.Get("feedback-never", 0) == 1
                  && buttons.First(b => b.Label == "Give feedback").Kind == ButtonKind.Primary,
                  "feedback: the toast's buttons never take focus; ✕ only hides it, and Never show again hides it for good");
        } finally {
            Feedback.Launcher = null;
            ShellSettings.File = null;
            Directory.Delete(dir, true);
        }
    }
}

namespace OrionShell.Chrome;

/// The feedback form: the toolbar's Feedback button and the reminder toast
/// both open it, with FeedbackFacts in its hidden fields.
static class Feedback {
    public const string Form = "https://tally.so/r/NqrZDG";
    const string ShownKey = "feedback-shown-", NeverKey = "feedback-never", GivenKey = "feedback-given-version";

    /// The three values the form's `os` field expects.
    public static string Os => OperatingSystem.IsWindows() ? "Windows" : OperatingSystem.IsMacOS() ? "Mac" : "Linux";

    /// The form's link with each known fact as a Tally hidden field, encoded. An empty value, or one that
    /// names a path or the user, is left out: nothing here may say who or where the photographer is.
    public static string Url(FeedbackFacts f) {
        (string Field, string? Value)[] fields = [
            ("os", f.Os), ("version", f.Version), ("os_version", f.OsVersion), ("chip", f.Chip), ("gpu", f.Gpu),
            ("ram_gb", f.RamGb?.ToString()), ("display", f.Display), ("build", f.Build), ("minutes", f.Minutes?.ToString()),
            ("photos", f.Photos?.ToString()), ("camera", f.Camera), ("format", f.Format),
        ];
        return Form + "?" + string.Join("&", fields.Where(p => Safe(p.Value)).Select(p => $"{p.Field}={Uri.EscapeDataString(p.Value!.Trim())}"));
    }

    static bool Safe(string? value) =>
        !string.IsNullOrWhiteSpace(value) && !value.TrimStart().StartsWith('/') && !value.TrimStart().StartsWith('~')
        && !value.Contains('\\') && !value.Contains("/Users/") && !value.Contains("/home/")
        && !(Environment.UserName.Length >= 3 && value.Contains(Environment.UserName, StringComparison.OrdinalIgnoreCase));

    /// What opens the URL instead of the browser: the selftest records it. Null is the browser, at a window only.
    internal static Func<Uri, Task>? Launcher;

    static Task Browser(Uri uri) => App.Window?.Launcher.LaunchUriAsync(uri) ?? Task.CompletedTask;

    /// Opens the form and counts it as feedback given for this version, which quiets the reminder until the next one.
    public static void Give() {
        (Launcher ?? (AppFolder.Writable ? Browser : null))?.Invoke(new Uri(Url(FeedbackFacts.Now())));
        ShellSettings.Set(GivenKey, Updates.Version);
    }

    public static void Never() => ShellSettings.Set(NeverKey, 1);

    /// The last two times the reminder showed, oldest first.
    public static DateTimeOffset[] Shown => [.. Enumerable.Range(0, 2).Select(i => ShellSettings.Get(ShownKey + i, double.NaN))
        .Where(double.IsFinite).Select(s => DateTimeOffset.FromUnixTimeSeconds((long)s)).Order()];

    public static void RecordShown(DateTimeOffset now) {
        var last = Shown.Append(now).Order().TakeLast(2).ToArray();
        for (int i = 0; i < last.Length; i++) ShellSettings.Set(ShownKey + i, last[i].ToUnixTimeSeconds());
    }

    /// Whether the reminder may show now, with the stored state read.
    public static bool DueNow(double activeMinutes, bool shownThisLaunch, bool busy) =>
        Due(DateTimeOffset.UtcNow, activeMinutes, shownThisLaunch, Shown, ShellSettings.Get(NeverKey, 0) != 0,
            ShellSettings.Get(GivenKey), Updates.Version, busy);

    /// The reminder's rule: ten minutes of real use this launch, once per launch, at most twice in any three days,
    /// never mid-task, never after Never, and not again for a version feedback was given for.
    public static bool Due(DateTimeOffset now, double activeMinutes, bool shownThisLaunch, IReadOnlyList<DateTimeOffset> shown,
                           bool never, string? givenFor, string version, bool busy) =>
        !never && givenFor != version && !shownThisLaunch && !busy && activeMinutes >= 10
        && shown.Count(t => now - t < TimeSpan.FromDays(3)) < 2;
}

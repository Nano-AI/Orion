using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
using OrionShell.Sheets;

namespace OrionShell.Library;

/// The Library's one line when the folder holds a stack (#391): "14 frames look like a stack.
/// Stack them?", a Stack… that selects the group and opens the Stack sheet over exactly it, and a ×
/// that hides that group until another folder opens. It sits on the strip's film, at its right end.
///
/// The folder is scanned off the UI thread whenever the list changes (StackGroups.Of reads only
/// headers), which also fills StackGroups.Cached, so the `stack` command finds the open photo's group.
sealed class StackSuggestion : Border {
    /// The embedded preview's mean luma (Rec. 709 weights on its encoded values) under which a candidate
    /// astro group's middle frame is a night sky. research/UNSOURCED.md §32.
    const double DarkLuma = 0.15;

    readonly TextBlock _text = new() { FontSize = Faces.Label, Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center };
    // Each dismissed group's first path. Static: a stack the sheet just made dismisses its frames too (DismissFrames).
    static readonly HashSet<string> _dismissed = new();
    IReadOnlyList<StackGroup> _groups = [];
    StackGroup? _shown;

    /// The scan in flight, done once its result is posted to the UI thread: --screenshot waits on it.
    public static Task Scanned { get; private set; } = Task.CompletedTask;

    /// The first group not dismissed this session, or null. Dismissals are keyed by a group's first path,
    /// so a rescan of the same folder (a sort, a trash) keeps them.
    public static StackGroup? Pick(IReadOnlyList<StackGroup> groups, IReadOnlySet<string> dismissed) =>
        groups.FirstOrDefault(g => !dismissed.Contains(g.Paths[0]));

    /// After a stack: every group starting with one of these frames stays hidden (the rescan the new DNG
    /// causes would otherwise offer it again), as × does.
    public static void DismissFrames(IEnumerable<string> paths) => _dismissed.UnionWith(paths);

    public StackSuggestion() {
        HorizontalAlignment = HorizontalAlignment.Right;
        VerticalAlignment = VerticalAlignment.Center;
        Margin = new Thickness(0, 0, Space.Pad, 0);
        Padding = new Thickness(Space.Md, Space.Xs, Space.Xs, Space.Xs);
        CornerRadius = new CornerRadius(Radius.Md);
        Background = Palette.Raised;
        BorderBrush = Palette.Line;
        BorderThickness = new Thickness(1);
        var stack = new StateButton(null, "Stack…", Stack, ButtonKind.Bare, "Select these frames and stack them") {
            Ink = Palette.Accent, Padding = new Thickness(Space.Sm, 0), Height = 24,
        };
        var dismiss = new StateButton { Kind = ButtonKind.Bare, Content = Icons.Create("xmark", IconSize.Sm, Palette.Dim), Padding = new Thickness(Space.Gap, 0),
                                        MinWidth = 0, Height = 24 }.Help("Not a stack");
        dismiss.Click += (_, _) => { if (_shown != null) _dismissed.Add(_shown.Paths[0]); Show(); };
        Child = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Gap, Children = { _text, stack, dismiss } };
        IsVisible = false;

        var engine = EngineModel.Shared;
        engine.FolderLoaded += _dismissed.Clear;
        engine.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(EngineModel.Photos)) Scan(); };
        Scan();   // a folder opened before the strip was built
    }

    void Scan() {
        var photos = EngineModel.Shared.Photos;
        if (photos.Count < StackGroups.MinFrames) { _groups = []; Show(); return; }
        Scanned = Task.Run(() => StackGroups.Of(photos, Dark)).ContinueWith(t => Dispatcher.UIThread.Post(() => {
            if (t.IsFaulted) Console.Error.WriteLine($"orion stack: folder scan failed: {t.Exception?.GetBaseException().Message}");
            // A scan of a list since replaced is dropped: the newer list's scan is on its way.
            if (t.IsCompletedSuccessfully && ReferenceEquals(photos, EngineModel.Shared.Photos)) { _groups = t.Result; Show(); }
        }));
    }

    void Show() {
        _shown = Pick(_groups, _dismissed);
        IsVisible = _shown != null;
        if (_shown != null) _text.Text = StackGroups.Suggestion(_shown);
    }

    /// The group becomes the selection, so the sheet's candidates are exactly it.
    void Stack() {
        if (_shown == null) return;
        LibraryModel.Shared.Select(_shown.Paths);
        if (Commands.Run("stack") is var said && !said.StartsWith("ran ")) EngineModel.Shared.Say(said);
    }

    /// Asked of a candidate astro group's middle frame. The strip's own thumbnail, read on the UI
    /// thread that owns it; no preview leaves the settings' verdict standing.
    static bool Dark(string path) => Dispatcher.UIThread.Invoke(() => {
        if (LibraryModel.Shared.Photos.FirstOrDefault(p => p.Path == path)?.Thumbnail is not { } thumbnail) return true;
        var (rgb, _, _) = StackSplit.Rgb(thumbnail, 64);
        double sum = 0;
        for (int i = 0; i < rgb.Length; i += 3) sum += 0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2];
        return sum / (rgb.Length / 3) < DarkLuma;
    });
}

using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using OrionShell.Sheets;

namespace OrionShell.Chrome;

/// When the feedback toast shows (Feedback.Due has the rule). Use is counted
/// in 15 s ticks, each only while the window is focused and had input in the
/// last minute, so an idle or background window earns nothing.
static class FeedbackReminder {
    static readonly TimeSpan Tick = TimeSpan.FromSeconds(15), Idle = TimeSpan.FromMinutes(1);

    /// A drag, an export, a stack or a merge in flight: the toast waits for it to end.
    static bool Busy => EngineModel.Shared.Interacting || ExportRun.Progress != null || Stack.Progress != null || HdrMerge.Progress != null;

    /// Minutes of use this launch, and how many photos were opened: two of the form's fields.
    public static double Minutes { get; private set; }
    public static int Photos => Opened.Count;
    static readonly HashSet<string> Opened = [];

    /// At a window only: the headless modes never ask.
    public static void Start(Window window, FeedbackToast toast) {
        if (!AppFolder.Writable) return;
        var e = EngineModel.Shared;
        e.Opened += () => Opened.Add(e.Photos[e.Current]);
        var input = DateTimeOffset.MinValue;
        const RoutingStrategies tunnel = RoutingStrategies.Tunnel;
        window.AddHandler(InputElement.PointerMovedEvent, (_, _) => input = DateTimeOffset.UtcNow, tunnel, handledEventsToo: true);
        window.AddHandler(InputElement.PointerPressedEvent, (_, _) => input = DateTimeOffset.UtcNow, tunnel, handledEventsToo: true);
        window.AddHandler(InputElement.PointerWheelChangedEvent, (_, _) => input = DateTimeOffset.UtcNow, tunnel, handledEventsToo: true);
        window.AddHandler(InputElement.KeyDownEvent, (_, _) => input = DateTimeOffset.UtcNow, tunnel, handledEventsToo: true);
        bool shown = false;
        var timer = new DispatcherTimer { Interval = Tick };
        timer.Tick += (_, _) => {
            if (window.IsActive && DateTimeOffset.UtcNow - input < Idle) Minutes += Tick.TotalMinutes;
            if (!Feedback.DueNow(Minutes, shown, Busy)) return;
            shown = true;
            Feedback.RecordShown(DateTimeOffset.UtcNow);
            toast.Show();
        };
        timer.Start();
    }
}

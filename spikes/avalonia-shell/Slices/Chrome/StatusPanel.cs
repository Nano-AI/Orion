using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Chrome;

/// The foot of the tool column (Main.dc.html; OrionApp+Chrome.swift's `footer`):
/// a status line when there is something to say (worst first: what went wrong,
/// then what the armed tool wants), and the browsing keys as keycaps, read
/// from the command table so the caps and the keys cannot disagree.
[Panel("status", Region.Status)]
public sealed class StatusPanel : Border {
    const string RenderFailed = "render failed: ";   // EngineModel.Render's Timing on a failure

    readonly TextBlock _message = new() { FontSize = Faces.Label, TextWrapping = TextWrapping.Wrap, MaxLines = 2,
                                          TextTrimming = TextTrimming.CharacterEllipsis };

    public StatusPanel() {
        BorderBrush = Palette.Line;
        BorderThickness = new Thickness(0, 1, 0, 0);
        Padding = new Thickness(Space.Pad, 8);
        Child = new StackPanel { Spacing = 8, Children = { _message, Keys() } };

        EngineModel.Shared.PropertyChanged += (_, _) => Refresh();   // Timing, Tool, Current: every render and open
        Refresh();
    }

    void Refresh() {
        var engine = EngineModel.Shared;
        bool loaded = engine.Handle != 0;
        string? failure = engine.Timing.StartsWith(RenderFailed) ? engine.Timing[RenderFailed.Length..] : null;

        // Worst first: a complaint costs the session, a failed render costs a
        // redraw, a flat frame is a success that shows nothing. Outside
        // `loaded` for the complaint: it is what explains an empty window.
        // Then a stub that was asked for: "not built yet: undo (EditHistory.swift)".
        string? warning = ChromeStubs.Complaint
            ?? (!loaded ? null
              : failure != null ? $"Render failed: {failure}"
              : ChromeStubs.FlatFrame is { } flat ? $"Not a photograph: {flat}" : null)
            ?? (engine.Status.Length > 0 ? engine.Status : null);
        _message.Text = warning ?? Hint(engine);
        _message.Foreground = warning != null ? Palette.Star : Palette.Dim;
        _message.IsVisible = _message.Text.Length > 0;
        IsVisible = engine.Photos.Count > 0 || warning != null;
    }

    /// The armed tool wins: it is the one state where a click on the photo
    /// does something the panel no longer shows. Browsing is the keycaps' job.
    static string Hint(EngineModel engine) => engine.Tool switch {
        CanvasTool.Targeted => "Drag on the photo to adjust its color. Esc cancels.",
        CanvasTool.MaskColor => "Click the photo to set the mask color. Esc cancels.",
        CanvasTool.Spot => "Press on the photo, then drag out the source. Esc cancels.",
        _ when ChromeStubs.CropTab => "Drag the rectangle or its corners.",
        _ when !ChromeStubs.IsFit => "Drag to pan. Right-click to fit.",
        _ => "",
    };

    /// ← → Browse, 1–5 Rate, R Reject: whatever bare keys the table binds.
    static Control Keys() {
        var row = new Grid { ColumnDefinitions = new("Auto,*,Auto,*,Auto") };
        Control[] groups = [
            Group("Browse", [.. Caps("previous-photo"), .. Caps("next-photo")]),
            Group("Rate", [.. Caps("rate-1"), Dash(), .. Caps("rate-5")]),
            Group("Reject", Caps("reject")),
        ];
        for (int i = 0; i < groups.Length; i++) { Grid.SetColumn(groups[i], i * 2); row.Children.Add(groups[i]); }
        return row;
    }

    static StackPanel Group(string word, params Control[] caps) {
        var group = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        group.Children.AddRange(caps);
        group.Children.Add(new TextBlock { Text = word, FontSize = Faces.Label, Foreground = Palette.Dim, Margin = new Thickness(2, 0, 0, 0),
                                           VerticalAlignment = VerticalAlignment.Center });
        return group;
    }

    static Control Dash() => new TextBlock { Text = "–", FontSize = Faces.Label, Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center };

    /// The command's key without modifiers, as a keycap: raised, a hairline, a deeper bottom edge.
    static Control[] Caps(string command) =>
        Commands.Find(command)?.Keys.Where(k => k.KeyModifiers == KeyModifiers.None).Take(1).Select(k => (Control)new Border {
            MinWidth = 17, Height = 18, Padding = new Thickness(4, 0), CornerRadius = new CornerRadius(Radius.Sm),
            Background = Palette.Raised, BorderBrush = Palette.Rail, BorderThickness = new Thickness(1, 1, 1, 2),
            VerticalAlignment = VerticalAlignment.Center, Child = Cap(KeyName(k.Key)),
        }).ToArray() ?? [];

    static TextBlock Cap(string text) {
        var t = Faces.Figure(text, Faces.Mark);
        t.HorizontalAlignment = HorizontalAlignment.Center;
        return t;
    }

    static string KeyName(Key key) => key switch {
        Key.Left => "←", Key.Right => "→", >= Key.D0 and <= Key.D9 => ((int)(key - Key.D0)).ToString(), _ => key.ToString(),
    };
}

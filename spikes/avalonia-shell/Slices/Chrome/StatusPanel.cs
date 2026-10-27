using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Chrome;

/// A port of OrionApp+Chrome.swift's `footer`, under the tool column: what the
/// next click does (or, worst first, what went wrong), which photograph this
/// is, and the frame's numbers. Two lines rather than one, so the stable
/// numbers do not jump every time the hint changes length.
[Panel("status", Region.Status)]
public sealed partial class StatusPanel : Border {
    const string RenderFailed = "render failed: ";   // EngineModel.Render's Timing on a failure

    readonly TextBlock _message = Line(Palette.Dim), _file = Line(Palette.Dim);
    readonly TextBlock _size = Engraved.Readout("", Palette.Dim), _ms = Engraved.Readout("");
    readonly DockPanel _numbers = new();

    public StatusPanel() {
        BorderBrush = Palette.Line;
        BorderThickness = new Thickness(0, 1, 0, 0);
        Padding = new Thickness(Space.Pad, 8);
        _file.MaxLines = 1;
        DockPanel.SetDock(_ms, Dock.Right);
        _numbers.Children.Add(_ms);
        _numbers.Children.Add(_size);
        Child = new StackPanel { Spacing = 5, Children = { _message, _file, _numbers } };

        var engine = EngineModel.Shared;
        engine.PropertyChanged += (_, _) => Refresh();   // Timing, Tool, Current: every render and open
        Refresh();
    }

    static TextBlock Line(IBrush ink) => new() {
        Foreground = ink, TextWrapping = TextWrapping.Wrap, MaxLines = 2, TextTrimming = TextTrimming.CharacterEllipsis,
    };

    void Refresh() {
        var engine = EngineModel.Shared;
        bool loaded = engine.Handle != 0;
        string? failure = engine.Timing.StartsWith(RenderFailed) ? engine.Timing[RenderFailed.Length..] : null;

        // Worst first: a complaint costs the session, a failed render costs a
        // redraw, a flat frame is a success that shows nothing. Outside
        // `loaded` for the complaint: it is what explains an empty window.
        string? warning = ChromeStubs.Complaint
            ?? (!loaded ? null
              : failure != null ? $"Render failed — {failure}"
              : ChromeStubs.FlatFrame is { } flat ? $"Not a photograph — {flat}" : null);
        IsVisible = loaded || warning != null;
        if (warning != null) Engrave(_message, warning, Palette.Star);
        else { _message.Text = Hint(engine); _message.FontSize = TypeSize.Xs; _message.FontWeight = FontWeight.Normal;
               _message.LetterSpacing = 0; _message.Foreground = Palette.Dim; }

        _file.IsVisible = _numbers.IsVisible = loaded;
        if (!loaded) return;
        Engrave(_file, engine.CurrentName, Palette.Dim);
        _size.Text = orion_engine_image_size(engine.Handle, out uint w, out uint h) == 0 ? $"{w} × {h}" : "";
        double ms = engine.Last.Render;
        _ms.Text = failure == null ? $"{ms:0.0} ms" : "failed";
        _ms.Foreground = failure == null && ms < 16 ? Palette.Accent : Palette.Star;
    }

    /// Engraved.Label's look on a line that keeps its place in the stack.
    static void Engrave(TextBlock line, string text, IBrush ink) {
        var engraved = Engraved.Label(text, ink, 9);
        line.Text = engraved.Text;
        line.FontSize = engraved.FontSize;
        line.FontWeight = engraved.FontWeight;
        line.LetterSpacing = engraved.LetterSpacing;
        line.Foreground = ink;
    }

    /// The armed tool wins: it is the one state where a click on the photo
    /// does something the panel no longer shows.
    static string Hint(EngineModel engine) => engine.Tool switch {
        CanvasTool.Targeted => "drag on the photo to adjust its color · esc cancels",
        CanvasTool.MaskColor => "click the photo to set the mask color · esc cancels",
        CanvasTool.Spot => "press on the photo, drag out the source · esc cancels",
        _ when ChromeStubs.CropTab => "drag the rectangle or its corners",
        _ when engine.Photos.Count > 0 => ChromeStubs.IsFit ? "← → to browse · 1–5 rates · R rejects" : "drag to pan · right-click to fit",
        _ => ChromeStubs.IsFit ? "scroll or pinch to zoom · right-click to fit" : "drag to pan · right-click to fit",
    };

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_image_size(nint engine, out uint width, out uint height);
}

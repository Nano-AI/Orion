using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Develop;

/// A port of the Swift app's DevelopPanels+Color.swift: presence, the three grading wheels
/// with their balance, the eight-band mixer with its targeted picker, and the
/// black and white conversion with its eight-band gray mix.
[Panel("develop-color", Region.Tools, 10, Tab = "Color")]
public sealed class ColorPanel : StackPanel {
    /// The mixer's band (Editor.band in OrionApp.swift): blue until a pick moves it.
    HueBand _band = HueBand.Blue;
    readonly StackPanel _swatches = new() { Orientation = Orientation.Horizontal };
    readonly ContentControl _bandName = new() { VerticalAlignment = VerticalAlignment.Center };
    readonly Panel _bandSliders = new();
    readonly Segmented _mode = new(["Hue", "Saturation", "Luminance"], (int)TargetedAdjust.Current);
    /// Lightroom's Treatment: Color or Black & white.
    readonly Segmented _treatment = new(["Color", "Black & white"], 0);
    readonly StackPanel _grayMix = new() { Spacing = Space.Md };

    public ColorPanel() {
        Spacing = SectionPlate.Gap;
        Children.Add(new SectionPlate("Presence", null,
            new AdjustmentSlider("Vibrance", "vibrance", -1, 1, tint: TrackTint.Presence),
            new AdjustmentSlider("Saturation", "saturation", -1, 1, tint: TrackTint.Presence)));

        // Side by side rather than stacked: grading is a comparison.
        var wheels = new Grid { ColumnDefinitions = new("*,8,*,8,*") };
        string[] titles = ["Shadows", "Midtones", "Highlights"], fields = ["grade_shadow", "grade_midtone", "grade_highlight"];
        for (int i = 0; i < 3; i++) {
            var wheel = new ColorWheel(titles[i], fields[i]) { HorizontalAlignment = HorizontalAlignment.Center };
            Grid.SetColumn(wheel, i * 2);
            wheels.Children.Add(wheel);
        }
        Children.Add(new SectionPlate("Color grading",
            "Angle picks the hue, distance picks how far. The wheels only change color — the slider under each one " +
            "is what changes that zone's brightness. Balance slides where the three zones sit: right hands more of " +
            "the picture to the highlight wheel, left to the shadow wheel.",
            wheels,
            // Under the wheels: the axis they sit on, not a fourth wheel (decision #101).
            new AdjustmentSlider("Balance", "grade_balance", -1, 1)));

        var target = new ToolButton(CanvasTool.Targeted, "eyedropper", "Target", "Drag on the photo", "scope",
                                    "Targeted adjustment — drag on the photo");
        _mode.Changed += i => TargetedAdjust.Current = (TargetedAdjust.Mode)i;
        var tools = Develop.Row(6, [target]);
        var rail = new DockPanel();
        DockPanel.SetDock(_swatches, Dock.Left);
        rail.Children.Add(_swatches);
        _bandName.HorizontalAlignment = HorizontalAlignment.Right;
        rail.Children.Add(_bandName);
        Children.Add(new SectionPlate("Color mixer",
            "Pick a band below, or press Target and drag on the photograph itself — that finds which band the pixel " +
            "under the pointer belongs to, which beats guessing which of eight swatches the sky falls into.",
            tools, _mode, rail, _bandSliders));

        foreach (var b in Enum.GetValues<HueBand>())
            _grayMix.Children.Add(new AdjustmentSlider(TargetedAdjust.Name(b), "bw_mix", -1, 1, index: (int)b, baseValue: 0,
                                                       tint: TrackTint.Luminance(TargetedAdjust.Centers[(int)b])));
        _treatment.Changed += i => {
            EngineModel.Shared.Set("black_white", i);
            ShowGrayMix();
        };
        Children.Add(new SectionPlate("Treatment",
            "Black & white renders the photograph in gray from its luminance. Each slider brightens or darkens what was " +
            "that color: pull Blue down for a darker sky, push Orange up for lighter skin. Color grading still tones the result.",
            _treatment, _grayMix));
        EngineModel.Shared.Opened += ShowGrayMix;
        EngineModel.Shared.ValuesChanged += ShowGrayMix;

        EngineModel.Shared.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(EngineModel.Tool)) ShowMode(); };
        // A pick on the photo moves the panel to the band it landed in.
        TargetedAdjust.Changed += () => { if (TargetedAdjust.ActiveBand is { } b) _band = b; ShowBand(); };
        // Built once per band and shown one band at a time, so each keeps its own detent and readout.
        foreach (var b in Enum.GetValues<HueBand>()) {
            double centre = TargetedAdjust.Centers[(int)b];
            _bandSliders.Children.Add(new StackPanel { Spacing = Space.Md, Children = {
                new AdjustmentSlider("Hue", "hue_shift", -1, 1, index: (int)b, baseValue: 0, tint: TrackTint.Hue(centre)),
                new AdjustmentSlider("Saturation", "sat_shift", -1, 1, index: (int)b, baseValue: 0, tint: TrackTint.Saturation(centre)),
                new AdjustmentSlider("Luminance", "lum_shift", -1, 1, index: (int)b, baseValue: 0, tint: TrackTint.Luminance(centre)),
            } });
        }
        ShowMode();
        ShowBand();
        ShowGrayMix();
    }

    /// The gray mix is only there while the photograph is black and white, as Lightroom's B&W panel.
    void ShowGrayMix() {
        bool on = EngineModel.Shared.Get("black_white") != 0;
        _treatment.Selected = on ? 1 : 0;
        _grayMix.IsVisible = on;
    }

    void ShowMode() => _mode.IsVisible = EngineModel.Shared.Tool == CanvasTool.Targeted;

    /// The swatch rail and the three sliders for the chosen band. The tracks
    /// wear the band's own colours: the Hue ends are the shader's ±30° travel.
    void ShowBand() {
        if (_swatches.Children.Count == 0)
            foreach (var b in Enum.GetValues<HueBand>()) {
                // The target is the 24 pt square, not the 16 pt dot: no dead strip between eight of them.
                // A button, so Tab reaches each band and Space picks it; a pressed Border did neither.
                // Built once and restroked, so the focus stays on the band just picked.
                var cell = new StateButton {
                    Kind = ButtonKind.Bare, Width = 24, Height = 24, MinWidth = 0, Padding = default,
                    Content = new Ellipse { Width = 16, Height = 16, Fill = TargetedAdjust.Swatch(b), Tag = StateButton.Keep, StrokeThickness = 1.5 },
                }.Help(TargetedAdjust.Name(b));
                cell.Click += (_, _) => { _band = b; ShowBand(); };
                _swatches.Children.Add(cell);
            }
        foreach (var b in Enum.GetValues<HueBand>())
            ((Ellipse)((StateButton)_swatches.Children[(int)b]).Content!).Stroke = b == _band ? Palette.Text : null;
        _bandName.Content = TargetedAdjust.ActiveBand is { } active
            ? Engraved.Label(TargetedAdjust.Name(active), Palette.Accent)
            : Engraved.Label(TargetedAdjust.Name(_band));

        for (int i = 0; i < _bandSliders.Children.Count; i++) _bandSliders.Children[i].IsVisible = i == (int)_band;
    }
}

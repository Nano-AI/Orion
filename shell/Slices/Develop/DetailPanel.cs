using System.Globalization;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using OrionShell.CanvasSlice;

namespace OrionShell.Develop;

/// A port of the Swift app's DevelopPanels+Detail.swift: noise reduction, dust spots, the
/// creative LUT, and the finishing controls.
[Panel("develop-detail", Region.Tools, 10, Tab = "Detail")]
public sealed class DetailPanel : StackPanel {
    /// How many spots the photo has: a number, so the numeral face.
    readonly TextBlock _spotCount = Faces.Figure("", Faces.Readout, Palette.Dim);
    readonly Button _undoSpot, _clearSpots;
    readonly ContentControl _lutButton = new();
    readonly Button _lutRemove;
    readonly TextBlock _lutError = Engraved.Note("", Palette.Star);
    readonly AdjustmentSlider _lutStrength = new("Strength", "lut_strength", 0, 1, baseValue: 1);
    /// Engine.lutName, which the session keeps (Session.LutName); one loaded
    /// past the panel (a preset, the selftest) is named by its TITLE.
    static string _lutName {
        get => Engine.Session?.LutName is { Length: > 0 } name ? name : LutTitle();
        set { if (Engine.Session is { } s) s.LutName = value; }
    }

    static EngineModel Engine => EngineModel.Shared;

    public DetailPanel() {
        Spacing = SectionPlate.Gap;
        Children.Add(new SectionPlate("Noise reduction",
            "Measured from this frame, so 1.00 means \"remove what is smaller than one standard deviation of its own noise\" " +
            "rather than a fixed amount. Detail backs the threshold off where the picture has more texture than noise, so " +
            "rock and fabric keep their grain while flat areas still clean up; 0 is the threshold every photograph so far " +
            "was rendered with. Non-local averages pixels whose neighbourhoods match instead of pixels that are merely near, " +
            "so fabric and brick keep their weave through a removal Luminance would smooth; 0.4 is the published strength, " +
            "and with it on Luminance is usually best left at zero. Smoothness reaches the large colour blotches Color " +
            "leaves in the shadows of a high-ISO frame: two coarser scales on the colour alone, at Color times Smoothness, " +
            "so it does nothing while Color is zero.",
            new AdjustmentSlider("Luminance", "denoise_luma", 0, 4),
            new AdjustmentSlider("Non-local", "denoise_nlm", 0, 1),
            new AdjustmentSlider("Detail", "denoise_detail", 0, 1),
            new AdjustmentSlider("Color", "denoise_color", 0, 4),
            new AdjustmentSlider("Smoothness", "denoise_smoothness", 0, 1)));

        // spotHeal, spotRadius and spotFeather are what the next spot is placed with: the canvas's SpotModel
        // holds them, and the spots, which reach the engine and the sidecar (Engine+Spots.swift).
        var heal = new Segmented(["Heal", "Clone"], SpotModel.Heal ? 0 : 1);
        heal.Changed += i => SpotModel.Heal = i == 0;
        _undoSpot = Develop.SmallButton("Undo spot", SpotModel.RemoveLast);
        _clearSpots = Develop.SmallButton("Clear", SpotModel.Clear);
        Children.Add(new SectionPlate("Spots",
            "Turn on Place spots, then drag on the photo: press where the dust is and pull to choose what covers it. " +
            "Drag either circle afterwards to move it — solid is the spot, dashed is where the replacement comes from. " +
            "Heal takes the brightness from around the spot and the detail from the source, which is what makes it " +
            "invisible on a sky. ⚠ It has one known limit: placed across a hard edge the correction is wrong on both " +
            "sides, because it is a single number for the whole disc.",
            Develop.Row(8, [heal], _spotCount),
            Develop.LocalSlider("Size", 0.004f, 0.12f, (float)SpotModel.Radius, 3, v => SpotModel.Radius = v),
            Develop.LocalSlider("Softness", 0, 1, (float)SpotModel.Feather, 2, v => SpotModel.Feather = v),
            Develop.Row(8, [new ToolButton(CanvasTool.Spot, "bandage", "Place spots", "Placing — drag on the photo",
                                           help: "Press on the photo to place a spot, then drag out its source")]),
            Develop.Row(8, [_undoSpot, _clearSpots])));

        _lutRemove = Develop.TextButton("Remove", Palette.Faint, () => {
            DevelopFacade.orion_engine_clear_lut(Engine.Handle);
            (_lutName, _lutError.Text) = ("", null);
            Engine.Render(preview: false);
            ShowLut();
        });
        Children.Add(new SectionPlate("Look",
            "A .cube look, applied last — after the tone curve, to a finished picture. Tetrahedral interpolation, so a " +
            "LUT with a hard edge in it keeps the edge.",
            Develop.Row(6, [_lutButton], _lutRemove), _lutError, _lutStrength));

        Children.Add(new SectionPlate("Shadow lift",
            "Opens the shadows without flattening them: the picture is blended with brighter versions of itself, " +
            "feature by feature rather than tone by tone, so local contrast survives.",
            new AdjustmentSlider("Lift", "fusion", 0, 1)));
        Children.Add(new SectionPlate("Grain",
            "Film grain, added to the finished picture rather than to the scene — it belongs to the print, not to the " +
            "light. Loudest in the midtones and silent at both ends, which is how a real emulsion behaves. Size is in " +
            "pixels of the *negative*, so cropping enlarges the grain instead of resampling it.",
            new AdjustmentSlider("Amount", "grain_amount", 0, 0.06f, decimals: 3),
            new AdjustmentSlider("Size", "grain_size", 1.2f, 8, " px", 1)));
        Children.Add(new SectionPlate("Vignette",
            "A deliberate falloff toward the corners of the crop, not a lens correction — the Lens panel's Vignetting " +
            "takes one out, this puts one in, and a photograph can carry both. Amount is the exposure change at the " +
            "corner in stops. Field angle is the half-diagonal angle of view of the lens whose natural cos⁴ falloff is " +
            "being imitated: wide reaches into the frame, narrow stays in the corners.",
            new AdjustmentSlider("Amount", "vignette_amount", -3, 3, " EV"),
            new AdjustmentSlider("Field angle", "vignette_field_angle", 10, 70, "°", 0)));
        Children.Add(new SectionPlate("Dehaze",
            "Estimates how much of each part of the picture is haze rather than subject, and subtracts it. It has a " +
            "known blind spot: a large pale surface with no shadow on it — white stone, a white car — reads as haze, " +
            "and will darken.",
            new AdjustmentSlider("Dehaze", "dehaze", 0, 1)));
        Children.Add(new SectionPlate("Clarity",
            "Local contrast, edge by edge rather than everywhere at once — a large edge keeps its shape while the " +
            "texture inside it gains or loses contrast. Negative smooths. Both read the frame's own measured noise: " +
            "Clarity declines to amplify grain that sits under it, and Texture lifts the two finest scales only where " +
            "they stand above it, so a surface comes back after a hard denoise without the grain. Negative Texture " +
            "smooths weak texture such as skin and leaves edges alone.",
            new AdjustmentSlider("Clarity", "clarity", -1, 1),
            new AdjustmentSlider("Texture", "texture", -1, 1)));
        Children.Add(new SectionPlate("Sharpening", "Masking protects flat areas, where noise lives and detail does not.",
            new AdjustmentSlider("Amount", "sharpen_amount", 0, 2),
            new AdjustmentSlider("Radius", "sharpen_radius", 0.5f, 3, " px", 1),
            new AdjustmentSlider("Masking", "sharpen_masking", 0, 1)));

        CanvasState.Changed += ShowSpots;   // the overlay places, moves and removes through CanvasState.Touch
        // A kept session keeps its LUT and its name (#318): coming back to the photo, Remove is there.
        Engine.Opened += () => { _lutError.Text = null; ShowLut(); };
        ShowSpots();
        ShowLut();
    }

    static int SpotCount => SpotModel.Spots.Count;

    /// The loaded .cube's TITLE, "" when none (or when it has no TITLE).
    static string LutTitle() {
        var title = new byte[256];
        return Engine.Handle != 0 && DevelopFacade.orion_engine_lut_title(Engine.Handle, title, title.Length) == 0 ? DevelopFacade.CString(title) : "";
    }

    void ShowSpots() {
        int n = SpotCount;
        _spotCount.Text = n.ToString(CultureInfo.InvariantCulture);
        _undoSpot.IsEnabled = _clearSpots.IsEnabled = n > 0;
    }

    void ShowLut() {
        _lutButton.Content = Develop.PanelButton(_lutName.Length == 0 ? "Load LUT…" : _lutName, () => _ = PickLut());
        _lutRemove.IsVisible = _lutStrength.IsVisible = _lutName.Length > 0;
        _lutError.IsVisible = !string.IsNullOrEmpty(_lutError.Text);
    }

    /// Engine.loadLut: the .cube's own TITLE, else the file's name.
    async Task PickLut() {
        if (TopLevel.GetTopLevel(this)?.StorageProvider is not { } storage) return;
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions {
            AllowMultiple = false, FileTypeFilter = [new FilePickerFileType("Cube LUT") { Patterns = ["*.cube"] }],
        });
        if (files.Count == 0 || files[0].TryGetLocalPath() is not { } path) return;
        int status = DevelopFacade.orion_engine_load_lut(Engine.Handle, path);
        if (status != 0) {
            _lutError.Text = DevelopFacade.ErrorText(status);
        } else {
            string named = LutTitle();
            (_lutName, _lutError.Text) = (named.Length > 0 ? named : System.IO.Path.GetFileNameWithoutExtension(path), null);
            Engine.Render(preview: false);
        }
        ShowLut();
    }
}

using Avalonia;

namespace OrionShell.CanvasSlice;

/// The Engine+Geometry.swift calls the crop tools make. Unlike masks and spots,
/// every number they touch is an OrionAdjustments field EngineModel reaches, so
/// these are ports, not stubs; only the history entries are.
static class CropEdit {
    static EngineModel Engine => EngineModel.Shared;
    static float Get(string field) => Engine.Handle == 0 ? 0 : Engine.Get(field);

    /// The crop, normalized against the turned frame.
    public static Rect Crop => new(Get("crop_x"), Get("crop_y"), Get("crop_w"), Get("crop_h"));
    public static bool Previewing => Get("crop_preview") != 0;
    static double Angle => Get("straighten_deg");

    /// `previewCanvas`: the region the crop preview renders, in crop coordinates.
    public static (Point Origin, double Size) Canvas =>
        Previewing ? CanvasLayout.PreviewCanvas(CanvasNative.FrameAspect, Angle) : (default, 1);

    static (double Angle, double Aspect) _seen = (double.NaN, double.NaN);
    static bool _following;

    static CropEdit() {
        Engine.Rendered += Follow;
        Engine.Opened += () => {
            _seen = (double.NaN, double.NaN);
            // crop_preview lives in the session, and sessions are kept (#318): a photo left
            // on the Crop tab came back showing the whole frame, and one arriving on it
            // showed its crop with the tool's rectangle drawn over the wrong picture.
            Preview(CanvasState.Tab == "Crop");
        };
    }

    /// Engine.swift's `straightenDeg` didSet and the quarter turn's frame swap:
    /// the crop is pulled back inside the turned frame and the preview canvas
    /// regrows to cover it. Runs after any render, so a slider is enough to trip it.
    static void Follow() {
        if (_following || Engine.Handle == 0) return;
        var now = (Angle, CanvasNative.FrameAspect);
        if (now == _seen) return;
        _seen = now;
        _following = true;
        try {
            SetCrop(Crop);
            if (Previewing) Preview(true);
        } finally {
            _following = false;
        }
    }

    /// `setCrop`: constrained to the turned frame, then written in one batch.
    public static void SetCrop(Rect r) {
        var c = CanvasLayout.ConstrainedCrop(r, CanvasNative.FrameAspect, Angle);
        CanvasState.Write(("crop_w", c.Width), ("crop_h", c.Height), ("crop_x", c.X), ("crop_y", c.Y));
    }

    /// `cropPreview`: the whole turned frame with the crop as context, while the crop tab is open.
    public static void Preview(bool on) {
        var (origin, size) = on ? CanvasLayout.PreviewCanvas(CanvasNative.FrameAspect, Angle) : (default(Point), 1.0);
        CanvasState.Write(("preview_x", origin.X), ("preview_y", origin.Y), ("preview_size", size), ("crop_preview", on ? 1 : 0));
    }

    /// `setAspect`: the largest centered rectangle of that ratio; nil is `resetCrop`.
    public static void SetAspect(double? ratio) {
        if (ratio is not { } r) { Reset(); return; }
        double frame = CanvasNative.FrameAspect, w = 1, h = 1;
        if (r > frame) h = frame / r; else w = r / frame;
        Commit("Aspect");
        CanvasState.Write(("crop_w", w), ("crop_h", h), ("crop_x", (1 - w) / 2), ("crop_y", (1 - h) / 2));
    }

    /// `resetCrop`: the whole frame, upright.
    public static void Reset() {
        if (Engine.Handle == 0 || (Crop == new Rect(0, 0, 1, 1) && Angle == 0)) return;
        Commit("Crop");
        // Through Apply, which tells the sliders their values moved: after an Escape the
        // Angle slider went on showing the old angle and the next drag started from it.
        Engine.Apply(s => {
            foreach (var (field, value) in (ReadOnlySpan<(string, float)>)[("crop_x", 0), ("crop_y", 0), ("crop_w", 1), ("crop_h", 1), ("straighten_deg", 0)])
                s.Set(field, value);
        }, reread: true);
    }

    /// `rotate`: a quarter turn swaps the frame, so the crop resets rather than
    /// silently reframing the picture.
    public static void Rotate(int turns) {
        int quarters = (((int)Get("rotate_quarters") + turns) % 4 + 4) % 4;
        Commit("Rotate");
        CanvasState.Write(("crop_x", 0), ("crop_y", 0), ("crop_w", 1), ("crop_h", 1), ("rotate_quarters", quarters));
        CanvasState.Viewport.Reset();
    }

    /// `engine.edit(label)`: names the history step the next settled render records.
    /// Called before the writes, as Swift's closure wraps them.
    public static void Commit(string label) => Develop.EditHistory.Label(label);
}

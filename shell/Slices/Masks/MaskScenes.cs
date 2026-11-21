namespace OrionShell.Masks;

/// The window on `--tab Mask --screenshot`, posed: a brush stroke, a radial
/// lifted a stop and a path darkened (selected, its overlay up), so the cards,
/// the path's points and handles and two pins all show. `--panel masks` stays empty: the empty state is
/// the first thing anyone sees there. The window and --selftest never pose otherwise.
static class MaskScenes {
    static bool _posed;

    public static void PoseIfAsked() {
        var run = App.Run;
        bool asked = run.Panel == null && run.Screenshot != null && run.Tab == "Mask";
        if (!asked || _posed || EngineModel.Shared.Session is not { } session) return;
        _posed = true;
        var group = MaskGroup.Shared;
        group.Add(3, m => {
            for (int i = 0; i <= 24; i++) m.BrushStroke.AddRange([0.62f + 0.012f * i, 0.30f + 0.002f * i]);
            m.BrushRadius = 0.05f;
        });
        session.Set("local_exposure_ev", 0.8f, 0);
        group.Add(2, m => (m.CenterX, m.CenterY, m.Angle, m.RadiusX, m.RadiusY, m.Feather) = (0.34f, 0.55f, 0.6f, 0.26f, 0.16f, 0.35f));
        session.Set("local_exposure_ev", 1, 1);
        // Two corners and two smooth points: x, y, the handle in, the handle out.
        group.Add(4, m => {
            m.PathPoints.AddRange([0.56f, 0.62f, 0.56f, 0.62f, 0.56f, 0.62f,   0.74f, 0.52f, 0.68f, 0.50f, 0.80f, 0.54f,
                                   0.88f, 0.70f, 0.88f, 0.70f, 0.88f, 0.70f,   0.70f, 0.86f, 0.80f, 0.86f, 0.60f, 0.86f]);
            m.Feather = 0;
        });
        session.Set("local_exposure_ev", -0.6f, 2);
        group.Push("Pose");
    }
}

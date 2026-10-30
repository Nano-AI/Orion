using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;

namespace OrionShell.Develop;

/// A port of the Swift app's DevelopPanels+Optics.swift: the lens profile, and the manual
/// corrections it stands in for. Distortion and vignetting are the only
/// sliders a database can switch off, so they grey out while a profile applies.
[Panel("develop-optics", Region.Tools, 10, Tab = "Optics")]
public sealed class OpticsPanel : StackPanel {
    readonly AdjustmentSlider _distortion = new("Distortion", "lens_distortion", -1, 1);
    readonly AdjustmentSlider _vignetting = new("Vignetting", "lens_vignette", -1, 1);
    readonly AdjustmentSlider _fringeRC = new("Fringe R/C", "lens_ca_red", -1, 1, tint: TrackTint.FringeRC);
    readonly AdjustmentSlider _fringeBY = new("Fringe B/Y", "lens_ca_blue", -1, 1, tint: TrackTint.FringeBY);
    readonly LensPicker _picker;
    SectionPlate? _plate;

    /// Engine.lensProfileEnabled: on by default when a profile is found, since a
    /// correction the lens is known to need is not a creative decision. The
    /// engine's, not the photo's: the sidecar does not hold it and an open does
    /// not reset it, so a switch turned off stays off for every photo after.
    static bool _profileEnabled = true;

    static EngineModel Engine => EngineModel.Shared;

    public OpticsPanel() {
        _picker = new LensPicker(this);
        Engine.Opened += () => Refresh();
        Refresh();
    }

    /// Engine.swift:779 — lens_profile is (enabled && found), pushed whenever either changes.
    internal void Refresh(bool render = false) {
        var (name, approximate, source) = DevelopFacade.LensProfile();
        bool found = name.Length > 0, profiled = found && _profileEnabled;
        if (Engine.Handle != 0 && (render || Engine.Get("lens_profile") != (profiled ? 1 : 0)))
            Engine.Set("lens_profile", profiled ? 1 : 0);

        var rows = new List<Control>();
        if (found) {
            var toggle = new ToggleSwitch {
                IsChecked = _profileEnabled, OnContent = null, OffContent = null,
                Content = new StackPanel { Spacing = 1, Children = {
                    new TextBlock { Text = "Lens profile", FontSize = Faces.Label },
                    new TextBlock { Text = name, FontSize = Faces.Mark, Foreground = Palette.Dim, TextWrapping = Avalonia.Media.TextWrapping.Wrap, MaxLines = 2 },
                } },
            };
            toggle.IsCheckedChanged += (_, _) => { _profileEnabled = toggle.IsChecked == true; Refresh(render: true); };
            rows.Add(toggle);
            rows.Add(Develop.Note(source == DevelopFacade.LensSource.Dng
                ? "The camera's own correction, written into this DNG. It takes precedence over any profile. " +
                  "The two sliders below are what you would use instead."
                : approximate
                ? "Measured for a lens the database spells differently, so this is the nearest match rather than your " +
                  "exact copy. Turn it off to correct by hand."
                : "Distortion and vignetting measured for this lens at this focal length, from the lensfun database. " +
                  "The two sliders below are what you would use instead."));
        } else {
            // Two sentences because they have two fixes: a lens the database lacks
            // is a data gap; a file naming no lens is every manual lens ever made.
            string lens = DevelopFacade.Text(Engine.Handle == 0 ? 0 : DevelopFacade.orion_engine_lens(Engine.Handle));
            rows.Add(Develop.Note(lens.Length == 0
                ? "This file records no lens name, which is what an adapted or fully manual lens does — there is " +
                  "nothing to match against. Correct it by hand below."
                : $"No profile for “{lens}” in the bundled lensfun database, so distortion and vignetting are manual below."));
        }
        foreach (var s in new[] { _distortion, _vignetting }) {
            s.IsEnabled = !profiled;
            s.Opacity = profiled ? 0.4 : 1;
        }
        rows.AddRange([_picker, _distortion, _vignetting, _fringeRC, _fringeBY]);

        // The ⓘ text depends on the profile, and SectionPlate takes it once: rebuild the plate, keep the controls.
        _plate?.Rows.Children.Clear();
        Children.Clear();
        _plate = new SectionPlate("Lens", found
            ? "The fringe controls stay manual: the database's chromatic aberration figures are per-copy, and a wrong " +
              "one adds colored edges rather than removing them."
            : "No profile for this lens — every setting here is by eye. Negative distortion pulls the barrel out of a " +
              "wide lens; negative vignetting lifts the corners. The fringe controls rescale red and blue against " +
              "green, which is what removes the colored edges at the frame's corners.",
            rows.ToArray());
        Children.Add(_plate);
    }

    /// Choose a profile by typing part of its name: a filter over ~1,450
    /// lenses, not a menu of them. Nothing shows until two characters are typed,
    /// then at most a dozen matches, every typed word present in any order.
    sealed class LensPicker : StackPanel {
        readonly OpticsPanel _panel;
        readonly TextBox _query = new() {
            PlaceholderText = "Find a lens…", FontSize = Faces.Label, MinHeight = 0, Padding = new Thickness(6, 3),
            Background = Palette.Panel, BorderBrush = Palette.Raised, CornerRadius = new CornerRadius(Radius.Md),
        };
        readonly StackPanel _results = new() { Spacing = 6 };
        readonly ContentControl _chosen = new();

        public LensPicker(OpticsPanel panel) {
            _panel = panel;
            Spacing = 6;
            Children.Add(_chosen);
            Children.Add(_query);
            Children.Add(_results);
            _query.TextChanged += (_, _) => Show();
            Show();
        }

        IEnumerable<string> Matches(string q) {
            if (q.Length < 2) return [];
            var words = q.ToLowerInvariant().Split(' ', StringSplitOptions.RemoveEmptyEntries);
            return DevelopFacade.LensCatalogue.Value.Where(n => words.All(n.ToLowerInvariant().Contains)).Take(12);
        }

        void Show() {
            string choice = DevelopFacade.Text(EngineModel.Shared.Handle == 0 ? 0 : DevelopFacade.orion_engine_lens_choice(EngineModel.Shared.Handle));
            _chosen.Content = choice.Length == 0 ? null
                : Develop.Row(6, [Engraved.Label("Chosen by hand", Palette.Dim)], Develop.TextButton("Clear", Palette.Accent, () => Choose("")));
            string q = (_query.Text ?? "").Trim();
            var matches = Matches(q).ToList();
            _results.Children.Clear();
            foreach (var name in matches) {
                var b = Develop.TextButton(name, name == choice ? Palette.Accent : Palette.Text, () => Choose(name));
                b.HorizontalAlignment = HorizontalAlignment.Stretch;
                b.HorizontalContentAlignment = HorizontalAlignment.Left;
                _results.Children.Add(b);
            }
            if (q.Length >= 2 && matches.Count == 0)
                _results.Children.Add(Develop.Note("Nothing in the bundled database matches that."));
            // An empty slot takes no spacing, as an absent SwiftUI view takes none.
            _chosen.IsVisible = _chosen.Content != null;
            _results.IsVisible = _results.Children.Count > 0;
        }

        /// Engine.applyLensChoice: seat it, then show what the engine settled on —
        /// a refused name leaves the previous profile standing.
        void Choose(string name) {
            if (EngineModel.Shared.Handle == 0) return;
            DevelopFacade.orion_engine_set_lens_choice(EngineModel.Shared.Handle, name);
            _query.Text = "";
            Show();
            _panel.Refresh(render: true);
        }
    }
}

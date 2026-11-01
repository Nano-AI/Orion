using Avalonia.Markup.Xaml;
using Avalonia.Media;
using Avalonia.Styling;

namespace OrionShell;

public partial class Theme : Styles {
    public Theme() {
        AvaloniaXamlLoader.Load(this);
        // Fluent's Window sets its face from this resource ("Inter, $Default"), and a
        // later Styles' resource wins: so the words are the platform's (DESIGN.md).
        Resources["ContentControlThemeFontFamily"] = Faces.Words;
        // Grayscale text in every window and popup, as macOS draws it. Skia's default off macOS is
        // LCD subpixel, which fringes blue and orange over translucent chrome (the zoom pill on a
        // white photo) and in any rendered copy; TextOptions inherit down from the TopLevel (#395).
        Avalonia.Controls.Control.LoadedEvent.AddClassHandler<Avalonia.Controls.TopLevel>(
            (top, _) => TextOptions.SetTextRenderingMode(top, TextRenderingMode.Antialias));
        // Fluent's accent, every shade the one token: the dark theme reads the Light
        // shades. Set here rather than as hex in Theme.axaml, which a token change missed.
        var accent = ((ISolidColorBrush)Palette.Accent).Color;
        foreach (var shade in new[] { "", "Light1", "Light2", "Light3", "Dark1", "Dark2", "Dark3" })
            Resources["SystemAccentColor" + shade] = accent;
    }
}

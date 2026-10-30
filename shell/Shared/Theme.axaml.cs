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
        // Fluent's accent, every shade the one token: the dark theme reads the Light
        // shades. Set here rather than as hex in Theme.axaml, which a token change missed.
        var accent = ((ISolidColorBrush)Palette.Accent).Color;
        foreach (var shade in new[] { "", "Light1", "Light2", "Light3", "Dark1", "Dark2", "Dark3" })
            Resources["SystemAccentColor" + shade] = accent;
    }
}

using Avalonia.Markup.Xaml;
using Avalonia.Styling;

namespace OrionShell;

public partial class Theme : Styles {
    public Theme() {
        AvaloniaXamlLoader.Load(this);
        // Fluent's Window sets its face from this resource ("Inter, $Default"), and a
        // later Styles' resource wins: so the words are the platform's (DESIGN.md).
        Resources["ContentControlThemeFontFamily"] = Faces.Words;
    }
}

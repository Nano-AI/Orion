using Avalonia.Markup.Xaml;
using Avalonia.Styling;

namespace OrionShell;

public partial class Theme : Styles {
    public Theme() => AvaloniaXamlLoader.Load(this);
}

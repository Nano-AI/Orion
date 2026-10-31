using Avalonia;
using Avalonia.Controls;

namespace OrionShell.Chrome;

/// The footer's one line about an update (#389): shown only once Velopack has
/// downloaded one, with a Restart button. Quitting works too: the next launch applies it.
sealed class UpdateNotice : StackPanel {
    public UpdateNotice() {
        Spacing = Space.Sm;
        Margin = new Thickness(0, Space.Gap);
        Children.Add(Engraved.Note("Update ready, restart to apply."));
        Children.Add(new StateButton(null, "Restart", () => Commands.Run("restart-to-update"), help: "Restart Orion on the new version"));
        Updates.Changed += () => IsVisible = Updates.Ready != null;
        IsVisible = Updates.Ready != null;
    }
}

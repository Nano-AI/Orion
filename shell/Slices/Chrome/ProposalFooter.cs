using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;

namespace OrionShell.Chrome;

/// The footer's proposal block (OrionApp+Chrome.swift): shown only while an
/// agent's proposal is up, with what it changes and Approve (⌘⏎) and Reject
/// (⌘⎋). While it is up the tool column's panels and tabs are locked
/// (OrionApp+Tools.swift's `.disabled(documentEditsLocked)`); this block, in
/// the footer below them, stays live.
sealed class ProposalFooter : StackPanel {
    readonly TextBlock _keys = new() { FontSize = Faces.Label, Foreground = Palette.Text, TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                                       MaxLines = 2, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis };

    public ProposalFooter() {
        Spacing = Space.Sm;
        Margin = new Thickness(0, Space.Gap);
        Children.Add(Engraved.Label("Proposed edit", Palette.Accent));
        Children.Add(_keys);
        Children.Add(Engraved.Note("Approve or reject to keep editing. Both sides use the proposed framing."));
        Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, Children = {
            new StateButton(null, "Approve", () => Commands.Run("approve-proposal"), help: "Approve the proposed edit  (⌘⏎)"),
            new StateButton(null, "Reject", () => Commands.Run("reject-proposal"), help: "Reject the proposed edit  (⌘⎋)"),
        } });
        ProposalWatcher.Shared.Changed += Refresh;
        CurrentPhoto.Install();
        ProposalWatcher.Install();
        Refresh();
    }

    void Refresh() {
        var w = ProposalWatcher.Shared;
        IsVisible = w.IsLive;
        _keys.Text = string.Join(", ", w.Keys.Select(Proposal.ProductLabel));
        ToolTip.SetTip(_keys, _keys.Text);
        // The panels and the tabs: a manual edit now would be neither saved nor undoable.
        foreach (var name in new[] { "ToolsHost", "TabBar" })
            if (App.Window?.FindControl<Control>(name) is { } c) c.IsEnabled = !w.IsLive;
    }
}

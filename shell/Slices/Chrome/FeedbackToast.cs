using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;

namespace OrionShell.Chrome;

/// The occasional ask for feedback (FeedbackReminder decides when), at the
/// canvas's top left under the toolbar. It stays until answered or closed (✕
/// is Later), never takes focus, and covers nothing but itself.
/// Registered as a panel only so `--panel feedback-toast --screenshot` poses it.
[Panel("feedback-toast", Region.Sheet)]
public sealed class FeedbackToast : Panel {
    public const string Ask = "How's Orion working for you? Two minutes of feedback helps.";
    static readonly TransformOperations High = TransformOperations.Parse("translateY(-8px)"), Still = TransformOperations.Parse("translateY(0px)");

    /// The card under the words: its fill, accent edge and shadow. A control with no children, because a
    /// BoxShadow drawn by a control that has them (a Border's, or a Decorator's Render) drew those children
    /// at twice their size in a --screenshot.
    sealed class Card : Control {
        static readonly IPen Edge = new Pen(Palette.Accent, 1);
        static readonly BoxShadows Deep = AnalogTrack.Lift(Space.Gap, Space.Xl);
        public override void Render(DrawingContext c) =>
            c.DrawRectangle(Palette.Panel, Edge, new RoundedRect(new Rect(Bounds.Size).Deflate(0.5), Radius.Lg), Deep);
    }

    public FeedbackToast() {
        (Margin, MaxWidth) = (new Thickness(Space.Pad), 360);
        (HorizontalAlignment, VerticalAlignment, Focusable) = (HorizontalAlignment.Left, VerticalAlignment.Top, false);
        var close = StateButton.Glyph("xmark", "Close", Hide, size: IconSize.Sm);
        (close.Width, close.Height, close.MinWidth, close.MinHeight, close.Focusable) = (StateButton.MinSize, StateButton.MinSize, 0, 0, false);
        (close.VerticalAlignment, close.Margin) = (VerticalAlignment.Top, new Thickness(Space.Sm, -Space.Sm, -Space.Sm, 0));
        var ask = new TextBlock { Text = Ask, FontSize = Faces.Body, LineHeight = 20, Foreground = Palette.Text, TextWrapping = TextWrapping.Wrap };
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, Margin = new Thickness(0, Space.Md, 0, 0), Children = {
            Button("Give feedback", () => { Hide(); Feedback.Give(); }, ButtonKind.Primary),
            Button("Later", Hide, ButtonKind.Standard),
            Button("Never show again", () => { Hide(); Feedback.Never(); }, ButtonKind.Bare, Palette.Dim),
        } };
        Grid.SetColumn(close, 1);
        Grid.SetRow(buttons, 1);
        Grid.SetColumnSpan(buttons, 2);
        Children.AddRange([new Card(), new Grid { ColumnDefinitions = new("*,Auto"), RowDefinitions = new("Auto,Auto"), Margin = new Thickness(Space.Lg),
                                                  Children = { ask, close, buttons } }]);
    }

    /// Fades and drops 8 px in from the toolbar, Motion.Enter long; at once under reduced motion.
    public void Show() {
        Transitions = null;
        (Opacity, RenderTransform, IsVisible) = (Motion.Reduced ? 1 : 0, Motion.Reduced ? Still : High, true);
        Transitions = [new DoubleTransition { Property = OpacityProperty, Duration = Motion.Enter, Easing = Motion.EaseOut },
                       new TransformOperationsTransition { Property = RenderTransformProperty, Duration = Motion.Enter, Easing = Motion.EaseOut }];
        (Opacity, RenderTransform) = (1, Still);
    }

    public void Hide() => IsVisible = false;

    static StateButton Button(string title, Action run, ButtonKind kind, IBrush? ink = null) =>
        new(null, title, run, kind) { Focusable = false, Ink = ink };
}

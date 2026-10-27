namespace OrionShell.Sheets;

/// A port of the parts of app/Watermark.swift the two sheets read: what the
/// mark is, where it goes, how big and how present. One gray, always (#284).
// Loading and saving watermark.json in Application Support:
// stub(#315): Watermark.swift owns this until the logic moves below the facade
sealed class Watermark {
    public static Watermark Shared { get; } = new();

    /// The 3x3 grid, row-major, the order the panel draws it in.
    public static readonly string[] Anchors =
        ["top left", "top", "top right", "left", "center", "right", "bottom left", "bottom", "bottom right"];

    public bool Enabled;
    // Content: text (Words, Family, Bold) or an SVG (only its name lives here).
    public bool IsText = true;
    public string Words = "", Family = "Helvetica Neue", SvgName = "";
    public bool Bold;
    /// An index into Anchors, or -1 for the diagonal.
    public int Anchor = 8;
    public double Size = 0.04, Span = 0.6, Margin = 0.03, Opacity = 0.35;
    /// Why the last save did not land. Always null: saving is stubbed.
    public string? LastFailure => null;

    public bool Diagonal => Anchor < 0;

    /// A mark switched on with no text and no file writes nothing.
    public bool IsDrawable => IsText ? Words.Trim().Length > 0 : SvgName.Length > 0;

    /// One line for the Export sheet's row: what the mark is and where it goes.
    public string Summary {
        get {
            if (!IsDrawable) return "Nothing to draw yet";
            string what = IsText ? $"“{Words}”" : SvgName;
            return $"{what}, {(Diagonal ? "diagonal" : Anchors[Anchor])}";
        }
    }

    // stub(#315): Watermark.swift owns this until the logic moves below the facade
    public void Save() { }
}

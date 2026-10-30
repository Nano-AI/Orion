namespace OrionShell;

public sealed partial class Session {
    /// Engine.lutName: the loaded .cube's TITLE, else the name of the file that
    /// was picked; "" with none. Kept here because the facade knows only the
    /// TITLE, and a kept session keeps its LUT (#318), so its name goes with it.
    public string LutName { get; set; } = "";
}

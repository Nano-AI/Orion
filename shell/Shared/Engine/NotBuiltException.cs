namespace OrionShell;

/// A seam whose body a later stage builds (#315 stage 1b). Callers catch it and
/// put the message on the status line, so a missing piece says so instead of
/// doing nothing. EngineModel.Apply does that for them.
public sealed class NotBuiltException(string what, string owner) : Exception(NotBuilt.Text(what, owner));

/// What a piece not ported yet says: what it is, in the user's words, on the
/// status line. The Swift file that still owns it goes to the log, for the
/// developer; a file name on screen read as a broken build (#349).
public static class NotBuilt {
    public static string Text(string what, string owner) {
        Console.Error.WriteLine($"not built yet: {what} ({owner})");
        return $"{char.ToUpperInvariant(what[0])}{what[1..]} isn't in this version yet.";
    }

    public static void Say(string what, string owner) => EngineModel.Shared.Say(Text(what, owner));
}

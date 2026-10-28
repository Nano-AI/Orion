namespace OrionShell;

/// A seam whose body a later stage builds (#315 stage 1b). Callers catch it and
/// put the message on the status line, so a missing piece says so instead of
/// doing nothing. EngineModel.Apply does that for them.
public sealed class NotBuiltException(string what, string owner)
    : Exception($"not built yet: {what} ({owner})");

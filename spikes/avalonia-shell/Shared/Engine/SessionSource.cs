namespace OrionShell;

/// How the session for photo i is obtained. EngineModel asks for the photo it
/// shows, hands it back when it leaves, and says which way the hand is going.
/// Stage 1a puts the direction-biased two-tier cache (decision #318) behind
/// this: decoded photos plus small rendered pictures, without touching panels.
public interface ISessionSource {
    /// The session for photo `index` of the list, or null when it will not open.
    /// EngineModel holds it until Release; the source must not close it before.
    Session? Acquire(int index);

    /// EngineModel has stopped showing it: close it, or keep it warm.
    void Release(Session session);

    /// The photo now shown and the direction of travel: +1 forward, -1 back,
    /// 0 for a jump with no direction. Called after every Acquire.
    void Hint(int index, int direction);

    /// The list changed: drop everything kept, never the session acquired.
    void Clear();
}

/// Today's behaviour: every photo decodes afresh and closes when it is left.
public sealed class FreshSessions(PhotoList photos) : ISessionSource {
    public Session? Acquire(int index) => Session.Open(photos.Paths[index]);
    public void Release(Session session) => session.Dispose();
    public void Hint(int index, int direction) { }
    public void Clear() { }
}

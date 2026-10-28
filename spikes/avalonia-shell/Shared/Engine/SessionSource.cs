using Avalonia.Media.Imaging;

namespace OrionShell;

/// How the session for photo i is obtained. EngineModel says which way the
/// hand is going, shows the photo's kept picture, asks for its session, and
/// hands the session back when it leaves. SessionCache is the two-tier cache
/// of decision #318; FreshSessions is how it was before.
public interface ISessionSource {
    /// The session for photo `index` of the list, or null when it will not open.
    /// Blocking. EngineModel holds it until Release; the source must not close it before.
    Session? Acquire(int index);

    /// Hands photo `index`'s session to `ready` on the UI thread: at once when
    /// it is warm, after a background decode when not, null when it will not
    /// open. A newer Request replaces one not yet answered, so held arrow keys
    /// open only the photo they stop on. The caller holds it until Release.
    void Request(int index, Action<Session?> ready);

    /// The screen-sized picture kept for photo `index`, to show before its
    /// session is ready. Null when none is kept.
    Bitmap? Picture(int index);

    /// EngineModel has stopped showing it: close it, or keep it warm.
    void Release(Session session);

    /// The photo now shown and the direction of travel: +1 forward, -1 back,
    /// 0 for a jump with no direction. Called before every Request.
    void Hint(int index, int direction);

    /// The list changed: drop everything kept, never the session acquired.
    void Clear();

    /// The same photos moved (a sort) or some left (a trash): `to[old]` is each
    /// one's new index, -1 when it left. What is kept follows its photo.
    void Moved(IReadOnlyList<int> to);
}

/// Before the cache: every photo decodes afresh and closes when it is left.
public sealed class FreshSessions(PhotoList photos) : ISessionSource {
    public Session? Acquire(int index) => Session.Open(photos.Paths[index]);
    public void Request(int index, Action<Session?> ready) => ready(Acquire(index));
    public Bitmap? Picture(int index) => null;
    public void Release(Session session) => session.Dispose();
    public void Hint(int index, int direction) { }
    public void Clear() { }
    public void Moved(IReadOnlyList<int> to) { }
}

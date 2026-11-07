using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using OrionShell.Library;

namespace OrionShell.CanvasSlice;

/// Lightroom Classic's Compare view (C), decision #400: two photographs side by
/// side, the select on the left (the photo open in the editor) and a candidate
/// on the right, zoomed and panned together while linked. Each side has its own
/// Viewport; linked, whichever a hand moves leads and the other mirrors it, so
/// both show the same normalized part of their picture. Ratings and rejects go
/// to the active side only.
///
/// The select's session is EngineModel's own; the candidate's is borrowed from
/// the cache (SessionCache.Borrow) and handed back when it changes. Each side
/// renders only what it shows: the preview graph at fit, past fit the region in
/// view at the pane's device pixels (#22), never a full graph unless the engine
/// says the edit needs one. Out of GPU memory, the cache's decoded neighbours go
/// first (#392); if that is not enough the side says so in its pane.
static class SideBySide {
    public sealed class Side {
        internal Side(bool select) {
            Select = select;
            View = new Viewport(() => Moved(this));
        }
        public bool Select { get; }
        public readonly Viewport View;
        /// The photo shown, an index into EngineModel.Photos; -1 for none. The select's is the editor's.
        public int Index { get => Select ? E.Current : _index; internal set => _index = value; }
        int _index = -1;
        internal Session? Borrowed;
        public Session? Session => Select ? E.Session : Borrowed;
        /// The whole picture, screen-sized, and past fit the part in view with where it sits.
        public Bitmap? Preview { get; internal set; }
        public Bitmap? Region { get; internal set; }
        public Rect RegionUv { get; internal set; }
        /// Why there is no picture, or why it is not sharp; null when all is well.
        public string? Problem { get; internal set; }
        /// What the pane last asked for: the part in view and the device pixels it covers.
        internal (Rect Uv, PixelSize Pixels)? Wanted, Rendered;
        internal bool Stale = true;
        public LibraryModel.Photo? Photo => Index >= 0 && Index < LibraryModel.Shared.Photos.Count ? LibraryModel.Shared.Photos[Index] : null;
        public string Name => Index >= 0 && Index < E.Photos.Count ? Path.GetFileName(E.Photos[Index]) : "";
        /// Width over height of the rendered picture, 0 before there is one.
        public double Aspect => Session?.ImageSize is { Width: > 0, Height: > 0 } s ? (double)s.Width / s.Height
            : Preview is { } p ? p.Size.Width / p.Size.Height : 0;
    }

    static EngineModel E => EngineModel.Shared;
    static bool _mirroring, _posted;

    public static bool On { get; private set; }
    public static readonly Side Left = new(true), Right = new(false);
    public static Side Active { get; private set; } = Left;
    public static Side Other(Side s) => s == Left ? Right : Left;
    /// Zoom and pan move both sides (Lightroom's lock). Off, each side moves alone.
    public static bool Linked { get; private set; } = true;
    /// While a hand holds Shift: the link lets go for that gesture only (Lightroom, Photo Mechanic).
    public static bool Unhooked { get; set; }
    /// What a pane shows changed: a picture, the active side, a mark, a problem.
    public static event Action? Changed;
    /// A side's view moved: a frame of a gesture, so the panes redraw and re-place, nothing more.
    public static event Action? Viewed;
    /// The graph EngineModel's last render of the select used: a preview it has just made is read, not made again.
    static Graph _selectMade;

    static SideBySide() {
        E.Rendered += () => {   // an edit to the select, or its photo arriving
            if (!On) return;
            (Left.Stale, _selectMade) = (true, E.Shown.Graph);
            Post();
        };
        E.Opened += () => { if (On) { Left.Preview = Left.Region = null; Left.Rendered = null; Left.Stale = true; } };
        E.FolderLoaded += Exit;
        E.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(EngineModel.Photos)) Exit(); };   // a sort or a trash: indices moved
        GalleryMode.Changed += () => { if (GalleryMode.On) Exit(); };
        LibraryModel.Shared.Changed += Touch;
    }

    static void Touch() { if (On) Changed?.Invoke(); }

    /// The pair to compare: the two selected (the photo open or focused on the
    /// left when it is one of them), or with no selection the photo and the next.
    public static (int Select, int Candidate)? Pair(LibraryModel.Photo? candidate = null) {
        var lib = LibraryModel.Shared;
        var first = GalleryMode.On ? GalleryMode.Focus : lib.Open;
        // A photo named by the strip's or the gallery's menu, beside the one open: that pair, selection or not.
        if (first != null && candidate != null && candidate != first) return (first.Index, candidate.Index);
        var pool = lib.HasExplicitSelection ? lib.Targets : lib.Visible.ToList();
        if (first == null || !pool.Contains(first)) first = pool.FirstOrDefault();
        if (first == null || pool.Count < 2) return null;
        int at = pool.IndexOf(first);
        var second = lib.HasExplicitSelection ? pool.First(p => p != first) : pool[at + 1 < pool.Count ? at + 1 : at - 1];
        return (first.Index, second.Index);
    }

    /// `wait`: decode the candidate here rather than on a worker (a posed screenshot).
    /// `with`: the candidate a menu named, else the pair C takes (Pair).
    public static void Enter(bool wait = false, LibraryModel.Photo? with = null) {
        if (On) return;
        if (Pair(with) is not var (select, candidate)) { E.Say("Compare needs two photos in view: clear the filter, or select two."); return; }
        if (GalleryMode.On) GalleryMode.Leave();
        if (E.Current != select && !E.Open(select)) return;
        On = true;
        Linked = true;
        Active = Left;
        Left.Problem = null;
        Left.Stale = true;
        Left.View.Reset();
        Right.View.Reset();
        CanvasState.Viewport.Reset();   // the canvas under the panes renders nothing past fit meanwhile
        Borrow(candidate, wait);
        E.Session?.Trim(full: true);   // at fit both panes show previews: the select's full graph goes till a render needs it
        Post();
        Changed?.Invoke();
    }

    public static void Exit() {
        if (!On) return;
        On = false;
        GiveBack();
        Left.Preview = Left.Region = null;
        Left.Wanted = Left.Rendered = null;
        // The select's session rendered regions for its pane: the canvas's picture again.
        if (E.Session != null) E.Render(preview: !E.ZoomedPastFit);
        Changed?.Invoke();
    }

    public static void Toggle() { if (On) Exit(); else Enter(); }

    public static void ToggleLink() {
        Linked = !Linked;
        if (Linked) Follow(Active);   // Lightroom's Sync: the other side snaps to the active one, no hidden offset
        Changed?.Invoke();
    }

    public static void Activate(Side s) {
        if (Active == s) return;
        Active = s;
        Changed?.Invoke();
    }

    /// Down: the two trade places, so the candidate becomes the photo open in the editor.
    public static void Swap() {
        int was = Left.Index, to = Right.Index;
        if (to < 0) return;
        var (pictures, regions, uvs) = ((Left.Preview, Right.Preview), (Left.Region, Right.Region), (Left.RegionUv, Right.RegionUv));
        GiveBack();   // first, so the open below finds it warm instead of decoding it twice
        if (!E.Open(to)) { Borrow(to, false); return; }
        Borrow(was, false);
        // Each picture moves with its photo, so neither pane blanks while the two re-render.
        (Left.Preview, Right.Preview) = (pictures.Item2, pictures.Item1);
        (Left.Region, Right.Region) = (regions.Item2, regions.Item1);
        (Left.RegionUv, Right.RegionUv) = (uvs.Item2, uvs.Item1);
        Left.Stale = Right.Stale = true;
        Post();
        Changed?.Invoke();
    }

    /// Up: the candidate becomes the select, and the next photo the candidate.
    public static void MakeSelect() {
        if (Right.Index < 0) return;
        int to = Right.Index;
        GiveBack();
        if (!E.Open(to)) { Borrow(to, false); return; }
        Borrow(Next(to, 1) ?? Next(to, -1) ?? -1, false);
        Changed?.Invoke();
    }

    /// Left and Right: the next candidate, through the selection when there is one, else the photos in view.
    public static void Step(int direction) {
        if (Next(Right.Index, direction) is not int to) return;
        GiveBack();
        Borrow(to, false);
        Changed?.Invoke();
    }

    /// By list position, not by place in the pool: a candidate just rated out of the
    /// filter (culling to Unrated) is no longer in the pool, and the step still goes on from it.
    static int? Next(int from, int direction) {
        var lib = LibraryModel.Shared;
        var pool = (lib.HasExplicitSelection ? lib.Targets : lib.Visible.ToList()).Select(p => p.Index).Where(i => i != Left.Index);
        var ahead = direction > 0 ? pool.Where(i => i > from).Order() : pool.Where(i => i < from).OrderDescending();
        return ahead.Cast<int?>().FirstOrDefault();
    }

    /// The active side's photo: what the rating and reject keys mark.
    public static LibraryModel.Photo? ActivePhoto => Active.Photo;

    public static void Fit() { Unhooked = false; Active.View.Reset(); }
    public static void ActualSize() { Unhooked = false; Active.View.ToggleFitAndActual(); }

    static void GiveBack() {
        if (Right.Borrowed is { } s) E.Cache.Release(s);
        Right.Borrowed = null;
        Right.Index = -1;
        Right.Preview = Right.Region = null;
        Right.Wanted = Right.Rendered = null;
    }

    static void Borrow(int index, bool wait, bool again = true) {
        Right.Index = index;
        Right.Problem = index < 0 ? "No other photo to compare with." : null;
        Right.Stale = true;
        if (index < 0) return;
        void Ready(Session? s) {
            if (!On || Right.Index != index || Right.Borrowed != null || index >= E.Photos.Count) { if (s != null) E.Cache.Release(s); return; }
            if (s == null) {
                string why = Session.OpenFailure(E.Photos[index]) ?? "the engine could not decode it";
                // Out of GPU memory: the decoded neighbours go, then once more (#392).
                if (again && why.Contains(EngineModel.OutOfGpuMemory) && E.Cache.Shed()) { Borrow(index, wait, again: false); return; }
                Say(Right, $"Couldn't open {Path.GetFileName(E.Photos[index])}: {why}");
                return;
            }
            Right.Borrowed = s;
            Post();
            Changed?.Invoke();
        }
        if (wait) Ready(E.Cache.Acquire(index)); else E.Cache.Borrow(index, Ready);
    }

    /// A side's problem goes in its pane and on the status line: never a blank pane with no word.
    static void Say(Side s, string message) {
        s.Problem = message;
        E.Say(message);
        Changed?.Invoke();
    }

    /// A side's viewport moved. Linked, the other follows; the pane asks for the new part once it rests.
    static void Moved(Side s) {
        if (!_mirroring && Linked && !Unhooked && On) Follow(s);
        if (!s.View.Live) {
            Post();             // at rest: the part in view, sharp
            Unhooked = false;   // after the frame it moved alone in: a Shift gesture ends when its spring does
        }
        Viewed?.Invoke();
    }

    static void Follow(Side leader) {
        _mirroring = true;
        try { Other(leader).View.Mirror(leader.View); } finally { _mirroring = false; }
    }

    /// The pane's part in view, in normalized picture coordinates and device pixels, as it lays out.
    public static void Want(Side s, Rect uv, PixelSize pixels) {
        if (s.Wanted == (uv, pixels)) return;
        s.Wanted = (uv, pixels);
        Post();
    }

    /// Renders once the hands and springs have rested: a pan draws the last region over the preview meanwhile.
    static void Post() {
        if (_posted) return;
        _posted = true;
        Dispatcher.UIThread.Post(() => { _posted = false; RenderNow(); }, DispatcherPriority.Background);
    }

    /// Each side whose picture is out of date and whose view is at rest, now.
    public static void RenderNow() {
        if (!On || Left.View.Live || Right.View.Live) return;   // a hand or spring on either: the last pictures meanwhile
        bool any = false;
        foreach (var s in new[] { Left, Right })
            if (!s.View.Live && s.Session != null && (s.Stale || (s.View.PastFit && s.Wanted != s.Rendered))) any |= Render(s);
        if (any) Changed?.Invoke();
    }

    static bool Render(Side s, bool again = true) {
        var session = s.Session!;
        bool stale = s.Stale;
        s.Stale = false;
        var graph = Graph.None;
        if (stale || s.Preview == null) {
            // The whole picture: what shows at fit, and under a pan until the region lands.
            graph = s.Select && _selectMade == Graph.Preview ? Graph.Preview : session.Render(preview: true);
            if (s.Select) _selectMade = Graph.None;
            if (graph is Graph.Preview or Graph.Full) s.Preview = session.Read(graph);
        }
        if (graph is not (Graph.None or Graph.Preview or Graph.Full)) graph = Graph.Failed;
        else if (s.View.PastFit && s.Wanted is var (uv, pixels) && pixels.Width > 0 && (stale || s.Wanted != s.Rendered)) {
            graph = session.RenderRegion(uv, pixels);
            if (graph == Graph.None) graph = session.Render(preview: false);   // this edit needs the whole graph
            if (graph is Graph.Region or Graph.Full) {
                s.Region = session.Read(graph);
                s.RegionUv = graph == Graph.Region ? uv : new Rect(0, 0, 1, 1);
            } else graph = Graph.Failed;
        }
        if (graph == Graph.Failed) {
            string why = session.LastError;
            // Out of GPU memory: the decoded neighbours go (#392), and the other
            // pane's engine gives its render memory back, its picture already
            // copied into its pane (#406); then once more, before saying so.
            if (again && why.Contains(EngineModel.OutOfGpuMemory)) {
                bool freed = E.Cache.Shed();
                if (Other(s).Session is { } other && other != session) { other.ReleaseRenderMemory(); freed = true; }
                if (freed) { s.Stale = stale; return Render(s, again: false); }
            }
            s.Region = null;
            s.Rendered = s.Wanted;   // said once, not retried every frame
            Say(s, $"Couldn't render {s.Name}: {why}." + (s.Preview != null ? " Showing its preview." : ""));
            return true;
        }
        if (s.Problem?.StartsWith("Couldn't render") == true) s.Problem = null;
        s.Rendered = s.View.PastFit ? s.Wanted : null;
        return true;
    }
}

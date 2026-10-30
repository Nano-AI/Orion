# Stage 1 streams (#315)

⚠ **Historical (decision #380):** the streams finished and the Swift app is
retired. Kept for the ownership rules code still cites.

Stage 0 (`swarm/contracts`) split the shell into contracts so three worktrees
can work at once. Each stream writes **only** the files it owns below. A file
in no stream is frozen: need a change, put the diff in your report and the
integrator makes it. Public signatures listed under *Contracts* may gain
members; none is renamed or removed.

## Ownership

| Stream | Owns (writes only these) | Consumes (reads, never edits) |
|---|---|---|
| **1a speed** — the cache behind the session seam, background decode, preview-first, arrow-key timing | `Shared/Engine/EngineModel.cs` · `Shared/Engine/Session.cs` · `Shared/Engine/SessionSource.cs` · `Shared/Engine/PhotoList.cs` · `Shared/Engine/ShownPicture.cs` · new files in `Shared/Engine/` (the cache) · `Program.cs` (the bench) · `Shared/SelfTest.Speed.cs` (new) | `Session.ReleaseRenderMemory`, `Session.PreviewSize` (1b's; throw `NotBuiltException` until 1b merges: tolerate it) · `Commands` next-photo/previous-photo, which call `EngineModel.Next/Previous` |
| **1b native** — new C facade functions, the shim's preview-crop bug, the seam bodies | `native/shim.mm` · `native/build.sh` · `native/gen_fields.py` · `engine/` (facade: `orion.h` and its sources) · `Shared/Native.cs` · `Shared/Engine/SessionSeams.cs` (bodies only) · `Shared/SelfTest.Native.cs` (new) | `Session`'s private `_shim` and `Handle` (it is a `partial class`) · `MaskShape` · `NotBuiltException` · the existing `shim_*` ABI that `Session.cs` calls, which keeps its signatures |
| **1c wiring** — Compare, Gallery, Open and the toolbar through commands; the canvas's drag picture; Fluent's accent | `Slices/**` · `Shared/CommandTable.cs` (rows) · `Shell/MainWindow.axaml` · `Shell/MainWindow.axaml.cs` · `Shared/Theme.axaml` · `Shared/Theme.axaml.cs` · `Shared/SelfTest.Wiring.cs` (new) | `Commands` · `EngineModel` facade (`Shown`, `Status`/`Say`, `Apply`, `Next`/`Previous`) · `App.Window` |
| **frozen** (integrator) | `Shared/Commands.cs` · `Shared/Engine/MaskShape.cs` · `Shared/Engine/NotBuiltException.cs` · `Shared/SelfTest.cs` · `Shared/SelfTest.Contracts.cs` · `Shared/Panels.cs` · `Shared/AdjustmentSlider.cs` · `Shared/AnalogTrack.cs` · `Shared/AdjustmentMath.cs` · `Shared/Engraved.cs` · `Shared/Icons.cs` · `Shared/ToolButton.cs` · `Shared/TrackTint.cs` · `Shared/Palette.g.cs` · `OrionShell.csproj` · `slices.json` · `compare.py` · `SLICES.md` · `README.md` · this file | |

Tests: each stream implements its own `static partial void SpeedChecks()` /
`NativeChecks()` / `WiringChecks()` in its own `SelfTest.<Stream>.cs`; `SelfTest.Run`
already calls all three, and an unimplemented one compiles away.

## Contracts

| Contract | File | Signatures | Today | Who fills it |
|---|---|---|---|---|
| One photo over one shim handle | `Shared/Engine/Session.cs` | `static Session? Open(string path)` · `string Path` · `nint Handle` · `float Get(string field, int index = 0)` · `void Set(string field, float value, int index = 0)` (no render) · `Graph Render(bool preview)` · `WriteableBitmap? Read(Graph graph)` (per-graph bitmap, reused) · `PixelSize ImageSize` · `string LastError` · `Dispose()` | built | 1a (thread safety for background decode) |
| Stage 1b seams | `Shared/Engine/SessionSeams.cs` | `IReadOnlyList<(float X, float Y)> Curve(int channel)` · `void SetCurve(int channel, IReadOnlyList<(float X, float Y)> points)` (channel 0 master, 1–3 R G B; else `ArgumentOutOfRangeException`) · `void AutoEnhance()` · `void SetMaskGroup(IReadOnlyList<MaskShape> shapes)` · `void ReleaseRenderMemory()` · `PixelSize PreviewSize` | all throw `NotBuiltException` | 1b |
| How a session is obtained | `Shared/Engine/SessionSource.cs` | `interface ISessionSource { Session? Acquire(int index); void Release(Session s); void Hint(int index, int direction); void Clear(); }` · `FreshSessions(PhotoList)` | `FreshSessions`: decode on Acquire, dispose on Release | 1a: the two-tier cache of decision #318 |
| The list being browsed | `Shared/Engine/PhotoList.cs` | `IReadOnlyList<string> Paths` · `int Current` (−1 none) · `Load(IEnumerable<string>)` · `bool Go(int)` · `bool Next()` · `bool Previous()` (clamp at the ends, as `Library.neighbor`) | built | — |
| The canvas's picture | `Shared/Engine/ShownPicture.cs` | `Bitmap? Bitmap` · `Graph Graph` (`None`, `Preview`, `Full`) · `event Action Changed` · `void Show(Bitmap, Graph)` | every render shows its bitmap | 1a (cached, preview-first); #317 (shared texture) |
| Panel-facing facade | `Shared/Engine/EngineModel.cs` | everything it had, plus `Session? Session` · `ShownPicture Shown` · `string Status` · `void Say(string)` · `bool Next()` · `bool Previous()` · `bool Apply(Action<Session> write, bool reread = false)` · `event Action ValuesChanged` | `Apply` renders after the write; `NotBuiltException` becomes `Status` | 1a |
| Commands | `Shared/Commands.cs` | `record Command(Name, Title, Shortcut, Owner, Func<bool> CanRun, Action? Run)` · `Commands.All` · `Index(...)` (throws on a shared name or key) · `Find(name)` · `CanRun(name)` · `string Run(name)` · `Key(KeyEventArgs)` | 38: 35 built, 3 stubs (`--commands`) | 1c turns stubs into built rows in `CommandTable.cs` |
| A seam not built yet | `Shared/Engine/NotBuiltException.cs` | `NotBuiltException(string what, string owner)`: message `not built yet: <what> (<owner>)` | — | — |

## Notes per stream

- **1a**: `EngineModel.Show` calls `Acquire`, then `Hint(index, sign of the step)`; a cache must not
  dispose a session between `Acquire` and `Release`. Coalesce held arrow keys inside `Next`/`Previous`,
  not in the command table. `Session` touches the shim, which is not thread-safe per handle.
- **1b**: `shim_read` derives the preview crop from the full output texture, so a preview read before
  any full render is 2183×2183; `PreviewSize` and that fix are the same job. The selftest's
  `NotBuiltOr` checks become real round-trips the moment a body stops throwing: make them pass.
  `AutoEnhance`'s caller (Light panel) already passes `reread: true`, which moves the sliders.
- **1c**: Compare is a stub on purpose: `CompareState.Toggle` exists, but the toolbar reads
  `ChromeStubs.Comparing`, so wiring the toggle alone would split the canvas under a toolbar that says
  it is off. The drag picture: the engine's preview and full renders agree within one level at
  −4…+1.5 EV with lens correction on, so look at how `CanvasPanel.Draw` scales and filters the
  quarter-size preview bitmap (its `FitScale` is computed against the full image size). Fluent's
  accent: a `SystemAccentColor` resource in `Theme.axaml`, which loads after `FluentTheme`.

## Round 2: the developer's UI pass (2026-10-28)

| Stream | Owns (writes only these) | Selftest hook (own file) |
|---|---|---|
| **u1 layout** | `Shell/MainWindow.axaml(.cs)` · `Shared/Theme.axaml(.cs)` · `Shared/Segmented.cs` · `Slices/Develop/HistogramPanel.cs` · `Slices/Library/{FilmstripPanel,GalleryPanel,LibraryUi,RatingBarPanel}.cs` · `Slices/Chrome/**` | `LayoutChecks` in `Shared/SelfTest.Layout.cs` |
| **u2 canvas** | `Slices/Canvas/{CanvasPanel,CanvasState,CanvasLayout,CanvasScenes,CanvasNative,CompareOverlay,Navigator,ColorLoupe}.cs` · `Shared/Engine/**` | `CanvasUxChecks` in `Shared/SelfTest.CanvasUx.cs` |
| **u3 tools** | `Slices/Canvas/{CropEdit,CropOverlay,CropTools}.cs` · `Slices/Develop/**` except `HistogramPanel.cs` · `Slices/Sheets/**` · `Shared/CommandTable.cs` | `ToolsChecks` in `Shared/SelfTest.Tools.cs` |
| **u4 masks** | `Slices/Masks/**` · `Slices/Canvas/{MaskOverlay,MaskLayout,SpotOverlay}.cs` · `native/*` · `Shared/Native.cs` · `Shared/Engine/SessionSeams.cs` · a design doc under `design/masks/` | `MaskUxChecks` in `Shared/SelfTest.MaskUx.cs` |
| **u5 motion** | `Shared/{AnalogTrack,AdjustmentSlider,AdjustmentMath,Engraved,ToolButton,TrackTint}.cs` · `Shared/Design/**` · `design/DESIGN.md` (motion section) | `MotionChecks` in `Shared/SelfTest.Motion.cs` |

Everything else is frozen; put the diff you need in your report.

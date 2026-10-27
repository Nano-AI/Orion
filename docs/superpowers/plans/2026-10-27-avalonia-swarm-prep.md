# Avalonia Swarm Prep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the shared contract six slice agents build on, so each can port one group of SwiftUI views into `spikes/avalonia-shell/Slices/<Slice>/` without touching another agent's files, and prove its work with a screenshot beside the Swift one.

**Architecture:** The spike from #315 is split into `Shared/` (frozen contract: palette, `AnalogTrack`, engine model, native imports, panel registry), `Shell` (the window composes registered panels by region) and `Slices/` (one folder per agent). Panels register by attribute, so integration is merging folders. The native shim exposes every scalar adjustment by name through a table generated from `orion.h`, and links the whole engine so any facade function can be called from C# directly.

**Tech Stack:** Avalonia 12.1.4, .NET 10, C# 14, Objective-C++ shim over `engine/include/orion/orion.h`, Python 3 generators.

**Spec:** Conversation of 2026-10-27, recorded in decision #315 and `STATUS.md`: view layer only; `Engine+*`, `Library`, `OrionApp+Files/+Commands` logic is stubbed, not translated; done = builds + screenshot next to the matching Swift `--screenshot` scene; at most ~3 agents run at once (24 GB machine).

## Global Constraints

- No new NuGet packages beyond Avalonia, Avalonia.Desktop, Avalonia.Themes.Fluent, Avalonia.Fonts.Inter (12.1.4).
- No Rust, no GPL code (CLAUDE.md). C# is confined to `spikes/avalonia-shell/`.
- Colours come only from `Palette` (generated from `design/tokens.json`); no hex literals in slices.
- A stub says so: `// stub(#315): <Swift file> owns this until the logic moves below the facade`.
- An unknown adjustment name is an error, never a silent no-op.
- Builds and runs need the sandbox off (Metal toolchain, `~/.nuget`).

## Review Focus

1. A fresh worktree cannot build or find the engine dylib — the vibesys failure. → Task 4 builds and screenshots from a linked worktree.
2. Two slices register the same panel name → the registry throws at startup naming both types. → Task 2 self-check.
3. One slice's constructor throws → the others still render; that panel shows its exception text. → Task 2 self-check.
4. `orion.h` gains a field → the table regenerates on every native build; the generator asserts it found the struct. → Task 1.
5. `Set("exposre_ev", …)` (a typo) → throws `ArgumentException` with the name. → Task 1 self-check.

---

### Task 1: Native — every adjustment by name, every facade function exported

**Files:**
- Create: `spikes/avalonia-shell/native/gen_fields.py`
- Modify: `spikes/avalonia-shell/native/shim.mm`, `spikes/avalonia-shell/native/build.sh`

**Interfaces:**
- Produces (C): `float shim_get(Shim*, const char* field, int index, int* ok)`, `int shim_set(Shim*, const char* field, int index, float value)` — `field` is the C name in `OrionAdjustments` (`exposure_ev`, `hue_shift`…); `index` is 0 for scalars; ints are cast. `int shim_field_count(void)`, `const char* shim_field_name(int i, int* length)` — `length` is 1 for scalars, N for `float x[N]`. `OrionEngine* shim_engine(Shim*)` so C# can call `orion_*` directly.
- Produces (dylib): every `orion_*` symbol exported (`-Wl,-force_load`).

- [x] Step 1: `gen_fields.py <orion.h> <out.inc>` writes `F(name, type)` / `A(name, N)` lines for scalar and `float[N]` members of `OrionAdjustments`; exits 1 if the struct or fewer than 50 scalars are found.
- [x] Step 2: `build.sh` runs it before compiling, adds `-Wl,-force_load,…/liborion_engine.a`.
- [x] Step 3: Verify: `nm -gU bin-native/liborion_shim.dylib | grep -c ' T _orion_'` prints ≥ 48; `shim_field_count` > 53 via Task 2's self-check.

### Task 2: Shared contract and panel registry

**Files:**
- Create: `Shared/Palette.g.cs` (Task 3 generates it), `Shared/AnalogTrack.cs` (moved), `Shared/Native.cs` (moved + new imports), `Shared/EngineModel.cs`, `Shared/Panels.cs`, `Shell/MainWindow.axaml(.cs)` (moved), `Slices/Develop/LightPanel.cs`, `Slices/Library/FilmstripPanel.cs`, `Slices/Chrome/ToolbarPanel.cs` (the spike's current UI, moved as worked examples)
- Modify: `Program.cs` (`--panel`, `--selftest`)

**Interfaces:**
- `sealed class EngineModel : INotifyPropertyChanged` — `IReadOnlyList<string> Photos`, `int Current`, `void Open(int i)`, `float Get(string field, int index = 0)`, `void Set(string field, float v, int index = 0)` (throws `ArgumentException` on unknown name), `void BeginInteraction()`, `void EndInteraction()`, `Bitmap? Picture`, `string Timing`, `nint Handle` (the `OrionEngine*`), `event Action? Rendered`. One instance: `EngineModel.Shared`.
- `[Panel(name, Region, Order)]` on a `Control` subclass with a parameterless constructor; `enum Region { Toolbar, Tools, Canvas, Filmstrip, Sheet }`. `Panels.All` (sorted by region, order; throws on a duplicate name), `Panels.Create(name)` (returns a red `TextBlock` with the exception when the constructor throws).
- CLI: `--panel <name> --screenshot out.png` renders that panel alone at its region's size (Tools 322×900, Toolbar 1440×44, Filmstrip 1440×98, Canvas/Sheet 1100×800); `--selftest` checks the Review Focus items 2, 3 and 5 plus a set/get round trip of every field, prints `selftest: N checks passed`, exits 1 on any failure.

- [x] Step 1: Write `--selftest` checks first; run; expect failures (types missing).
- [x] Step 2: Move code and implement the interfaces; Shell composes `Panels.All` by region.
- [x] Step 3: `dotnet build -c Release` clean; `--selftest` passes; the full window screenshot matches the earlier spike screenshot by eye.

### Task 3: Palette generated from tokens.json

**Files:** Modify `design/build-tokens.py` (add `csharp_file(t)` → `spikes/avalonia-shell/Shared/Palette.g.cs`: `static class Palette` with an `IBrush` per colour and `const double` per space/radius/type/layout token).

- [x] Step 1: Run `python3 design/build-tokens.py` twice; `git diff --stat` shows only `Palette.g.cs` created and no change on the second run (mockup and Swift output unchanged).
- [x] Step 2: Build passes with the hand-written `Palette` deleted.

### Task 4: Reference screenshots and the compare page

**Files:** Create `spikes/avalonia-shell/compare.py`, `spikes/avalonia-shell/SLICES.md`; modify `.gitignore` (`reference/`, `compare/`).

**Interfaces:** `compare.py swift` renders each scene named in `SLICES.md` with `build/Orion.app … --screenshot reference/<scene>.png --photo <sample> --scene <scene>`, serially. `compare.py avalonia` renders every registered panel to `compare/<panel>.png`. `compare.py page` writes `compare/index.html`: one row per slice, Swift scene beside its panels, offline, no external URLs.

- [x] Step 1: `SLICES.md`: the six slices (Swift files, scenes, panel names to register), the rules from Global Constraints, the done criteria, and the Avalonia 12 notes learned in #315.
- [x] Step 2: Build `orion_app` from current main, run `compare.py swift`; every listed scene has a PNG.
- [x] Step 3: `compare.py avalonia && compare.py page`; open the page; seed panels appear beside their scenes.
- [x] Step 4: In a fresh `git worktree`, `dotnet build` and `--selftest` pass with no setup beyond `tools/worktree-setup.sh` (Review Focus 1). Remove the worktree.
- [x] Step 5: Record the session in `STATUS.md`.

## Changes during execution

| Planned | Done instead | Why |
|---|---|---|
| Screenshots through the native window | `Avalonia.Headless` (Skia, real drawing) for `--screenshot`, `--bench`, `--selftest` | macOS backend dies with RenderTimer -6661 when the display sleeps; agents run unattended |
| Slices build their own sliders | `Shared/` ports AnalogTrack, TrackTint, AdjustmentSlider, AdjustmentMath, Engraved, ToolButton, Icons | six agents porting one slider six ways is the collision the swarm cannot afford |
| Shell regions: toolbar, tools, canvas, filmstrip | + `Status`, `Sheet`, and a shell-owned tab bar (`Panel(..., Tab = ...)`) | the footer and the sheets had no home; the tab bar is two slices' boundary |
| Step 2 every scene has a PNG | 39 of 40; `menu` dropped from slices.json | it asserts the menu bar and draws nothing |
| Step 4 setup is `worktree-setup.sh` alone | + `native/build.sh` (finds main's engine) and the spike **committed** first | the spike is untracked, so a worktree off `main` does not contain it; compare.py now finds main's sample and defaults `DOTNET_ROOT` |

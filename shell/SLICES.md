# Slice brief (#315)

You are porting one slice of Orion's SwiftUI view layer to Avalonia 12 / .NET 10.
`slices.json` is the contract: your slice's Swift files, the panel names you
must register, and the Swift scenes your screenshots are judged against. This
is a **view-layer port**: the engine, sidecars, library and undo stay in Swift
until their logic moves below the C facade. Port what you see, stub what you
cannot reach.

## Rules

| # | Rule |
|---|---|
| 1 | Write only in `Slices/<Folder>/`. Nothing else in the tree changes. |
| 2 | `Shared/`, `Shell/`, `Program.cs`, `native/`, the csproj are frozen. Need a change? Say so in your report, with the diff you would make. |
| 3 | Register every panel in your `slices.json` entry, by exactly that name, region and tab: `[Panel("develop-light", Region.Tools, 10, Tab = "Light")]`. No other names. |
| 4 | `replaces` lists seeds you delete when your panels land (develop deletes `Slices/Develop/BasicPanel.cs`). |
| 5 | Colours only from `Palette` / `TrackTint`, spacing from `Space` / `Radius` / `TypeSize` / `Layout` (`Shared/Palette.g.cs`, generated from `design/tokens.json`). No hex literals. |
| 6 | Icons only through `Icons.Create("sf.symbol.name", size, brush)`. A missing symbol draws a dot and logs; list it in your report, do not draw your own. |
| 7 | Reuse `EngineModel.Shared`, `AdjustmentSlider`, `SectionPlate`, `Engraved`, `AnalogTrack`, `ToolButton`, `AdjustmentMath`. Do not fork them. |
| 8 | Adjustment fields are the C names in `OrionAdjustments` (`engine/include/orion/orion.h`). `EngineModel.Get/Set` throw on a misspelt field or an index past an array: let them. |
| 9 | Behaviour the Swift reaches through `Engine.swift`, `Library.swift`, `EditHistory`, sidecars or AppKit gets a stub, marked exactly: `// stub(#315): <Swift file> owns this until the logic moves below the facade` |
| 10 | A facade call not on `EngineModel` (`orion_*` in `orion.h`): add your own `[LibraryImport("orion_shim")]` in your folder. `liborion_shim.dylib` exports all 48. |
| 11 | No new NuGet packages, no `unsafe` outside a P/Invoke, no files over ~300 lines (CLAUDE.md: one file, one job). |
| 12 | Run headless (`--screenshot`, `--selftest`, `--bench`). Never launch an interactive window; the macOS backend fails with RenderTimer -6661 when the display sleeps. |
| 13 | Namespace `OrionShell.<Folder>` (Canvas: `OrionShell.CanvasSlice`, since `Canvas` is an Avalonia type). Folders keep files apart; namespaces keep type names apart: the first swarm's develop and sheets slices each defined `Segmented`. |

## Done means

```
cd shell
dotnet build -c Release                                    # 0 warnings in Slices/<Folder>/
bin/Release/net10.0/OrionShell "$ORION_SAMPLE" --selftest  # all pass; the 'panel Boom failed' trace is the check itself
bin/Release/net10.0/OrionShell --list                      # every panel of yours listed
python3 compare.py avalonia <slice>                        # every panel screenshots, ok
python3 compare.py page                                    # yours beside the Swift scenes
```

A panel whose constructor throws renders as a red line (`Panels.Failed`); the
selftest fails on it. Your report: panels registered, stubs (file + what),
missing icons, `Shared/` changes you would want, and what in the screenshot
still differs from the Swift scene.

## Setup (once, main checkout)

```
cmake --build build --target orion_engine      # Metal toolchain: run outside the sandbox
shell/native/build.sh          # -> bin-native/liborion_shim.dylib; finds main's engine from a worktree
export DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec DOTNET_CLI_TELEMETRY_OPTOUT=1
```

Set `ORION_SAMPLE` to a RAW. compare.py otherwise takes the first of `samples/_PIC8095.ARW`, or a
worktree's copy. The Swift reference shots are in `reference/` (`compare.py swift`).

## Avalonia 12 traps already hit

| Symptom | Fix |
|---|---|
| `Path` is ambiguous | `using Path = Avalonia.Controls.Shapes.Path;` |
| `FontFeatures` will not take a string | style class `tnum` (Theme.axaml) |
| `DispatcherTimer.RunOnce` | returns `IDisposable`; keep it to cancel |
| `Bitmap.Save` encoder options | `new PngBitmapEncoderOptions()` (the base is abstract) |
| AXAML comment fails to parse | XML comments cannot contain `--` |
| Window ignores `Width` | `SizeToContent` wins; screenshots render a fixed frame, not the window |
| `HsvColor` to RGB | `HsvColor.ToRgb(h, s, v)` |

## Concurrency

At most three slice agents at once: one GPU, 24 GB, and every screenshot opens
the engine. Each agent works in its own worktree; the integrator merges
`Slices/<Folder>/` directories, which cannot collide by rule 1. A panel-name
clash fails `--selftest` naming both types.

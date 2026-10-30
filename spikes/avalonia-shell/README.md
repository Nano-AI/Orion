# Avalonia shell spike (#315)

What a Windows/Linux Orion could look like: Avalonia 12 on .NET 10, driving the
real engine through `engine/include/orion/orion.h`. Runs on macOS today, because
the engine still only has a Metal backend.

```
brew install dotnet                                   # once
cmake --build build --target orion_engine             # sandbox off (Metal toolchain)
spikes/avalonia-shell/native/build.sh                 # engine -> bin-native/liborion_shim.dylib
export DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec
dotnet run -c Release --project spikes/avalonia-shell -- <RAWs or folders>
```

`tools/package-avalonia.sh` builds all of that from a clean tree into
`dist-avalonia/Orion Avalonia.app` (`com.nanoai.orion.avalonia`, beside the Swift
`Orion.app`) and `Orion-<git describe>-avalonia.dmg`, ad-hoc signed.

| Flag | Does |
|---|---|
| `--panel NAME` / `--tab NAME` | one registered panel, or the window on that tool tab |
| `--screenshot OUT.png` | render headless and exit |
| `--bench` | 60-tick exposure drag, prints engine and copy timings |
| `--selftest` | the shell's invariants (needs a photo); headless, so the canvas takes the CPU copy |
| `--proof DIR` | ⚠ **opens one window** for about 5 s: the canvas on the shared surface, then on the CPU copy; CPU per frame and a `screencapture -l` of each into `DIR` |
| `--list` | registered panels: name, region, tab |
| `--commands` | every command: name, built or stub, Swift owner, keys |
| `--command NAME` | run one command in the headless window and print what happened (exit 1 unless it ran) |

| Path | What |
|---|---|
| `Shared/` | building blocks every slice uses: slider, plates, icons, tokens, commands |
| `Shared/Engine/` | the engine model and its pieces: session, photo list, session source, shown picture; the XMP sidecar (`Sidecar.cs`, `DevelopJson.cs`, `SessionDocument.cs`, decision #362) |
| `Shared/Autosave.cs` | edits reach the sidecar 900 ms after the first change, and on switch, deactivate and quit. Only the app at a window writes: the headless flags and `--proof` read sidecars and never write one |
| `Slices/Chrome/Assistant*.cs`, `Proposal*.cs`, `CurrentPhoto.cs` | the agent's terminal column (⌘⌥A, #366), the MCP proposal watcher with Approve ⌘⏎ / Reject ⌘⎋ (#368), and `current.json`; the headless modes never start the agent nor write `current.json` |
| `Slices/Sheets/Watermark*.cs`, `HdrMerge.cs` | `watermark.json` in Swift's shape, the coverage mask every export carries (#367), the HDR merge on a worker |
| `Slices/Library/LibraryModel.cs`, `PhotoSelection.cs` | ratings, rejects and labels merged into the sidecar (window only), and the command/shift-click selection culls and batches act on (decision #363) |
| `Slices/Sheets/Snapshots.cs` | saved versions in `PHOTO.orion-snapshots.json`, Swift's file shape; `Shared/ShellSettings.cs` is the @AppStorage stand-in |
| `Fixtures/` | sidecars the Swift app wrote: `--selftest` rewrites them byte for byte and reopens each through the engine |
| `STREAMS.md` | stage 1's three parallel streams: who owns which file, which contracts they consume |
| `Shell/` | the window: regions and the tool tab bar |
| `Slices/<Folder>/` | one per slice in `slices.json`; seeds only, until the swarm |
| `SLICES.md` | the brief a slice agent works from |
| `compare.py` | `check` file ownership, `swift`/`avalonia` screenshots, `page` side by side |
| `native/present.mm` | the picture to Avalonia on the GPU: an IOSurface per graph, handed over on `MTLSharedEvent`s |
| `native/matte.mm` | Subject and Person selection through Apple's Vision (macOS only; decision #365) |
| `bin-native/present_test RAW` | its headless check: surface against CPU readback, the GPU wait, timings. No window |

Spike-only, not product code: `native/shim.mm` repeats the default look from
`Engine.swift`; a real port moves it below the facade (#315).

The picture reaches the canvas with no CPU copy (#317, decision #354): on
Avalonia's Metal renderer, `native/present.mm` copies each render on the GPU
into an IOSurface that `Slices/Canvas/SharedPicture.cs` imports, and the two
take turns over `MTLSharedEvent`s. Wherever that is not available (headless,
the OpenGL renderer, which refuses an RGBA surface, or a failed import) the
render is copied through the CPU into a `WriteableBitmap`, as before.

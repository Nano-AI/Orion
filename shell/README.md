# Orion

The app: Avalonia 12 on .NET 10, driving the engine through
`engine/include/orion/orion.h` (#315, #380). It replaced the SwiftUI app on the
Mac; Windows and Linux wait on the engine's Dawn backend (#317).

```
brew install dotnet                                   # once
cmake --build build --target orion_engine             # sandbox off (Metal toolchain)
shell/native/build.sh                                 # engine -> bin-native/liborion_shim.dylib
export DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec
dotnet build -c Release shell                         # -> shell/bin/Release/net10.0/Orion
shell/bin/Release/net10.0/Orion <RAWs or folders>     # opens a window
```

`tools/package-app.sh` builds all of that from a clean tree into
`dist/Orion.app` (`com.nanoai.orion`, the executable `Orion`, the MCP server as
`Contents/MacOS/orion-mcp`) and `Orion-<git describe>.dmg`, ad-hoc signed.

The app keeps its files in `~/Library/Application Support/Orion`, the folder the
Swift app used: `watermark.json` and `current.json` are shared as they were, and
`Shared/SettingsImport.cs` carries the Swift app's `presets.json` and its
UserDefaults (assistant, gallery size) into `presets-shell.json`,
`assistant-shell.json` and `shell-settings.json` once, on the first launch at a
window.

| Flag | Does |
|---|---|
| `--panel NAME` / `--tab NAME` | one registered panel, or the window on that tool tab |
| `--screenshot OUT.png` | render headless and exit |
| `--bench` | 60-tick exposure drag, prints engine and copy timings |
| `--selftest` | the shell's invariants (needs a photo); headless, so the canvas takes the CPU copy |
| `--proof DIR` | ⚠ **opens one window** for about 5 s: the canvas on the shared surface, then on the CPU copy; CPU per frame and a `screencapture -l` of each into `DIR` |
| `--list` | registered panels: name, region, tab |
| `--commands` | every command: name, built or stub, the Swift file it was ported from, keys |
| `--command NAME` | run one command in the headless window and print what happened (exit 1 unless it ran) |
| `--agent VERB RAW …` | the MCP server's verbs (`Agent/`): JSON on stdout, exit 2 usage, 1 failure; `tools/check-agent.py` |
| `--batch-export DIR RAW…` | export each photo with its sidecar's edit; exit 1 if any failed; `tools/check-modes.py` |
| `--hdr-merge OUT.dng RAW RAW…` | merge, then open and render what it wrote; `tools/check-modes.py` |

| Path | What |
|---|---|
| `Shared/` | building blocks every slice uses: slider, plates, icons, tokens, commands |
| `Shared/Engine/` | the engine model and its pieces: session, photo list, session source, shown picture; the XMP sidecar (`Sidecar.cs`, `DevelopJson.cs`, `SessionDocument.cs`, decision #362) |
| `Agent/` | `Orion --agent`, ported from the Swift app's AgentCLI: the verbs, option vocabulary, ranges, notes and exit codes `mcp/server.ts` is built against, on headless engines (#312) |
| `Shared/Autosave.cs` | edits reach the sidecar 900 ms after the first change, and on switch, deactivate and quit. Only the app at a window writes: the headless flags and `--proof` read sidecars and never write one |
| `Slices/Chrome/Assistant*.cs`, `Proposal*.cs`, `CurrentPhoto.cs` | the agent's terminal column (⌘⌥A, #366), the MCP proposal watcher with Approve ⌘⏎ / Reject ⌘⎋ (#368), and `current.json`; the headless modes never start the agent nor write `current.json` |
| `Slices/Sheets/Watermark*.cs`, `HdrMerge.cs` | `watermark.json` in Swift's shape, the coverage mask every export carries (#367), the HDR merge on a worker |
| `Slices/Library/LibraryModel.cs`, `PhotoSelection.cs` | ratings, rejects and labels merged into the sidecar (window only), and the command/shift-click selection culls and batches act on (decision #363) |
| `Slices/Sheets/Snapshots.cs` | saved versions in `PHOTO.orion-snapshots.json`, Swift's file shape; `Shared/ShellSettings.cs` is the @AppStorage stand-in |
| `Fixtures/` | sidecars the Swift app wrote: `--selftest` rewrites them byte for byte and reopens each through the engine |
| `Fixtures/foreign/` | hand-written Lightroom-, Capture One- and darktable-style XMP: Orion writes into each and every other node must survive, in this selftest (decision #370) |
| `STREAMS.md` | stage 1's three parallel streams (the port, #315), kept for the ownership rules code still cites |
| `Shell/` | the window: regions and the tool tab bar |
| `Slices/<Folder>/` | one per slice of the port: Canvas, Chrome, Develop, Library, Masks, Sheets |
| `SLICES.md` | the brief the port's slice agents worked from; its numbered rules are cited in code |
| `native/present.mm` | the picture to Avalonia on the GPU: an IOSurface per graph, handed over on `MTLSharedEvent`s |
| `native/matte.mm` | Subject and Person selection through Apple's Vision (macOS only; decision #365) |
| `bin-native/present_test RAW` | its headless check: surface against CPU readback, the GPU wait, timings. No window |

Still open: `native/shim.cpp` repeats the default look the Swift app's
`Engine.swift` set; it belongs below the facade (#315).

The picture reaches the canvas with no CPU copy (#317, decision #354): on
Avalonia's Metal renderer, `native/present.mm` copies each render on the GPU
into an IOSurface that `Slices/Canvas/SharedPicture.cs` imports, and the two
take turns over `MTLSharedEvent`s. Wherever that is not available (headless,
the OpenGL renderer, which refuses an RGBA surface, or a failed import) the
render is copied through the CPU into a `WriteableBitmap`, as before.

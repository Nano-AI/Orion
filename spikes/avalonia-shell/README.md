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

| Flag | Does |
|---|---|
| `--panel NAME` / `--tab NAME` | one registered panel, or the window on that tool tab |
| `--screenshot OUT.png` | render headless and exit |
| `--bench` | 60-tick exposure drag, prints engine and copy timings |
| `--selftest` | the shell's invariants (needs a photo) |
| `--list` | registered panels: name, region, tab |

| Path | What |
|---|---|
| `Shared/` | building blocks every slice uses: engine model, slider, plates, icons, tokens |
| `Shell/` | the window: regions and the tool tab bar |
| `Slices/<Folder>/` | one per slice in `slices.json`; seeds only, until the swarm |
| `SLICES.md` | the brief a slice agent works from |
| `compare.py` | `check` file ownership, `swift`/`avalonia` screenshots, `page` side by side |

Spike-only, not product code: `native/shim.mm` repeats the default look from
`Engine.swift` and copies pixels off the GPU. A real port removes both (#315).

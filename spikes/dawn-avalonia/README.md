# Dawn in Avalonia, zero-copy spike (#317)

Asks whether Dawn can write a texture that the Avalonia UI composites with no CPU copy (no readback, no `MapAsync`, no `WriteableBitmap`).

## Result

| Platform | Answer | How far it was verified |
|---|---|---|
| **macOS** | **Yes.** Dawn renders into an IOSurface through `SharedTextureMemory`. Avalonia 12.1.4 imports the same IOSurface through `ICompositionGpuInterop` and composites it. Pixels never reach the CPU. | Built and run on this Mac with **both** of Avalonia's macOS renderers, including OpenGL, which is the shell's current one. Measured, screenshotted, and checked headlessly. |
| **Windows** | **Pairs on paper.** DXGI NT handle plus keyed mutex on both sides. | Headers and source only. Not built. |
| **Linux** | **Partial.** The handle types match (Vulkan opaque fd), but both libraries can only *import*. Something has to allocate the memory and the consumer's semaphore, and on Linux that something is Vulkan code we would write. | Headers and source only. Not built. |

⚠ **"No CPU copy" does not mean "no copy".** On every frame Avalonia makes one **GPU-to-GPU** copy of the imported image (its "snapshot"), so that the producer can reuse the image. This happens with both renderers and is by design. See [The code path](#the-code-path).

## Run it

```
../../tools/build-dawn.sh                        # once, through the heavy-job guard: Dawn from source, ~3 min
native/build.sh                                  # builds the dylib and the selftest against it
bin-native/dawn_selftest                         # headless: no window
DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec dotnet build -c Release DawnAvalonia.csproj
./demo.sh            # ⚠ shows ONE window for about 5 s, then exits by itself (hard cap 10 s)
./demo.sh --gl       # the same with Avalonia's OpenGL renderer
```

`demo.sh` runs two phases in a single window. The first renders the shared texture at the view's size (1920×1200 on this display); the second renders it at 6000×4000 (24 MP), which Avalonia scales to the view. Each phase does 20 warm-up frames and then 100 measured ones. For each phase the demo prints CPU time per frame and writes one `screencapture -l<window>` screenshot to `shots/`. ⚠ The second phase has **not yet been run in a window**. A run with `--size 6000x4000` was meant to cover it, but the script passed the flag as a single word, so that run happened at 1920×1200 (it appears as a repeat below). The 24 MP path has passed only in the headless selftest.

## Evidence

**Headless (`bin-native/dawn_selftest`, no window).** This uses the same C facade as the app. To read the result, it locks the IOSurface and reads it on the CPU **once, in the test only**:

```
pass  frame 1: bottom-left corner is black (not flipped)
pass  frame 1: bar at x = w/4 (t = 1 s)
pass  frame 1: colour field matches the shader formula      pixel (900,400) rgb 127 16 239, formula 127 16 239
pass  frame 2 held on the GPU until the consumer signals
pass  frame 2 ran once the consumer signalled
pass  frame 2: bar moved to x = w/2 (t = 2 s)
pass  re-created at 6000x4000 / 24 MP frame rendered / corner black, bar at x = 3w/4
selftest passed
```

The test was checked against a planted bug. With the consumer fence deleted from `BeginAccess`, it fails `frame 2 held on the GPU until the consumer signals`.

**Windowed.** Seven launches, each about 10 s, all made before the no-window rule; the timings below come from the 570-frame runs. Two screenshots per renderer, taken about 2 s apart, are in `evidence/`:

| | t ≈ 2.0 s | t ≈ 4.0 s |
|---|---|---|
| Metal | `evidence/metal-frame150.jpg`: bar at mid-width | `evidence/metal-frame270.jpg`: bar back at the left edge |
| OpenGL | `evidence/gl-frame150.jpg`: same | `evidence/gl-frame270.jpg`: same |

The bar sits where `fract(t/4)` says it should. The black marker is in the bottom-left corner, so the picture is not flipped. The frame-counter label is ordinary Avalonia text drawn **over** the Dawn picture, so overlays such as crop handles and masks can draw on top of it. That is the concern #317 raised about native child windows.

**Numbers.** M-series Mac, 1920×1200 BGRA8 surface, 570 measured frames per run:

| Renderer | fps | `ds_frame` (Dawn encode + submit + fence export), UI thread | `UpdateWithTimelineSemaphoresAsync` call | frame interval p50 / p95 | process CPU per frame, all threads |
|---|---|---|---|---|---|
| Metal | 59.7 | 0.213 ms mean, 0.488 p95 | 0.018 ms | 16.67 / 17.60 ms | **3.06 ms** (repeat: 2.77) |
| OpenGL | 60.0 | 0.122 ms mean, 0.183 p95 | 0.008 ms | 16.67 / 17.44 ms | **6.91 ms** (repeat: 5.72) |

- **On the UI thread, a frame costs about 0.23 ms whatever its size.** For comparison, the shell's current copy path (#315, STATUS 2026-10-27) spends **0.7 ms** per tick copying a 1006×1506 preview and **10.5 ms** copying the full 4024×6024 image.
- **The OpenGL renderer costs about twice the CPU.** Its wait on Dawn's event is a CPU wait on the render thread (`inner.Wait(value, 1000)`), and its signal is a CPU `SetSignaledValue` issued after `glFlush`. The Metal renderer encodes both on the GPU.
- The first Metal run (300 frames) averaged 49 fps with a 20.3 ms mean interval: some frames were dropped. The later runs were steady. The cause was not investigated.
- `producer imported 1 time(s)`: Dawn's queue exports the same `MTLSharedEvent` on every frame (`SharedTextureMemoryMTL.mm:252-254`), so Avalonia imports it once.

## The code path

These are the only places pixels go. None of them is on the CPU:

| Step | Where | What |
|---|---|---|
| Allocate | `native/dawn_surface.mm`, `IOSurfaceCreate` | A single BGRA IOSurface for the whole run. |
| Dawn writes | `ImportSharedTextureMemory` → `memory.CreateTexture()` → render pass | Dawn wraps the IOSurface with `newTextureWithDescriptor:iosurface:plane:`, with no copy (Dawn `src/dawn/native/metal/SharedTextureMemoryMTL.mm:282-283`). The only CPU→GPU write is the 16-byte uniform block. |
| Hand-off | `EndAccess` → `SharedFenceMTLSharedEventExportInfo` | Returns Dawn's queue `MTLSharedEvent` and the last-usage serial (`SharedTextureMemoryMTL.mm:235-255`). |
| Avalonia reads | `ImportImage(IOSurfaceRef)`, `UpdateWithTimelineSemaphoresAsync` | Metal: `SubmitWait`, an `SKSurface` over the imported `MTLTexture`, `Snapshot()`, `SubmitSignal`, all GPU commands (Avalonia `src/Skia/Avalonia.Skia/Gpu/Metal/SkiaMetalExternalObjectsFeature.cs:33-47`). OpenGL: `CGLTexImageIOSurface2D` (`src/Avalonia.Native/AvaloniaNativeGlPlatformGraphics.cs:290-310`), then `glCopyTexSubImage2D` into a new texture (`src/Skia/Avalonia.Skia/Gpu/OpenGl/GlSkiaExternalObjectsFeature.cs:266-299`). That copy is GPU to GPU. |
| Hand-back | the consumer `MTLSharedEvent` (made in `ds_create`, imported by both sides) | Avalonia signals `n+1`. Dawn's next `BeginAccess` waits on it on the GPU, which the selftest checks. |

`grep -n "MapAsync\|GetMappedRange\|CopyTextureToBuffer\|WriteTexture\|WriteableBitmap\|IOSurfaceLock" native/dawn_surface.mm Program.cs` finds nothing. `IOSurfaceLock` appears only in `native/selftest.mm`.

## Which Avalonia renderer

The shell calls `UsePlatformDetect()` with no options, so on macOS it renders with **OpenGL** (the default is `OpenGl, Software`, according to `AvaloniaNativePlatformOptions.RenderingMode` in `Avalonia.Native.xml`). **Both** macOS renderers can import:

| | OpenGL (the shell today) | Metal |
|---|---|---|
| Image handle | `IOSurfaceRef`, **BGRA8 only** (`AvaloniaNativeGlPlatformGraphics.cs:272-283`) | `IOSurfaceRef`, RGBA8 or BGRA8 (`Metal.cs:110-134`) |
| Semaphore | `MetalSharedEvent`, waited and signalled **on the CPU** (`:340-352`) | `MetalSharedEvent`, waited and signalled **on the GPU** (`Metal.cs:168-172`) |
| Sync | `Automatic, TimelineSemaphores` | `TimelineSemaphores` |

**Recommendation: Metal.** Switching costs one option on the shell's `AppBuilder`: `.With(new AvaloniaNativePlatformOptions { RenderingMode = [Metal, OpenGl, Software] })`. ⚠ This spike did not check that the shell's own UI renders the same under Skia-on-Metal. With OpenGL there is also a race this spike could not observe: Avalonia signals "done" from the CPU after `glFlush`, before its GPU copy has finished, so Dawn could start overwriting the image while it is still being read. Double-buffering would close that race; Metal does not have it.

## Dawn: version, source, cost

| | |
|---|---|
| Version | Release `v20261027.184652`, commit `106704a9ac3fc21b1580ea85aea302ac17d416f1`, `DAWN_VERSION` 1.2026101200 |
| Where | `github.com/google/dawn/releases`. The Dawn project's own GitHub publishes a nightly built by GitHub Actions. Its release notes say: *"Only dawn.googlesource.com Git source code is official. Nightly releases of native binaries … are provided as a best-effort service. They are not signed or guaranteed by Google or the Dawn team."* There is no build-provenance attestation (`gh attestation verify` returns 404). Each asset's SHA-256 is published by GitHub and pinned in `native/build.sh`. |
| Asset | `Dawn-…-macos-latest-Release.tar.gz`, 13.8 MB, sha256 `d4f419dc…d8e2`: arm64 only, static `libwebgpu_dawn.a` (21 MB) plus headers and CMake config |
| Cost | 0.5 s to download, 0.2 s to extract, 57 MB on disk (`third_party/`, git-ignored). Building `libdawn_surface.dylib` (9.4 MB, Dawn linked in) and the selftest takes 1.6 s. No Dawn source build was needed. |
| ⚠ Deployment target | The prebuilt objects are `minos 26.0` (`otool -l`: all 659 objects). **Orion's floor is macOS 14**, so for shipping Dawn must be built from source with `CMAKE_OSX_DEPLOYMENT_TARGET=14.0`, and for x86_64 too if Intel stays supported (an Intel tarball exists, also built on a current runner). The cost of a source build was not measured. |
| ⚠ Notices | The tarball has **no LICENSE or NOTICE files**. Dawn is BSD-3 (every header carries the text), but `libwebgpu_dawn.a` also bundles its dependencies (Tint, Abseil, and others). Their notices have to be collected from the source tree at that commit before anything ships. |
| **Now (#361)** | The spike links the **source build** from `tools/build-dawn.sh` instead: same commit, fetched from dawn.googlesource.com, Metal only, `minos 14.0` (all 653 objects), arm64, `libwebgpu_dawn.a` 21 MB. Fetch 41 s and 1.3 GB, build 174 s, 1.43 GB peak; the notices are in the repository's `NOTICE`. The selftest passes on it. The nightly rows above are kept as the record of what the spike first used. |

## Windows and Linux: what pairs with what

All lines are cited at Dawn commit `106704a9` and Avalonia tag `12.1.4`. Avalonia's interop API is **import-only** (`src/Avalonia.Base/Rendering/Composition/CompositionInterop.cs:34-55`), and so is Dawn's `SharedTextureMemory`. Dawn's `ExportVulkanImage` exports only images that were wrapped in the first place (`include/dawn/native/VulkanBackend.h:187-195`). So on every platform, **something outside both libraries has to allocate the shared memory.** On macOS that something is `IOSurfaceCreate`.

| | Windows: Avalonia default, ANGLE on D3D11 (`Win32PlatformOptions.RenderingMode` = `AngleEgl, Software`) | Linux: Avalonia default, GLX (`X11PlatformOptions.RenderingMode` = `Glx, Software`), or the opt-in Vulkan renderer |
|---|---|---|
| Dawn memory | `SharedTextureMemoryDXGISharedHandleDescriptor { handle, useKeyedMutex }` (`include/dawn/webgpu.h:2802`, D3D12 `SharedTextureMemoryD3D12.cpp:46-51`) | `SharedTextureMemoryOpaqueFDDescriptor { vkImageCreateInfo, memoryFD, memoryTypeIndex, allocationSize, dedicatedAllocation }` (`webgpu.h:2848`, `SharedTextureMemoryVk.cpp:791-842`) |
| Avalonia image | `D3D11TextureNtHandle` (or the global handle) (`AngleExternalObjectsFeature.cs:30-34`) | `VulkanOpaquePosixFileDescriptor`. GLX needs `GL_EXT_memory_object_fd` + `GL_EXT_semaphore_fd` (`ExternalObjectsOpenGlExtensionFeature.cs:114-118`); the Vulkan renderer does the same (`VulkanExternalObjectsFeature.cs:64-74`) |
| Sync | **Keyed mutex only.** ANGLE imports no semaphores (`AngleExternalObjectsFeature.cs:37, 94-99`). Dawn acquires and releases with key 0 (`include/dawn/native/D3DBackend.h` `kDXGIKeyedMutexAcquireKey = 0`; `d3d/KeyedMutex.cpp:51,69`). Avalonia side: `UpdateWithKeyedMutexAsync(image, 0, 0)`. | Binary semaphores: `UpdateWithSemaphoresAsync` (`…OpenGlExtensionFeature.cs:207-208`, `VulkanExternalObjectsFeature.cs:104-109`). Dawn's `EndAccess` exports `VkSemaphoreOpaqueFD`, but **only if the device does not enable `SharedFenceSyncFD`**, which it otherwise prefers (`SharedTextureMemoryVk.cpp:1144-1160`). Its `BeginAccess` takes binary fences of value 1 and requires a `VkImageLayoutBeginState` (`:1059-1081`). |
| Who allocates | Our C++: one `ID3D11Texture2D` (or D3D12 resource) with `SHARED_NTHANDLE \| SHARED_KEYEDMUTEX`, then `IDXGIResource1::CreateSharedHandle`. That is about 20 lines of **D3D**, not Vulkan. Avalonia's ANGLE layer can make exactly this texture (`AngleExternalObjectsFeature.cs:46-70`), but that method is not on the public composition API. | **Vulkan code by us**: a `VkImage` with exportable memory to get the fd and fill in `vkImageCreateInfo`, plus an exportable `VkSemaphore` for Avalonia to signal. Neither library creates one. ⚠ CLAUDE.md forbids Vulkan code written by us. |
| Format | RGBA8 (ANGLE lists only `R8G8B8A8UNorm`, `:40-43`) | RGBA8 for GLX (`…OpenGlExtensionFeature.cs:129-134`); RGBA8 or BGRA8 for Vulkan |
| Verdict | Plausible with no Vulkan at all. Unverified. | **Blocked by a constraint, not by the libraries.** One route needs a small Vulkan allocator, which only the developer can allow. The other is DMA-BUF: Dawn imports it (`SharedTextureMemoryDmaBufDescriptor`, `webgpu.h:3957`) and GBM can allocate it without Vulkan, but Avalonia 12.1.4 has **no DMA-BUF handle type**, so that would need a change to Avalonia. |

## Open risks

1. **Linux needs Vulkan code or an Avalonia change** (see the table). This is the main thing standing in the way of "Dawn everywhere".
2. **Windows is unbuilt.** The keyed mutex with key 0 on both sides gives mutual exclusion, not ordering. Ordering would come from awaiting each update before rendering the next frame, as this spike does.
3. ~~**The prebuilt is `minos 26.0`, arm64 only, unsigned, and has no notices.**~~ Closed by `tools/build-dawn.sh` (#361): source build, `minos 14.0`, notices in `NOTICE`. Still arm64 only; x86_64 is untested (`ARCHS="arm64;x86_64"`).
4. **The shell would move to Avalonia's Metal renderer.** Its UI under Skia-Metal is untested.
5. **Resizing is not handled.** The demo fixes the texture size per phase; a real canvas re-creates the IOSurface when the view changes size.
6. **The engine is still Metal-only, and Dawn would replace it.** But this spike also shows that the **current Metal engine could feed Avalonia without a copy today** if its output texture were IOSurface-backed. The shim's comment that "C# can't draw that on macOS" (`shell/native/shim.mm:7-9`) no longer holds.
7. The dropped frames in the first Metal run were not explained.

## Proposed decision row

> **#317 confirmed on macOS: Dawn → Avalonia with no CPU copy, through an IOSurface plus `MTLSharedEvent`. Windows pairs on paper; Linux needs either Vulkan code or a DMA-BUF change to Avalonia.** The spike (`spikes/dawn-avalonia/`) renders with Dawn into an IOSurface via `SharedTextureMemory`. Avalonia 12.1.4 imports the IOSurface (`ICompositionGpuInterop.ImportImage`, `IOSurfaceRef`) and hands it back and forth over timeline `MTLSharedEvent`s (`UpdateWithTimelineSemaphoresAsync`). No readback, map or upload. Avalonia makes one GPU-to-GPU snapshot copy per frame, by design. Measured at 1920×1200: 60 fps, 0.23 ms per frame on the UI thread, against 0.7 ms of CPU copy for the shell's preview today. Both macOS renderers import: OpenGL (the shell's default) at 6.9 ms of process CPU per frame because it waits on the CPU, Metal at 3.1 ms because it waits on the GPU. So the shell should move to `AvaloniaNativeRenderingMode.Metal`. Windows: Dawn `DXGISharedHandle` plus keyed mutex, paired with Avalonia/ANGLE `D3D11TextureNtHandle` plus `UpdateWithKeyedMutexAsync`, allocated by about 20 lines of D3D11. Not built. Linux: both sides speak Vulkan opaque fd, but both only import, so the memory and the consumer semaphore would have to be allocated by Vulkan code of ours, which CLAUDE.md forbids. The alternative is DMA-BUF (Dawn imports it, GBM allocates it), which Avalonia 12.1.4 cannot import. ⚠ Dawn's GitHub nightly prebuilt (`v20261027.184652`, `106704a9`) is unsigned, `minos 26.0` and arm64 only, and has no notices files; shipping needs a source build at `CMAKE_OSX_DEPLOYMENT_TARGET=14.0`. Side finding: the current Metal engine could reach Avalonia the same way, with an IOSurface-backed output texture. SDL3 GPU stays the fallback only if Linux cannot be unblocked.

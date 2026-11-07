# Orion: status

Where the project stands and what comes next.
Keep it to this page: history is `git log`, work is [GitHub issues](https://github.com/Nano-AI/Orion/issues).

**Updated:** 2026-11-07

## Where it stands

The Avalonia app is Orion on macOS and ships as alpha pre-releases on GitHub.
Windows and Linux build, test and package in CI on Dawn and run on real NVIDIA hardware on Linux; neither is a supported release yet.
Feedback: a toolbar Feedback button and an occasional toast (after 10 active minutes, at most twice in 3 days, quiet once given for a version) open the Tally form with the system, hardware, session counts and the open photo's camera and format, never a path or name (`Chrome/Feedback.cs`, `FeedbackFacts.cs`).
Shipped on `main`: JPEG XL export, foreign-XMP preservation, region-of-interest rendering, lens correction from DNG opcodes, stacking (noise and star modes, streamed), Velopack installers and updates, one palette and brand across app and site.

`main` passes every gate on a real Mac, on a Linux machine with an RTX 3070 (`tools/open-pr.sh` runs there too) and in CI on all three systems (issues 37 and 41 closed, #47, #48).

This session (PRs 57 to 60), measured with `tools/bench.py --ab` on the RTX 3070 at 24 MP:

| Change | Before | After |
|---|---|---|
| Pan past fit, the rest render (UI thread), PR 58 | 171 ms stall | 0 ms; region renders submit without waiting, the hand renders under it |
| Slider drag, a preview frame on the CPU canvas, PR 59 | 108 ms | 25 ms (bilinear under 2x; histogram and flat frame read on settle) |
| GPU held by the engine after a render / a swap, PR 60 | 1934 / 2040 MiB | 1094 / 1094 MiB (kept photos keep nothing on the card, one grain plate, lazy mattes, idle trim) |
| Swap peak | 2584 MiB | 2220 MiB |
| Lens profile, PR 57 | only the editor's session | every session, compare's candidate and batch export included |

## Next, in order

1. Zero-copy present off macOS: Windows (D3D11 keyed mutex into ANGLE, opt-in) and Linux (the one Vulkan allocator, not yet built). Issue 24.
2. A Linux CI runner with a GPU; AMD, Intel, native Wayland and a non-NixOS desktop are untested. Issue 24.
3. A hand render still costs the UI thread 15 to 25 ms (window upload and encode) plus ~12 ms for the copy's take: a worker-thread engine, or a GPU-side window crop from the full graph's source, and the copy off the UI thread.
4. Export off macOS writes sRGB only and carries no camera metadata.
5. Adobe `.lcp` lens profiles wait on the developer: the reader is on branch `lcp-adobe-profiles`, and its only source for four field meanings is a patent. Issue 5.
6. Owed checks: one real DJI `FC8482` DNG through the opcode path; stacking thresholds tuned on real star, flat and dark frames; band streaming for stacks over the memory available.
7. Camera profiles. Issue 4.
8. Smaller follow-ups: the lens switch reaches sessions already open; the selftest closes the canvases it leaves subscribed; an exact-size copy for the fit picture; highlight pins are unmeasured; a kept session's 5 MB decimated preview copy; frugal renders still wait inside `giveBack`.
9. CI flake: macOS packaging failed once with `hdiutil: create failed - Resource busy` on a green selftest (PR 57); `tools/package-app.sh` should retry the image.

## Hazards worth knowing

- `samples/_PIC8095.ARW` has people in the plaza at its base: fine as a test frame, never for a published render.
- The mask selftest checks need a lit sample: on a near-black frame their +10 of 255 never arrives, though the masks work.
- On the Linux machine another process holds 2 GB of the 8 GB card for good; two Orion GPU runs at once run each other out of memory, so every gate and bench there goes under one lock (`flock`).
- On Linux the shim builds only when CMake is configured with `-DORION_SHELL_DIR=shell`, and `dotnet build` does not copy it next to the app (`open-pr.sh` does).
- 42 MP with clarity, denoise and texture all on is slow to tick (Luminance ~300 ms) and unprofiled.
- Snapshots and mattes share a stem across `photo.ARW` and `photo.dng`.

# Orion: status

Where the project stands and what comes next.
Keep it to this page: history is `git log`, work is [GitHub issues](https://github.com/Nano-AI/Orion/issues).

**Updated:** 2026-11-04

## Where it stands

The Avalonia app is Orion on macOS and ships as alpha pre-releases on GitHub.
Windows and Linux build, test and package in CI on Dawn and run on real NVIDIA hardware on Linux; neither is a supported release yet.
Feedback: a toolbar Feedback button and an occasional toast (after 10 active minutes, at most twice in 3 days, quiet once given for a version) open the Tally form with the system, hardware, session counts and the open photo's camera and format, never a path or name (`Chrome/Feedback.cs`, `FeedbackFacts.cs`).
Shipped on `main`: JPEG XL export, foreign-XMP preservation, region-of-interest rendering, lens correction from DNG opcodes, stacking (noise and star modes, streamed), Velopack installers and updates, one palette and brand across app and site.

`main` passes every gate on a real Mac and in CI on all three systems (issues 37 and 41 closed, #47, #48).

## Next, in order

1. Zero-copy present off macOS: Windows (D3D11 keyed mutex into ANGLE, opt-in) and Linux (the one Vulkan allocator, not yet built). Issue 24.
2. A Linux CI runner with a GPU; AMD, Intel, native Wayland and a non-NixOS desktop are untested. Issue 24.
3. The rest render after a pan still waits on the GPU on the UI thread (~170 ms at 42 MP): submit without waiting.
4. Export off macOS writes sRGB only and carries no camera metadata.
5. Adobe `.lcp` lens profiles wait on the developer: the reader is on branch `lcp-adobe-profiles`, and its only source for four field meanings is a patent. Issue 5.
6. Owed checks: one real DJI `FC8482` DNG through the opcode path; stacking thresholds tuned on real star, flat and dark frames; band streaming for stacks over the memory available.
7. Camera profiles. Issue 4.

## Hazards worth knowing

- `samples/_PIC8095.ARW` has people in the plaza at its base: fine as a test frame, never for a published render.
- 42 MP with clarity, denoise and texture all on is slow to tick (Luminance ~300 ms) and unprofiled.
- Snapshots and mattes share a stem across `photo.ARW` and `photo.dng`.

# Histogram readback memory implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development. Keep implementation, task review and final branch review separate.

**Goal:** Remove the full-frame float copy from the settled histogram without changing its finite-pixel results.

**Architecture:** Read the existing full-resolution output once in its native RGBA8 or RGBA16F format, converting only sampled RGB values. Keep the stride, normalization, binning, public API and callers unchanged; delete the private float-read helper once unused.

**Tech Stack:** C++20, Metal texture readback, existing assertion harness; Swift comment correction only.

**Spec:** User's active performance/RAM goal and repository `AGENTS.md`; current `Engine::histogram` is the compatibility reference. The source audit found 20 bytes/pixel of simultaneous narrow readback plus float payload before stride-31 sampling. This is an allocation calculation, not measured RSS.

## Global Constraints

- No new dependency, filter math, shader, preview sampling, public API or threading change.
- Use one engine and one serial build/GPU process, builds at most `-j2`; check kernel pressure first. Stop owned heavy work on warning pressure. Use tiny tests and at most 3 MP for measurements.
- Preserve original RAW/XMP/matte/snapshot files. Reversibly protect pre-existing fixed `/tmp` fixtures before tests.
- Work on `codex/histogram-memory` from pushed `4479927`; batch branch remains intact pending an unlocked keyboard check. One checkout/build directory avoids another resident build.
- Parent owns commits, documentation, integration and push. All nine repository gates are required before delivery.

## Review Focus

- Both output formats and channel order must agree with actual rendered pixels.
- Non-power-of-two bin counts and endpoint clamping must retain current finite-value behavior.
- Sampling must cross row boundaries with the original global stride and count.
- Null output, zero bins and no open image must keep their existing behavior.
- Report transient allocation payload separately from sampled process footprint and measured latency; reduced fixtures do not establish 42 MP capacity.

### Task 1: Bin native readback without expanding the frame

**Files:** Modify `engine/src/Engine.cpp`, `engine/src/Engine.h`, `app/Engine+Render.swift`, and `apps/tests/tests_dng.cpp`. Reuse the small LinearRaw fixture in `testLinearDngEngineOpen`; no new test framework or production abstraction.

**Interfaces:** Consume `DevelopPipeline::output()`, `outputWidth()`, `outputHeight()` and `Texture::download(dst, rowBytes, width, height)`. Preserve `void Engine::histogram(uint32_t*, uint32_t) const`, packed R/G/B, and samples `0, 31, 62, ... < width*height`.

- [ ] Trace every histogram/float-read caller. Add a compact real-texture regression to the existing DNG fixture with varied RGB; compare every bin against independently computed bins from the rendered native pixels for 1, 7, 128 and 256 bins in narrow and wide modes. Assert per-channel totals `(width*height + 30)/31`, distinct channel distributions, endpoint behavior when present, and unchanged null/zero/unloaded contracts. Keep existing export restoration assertions meaningful.
- [ ] Establish current compatibility baseline. For this performance change, the old implementation should pass value assertions; demonstrate their sensitivity with a temporary stride or channel mutation that fails, then restore. No requirement to manufacture a functional failure in already-correct baseline bins.
- [ ] Replace full float expansion with a single native readback buffer, normalize/convert only sampled values, preserving the exact expression order for byte/255.0f and bin selection. Remove `readOutputFloat` if no remaining callers. Correct the Swift scheduling comment's allocation description.
- [ ] Run the engine test target and check the mutation is restored. Build with `cmake --build build --target orion-tests -j2`; run `./build/apps/tests/orion-tests`. Do not run unrelated app/GPU processes concurrently.
- [ ] Use a focused scratch probe in this plan's workspace to compare old and new histogram calls on the same deterministic reduced fixture, in separate serial processes. Time the calls themselves after warmup, assert identical histogram output, and sample process footprint during the call if feasible. Keep fixture creation, render and oracle downloads outside the timed region. Report sampling limits and source-derived payload separately; no full-resolution stress. Preserve exact probe source and invocation in the report for reproduction. Restore safe source and rebuild after any temporary mutation.
- [ ] Self-review `git diff --check`, report files/results/limitations in this plan's workspace, and release the build/GPU lock. Parent performs reviews, records the decision and runs all nine gates before committing/pushing.

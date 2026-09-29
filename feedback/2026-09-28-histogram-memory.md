# Histogram readback memory — 2026-09-28

**Implementation:** `e269720`, scoped and final source reviews approved; fresh build and all nine gates pass, with the pressure stop/recovery recorded below. This is a narrow readback allocation optimization, not a full-resolution capacity or Lightroom comparison result.

## Change

- `Engine::histogram` downloads exactly one native RGBA8 or RGBA16F buffer and converts only sampled RGB values. Global stride 31, clamp/bin expression order, public API, and unloaded/null/zero-bin behavior remain unchanged.
- Removed the sole-use `readOutputFloat` helper and corrected the Swift scheduling comment.
- The existing 64×48 LinearRaw DNG test now uses varied channels and compares 1, 7, 128, and 256-bin results with an independent native texture download in both output formats. It checks each channel total, channel distinction, and restored export screen pixels. The source values include 0 and 1 landmarks; expected bins are derived from actual rendered pixels, so endpoint clamping is checked wherever the output retains them.

## Verification

| Check | Result |
|---|---|
| Old implementation with new test | `cmake --build build --target orion-tests -j2`; `./build/apps/tests/orion-tests`: **1192 checks, 0 failures** (`baseline-tests.log`) |
| Temporary negative mutation | Changed only `kStride = 31` to `29`, rebuilt, ran suite: **32 histogram failures**, including native bin equality and channel totals (`mutation-tests.log`). Restored to 31 before optimization. |
| New implementation | Same build/test commands: **1192 checks, 0 failures** (`final-tests.log`) |
| Cleanup | `git diff --check` clean; no `readOutputFloat`, stride-29, or scratch probe references in production/test source. |

## Reduced-fixture probe

A deterministic 1024×768 (0.79 MP) DNG was created before timing. One engine rendered it; the probe downloaded native pixels to assert the output histogram before and after timing. Three warmups preceded 30 histogram-only calls. Median is the upper middle sorted call. Old and new used separate serial processes, each built with `-j2`. The scratch include was removed from `tests_dng.cpp` afterward. Exact source is reproduced below.

| Implementation | Bin hash | Median call | Minimum call | Footprint before / sampled peak / after |
|---|---:|---:|---:|---:|
| Old, `HEAD` `701d0c51` Engine.cpp/.h | 14363085319922008396 | 0.695042 ms | 0.649375 ms | 307643184 / 307659568 / 307643184 bytes |
| New | 14363085319922008396 | 0.363084 ms | 0.352625 ms | 307692360 / 307708744 / 307692360 bytes |

The single-process physical-footprint polling interval was 100 µs during one additional histogram call. It observed only a 16 KiB rise in both runs; allocator reuse and the short call make this **inconclusive for transient memory**. A post-call footprint is not a transient reading. The source-derived simultaneous buffer payload is the useful memory evidence: narrow old `4+16=20` bytes/pixel versus new `4`, or **15 MiB → 3 MiB at 1024×768**; wide old `8+16=24` versus new `8`, or **18 MiB → 6 MiB**. These numbers describe allocated pixel buffers, not process RSS or 42 MP capacity. The timing result is one paired local run, not a benchmark distribution.

Exact commands for probe setup/run (the scratch include line and environment branch were temporarily inserted in `tests_dng.cpp`, then removed):

```sh
# The scratch include path was: ../../.superpowers/sdd/2026-09-28-histogram-memory/histogram_probe.inc
# Insert the include immediately before the testLinearDngEngineOpen definition.
# At testLinearDngEngineOpen entry: if (std::getenv("ORION_HISTOGRAM_PROBE")) { runHistogramProbe(); return; }
# Before old run, save current Engine.cpp/.h under new-Engine.cpp/.h in this workspace,
# then install git show 701d0c5:engine/src/Engine.cpp and Engine.h respectively.
cmake --build build --target orion-tests -j2
ORION_HISTOGRAM_PROBE=1 ./build/apps/tests/orion-tests > .superpowers/sdd/2026-09-28-histogram-memory/old-probe.log 2>&1
# Restore saved new-Engine.cpp/.h, touch both files to invalidate Ninja mtimes.
touch engine/src/Engine.cpp engine/src/Engine.h
cmake --build build --target orion-tests -j2
ORION_HISTOGRAM_PROBE=1 ./build/apps/tests/orion-tests > .superpowers/sdd/2026-09-28-histogram-memory/new-probe.log 2>&1
```

Create the ignored scratch directory and save the following as `histogram_probe.inc` there before following the commands above. This probe runs inside the existing test translation unit and reuses its DNG constants; it is not a standalone program. Restore the safe sources and remove the temporary include/entry hook before rebuilding for normal use.

Exact scratch probe source (`histogram_probe.inc`):

```cpp
// Scratch include for tests_dng.cpp, removed from product tests after measurement.
#include <mach/mach.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <iostream>

void runHistogramProbe() {
    constexpr std::uint32_t w = 1024, h = 768, bins = 128;
    const std::string path = "/tmp/orion-histogram-probe.dng";
    std::vector<float> rgb(std::size_t(w) * h * 3);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto i = (std::size_t(y) * w + x) * 3;
            rgb[i] = float(x) / (w - 1);
            rgb[i + 1] = float(y) / (h - 1);
            rgb[i + 2] = float((x * 7 + y * 11) % 1024) / 1023.0f;
        }
    orion::util::DngLinearImage img;
    img.width = w;
    img.height = h;
    img.rgb = rgb.data();
    img.xyzToCam = kXyzToCam;
    img.asShotNeutral = kNeutral;
    img.camera = "SONY ILCE-7RM3";
    orion::util::writeDngLinear(path, img);
    orion::Engine engine;
    engine.openRaw(path);
    engine.render();
    const auto& d = engine.develop();
    const auto rw = d.outputWidth(), rh = d.outputHeight();
    std::vector<std::uint8_t> native(std::size_t(rw) * rh * 4);
    d.output().download(native.data(), std::size_t(rw) * 4, rw, rh);
    std::vector<std::uint32_t> expected(bins * 3), actual(bins * 3);
    for (std::size_t i = 0; i < std::size_t(rw) * rh; i += 31)
        for (std::uint32_t c = 0; c < 3; ++c) {
            const float v = native[i * 4 + c] / 255.0f;
            ++expected[c * bins + std::min(bins - 1, std::uint32_t(v * float(bins)))];
        }
    engine.histogram(actual.data(), bins);
    if (actual != expected) throw std::runtime_error("probe histogram differs from native pixels");

    const auto footprint = [] {
        task_vm_info_data_t info{};
        mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) != KERN_SUCCESS)
            throw std::runtime_error("task_info failed");
        return std::uint64_t(info.phys_footprint);
    };
    for (int i = 0; i < 3; ++i) engine.histogram(actual.data(), bins);
    std::vector<double> ms;
    for (int i = 0; i < 30; ++i) {
        const auto start = std::chrono::steady_clock::now();
        engine.histogram(actual.data(), bins);
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(ms.begin(), ms.end());
    const auto before = footprint();
    std::atomic<bool> sampling{true};
    std::atomic<std::uint64_t> peak{before};
    std::thread sampler([&] {
        while (sampling.load(std::memory_order_relaxed)) {
            peak.store(std::max(peak.load(std::memory_order_relaxed), footprint()), std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    engine.histogram(actual.data(), bins);
    sampling = false;
    sampler.join();
    if (actual != expected) throw std::runtime_error("probe histogram changed after timing");
    const auto after = footprint();
    std::uint64_t hash = 1469598103934665603ull;
    for (auto v : actual) hash = (hash ^ v) * 1099511628211ull;
    std::cout << "PROBE pixels=" << rw << "x" << rh << " hash=" << hash
              << " median_ms=" << ms[ms.size() / 2] << " min_ms=" << ms.front()
              << " footprint_before=" << before << " sampled_peak=" << peak
              << " footprint_after=" << after << "\n";
    std::remove(path.c_str());
}
```

## Safety and self-review

`memory_pressure` reported 64%, 64%, then 59% system-wide free; no warning pressure occurred. Builds and GPU runs were serial. **Fixture-protection correction:** my preflight scanned 13 *literal* `/tmp` path strings in C++ tests; none of those paths existed, so `tmp-protection/manifest.json` is `[]`. The separate probe path was absent and its temporary DNG was removed. That scan missed 23 dynamically constructed export paths in `tests_io.cpp` and `tests_watermark.cpp` (`orion-depth-*` 4, `orion-meta-*` 4, `orion-sharpen-*` 3, `orion-space-*` 4, `orion-wm-*` 8). The prior gate's `/tmp/orion-final-gates-sr6d6f7w/results.json` lists all 23 as pre-existing before this task; all 23 were **unprotected and overwritten** by my full engine-test runs, most recently at 17:38:20 local, matching the `final-tests.log` completion at 17:38:21. I did not delete or restore them. Against that prior gate's `generated-tmp/` copies, current bytes match 22 files; `orion-space-2.png` differs at byte 103 (same 909-byte size). Because I saved no task-start copies of these 23, exact task-start bytes cannot be proven or recovered from my manifest. Root has the build/GPU lock; no further `/tmp` changes were made during this correction. Existing RAW/XMP/mattes/snapshots were not modified by my code. The test export PNG is deleted by its existing test cleanup; no PNG was preserved solely for this change. Diff contains only the four assigned code files; parent-owned planning/feedback edits were untouched. No production API or dependency was added.

## Review limits and rulings

- Scoped spec/quality review found no blocking issue. The rendered fixture does not explicitly assert that exact 0/1 endpoints survive the display pipeline; those endpoints are therefore not independently established by this test. The unchanged clamp/bin expression and full native-bin comparison are the compatibility evidence.
- The implementation preflight missed 23 dynamically named generated `/tmp` exports. Their task-start bytes were not backed up, so no all-fixture preservation claim is made. The final verifier uses an explicit complete name roster and retains its runner and manifests. Original photo/sidecar/matte preservation is checked separately with streamed sample hashes.
- Full-frame native readback and main-actor scheduling remain; this removes the unnecessary float copy without adding a shader or readback API. Full-size process peak, wide-output timing, physical interaction and a paired Lightroom measurement remain open.

## Final source review

Independent whole-branch review at `006000d` approved the source with no Critical/Important findings, conditional on nine-gate verification. It checked removed-helper references, auto-enhance/facade callers, native output formats and the test's independent half decoder. It accepted the endpoint-presence limitation and declined inherited nonfinite/extreme-bin behavior, full-size/wide timing, physical UX, separate batch defects and independent Adobe-page revalidation. Those remain outside this finite-pixel allocation change, with no broader correctness claim.

The fresh full build passed with existing warnings: local LibRaw/OpenCV dylibs target macOS 26 despite the app's macOS 14 target; SwiftTerm has unused `withUnsafeBytes` results; `ViewportTests+Index.swift` captures and later mutates `now`. No histogram warning was emitted. This build does not validate the macOS 14 runtime floor.

## Final verification

At product source `e269720`, `cmake --build build -j2` passed. Engine **1192/0**, viewport **4269/0**, decisions **286 rows / 3 declared gaps**, gestures **6**, screens **3 asserting + 1 byte-stable**, modes **library 13 / batch 2 files / HDR DNG**, wiring **491 swept / 8 harness-only**, agent **21/21**, and site checks all passed.

The first screenshot gate was stopped after 6 seconds when kernel pressure reached level 2; remaining heavy gates were not started. After seven normal readings over one minute, the remaining light gates passed and a single controlled retry passed screens, modes and agent, all at peak pressure 1. No pressure threshold was bypassed, no unrelated process was stopped and no reduced sample was substituted. This is a completed gate run with an interrupted first attempt, not uninterrupted normal pressure.

Streamed hashes of 15 sample entries match the prior batch-gate inventory and the before/after manifests of both final-verification phases. All 23 files protected for these final phases were restored with identical hashes, types and mtimes. This does **not** undo the earlier implementation-run omission described above. A retained 64×48 generated watermark PNG was visually inspected: valid warm-orange fixture, too small/subtle to assess histogram UI, and no such UI claim is made.

Reusable runner, retry script, logs, results and manifests: `/tmp/orion-histogram-gates-5_owyrmx/`. The final source review's nine-gate condition is satisfied. The separate batch branch still needs its actual key-window Escape proof; these main-based gates do not contain that unmerged probe.

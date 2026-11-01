/*  Stacker — the orchestrator: file paths in, one stacked linear DNG out.
 *
 *  Modelled line for line on merge/HdrMerge: the stages are other files'
 *  work — decode (RawImage), darks and flats (Calibrate), demosaic
 *  (MergeRender), registration (Align for Noise, StarAlign for Stars and
 *  Landscape), the sky matte onto the sensor (SkySplit), integration
 *  (Integrate), the container (DngWriter) — and this
 *  class sequences them, carries progress across the facade, and owns the
 *  policy: a frame that does not register is left out (the reference never
 *  fails, it is identity), an existing output is a refusal, and a stack of
 *  fewer than two registered frames is an error rather than a copy.
 *
 *  Memory (#410): one frame is held at a time. Each registered frame is
 *  measured against the reference (Integrate.h's pieces) while its half
 *  floats are in memory, then spilled to one file in the system temp
 *  directory (6 bytes a pixel a frame, deleted on every exit); integration
 *  reads it back in bands of rows, each frame only the rows its warp
 *  reaches. Peak memory is flat in the frame count. Before any decode, a
 *  run whose estimateBytes exceeds the memory available now is refused with
 *  NotEnoughMemory, unless forced, and one whose spill exceeds the temp
 *  volume's free space is refused outright.
 *
 *  Cancellation is polled between frames and before integration and the
 *  write, and leaves nothing behind (the writer lands the file by rename).
 */

#pragma once

#include "gpu/Device.h"
#include "stack/Integrate.h"

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace orion::stack {

/// The integers are the C facade's; do not renumber.
enum class Mode { Noise = 0, Stars = 1, Landscape = 2 };

struct Request {
    std::vector<std::string> paths;
    int reference = 0;
    Mode mode = Mode::Noise;
    Options options;
    std::vector<std::string> darks, flats;
    /// Landscape only: matteWidth*matteHeight in 0..1, 1 = sky, upright (as displayed).
    std::vector<float> skyMatte;
    int matteWidth = 0, matteHeight = 0;
    std::string output;
    /// Run even when the estimate exceeds the memory available now.
    bool force = false;
    /// Test seam: the available memory to compare against; 0 asks the OS (util::availableMemory).
    std::uint64_t availableBytes = 0;
    /// Test seam: the free space on the temp volume the frames spill to; 0 asks the OS.
    std::uint64_t availableDisk = 0;
    /// Test seam: the memory a band of every frame aims for; 0 is the default (128 MB).
    std::uint64_t bandBytes = 0;
};

/// The refusal a forced run would not make: the facade returns it as ORION_ERR_MEMORY.
struct NotEnoughMemory : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Estimated peak bytes of a stack of `lights` frames of `pixels` each, with `darks` and `flats`
/// (research/UNSOURCED.md §32, measured): pixels · (78 + calibration) + pixels · lights / 10, calibration
/// 4·(max(darks, flats) + 2) when there is any.
[[nodiscard]] std::uint64_t estimateBytes(std::uint64_t pixels, std::size_t lights, std::size_t darks,
                                          std::size_t flats) noexcept;

/// Bytes of the system temp directory the registered frames spill to: their half planes, 6 a pixel.
[[nodiscard]] std::uint64_t spillBytes(std::uint64_t pixels, std::size_t lights) noexcept;

/// estimateBytes with `pixels` from one light's metadata alone (no decode). Throws if it will not read.
[[nodiscard]] std::uint64_t estimateBytes(const std::string& light, std::size_t lights, std::size_t darks, std::size_t flats);

class Stacker {
public:
    Stacker(gpu::Device& device, std::string shaderDir);

    /// Blocking; call from a worker thread and poll progress from another.
    /// Throws std::runtime_error with a sentence on failure or cancellation.
    void run(const Request& request);

    /// 0..1, monotone within one run.
    [[nodiscard]] float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }

    /// Makes the current run throw at its next stage boundary. Sticky until the next run() starts.
    void cancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }

    /// Frames left out of the last run because they did not register (for the log and the tests).
    [[nodiscard]] int skipped() const noexcept { return skipped_.load(std::memory_order_relaxed); }

private:
    void step(float progress);

    gpu::Device& device_;
    std::string  shaderDir_;
    std::atomic<float> progress_{0.0f};
    std::atomic<bool>  cancelled_{false};
    std::atomic<int>   skipped_{0};
};

}  // namespace orion::stack

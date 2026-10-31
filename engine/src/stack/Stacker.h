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
 *  Memory: frames are held as half floats (6 bytes a pixel, 144 MB at
 *  24 MP); each frame's float RGB lives only while it registers. Noise mode
 *  keeps the reference's float until every frame is aligned.
 *
 *  Cancellation is polled between frames and before integration and the
 *  write, and leaves nothing behind (the writer lands the file by rename).
 */

#pragma once

#include "gpu/Device.h"
#include "stack/Integrate.h"

#include <atomic>
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
};

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

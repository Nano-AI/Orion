/*  HdrMerge — the orchestrator: file paths in, one merged DNG out.
 *
 *  The stages are other files' work — decode (RawImage), demosaic
 *  (MergeRender), alignment (Align), the weighted merge (Merge), the
 *  container (DngWriter) — and this class only sequences them, carries
 *  progress across the facade, and owns the policy decisions the pieces
 *  deliberately do not make: how exposure ratios are established, what a
 *  failed alignment degrades to, and that an existing output path is a
 *  refusal rather than an overwrite.
 *
 *  Exposure policy: EXIF (t·ISO/N²) when the triplet is present, checked
 *  against the median of pixel ratios over the aligned overlap; past a
 *  sixth of a stop the pixels win, and a file with no usable EXIF (a
 *  synthetic frame, an adapted lens) runs on the measured ratio alone.
 *
 *  Cancellation is polled between stages and leaves nothing behind: the
 *  writer lands the file by rename, so there is no partial DNG to leak.
 */

#pragma once

#include "gpu/MetalDevice.h"

#include <atomic>
#include <string>
#include <vector>

namespace orion::merge {

class HdrMerge {
public:
    HdrMerge(gpu::Device& device, std::string shaderDir);

    /// Merges `paths[referenceIndex]`'s framing with the rest of the bracket
    /// and writes the DNG to `outputPath` (which must not exist). Blocking;
    /// call from a worker thread and poll progress from another. Returns the
    /// headroom in EV. Throws std::runtime_error on failure or cancellation.
    float run(const std::vector<std::string>& paths, int referenceIndex,
              const std::string& outputPath);

    /// 0..1, monotone within one run.
    [[nodiscard]] float progress() const noexcept {
        return progress_.load(std::memory_order_relaxed);
    }

    /// Makes the current run throw at its next stage boundary. Sticky until
    /// the next run() starts.
    void cancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }

private:
    void step(float progress);

    gpu::Device& device_;
    std::string  shaderDir_;
    std::atomic<float> progress_{0.0f};
    std::atomic<bool>  cancelled_{false};
};

}  // namespace orion::merge

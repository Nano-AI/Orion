/*  MergeRender — one bracketed frame to scene-linear camera RGB, on the GPU,
 *  without the develop graph.
 *
 *  The merge needs each frame demosaiced and nothing else. Running frames
 *  through DevelopPipeline would drag ~2.5 GiB of develop intermediates per
 *  compile (and #162's 7 GiB at 24 MP) into a batch operation that uses six
 *  nodes of it — so this is its own six-node graph reusing the shipped
 *  kernels: linearize with unit white balance, the four RCD passes, readback.
 *
 *  Unit white balance on purpose: the merge runs in *camera-native* RGB,
 *  before gains, exactly the state the merged DNG stores. The clip stays at
 *  1.0 — own-frame normalized, which is the y the merge's weights are
 *  defined on.
 *
 *  The graph compiles once and reloads across a bracket (same camera, same
 *  shape), which is the batch-export lesson applied here.
 */

#pragma once

#include "gpu/MetalDevice.h"
#include "pipe/Pipeline.h"
#include "raw/RawImage.h"

#include <memory>
#include <string>
#include <vector>

namespace orion::merge {

class MergeRender {
public:
    MergeRender(gpu::Device& device, std::string shaderDir);

    /// Demosaics one frame to tightly packed RGB floats, own-frame
    /// normalized (1.0 = this frame's clip), camera-native — no white
    /// balance, no matrix. Rebuilds the graph only when the shape changes.
    [[nodiscard]] std::vector<float> demosaic(const raw::BayerImage& image);

    /// GPU bytes the graph holds at `width x height`, for the budget
    /// assertion: source R16Uint + three R32Float intermediates + the
    /// R32Float linearize plane + the RGBA16Float result.
    [[nodiscard]] static std::size_t gpuBytes(std::uint32_t width,
                                              std::uint32_t height) noexcept {
        return std::size_t(width) * height * (2 + 4 + 4 + 4 + 4 + 8);
    }

private:
    gpu::Device& device_;
    std::string  shaderDir_;

    std::unique_ptr<pipe::Pipeline> pipeline_;
    std::uint32_t width_ = 0, height_ = 0;
    int nLinearize_ = -1, nDirs_ = -1, nLpf_ = -1, nGreen_ = -1, nRgb_ = -1;
};

}  // namespace orion::merge

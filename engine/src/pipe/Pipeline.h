/*  The pixel pipeline — a DAG of compute nodes.
 *
 *  Deliberately a graph rather than a chain: it makes the dirty-subgraph
 *  trivial to compute, which is what lets a slider recompute only what sits
 *  downstream of it instead of the whole pipeline. That is the difference
 *  between a 16 ms response and a 100 ms one.
 *
 *  Each node is exactly one compute shader (ARCHITECTURE.md), and every
 *  intermediate stays resident on the GPU.
 */

#pragma once

#include "gpu/Resources.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orion::pipe {

/// Sentinel input meaning "the pipeline's source texture".
inline constexpr int kSource = -1;

struct Node {
    std::string             name;     // for logs and profiling
    std::string             kernel;   // entry point in the metallib
    std::vector<int>        inputs;   // node ids, or kSource
    gpu::PixelFormat        format;   // this node's output format
    std::vector<std::byte>  params;   // opaque block bound at buffer(0)
    std::vector<int>        aux;      // auxiliary texture ids, bound after inputs

    // Output dimensions, when this node changes them — rotation swaps width
    // and height, and crop will shrink them. Zero means "same as the graph".
    /// A disabled node copies its first input through instead of running.
    bool enabled = true;

    std::uint32_t outWidth  = 0;
    std::uint32_t outHeight = 0;
};

struct NodeTiming {
    std::string name;
    bool        executed = false;   // false when the cache served it
    /// GPU milliseconds for this node alone. Zero unless profiling is on —
    /// see `setProfiling`.
    double      ms = 0.0;
};

class Pipeline {
public:
    /// shaderDir holds one <entryPoint>.metallib per kernel.
    Pipeline(gpu::Device& device, std::string shaderDir);
    ~Pipeline();

    Pipeline(const Pipeline&)            = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    /// Returns the new node's id. Inputs must already have been added — the
    /// graph is built in dependency order, then verified by compile().
    int add(Node node);

    /// Topologically sorts, builds pipeline states, and allocates
    /// intermediates at the given working resolution.
    void compile(std::uint32_t width, std::uint32_t height);

    /// Replaces a node's parameter block and marks it — and everything
    /// downstream — for recomputation.
    void setParams(int nodeId, const void* data, std::size_t bytes);

    /// Enables or disables a node. Disabled nodes are skipped entirely.
    void setEnabled(int nodeId, bool enabled);

    /// Changes a node's output format, reallocating its texture.
    ///
    /// Exists so the tail of the graph can be narrow for the screen — whose
    /// drawable is `bgra8Unorm`, so anything wider is bytes moved for
    /// precision the display cannot show — and wide for an export, which is
    /// the only consumer that can use sixteen bits. Reallocation is not free,
    /// so this belongs on a mode switch and never on a slider.
    void setNodeFormat(int nodeId, gpu::PixelFormat format);

    [[nodiscard]] gpu::PixelFormat nodeFormat(int nodeId) const;

    /// Which node's texture a consumer should read, following any
    /// disabled nodes back to a live producer.
    [[nodiscard]] int resolve(int nodeId) const;

    /// Uploads the source mosaic. Marks the whole graph dirty.
    void setSource(const void* samples, std::size_t bytesPerRow);

    /// Registers a texture that nodes can read but no node produces — curve
    /// LUTs now, masks and lens-correction maps later. Must be called before
    /// compile(). Returns its id, for Node::aux.
    int addAuxTexture(std::uint32_t width, std::uint32_t height, gpu::PixelFormat);

    /// Replaces an auxiliary texture's contents and dirties every node that
    /// reads it.
    void updateAux(int auxId, const void* data, std::size_t bytesPerRow);

    /// Times every node separately instead of batching them.
    ///
    /// One command buffer per node, so `lastRun()` carries real per-node
    /// milliseconds. That serializes the queue and adds a submission per node,
    /// so the total is *not* comparable to a normal render — this answers
    /// "which node is expensive", not "how long does a frame take". It exists
    /// because the alternative is guessing, and guessing cost an afternoon
    /// optimizing the wrong kernel.
    void setProfiling(bool on) noexcept { profiling_ = on; }

    /// Executes every dirty node in one command buffer.
    /// Returns the GPU-side duration in milliseconds.
    double render();

    /// Which nodes actually ran on the last render.
    [[nodiscard]] const std::vector<NodeTiming>& lastRun() const noexcept { return lastRun_; }

    /// The terminal node's texture.
    [[nodiscard]] const gpu::Texture& output() const;

    /// A specific node's output, for debugging and stage inspection.
    [[nodiscard]] const gpu::Texture& nodeOutput(int nodeId) const;

    /// The source mosaic, for debugging.
    [[nodiscard]] const gpu::Texture& sourceTexture() const;

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t intermediateBytes() const noexcept;

private:
    void markDownstreamDirty(int nodeId);

    gpu::Device& device_;
    std::string  shaderDir_;

    std::vector<Node>                          nodes_;
    std::vector<std::unique_ptr<gpu::Library>> libraries_;
    std::vector<std::unique_ptr<gpu::Kernel>>  kernels_;
    std::vector<std::unique_ptr<gpu::Texture>> outputs_;
    std::vector<bool>                          dirty_;
    std::vector<int>                           order_;

    struct AuxSpec { std::uint32_t width, height; gpu::PixelFormat format; };
    std::vector<AuxSpec>                       auxSpecs_;
    std::vector<std::unique_ptr<gpu::Texture>> aux_;

    std::unique_ptr<gpu::Texture> source_;
    std::uint32_t width_ = 0, height_ = 0;
    bool compiled_ = false;
    bool profiling_ = false;

    std::vector<NodeTiming> lastRun_;
};

}  // namespace orion::pipe

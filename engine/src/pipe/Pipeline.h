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
#include <stdexcept>
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

    /// What the source texture is. R16Uint — one mosaic sample per pixel —
    /// unless a graph whose source is already demosaiced says otherwise.
    /// Must be called before compile(); the texture is allocated there.
    void setSourceFormat(gpu::PixelFormat format) {
        if (compiled_) throw std::runtime_error("setSourceFormat after compile()");
        sourceFormat_ = format;
    }

    /// Registers a texture that nodes can read but no node produces — curve
    /// LUTs now, masks and lens-correction maps later. Must be called before
    /// compile(). Returns its id, for Node::aux.
    int addAuxTexture(std::uint32_t width, std::uint32_t height, gpu::PixelFormat);

    /// Replaces an auxiliary texture's contents and dirties every node that
    /// reads it.
    void updateAux(int auxId, const void* data, std::size_t bytesPerRow);

    /// Reallocates an auxiliary texture at a new size, discarding its contents.
    ///
    /// Exists so a texture that is expensive and usually unwanted can be
    /// registered at 1x1 and grown on first use — the brush accumulator is
    /// ~97 MB at 24 Mpx and most photographs are never painted on. Does **not**
    /// dirty anything: the contents are gone, so whoever grows it is also
    /// responsible for saying that what it held is no longer there.
    void resizeAux(int auxId, std::uint32_t width, std::uint32_t height);

    [[nodiscard]] std::uint32_t auxWidth(int auxId) const;

    /// Whether a node will run at the next render.
    ///
    /// ⚠ Exists for one caller and one reason: a kernel that *accumulates into
    /// a persistent texture* is not idempotent, so running it twice with one
    /// set of parameters composites the same dabs twice. Every other node in
    /// this graph is a pure function of its inputs and can be re-run freely,
    /// which is why nothing needed to ask this before. See
    /// `DevelopPipeline::render`.
    [[nodiscard]] bool nodeDirty(int nodeId) const;

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

    /// The bytes a graph would need if a texture were reused the moment its
    /// last consumer had run, rather than held for the graph's lifetime.
    ///
    /// ⚠ **This is a costing number, not a promise.** `intermediateBytes` is
    /// what is allocated today: one texture per node, alive from construction
    /// to destruction. This walks the execution order, keeps a texture live
    /// only between the node that writes it and the last node that reads it,
    /// and reports the high-water mark — which is what a pooled allocator
    /// could reach. The gap between the two is the size of the prize, and it
    /// is worth knowing before choosing between tiling, dropping precision and
    /// refusing to open (#152).
    ///
    /// It assumes textures of differing sizes can share an allocation, which a
    /// real pool cannot always do, so the true figure sits between this and
    /// `intermediateBytes`. Deliberately optimistic: the point is an upper
    /// bound on the saving, and a disappointing upper bound settles the
    /// question immediately.
    /// Node outputs that must never be recycled, because something outside the
    /// graph reads them after the render has finished.
    ///
    /// ⚠ **This is the difference between a pool that works and one that
    /// renders another node's picture.** `nodeOutput` hands out any node's
    /// texture once the render is done, and a liveness walk cannot see that —
    /// it only knows who reads a texture *inside* the graph. A recycled
    /// intermediate handed to such a reader is correctly sized, correctly
    /// formatted and wrong, which is a defect this repository has shipped
    /// before and caught only byte for byte. Decision #156.
    ///
    /// `DevelopPipeline` declares its own: the fusion proxy and the dehaze peak
    /// (`DevelopLocal.cpp`). The final output and the source are pinned by
    /// `peakLiveBytes` itself and need not be listed — they are structural
    /// rather than a property of any particular graph.
    void setPinned(std::vector<int> nodeIds);

    /// The bytes a pooled graph would need, honestly — see `setPinned`.
    ///
    /// ⚠ Still optimistic in one remaining way, stated so nobody treats it as a
    /// target: it lets textures of differing shapes share an allocation, which
    /// a real pool keyed by exact shape cannot always do. The truth sits
    /// between this and `intermediateBytes`.
    [[nodiscard]] std::size_t peakLiveBytes() const noexcept;

private:
    void markDownstreamDirty(int nodeId);

    gpu::Device& device_;
    std::string  shaderDir_;

    std::vector<Node>                          nodes_;
    std::vector<std::unique_ptr<gpu::Library>> libraries_;
    std::vector<std::unique_ptr<gpu::Kernel>>  kernels_;
    /// ⚠ **Non-owning, as of decision #158.** These were `unique_ptr`, one
    /// owner per node — which cannot express what a texture pool is for: two
    /// nodes that never overlap in time sharing one texture. There is no way to
    /// put the same `unique_ptr` at index 12 and index 47.
    ///
    /// Ownership lives in `ownedOutputs_` below. Today that is still exactly
    /// one texture per node and the render is bit-identical to before the
    /// change; the pool replaces it as a second, separately revertible step.
    /// A null entry means the node has no output yet.
    std::vector<gpu::Texture*>                 outputs_;

    /// What actually owns the textures `outputs_` points into.
    ///
    /// ⚠ Entries are never erased while a graph lives, only replaced — see
    /// `reallocateOutput`. A pointer in `outputs_` is invalidated the moment
    /// its owner is dropped, and the reallocation path is the one place that
    /// happens.
    std::vector<std::unique_ptr<gpu::Texture>> ownedOutputs_;
    std::vector<bool>                          dirty_;
    std::vector<int>                           order_;
    std::vector<int>                           pinned_;

    struct AuxSpec { std::uint32_t width, height; gpu::PixelFormat format; };
    std::vector<AuxSpec>                       auxSpecs_;
    std::vector<std::unique_ptr<gpu::Texture>> aux_;

    std::unique_ptr<gpu::Texture> source_;
    gpu::PixelFormat sourceFormat_ = gpu::PixelFormat::R16Uint;
    std::uint32_t width_ = 0, height_ = 0;
    bool compiled_ = false;
    bool profiling_ = false;

    std::vector<NodeTiming> lastRun_;
};

}  // namespace orion::pipe

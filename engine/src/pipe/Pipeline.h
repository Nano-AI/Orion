/*  The pixel pipeline — a DAG of compute nodes.
 *
 *  Deliberately a graph rather than a chain: it makes the dirty-subgraph
 *  trivial to compute, which is what lets a slider recompute only what sits
 *  downstream of it instead of the whole pipeline. That is the difference
 *  between a 16 ms response and a 100 ms one.
 *
 *  Each node is exactly one compute shader (ARCHITECTURE.md). Intermediates
 *  are pooled (decision #219): a texture is only resident between the node
 *  that writes it and the last node that reads it, and a node whose input was
 *  recycled simply recomputes it — every node here is a pure function of its
 *  own inputs, so that costs time once and never changes a pixel.
 */

#pragma once

#include "gpu/Resources.h"
#include "gpu/TexturePool.h"

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
    /// ⚠ **This is `render()`'s own schedule, read back rather than
    /// re-derived — decision #219 wires the pool this used to only cost.**
    /// `intermediateBytes()` now reports whatever is *currently* resident,
    /// which right after `compile()` is close to nothing: nodes allocate
    /// lazily, on their first `computeOne`/`ensure`, and this walks the
    /// execution order, keeps a texture live only between the node that
    /// writes it and the last node that reads it, and reports the high-water
    /// mark that `TexturePool` actually reaches during a render. Before the
    /// pool this was a costing number rather than a promise; it is worth
    /// knowing before choosing between tiling, dropping precision and
    /// refusing to open (#152) either way.
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
    /// a real pool keyed by exact shape cannot always do (`TexturePool::acquire`
    /// matches on exact width, height and format). The truth sits between this
    /// and `intermediateBytes`. `poolPeakBytes()` below is what to compare it
    /// against now that `render()` actually pools (decision #219) —
    /// `intermediateBytes()` reads back what is resident *right now*, which
    /// a working pool keeps small once a render has freed what it can, so it
    /// is no longer the number that should land near this estimate.
    [[nodiscard]] std::size_t peakLiveBytes() const noexcept;

    /// A node's output size, from its own declared shape and format — never
    /// from `outputs_[id]->sizeBytes()`. Both this and `peakLiveBytes()` are
    /// hypotheticals ("if a full render ran right now"), and allocation is
    /// lazy, so a node's actual texture may not exist yet; sizing from the
    /// `Node` itself keeps the estimate independent of what has or hasn't been
    /// computed. Public because the bench sums it to report what eager
    /// allocation used to cost.
    [[nodiscard]] std::size_t nodeBytes(int id) const noexcept;

    /// The pool's own high-water mark — what `TexturePool::acquire` actually
    /// reached during rendering, as opposed to `peakLiveBytes()`'s static
    /// estimate of what it should reach. Comparing the two is how the wiring
    /// gets checked, the way decision #157 checked the pins against #153's
    /// estimate. Does not decay when memory is later freed — see
    /// `TexturePool::peakLiveBytes`.
    [[nodiscard]] std::size_t poolPeakBytes() const noexcept { return pool_.peakLiveBytes(); }

private:
    void markDownstreamDirty(int nodeId);

    /// The topological step of each node's last static reader — shared by
    /// `render()`'s pool and `peakLiveBytes()`'s cost estimate, so the two
    /// analyses can never say different things about which read is last.
    /// #153 already computed this once per call; this is that same walk,
    /// factored out so render() can act on it instead of merely reporting it.
    [[nodiscard]] std::vector<std::size_t> computeLastUseSteps() const;


    /// Whether `n` must never be recycled: it is `setPinned`'s list, or it is
    /// the graph's terminal node. Checked dynamically rather than cached,
    /// because `setPinned` is called after `compile()` (decision #156).
    [[nodiscard]] bool isPinnedOrTerminal(int n) const noexcept;

    /// Makes sure `id`'s output exists, recursing into its inputs first if
    /// they too were recycled. A no-op if the texture is already there.
    ///
    /// ⚠ **Why recomputing a "clean" node is safe.** Every node here is a pure
    /// function of its inputs (`nodeDirty`'s comment names the one exception),
    /// so rebuilding one whose own inputs have not changed reproduces the
    /// exact bytes it held before — this is what lets the pool recycle a
    /// texture without a reader ever seeing the difference.
    void ensure(int id, gpu::CommandBuffer* batch);

    /// Gives `id` a fresh texture (returning whatever it held to the pool
    /// first), dispatches its kernel, and frees any input whose last reader
    /// this was. The one place that actually writes a node's output —
    /// `render()`'s dirty path and `ensure()`'s recovery path both funnel
    /// through it, so the two can never disagree about how a texture is
    /// acquired, dispatched or released.
    void computeOne(int id, gpu::CommandBuffer* batch);

    /// After `reader` has been dispatched, releases every input whose last
    /// reader (per `lastReader_`) was `reader` — including `reader` itself,
    /// for the degenerate case of a node nobody reads at all.
    void releaseIfDone(int reader);

    /// Returns `n`'s texture to the pool if `reader` is its last static
    /// reader, it has never been freed before, and it is not pinned.
    ///
    /// ⚠ **Once, ever, per node — not once per render.** `lastReader_[n]`
    /// does not change, so without the "never freed before" guard this would
    /// fire again on *every* future render that dispatches `reader`: free it,
    /// let `ensure()` rebuild it for the next tick, free it again. That is
    /// the regression decision #161 measured (a drag tick turning into a full
    /// render) reintroduced one node at a time. Freeing once and letting a
    /// later independent read rebuild it (`ensure()`) costs that one rebuild
    /// exactly once for the pipeline's life; freeing on every match costs it
    /// forever.
    void maybeFree(int n, int reader);

    gpu::Device&      device_;
    std::string       shaderDir_;
    gpu::TexturePool  pool_;

    std::vector<Node>                          nodes_;
    std::vector<std::unique_ptr<gpu::Library>> libraries_;
    std::vector<std::unique_ptr<gpu::Kernel>>  kernels_;
    /// ⚠ **Non-owning, as of decision #158.** These were `unique_ptr`, one
    /// owner per node — which cannot express what a texture pool is for: two
    /// nodes that never overlap in time sharing one texture. There is no way to
    /// put the same `unique_ptr` at index 12 and index 47.
    ///
    /// Ownership lives in `ownedOutputs_` below, and as of the pool's adoption
    /// a null entry is the ordinary case rather than a construction detail: it
    /// means either "not computed yet" or "computed, read by everything that
    /// will ever read it, and returned to the pool" (`releaseIfDone`). Either
    /// way `ensure()` rebuilds it on demand, so a null pointer here is never a
    /// bug by itself — only a stale non-null one, if a release forgot to clear
    /// the pointer alongside it, would be.
    std::vector<gpu::Texture*>                 outputs_;

    /// What actually owns the textures `outputs_` points into.
    ///
    /// ⚠ Entries are replaced — never erased — throughout a graph's life: by
    /// `computeOne` on every recompute, by `setNodeFormat` on a format change,
    /// and set to `nullptr` by `releaseIfDone` once a node's last reader has
    /// run. A pointer in `outputs_` is invalidated the moment its owner here
    /// changes, which is why the two are always updated together.
    std::vector<std::unique_ptr<gpu::Texture>> ownedOutputs_;
    std::vector<bool>                          dirty_;
    std::vector<int>                           order_;
    std::vector<int>                           pinned_;

    /// `lastReader_[n]`: the id of the last node (in topological order) that
    /// reads `n`, or `n` itself if nothing does. Static — the graph's wiring
    /// never changes after `compile()` — computed once there from
    /// `computeLastUseSteps()` and read by `releaseIfDone`/`maybeFree` for the
    /// rest of the pipeline's life.
    std::vector<int>                           lastReader_;

    /// `everFreed_[n]`: has `n`'s texture already been returned to the pool
    /// once. See `maybeFree`'s comment for why this must never happen twice.
    std::vector<bool>                           everFreed_;

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

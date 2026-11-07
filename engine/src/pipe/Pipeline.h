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
    std::vector<int>        aux{};    // auxiliary texture ids, bound after inputs

    // Output dimensions, when this node changes them — rotation swaps width
    // and height, and crop will shrink them. Zero means "same as the graph".
    /// A disabled node copies its first input through instead of running.
    bool enabled = true;

    std::uint32_t outWidth  = 0;
    std::uint32_t outHeight = 0;

    /// A level of a multi-node effect that only its own chain reads, which
    /// reruns whole whenever any of it is dirty: never kept when the memory
    /// rule is frugal (`Pipeline::decideMemory`, #406).
    bool transient = false;

    /// What this node's pass costs per output pixel against a plain one, for
    /// which cached outputs a frugal render keeps (#406). 1 unless measured.
    float costWeight = 1.0f;
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

    [[nodiscard]] gpu::Device& device() const noexcept { return device_; }

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

    /// Changes a node's output format, reallocating on its next dispatch.
    ///
    /// Exists so the tail of the graph can be narrow for the screen — whose
    /// drawable is `bgra8Unorm`, so anything wider is bytes moved for
    /// precision the display cannot show — and wide for an export, which is
    /// the only consumer that can use sixteen bits. Reallocation is not free,
    /// so this belongs on a mode switch and never on a slider.
    void setNodeFormat(int nodeId, gpu::PixelFormat format);

    [[nodiscard]] gpu::PixelFormat nodeFormat(int nodeId) const;

    /// Changes a node's output size, reallocating on its next dispatch, as
    /// `setNodeFormat` does: for the one node whose size follows the edit
    /// (geometry, #399). The caller decides how often; this always obeys.
    void setNodeSize(int nodeId, std::uint32_t width, std::uint32_t height);

    /// A node's output size: its own, else the graph's working size.
    [[nodiscard]] std::uint32_t nodeWidth(int nodeId) const;
    [[nodiscard]] std::uint32_t nodeHeight(int nodeId) const;

    /// Which node's texture a consumer should read, following any
    /// disabled nodes back to a live producer.
    [[nodiscard]] int resolve(int nodeId) const;

    /// Uploads the source mosaic. Marks the whole graph dirty.
    void setSource(const void* samples, std::size_t bytesPerRow);

    /// Drops the source texture until the next `setSource`, which allocates it
    /// again (#318): a kept photo's mosaic lives on the CPU. ⚠ A render before
    /// that reads a null source; `DevelopPipeline::render` refuses it.
    void releaseSource() noexcept { source_.reset(); }
    [[nodiscard]] bool hasSource() const noexcept { return source_ != nullptr; }

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

    /// Binds a texture other graphs share (the grain plate, one per device) in
    /// place of this aux slot's own, and dirties its readers; null puts back
    /// one of its own at 1x1. A shared texture is not this graph's: `auxBytes`
    /// leaves it out, so the memory rule counts it once, with the rest of the
    /// device.
    void shareAux(int auxId, std::shared_ptr<gpu::Texture> texture);

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

    /// Return every intermediate to the pool at its last reader on *every*
    /// render, not once for the pipeline's life - `maybeFree`'s
    /// `everFreed_` guard switched off. Decision #312.
    ///
    /// ⚠ **For headless, one-shot users only: `Orion --agent`, the
    /// `--batch-export` driver, a test.** The guard exists to protect slider
    /// latency (#161): with it off, a drag tick that dirties one node
    /// rebuilds every clean intermediate the pool reclaimed last tick, and a
    /// tick becomes a full render. A process that renders a handful of
    /// times and exits has no tick to protect, and pays for the guard in
    /// the opposite currency: every render after the first keeps every
    /// texture it rebuilds, because a node already freed once is never
    /// freed again, so the pool cannot hand those bytes to the next node and
    /// allocates fresh ones instead. One agent call with a sidecar, a
    /// proposal and a matte peaked at 13.7 GB that way, and ten of them in
    /// parallel froze a 36 GB Mac. Must stay off in the interactive app.
    ///
    /// Correctness does not depend on it either way: a node whose texture
    /// went back to the pool is null in `outputs_`, and `ensure()` rebuilds
    /// any null input before a dispatch reads it - the same path the
    /// once-ever free already relies on, taken more often.
    void setReleaseEveryRender(bool on) noexcept { releaseEveryRender_ = on; }

    /// Marks nodes `Node::transient` (#406).
    void setTransient(const std::vector<int>& nodeIds);
    void setCostWeight(int nodeId, float weight);

    /// The memory rule's ceiling in bytes: a render plans within
    /// min(this, what the device can still give plus what the graph holds).
    /// 2.5e9 by decision #406; tests lower it.
    void setMemoryBudget(std::uint64_t bytes) noexcept { budgetCap_ = bytes; }
    [[nodiscard]] std::uint64_t memoryBudget() const noexcept { return budgetCap_; }

    /// What the rule decided for the last render (`decideMemory`): the
    /// budget, the planned peak under the caching policy, the planned peak
    /// frugal (0 when caching fit), and how many cached outputs it kept.
    struct MemoryPlan {
        std::uint64_t budget = 0, cached = 0, frugal = 0;
        bool          frugalMode = false;
        std::size_t   kept = 0;
    };
    [[nodiscard]] const MemoryPlan& lastPlan() const noexcept { return plan_; }

    /// Gives every node's texture back to Metal, pinned and terminal ones
    /// included, and marks the whole graph dirty: the state `compile()` left
    /// it in. The source, the aux plates and the kernels stay. For a photo
    /// kept decoded but not shown (decision #318); the next render rebuilds
    /// everything, through the same path a first render takes.
    void releaseOutputs() noexcept;

    /// `releaseOutputs` but for the picture and the pinned outputs, which
    /// readers outside the graph still read: an idle canvas short of memory
    /// keeps what it shows and what the eyedropper samples (#406).
    void releaseCache() noexcept;

    /// The pool's idle textures go back to the driver (after `Device::reclaim`
    /// on Dawn). An ordinary render never does this; an idle canvas does.
    void trimPool() noexcept { pool_.shrink(); }

    /// Executes every dirty node in one command buffer.
    /// Returns the GPU-side duration in milliseconds, 0 when nothing was dirty.
    ///
    /// `wait` false submits and returns (0 ms): the canvas's region past fit,
    /// whose copy is queued behind it (#22). Until the GPU is done, `busy` is
    /// true, and every call here that writes what the GPU may still read (a
    /// render, the source, an aux plate, a release) waits for it first
    /// (`finishPending`): Metal writes a texture from the CPU at once
    /// (replaceRegion), Dawn in queue order. Dawn's Readback, a CPU read, is
    /// queue-ordered too. One render in flight at most.
    double render(bool wait = true);
    [[nodiscard]] bool busy();
    void finishPending();

    /// Which nodes actually ran on the last render.
    [[nodiscard]] const std::vector<NodeTiming>& lastRun() const noexcept { return lastRun_; }

    /// The terminal node's texture.
    [[nodiscard]] const gpu::Texture& output() const;

    /// A specific node's output, for debugging and stage inspection.
    [[nodiscard]] const gpu::Texture& nodeOutput(int nodeId) const;

    /// The node as built, and the textures its dispatch binds, in order:
    /// inputs (resolved past disabled nodes), aux, then its own output. A
    /// texture not resident is null. For replaying one dispatch elsewhere —
    /// the Dawn parity test runs each node's kernel alone on Metal's inputs.
    [[nodiscard]] const Node& node(int nodeId) const { return nodes_.at(static_cast<std::size_t>(nodeId)); }
    [[nodiscard]] std::vector<const gpu::Texture*> bindings(int nodeId) const;

    /// The source mosaic, for debugging.
    [[nodiscard]] const gpu::Texture& sourceTexture() const;

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
    /// The name a node was built with — what `lastRun()` reports it as.
    [[nodiscard]] const std::string& nodeName(int id) const { return nodes_.at(std::size_t(id)).name; }
    [[nodiscard]] std::size_t intermediateBytes() const noexcept;
    /// All owned texture payloads, including idle pool entries and auxiliary
    /// plates. Excludes driver overhead; use process footprint for total RAM.
    [[nodiscard]] std::size_t allocatedBytes() const noexcept;
    /// The auxiliary plates alone: LUTs, mattes, the grain plate.
    [[nodiscard]] std::size_t auxBytes() const noexcept;

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

    /// The node whose texture the device refused on the last render, or -1:
    /// what the out-of-memory message names (#406).
    [[nodiscard]] int failedNode() const noexcept { return failedNode_; }

private:
    void markDownstreamDirty(int nodeId);

    /// The topological step of each node's last static reader — shared by
    /// `render()`'s pool and `peakLiveBytes()`'s cost estimate, so the two
    /// analyses can never say different things about which read is last.
    /// #153 already computed this once per call; this is that same walk,
    /// factored out so render() can act on it instead of merely reporting it.
    [[nodiscard]] std::vector<std::size_t> computeLastUseSteps() const;

    /// `lastReader_` from `computeLastUseSteps()`, for the graph as enabled now.
    void computeLastReaders();


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
    void ensure(int id);

    /// Gives `id` a fresh texture (returning whatever it held to the pool
    /// first), dispatches its kernel, and frees any input whose last reader
    /// this was. The one place that actually writes a node's output —
    /// `render()`'s dirty path and `ensure()`'s recovery path both funnel
    /// through it, so the two can never disagree about how a texture is
    /// acquired, dispatched or released.
    void computeOne(int id);

    /// After `reader` has been dispatched, releases every input whose last
    /// reader (per `lastReader_`) was `reader` — including `reader` itself,
    /// for the degenerate case of a node nobody reads at all.
    void releaseIfDone(int reader);

    /// Returns `n`'s texture to the pool if `reader` is its last static
    /// reader, it has never been freed before, and it is not pinned.
    ///
    /// ⚠ **Once, ever, per node - not once per render** - unless
    /// `setReleaseEveryRender` turned the guard off for a headless caller. `lastReader_[n]`
    /// does not change, so without the "never freed before" guard this would
    /// fire again on *every* future render that dispatches `reader`: free it,
    /// let `ensure()` rebuild it for the next tick, free it again. That is
    /// the regression decision #161 measured (a drag tick turning into a full
    /// render) reintroduced one node at a time. Freeing once and letting a
    /// later independent read rebuild it (`ensure()`) costs that one rebuild
    /// exactly once for the pipeline's life; freeing on every match costs it
    /// forever.
    void maybeFree(int n, int reader);

    /// Hands the pool's idle textures back to the driver now: the batch so far
    /// is submitted and waited on first, because Dawn and Metal keep a texture
    /// until the submit that used it is done (#399), then a fresh batch.
    void giveBack();
    /// The idle bytes a frugal render lets the pool hold before it gives them
    /// back: one RGBA16F plane of the working frame. Under a plane, a give-back
    /// (a submit and a wait, 2-7 ms) costs more than the memory it frees.
    [[nodiscard]] std::size_t givePlane() const noexcept {
        return std::size_t(width_) * height_ * gpu::bytesPerPixel(gpu::PixelFormat::RGBA16Float);
    }

    /// `render()`'s loop: every dirty, enabled node in order, inputs first.
    void runDirty();

    /// ⚠ **The memory rule, the one place it is decided (#406).** Plans this
    /// render's peak under the caching policy, counting everything the graph
    /// already holds (`planPeak`); if that exceeds the budget, this render is
    /// frugal: effect levels are freed as read, every output is freed at its
    /// last reader except a keep set chosen dearest-to-rebuild first while the
    /// frugal plan stays under budget, cached outputs this render does not
    /// read go back when it starts, the rest when it ends, and the pool gives
    /// idle memory back before it allocates. PipelineMemory.cpp.
    void decideMemory();
    /// Walks this render as `runDirty` would, on shapes and bytes alone, from
    /// what is resident and idle now, and returns the most it would hold.
    [[nodiscard]] std::size_t planPeak(bool frugal, const std::vector<bool>& keep) const;
    /// The clean outputs a render of the dirty nodes reads, with the inputs of
    /// any of them it must rebuild.
    [[nodiscard]] std::vector<bool> readThisRender(const std::vector<bool>& resident) const;

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
    /// reads `n`, or `n` itself if nothing does, among the nodes enabled now.
    /// Computed at `compile()` from `computeLastUseSteps()`, again at the next
    /// render after a node is switched (#406), and read by
    /// `releaseIfDone`/`maybeFree`.
    std::vector<int>                           lastReader_;
    /// A node was switched since `lastReader_` was computed (#406).
    bool                                       lastReadersStale_ = false;

    /// `everFreed_[n]`: has `n`'s texture already been returned to the pool
    /// once. See `maybeFree`'s comment for why this must never happen twice.
    std::vector<bool>                           everFreed_;

    struct AuxSpec { std::uint32_t width, height; gpu::PixelFormat format; };
    std::vector<AuxSpec>                       auxSpecs_;
    std::vector<std::shared_ptr<gpu::Texture>> aux_;
    std::vector<bool>                          auxShared_;   // `shareAux`'s, not counted

    std::unique_ptr<gpu::Texture> source_;
    gpu::PixelFormat sourceFormat_ = gpu::PixelFormat::R16Uint;
    std::uint32_t width_ = 0, height_ = 0;
    bool compiled_ = false;
    bool profiling_ = false;
    /// `setReleaseEveryRender`. Survives `compile()` and `setSource()`, so a
    /// headless engine that reloads the graph for the next frame keeps it.
    bool releaseEveryRender_ = false;
    bool trimPool_ = false; // a filter/format transition left disposable textures

    std::vector<NodeTiming> lastRun_;

    /// The render's open command buffer (null when profiling), and the GPU
    /// milliseconds of the batches `giveBack` already submitted.
    std::unique_ptr<gpu::CommandBuffer> batch_;
    /// A `render(false)` the GPU may still be running.
    std::unique_ptr<gpu::CommandBuffer> pending_;
    double submittedMs_ = 0.0;
    int failedNode_ = -1;

    // The memory rule's state (#406).
    bool              frugal_ = false;
    std::vector<bool> keep_;
    std::uint64_t     budgetCap_ = 2'500'000'000;
    MemoryPlan        plan_;
};

}  // namespace orion::pipe

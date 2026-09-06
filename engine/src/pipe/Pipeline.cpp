#include "pipe/Pipeline.h"

#include <memory>

#include <algorithm>
#include <cstring>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace orion::pipe {

Pipeline::Pipeline(gpu::Device& device, std::string shaderDir)
    : device_(device), shaderDir_(std::move(shaderDir)), pool_(device) {}

Pipeline::~Pipeline() = default;

int Pipeline::add(Node node) {
    if (compiled_) throw std::runtime_error("cannot add nodes after compile()");
    nodes_.push_back(std::move(node));
    return static_cast<int>(nodes_.size()) - 1;
}

void Pipeline::compile(std::uint32_t width, std::uint32_t height) {
    if (nodes_.empty()) throw std::runtime_error("pipeline has no nodes");

    width_  = width;
    height_ = height;

    // Kahn's algorithm. Also catches a cycle, which at this level would mean a
    // graph-construction bug rather than anything a user could cause.
    const int n = static_cast<int>(nodes_.size());
    std::vector<int> indegree(n, 0);
    std::vector<std::vector<int>> dependents(n);

    for (int i = 0; i < n; ++i) {
        for (int in : nodes_[i].inputs) {
            if (in == kSource) continue;
            if (in < 0 || in >= n) {
                throw std::runtime_error("node '" + nodes_[i].name + "' has a bad input id");
            }
            ++indegree[i];
            dependents[in].push_back(i);
        }
    }

    std::queue<int> ready;
    for (int i = 0; i < n; ++i) {
        if (indegree[i] == 0) ready.push(i);
    }

    order_.clear();
    order_.reserve(n);
    while (!ready.empty()) {
        const int i = ready.front();
        ready.pop();
        order_.push_back(i);
        for (int d : dependents[i]) {
            if (--indegree[d] == 0) ready.push(d);
        }
    }

    if (static_cast<int>(order_.size()) != n) {
        throw std::runtime_error("pipeline graph contains a cycle");
    }

    // The source: a 16-bit mosaic sample per pixel unless the graph said
    // otherwise — a linear DNG's graph reads demosaiced RGBA half data.
    source_ = gpu::Texture::create(device_, width_, height_, sourceFormat_);

    aux_.clear();
    aux_.reserve(auxSpecs_.size());
    for (const auto& spec : auxSpecs_) {
        aux_.push_back(gpu::Texture::create(device_, spec.width, spec.height, spec.format));
    }

    libraries_.clear();
    kernels_.clear();
    outputs_.clear();
    ownedOutputs_.clear();
    libraries_.reserve(n);
    kernels_.reserve(n);
    outputs_.reserve(n);
    ownedOutputs_.reserve(n);
    // ⚠ One `MTLLibrary` per distinct metallib, not one per node. The develop
    // graph is 149 nodes over **48** distinct kernels, so loading per node
    // created ~101 redundant libraries and held them for the graph's lifetime —
    // doubled, because a photograph builds a full graph and a preview graph.
    // Not a leak (they were released with the graph) but resident waste, and
    // each load also costs a file read and ~1.6 KB of autoreleased temporaries.
    //
    // ⚠ Keyed by kernel name because that *is* the file's basename, one line
    // below. Keying by anything else would let two nodes naming one file get
    // two libraries again, quietly.
    std::unordered_map<std::string, gpu::Library*> byKernel;

    for (const auto& node : nodes_) {
        gpu::Library* lib = nullptr;
        if (auto it = byKernel.find(node.kernel); it != byKernel.end()) {
            lib = it->second;
        } else {
            auto owned = gpu::Library::createFromFile(
                device_, shaderDir_ + "/" + node.kernel + ".metallib");
            lib = owned.get();
            byKernel.emplace(node.kernel, lib);
            libraries_.push_back(std::move(owned));
        }
        auto kernel = gpu::Kernel::create(device_, *lib, node.kernel);

        // ⚠ **The shader and the code that feeds it must agree, and Metal will
        // not say so if they do not.** An unbound texture slot is nil, not an
        // error: reads give zero and writes go nowhere, so a kernel one slot
        // short of its shader runs to completion and produces nothing. Because
        // metallibs are loaded from disk by path, right here, a long-running
        // process picks up shaders rebuilt underneath it — which is exactly how
        // every mask silently covered zero for an hour while the suite stayed
        // green. Cheap to check once per node at compile; impossible to see
        // afterwards.
        const std::size_t binding = node.inputs.size() + node.aux.size() + 1;
        if (binding < kernel->textureSlotsUsed()) {
            throw std::runtime_error(
                "kernel '" + node.kernel + "' (node '" + node.name + "') uses " +
                std::to_string(kernel->textureSlotsUsed()) +
                " texture slots but the graph binds " + std::to_string(binding) +
                " — the shader and the binary disagree; rebuild both");
        }

        kernels_.push_back(std::move(kernel));

        // Allocated lazily, on first use (`ensure()` / `computeOne()`), not
        // here. Eagerly creating one texture per node — 205 of them, ~14 GiB
        // — is the defect decision #219 fixes: every node paid for a texture
        // whether or not it ever coincided with the peak.
        ownedOutputs_.push_back(nullptr);
        outputs_.push_back(nullptr);
    }

    dirty_.assign(n, true);

    // Cached once, here, because the graph's wiring never changes after this
    // point: `render()` acts on "who reads me last" every call, so it cannot
    // afford to re-derive it the way `peakLiveBytes()` — a report, not a
    // schedule — still does from `computeLastUseSteps()` on demand.
    const auto lastUse = computeLastUseSteps();
    lastReader_.assign(static_cast<std::size_t>(n), 0);
    for (std::size_t i = 0; i < lastUse.size(); ++i) {
        lastReader_[i] = order_[lastUse[i]];
    }
    everFreed_.assign(static_cast<std::size_t>(n), false);

    compiled_ = true;
}

void Pipeline::setParams(int nodeId, const void* data, std::size_t bytes) {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) {
        throw std::runtime_error("setParams: bad node id");
    }
    auto& params = nodes_[nodeId].params;
    params.resize(bytes);
    std::memcpy(params.data(), data, bytes);
    markDownstreamDirty(nodeId);
}

void Pipeline::setEnabled(int nodeId, bool enabled) {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) return;
    if (nodes_[nodeId].enabled == enabled) return;
    nodes_[nodeId].enabled = enabled;
    markDownstreamDirty(nodeId);
}

void Pipeline::setNodeFormat(int nodeId, gpu::PixelFormat format) {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) return;
    if (nodes_[nodeId].format == format) return;

    nodes_[nodeId].format = format;
    if (!compiled_) return;   // compile() will honour it

    const auto& node = nodes_[nodeId];
    const std::uint32_t w = node.outWidth  ? node.outWidth  : width_;
    const std::uint32_t h = node.outHeight ? node.outHeight : height_;
    // ⚠ Replace the owner first, then re-point. The other order leaves
    // `outputs_[nodeId]` dangling for the length of one statement, which is
    // exactly long enough for a future reader to be added between them.
    ownedOutputs_[static_cast<std::size_t>(nodeId)] =
        gpu::Texture::create(device_, w, h, format);
    outputs_[static_cast<std::size_t>(nodeId)] =
        ownedOutputs_[static_cast<std::size_t>(nodeId)].get();

    // The old texture is gone, so whatever was cached in it is gone with it.
    dirty_[nodeId] = true;
    markDownstreamDirty(nodeId);
}

gpu::PixelFormat Pipeline::nodeFormat(int nodeId) const {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) {
        throw std::runtime_error("nodeFormat: bad node id");
    }
    return nodes_[nodeId].format;
}

int Pipeline::resolve(int nodeId) const {
    // Walk back past disabled nodes to whoever last produced real pixels.
    int id = nodeId;
    int guard = 0;
    while (id >= 0 && id < static_cast<int>(nodes_.size()) &&
           !nodes_[id].enabled && ++guard < 64) {
        if (nodes_[id].inputs.empty()) break;
        id = nodes_[id].inputs.front();
    }
    return id;
}

void Pipeline::markDownstreamDirty(int nodeId) {
    // Walk forward in topological order: once a node is dirty, anything that
    // consumes it is dirty too. Single pass, because order_ guarantees
    // producers are visited before consumers.
    dirty_[nodeId] = true;
    for (int id : order_) {
        if (dirty_[id]) continue;
        for (int in : nodes_[id].inputs) {
            if (in != kSource && dirty_[in]) {
                dirty_[id] = true;
                break;
            }
        }
    }
}

int Pipeline::addAuxTexture(std::uint32_t width, std::uint32_t height,
                            gpu::PixelFormat format) {
    if (compiled_) throw std::runtime_error("cannot add aux textures after compile()");
    auxSpecs_.push_back({width, height, format});
    return static_cast<int>(auxSpecs_.size()) - 1;
}

void Pipeline::updateAux(int auxId, const void* data, std::size_t bytesPerRow) {
    if (auxId < 0 || auxId >= static_cast<int>(aux_.size())) {
        throw std::runtime_error("updateAux: bad aux id");
    }
    aux_[auxId]->upload(data, bytesPerRow);

    for (int id : order_) {
        const auto& a = nodes_[id].aux;
        if (std::find(a.begin(), a.end(), auxId) != a.end()) markDownstreamDirty(id);
    }
}

void Pipeline::resizeAux(int auxId, std::uint32_t width, std::uint32_t height) {
    if (auxId < 0 || auxId >= static_cast<int>(aux_.size())) {
        throw std::runtime_error("resizeAux: bad aux id");
    }
    auto& spec = auxSpecs_[static_cast<std::size_t>(auxId)];
    if (spec.width == width && spec.height == height) return;
    spec.width  = width;
    spec.height = height;
    aux_[static_cast<std::size_t>(auxId)] =
        gpu::Texture::create(device_, width, height, spec.format);
}

std::uint32_t Pipeline::auxWidth(int auxId) const {
    if (auxId < 0 || auxId >= static_cast<int>(auxSpecs_.size())) {
        throw std::runtime_error("auxWidth: bad aux id");
    }
    return auxSpecs_[static_cast<std::size_t>(auxId)].width;
}

bool Pipeline::nodeDirty(int nodeId) const {
    if (nodeId < 0 || nodeId >= static_cast<int>(dirty_.size())) {
        throw std::runtime_error("nodeDirty: bad node id");
    }
    return dirty_[static_cast<std::size_t>(nodeId)] &&
           nodes_[static_cast<std::size_t>(nodeId)].enabled;
}

void Pipeline::setSource(const void* samples, std::size_t bytesPerRow) {
    if (!compiled_) throw std::runtime_error("setSource before compile()");
    source_->upload(samples, bytesPerRow);
    std::fill(dirty_.begin(), dirty_.end(), true);
}

std::vector<std::size_t> Pipeline::computeLastUseSteps() const {
    // Last reader of each node's output, in execution order. A node nobody
    // reads is still written, so it is live for exactly its own step. Shared
    // by `render()`'s pool (`compile()` caches this once as `lastReader_`)
    // and `peakLiveBytes()`'s cost estimate — one analysis, not two that
    // could quietly drift apart.
    std::vector<std::size_t> lastUse(nodes_.size(), 0);
    for (std::size_t step = 0; step < order_.size(); ++step) {
        const int id = order_[step];
        if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) continue;
        lastUse[static_cast<std::size_t>(id)] =
            std::max(lastUse[static_cast<std::size_t>(id)], step);
        for (int in : nodes_[static_cast<std::size_t>(id)].inputs) {
            if (in >= 0 && static_cast<std::size_t>(in) < nodes_.size()) {
                lastUse[static_cast<std::size_t>(in)] = step;
            }
        }
    }
    return lastUse;
}

std::size_t Pipeline::nodeBytes(int id) const noexcept {
    const Node& node = nodes_[static_cast<std::size_t>(id)];
    const std::uint32_t w = node.outWidth  ? node.outWidth  : width_;
    const std::uint32_t h = node.outHeight ? node.outHeight : height_;
    return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
           gpu::bytesPerPixel(node.format);
}

bool Pipeline::isPinnedOrTerminal(int n) const noexcept {
    if (!order_.empty() && n == order_.back()) return true;
    for (int id : pinned_) {
        if (id == n) return true;
    }
    return false;
}

void Pipeline::ensure(int id, gpu::CommandBuffer* batch) {
    if (id < 0 || static_cast<std::size_t>(id) >= outputs_.size()) return;
    if (outputs_[static_cast<std::size_t>(id)]) return;   // already there

    const Node& node = nodes_[static_cast<std::size_t>(id)];
    if (!node.enabled) return;   // never dispatched; resolve() steers past it

    for (int in : node.inputs) {
        if (in != kSource) ensure(resolve(in), batch);
    }
    computeOne(id, batch);
}

void Pipeline::computeOne(int id, gpu::CommandBuffer* batch) {
    const std::size_t idx = static_cast<std::size_t>(id);
    const Node& node = nodes_[idx];

    // Whatever this node held will never be read again: every consumer of a
    // node about to change is dirty too (`markDownstreamDirty`), so nothing
    // downstream is waiting on the value this write is about to replace.
    if (ownedOutputs_[idx]) pool_.release(std::move(ownedOutputs_[idx]));

    // Dispatch over the node's own output, which may differ from the graph's
    // working size once rotation or crop is in play.
    const std::uint32_t w = node.outWidth  ? node.outWidth  : width_;
    const std::uint32_t h = node.outHeight ? node.outHeight : height_;
    ownedOutputs_[idx] = pool_.acquire(w, h, node.format);
    outputs_[idx] = ownedOutputs_[idx].get();

    std::vector<const gpu::Texture*> textures;
    textures.reserve(node.inputs.size() + node.aux.size() + 1);
    for (int in : node.inputs) {
        const int src = (in == kSource) ? kSource : resolve(in);
        textures.push_back(src == kSource ? source_.get()
                                          : outputs_[static_cast<std::size_t>(src)]);
    }
    for (int a : node.aux) {
        textures.push_back(aux_[static_cast<std::size_t>(a)].get());
    }
    textures.push_back(outputs_[idx]);

    lastRun_[idx].executed = true;

    if (batch) {
        batch->dispatch(*kernels_[idx], textures,
                        node.params.empty() ? nullptr : node.params.data(),
                        node.params.size(), w, h);
    } else {
        // Profiling, or a recovery dispatch outside a batched render: its own
        // command buffer, so its GPU time is attributable to this node alone.
        gpu::CommandBuffer one(device_);
        one.dispatch(*kernels_[idx], textures,
                     node.params.empty() ? nullptr : node.params.data(),
                     node.params.size(), w, h);
        one.commitAndWait();
        lastRun_[idx].ms = one.gpuMilliseconds();
    }

    releaseIfDone(id);
}

void Pipeline::releaseIfDone(int reader) {
    const Node& node = nodes_[static_cast<std::size_t>(reader)];
    for (int in : node.inputs) {
        if (in == kSource) continue;
        maybeFree(resolve(in), reader);
    }
    // A node with no reader at all is live for exactly the step it was
    // written — freed here, immediately after that write.
    maybeFree(reader, reader);
}

void Pipeline::maybeFree(int n, int reader) {
    if (n < 0 || static_cast<std::size_t>(n) >= nodes_.size()) return;
    if (lastReader_[static_cast<std::size_t>(n)] != reader) return;
    if (everFreed_[static_cast<std::size_t>(n)]) return;   // once, ever — see the header
    if (isPinnedOrTerminal(n)) return;

    auto& owned = ownedOutputs_[static_cast<std::size_t>(n)];
    if (!owned) return;
    pool_.release(std::move(owned));
    outputs_[static_cast<std::size_t>(n)] = nullptr;
    everFreed_[static_cast<std::size_t>(n)] = true;
}

double Pipeline::render() {
    if (!compiled_) throw std::runtime_error("render before compile()");

    // Indexed by node id, not pushed in topological order — nothing reads
    // `lastRun()` by position, only by name or by counting `executed`, and
    // this lets `computeOne` update an entry no matter which path (the main
    // loop below, or a recovery inside `ensure()`) produced it.
    lastRun_.assign(nodes_.size(), NodeTiming{});
    for (std::size_t i = 0; i < nodes_.size(); ++i) lastRun_[i].name = nodes_[i].name;

    double total = 0.0;
    std::unique_ptr<gpu::CommandBuffer> batch;
    if (!profiling_) batch = std::make_unique<gpu::CommandBuffer>(device_);

    for (int id : order_) {
        const Node& node = nodes_[static_cast<std::size_t>(id)];
        if (!(dirty_[static_cast<std::size_t>(id)] && node.enabled)) continue;

        // Whatever this dispatch is about to read must exist first — pulling
        // back anything the pool already reclaimed. A no-op for every input
        // that is still resident, which is the ordinary case once a graph is
        // warm. See `ensure()`'s comment for why recomputing one is safe.
        for (int in : node.inputs) {
            if (in != kSource) ensure(resolve(in), batch.get());
        }
        computeOne(id, batch.get());
    }

    if (batch) {
        batch->commitAndWait();
        total = batch->gpuMilliseconds();
    } else {
        // Profiling: every dispatch above already timed itself.
        for (const auto& t : lastRun_) total += t.ms;
    }
    std::fill(dirty_.begin(), dirty_.end(), false);
    return total;
}

const gpu::Texture& Pipeline::output() const {
    if (order_.empty()) throw std::runtime_error("pipeline not compiled");
    // ⚠ Allocation is lazy (decision #219): before the first render() this is
    // null rather than a garbage-filled placeholder. A clear throw here beats
    // a null dereference for whoever calls this too early.
    if (!outputs_[static_cast<std::size_t>(order_.back())]) {
        throw std::runtime_error("output(): terminal node has not been rendered yet");
    }
    return *outputs_[order_.back()];
}

const gpu::Texture& Pipeline::nodeOutput(int nodeId) const {
    if (nodeId < 0 || nodeId >= static_cast<int>(outputs_.size())) {
        throw std::runtime_error("nodeOutput: bad node id");
    }

    // ⚠ **Steered past disabled nodes, exactly as an in-graph consumer is.**
    // A disabled node is never dispatched, so `resolve` walking back to the
    // last live producer is what its readers downstream already see — and an
    // external reader asking for `highlights` with recovery off wants the same
    // answer they get: the pixels that reached the next stage.
    //
    // Before the pool this looked like it worked. Every node was allocated
    // eagerly at `compile()`, so a disabled node handed back a texture that
    // had simply never been written — `highlightStages()` returned uninitialized
    // GPU memory to the highlight-fill census and the bench whenever
    // `highlightRecovery` was zero, which is the default. Lazy allocation
    // turned that silent garbage into a throw, which is how it was found.
    const int live = resolve(nodeId);
    if (live == kSource) return sourceTexture();
    if (live < 0 || live >= static_cast<int>(outputs_.size())) {
        throw std::runtime_error("nodeOutput: node '" + nodes_[static_cast<std::size_t>(nodeId)].name +
                                 "' resolves to no live producer");
    }
    if (!outputs_[static_cast<std::size_t>(live)]) {
        throw std::runtime_error("nodeOutput: node '" + nodes_[static_cast<std::size_t>(live)].name +
                                 "' has not been rendered yet");
    }
    return *outputs_[live];
}

const gpu::Texture& Pipeline::sourceTexture() const {
    if (!source_) throw std::runtime_error("pipeline not compiled");
    return *source_;
}

std::size_t Pipeline::intermediateBytes() const noexcept {
    std::size_t total = source_ ? source_->sizeBytes() : 0;
    for (const auto& t : ownedOutputs_) { if (t) total += t->sizeBytes(); }
    return total;
}

void Pipeline::setPinned(std::vector<int> nodeIds) {
    pinned_ = std::move(nodeIds);
}

std::size_t Pipeline::peakLiveBytes() const noexcept {
    // Last reader of each node's output, in execution order — the same walk
    // `compile()` caches as `lastReader_` for `render()`'s own use.
    const std::vector<std::size_t> lastUse = computeLastUseSteps();

    // ⚠ **Pinned outputs are live for the whole render and never come back.**
    // The last node is pinned structurally — it is the picture — and so is
    // anything `setPinned` names, because `nodeOutput` will hand it out after
    // the graph has finished and a liveness walk cannot see that reader.
    // Decision #156.
    std::vector<bool> pinned(nodes_.size(), false);
    for (int id : pinned_) {
        if (id >= 0 && static_cast<std::size_t>(id) < pinned.size()) {
            pinned[static_cast<std::size_t>(id)] = true;
        }
    }
    if (!order_.empty()) {
        const int last = order_.back();
        if (last >= 0 && static_cast<std::size_t>(last) < pinned.size()) {
            pinned[static_cast<std::size_t>(last)] = true;
        }
    }

    // The source is pinned too, and is already counted below as the starting
    // figure that is never subtracted.
    std::size_t live = source_ ? source_->sizeBytes() : 0;
    std::size_t peak = live;
    // ⚠ **Only what has actually been written can be freed**, which is not the
    // pedantry it looks like: a node absent from `order_` — every disabled one
    // — has `lastUse` 0, so an unguarded subtraction runs at step 0 against a
    // texture that was never added and wraps `size_t` to something enormous.
    // The first version of this did exactly that. Sized from `nodeBytes(n)`
    // rather than `outputs_[n]->sizeBytes()` — this is a hypothetical ("if a
    // full render ran right now"), and allocation is lazy, so the texture
    // this is costing may not exist yet.
    std::vector<bool> written(nodes_.size(), false);
    for (std::size_t step = 0; step < order_.size(); ++step) {
        const int id = order_[step];
        if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) continue;
        live += nodeBytes(id);
        written[static_cast<std::size_t>(id)] = true;
        peak = std::max(peak, live);
        // Everything whose last reader was this step can go back to the pool.
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            if (written[n] && !pinned[n] && lastUse[n] == step &&
                static_cast<int>(n) != id) {
                live -= nodeBytes(static_cast<int>(n));
                written[n] = false;
            }
        }
    }
    return peak;
}

}  // namespace orion::pipe

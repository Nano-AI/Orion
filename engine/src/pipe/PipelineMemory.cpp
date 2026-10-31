/*  The memory rule: whether a render may cache, or must be frugal (#406).
 *
 *  One rule, decided once per render in `Pipeline::decideMemory`:
 *
 *      budget = min(2.5e9 bytes less what the rest of the device holds,
 *                   what the device can still give + what this graph holds)
 *      plan   = this render's peak under the caching policy, from what is held now
 *      plan <= budget  ->  cache, exactly as before #406 (slider latency, #161)
 *      plan >  budget  ->  frugal
 *
 *  Caching keeps every output a render rebuilds after its first free (#219);
 *  that is what makes a tick fast, and what made one white-balance move hold
 *  2.8 GB at 42 MP. Frugal frees the effects' levels as read, frees every
 *  output at its last reader except a keep set, hands back cached outputs the
 *  render does not read before it starts and the rest when it ends, and lets
 *  the pool give idle memory back before it allocates. The keep set is chosen
 *  dearest-to-rebuild per byte first, while the frugal plan plus everything
 *  kept stays under the budget, so the next tick rebuilds the cheap part.
 *
 *  The plan is a walk of the render on shapes and bytes alone: the same early
 *  hand-back, the same pool (exact-shape reuse from what is idle now), the
 *  same last readers, the same rebuilds of freed inputs. "What the device can
 *  still give" is the driver's own count where there is one (Metal's
 *  recommended working set less what it holds; on Linux NVML or amdgpu's sysfs,
 *  VramFree.cpp), device-wide, so a second engine (side by side, #400) and
 *  other programs count; elsewhere it is unknown until an allocation is
 *  refused, and then that refusal's ceiling (`Device::noteOutOfMemory`).
 */
#include "pipe/Pipeline.h"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>

namespace orion::pipe {

std::vector<bool> Pipeline::readThisRender(const std::vector<bool>& resident) const {
    std::vector<bool> read(nodes_.size(), false);
    // An input that is not resident is rebuilt (`ensure`), which reads its own
    // inputs in turn.
    std::function<void(int)> need = [&](int id) {
        if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) return;
        const std::size_t i = static_cast<std::size_t>(id);
        if (read[i] || !nodes_[i].enabled) return;
        read[i] = true;
        if (resident[i] && !dirty_[i]) return;
        for (int in : nodes_[i].inputs)
            if (in != kSource) need(resolve(in));
    };
    for (int id : order_) {
        const std::size_t i = static_cast<std::size_t>(id);
        if (!(dirty_[i] && nodes_[i].enabled)) continue;
        for (int in : nodes_[i].inputs)
            if (in != kSource) need(resolve(in));
    }
    return read;
}

std::size_t Pipeline::planPeak(bool frugal, const std::vector<bool>& keep) const {
    using Shape = gpu::TexturePool::Shape;
    const std::size_t n = nodes_.size();
    std::vector<bool> resident(n), freed(everFreed_.begin(), everFreed_.end());
    for (std::size_t i = 0; i < n; ++i) resident[i] = ownedOutputs_[i] != nullptr;
    std::map<Shape, std::size_t> idleShapes = pool_.idleCounts();
    std::size_t idle = pool_.idleBytes();
    std::size_t held = allocatedBytes();
    std::size_t peak = held;

    const auto shapeOf = [&](std::size_t i) {
        const Node& node = nodes_[i];
        return Shape{node.outWidth ? node.outWidth : width_,
                     node.outHeight ? node.outHeight : height_, node.format};
    };
    const auto release = [&](std::size_t i) {
        if (!resident[i]) return;
        resident[i] = false;
        ++idleShapes[shapeOf(i)];
        idle += nodeBytes(int(i));
    };
    const auto acquire = [&](std::size_t i) {
        auto it = idleShapes.find(shapeOf(i));
        if (it != idleShapes.end() && it->second > 0) {
            --it->second;
            idle -= nodeBytes(int(i));
            return;
        }
        if (frugal && idle >= givePlane()) {   // computeOne's give-back
            held -= idle;
            idle = 0;
            idleShapes.clear();
        }
        held += nodeBytes(int(i));
        peak = std::max(peak, held);
    };
    const auto maybeFree = [&](int m, int reader) {
        if (m < 0 || static_cast<std::size_t>(m) >= n) return;
        const std::size_t i = static_cast<std::size_t>(m);
        if (lastReader_[i] != reader) return;
        if (frugal ? keep[i] : freed[i]) return;
        if (isPinnedOrTerminal(m)) return;
        if (!resident[i]) return;
        release(i);
        freed[i] = true;
    };
    const auto compute = [&](int id) {
        const std::size_t i = static_cast<std::size_t>(id);
        release(i);
        acquire(i);
        resident[i] = true;
        for (int in : nodes_[i].inputs)
            if (in != kSource) maybeFree(resolve(in), id);
        maybeFree(id, id);
    };
    std::function<void(int)> ensure = [&](int id) {
        if (id < 0 || static_cast<std::size_t>(id) >= n) return;
        const std::size_t i = static_cast<std::size_t>(id);
        if (resident[i] || !nodes_[i].enabled) return;
        for (int in : nodes_[i].inputs)
            if (in != kSource) ensure(resolve(in));
        compute(id);
    };

    // `runDirty`, step for step.
    for (int id : order_) {
        const std::size_t i = static_cast<std::size_t>(id);
        if (dirty_[i] && nodes_[i].enabled &&
            !(frugal ? id == order_.back() : isPinnedOrTerminal(id)))
            release(i);
    }
    if (frugal) {
        const auto read = readThisRender(resident);
        for (std::size_t i = 0; i < n; ++i)
            if (resident[i] && !read[i] && !keep[i] && !isPinnedOrTerminal(int(i))) release(i);
        held -= idle;   // and the pool gives everything idle back (`runDirty`)
        idle = 0;
        idleShapes.clear();
    }
    for (int id : order_) {
        const std::size_t i = static_cast<std::size_t>(id);
        if (!(dirty_[i] && nodes_[i].enabled)) continue;
        for (int in : nodes_[i].inputs)
            if (in != kSource) ensure(resolve(in));
        compute(id);
    }
    return peak;
}

void Pipeline::decideMemory() {
    const std::size_t n = nodes_.size();
    keep_.assign(n, false);
    frugal_ = false;
    plan_ = {};
    if (releaseEveryRender_) return;   // lean already frees everything (#312)

    // The cap is on the whole device, as the gate measures it: what the rest
    // of the device holds (other graphs, staging, the allocator's rounding)
    // comes off it, and a fiftieth more for the rounding still to come.
    const std::uint64_t held = allocatedBytes();
    const auto deviceHeld = [&] {
        return device_.allocatedBytes() ? device_.allocatedBytes() : device_.textureBytes();
    };
    std::uint64_t allocated = deviceHeld();
    // ⚠ Dawn frees a dropped texture only after a later submit (#392): a pool
    // trimmed at the last render's end still counts. Read as someone else's,
    // it once planned a budget of 659 MiB and a needless frugal render. So
    // when the rest of the device holds a plane or more, flush that first.
    if (allocated > held + givePlane()) {
        device_.reclaim();
        allocated = deviceHeld();
    }
    const std::uint64_t others = allocated > held ? allocated - held : 0;
    const std::uint64_t cap = budgetCap_ - std::min(budgetCap_, others + budgetCap_ / 50);
    // The driver's free count is device-wide: the other pane's engine (#400),
    // the desktop and every other program are already off it. A sixteenth of
    // it stays unplanned, for the driver and for whatever allocates next.
    const std::uint64_t free = device_.freeBytes();
    plan_.budget = free == gpu::Device::kUnknown ? cap : std::min(cap, free - free / 16 + held);
    plan_.cached = planPeak(false, keep_);
    if (plan_.cached <= plan_.budget) return;

    frugal_ = plan_.frugalMode = true;
    const std::size_t bare = planPeak(true, keep_);

    // What rebuilding an output costs: its own pass, and the passes of the
    // transient levels under it, which are never cached.
    std::vector<double> cost(n, -1.0);
    std::function<double(int)> rebuild = [&](int id) -> double {
        const std::size_t i = static_cast<std::size_t>(id);
        if (cost[i] >= 0.0) return cost[i];
        double c = double(nodes_[i].costWeight) * double(nodeBytes(id)) /
                   double(std::max<std::size_t>(gpu::bytesPerPixel(nodes_[i].format), 1));
        for (int in : nodes_[i].inputs) {
            const int from = in == kSource ? kSource : resolve(in);
            if (from >= 0 && nodes_[static_cast<std::size_t>(from)].transient) c += rebuild(from);
        }
        return cost[i] = c;
    };
    // Whatever this render will hold: resident now, dirty, or rebuilt for a
    // reader (a freed output a tick reads again must be able to come back
    // into the keep set).
    std::vector<bool> resident(n);
    for (std::size_t i = 0; i < n; ++i) resident[i] = ownedOutputs_[i] != nullptr;
    const auto read = readThisRender(resident);
    std::vector<int> candidates;
    for (std::size_t i = 0; i < n; ++i) {
        const int id = int(i);
        if (!nodes_[i].enabled || nodes_[i].transient || isPinnedOrTerminal(id)) continue;
        if (!(resident[i] || dirty_[i] || read[i])) continue;
        candidates.push_back(id);
    }
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        return rebuild(a) / double(nodeBytes(a)) > rebuild(b) / double(nodeBytes(b));
    });
    // Kept outputs can only add to the frugal plan, never more than their own
    // bytes: a bound, so the keep set never takes the render over budget.
    std::size_t room = plan_.budget > bare ? plan_.budget - bare : 0;
    std::vector<int> kept;
    for (int id : candidates) {
        const std::size_t bytes = nodeBytes(id);
        if (bytes > room) continue;
        keep_[static_cast<std::size_t>(id)] = true;
        kept.push_back(id);
        room -= bytes;
    }
    // The bound is not exact (a kept texture is not idle, so it can hold off
    // a give-back), so the plan is checked, and the cheapest kept output goes
    // first until it fits.
    plan_.frugal = kept.empty() ? bare : planPeak(true, keep_);
    while (!kept.empty() && plan_.frugal > plan_.budget) {
        keep_[static_cast<std::size_t>(kept.back())] = false;
        kept.pop_back();
        plan_.frugal = kept.empty() ? bare : planPeak(true, keep_);
    }
    plan_.kept = kept.size();
}

}  // namespace orion::pipe

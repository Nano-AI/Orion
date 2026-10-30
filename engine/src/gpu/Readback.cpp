// gpu::Readback (#386): three staging buffers taking turns, the newest landed
// copy handed out and older ones dropped. The buffers are the backend's
// (Backend.h's Readback::Slot); a backend with none (Metal) gets a CPU slot
// that copies at `start`, so the ring above it is the same everywhere.

#include "gpu/Backend.h"

#include <algorithm>
#include <cstring>

namespace orion::gpu {

namespace {

/// Synchronous: `start` reads the texture back there and then.
struct CpuSlot final : Readback::Slot {
    std::vector<std::uint8_t> bytes;   // grows, never shrinks: no allocation per frame
    std::size_t row = 0;
    std::uint32_t height = 0;

    void start(const Texture& t, std::uint32_t width, std::uint32_t h) override {
        row = std::size_t(width) * bytesPerPixel(t.format());
        height = h;
        if (bytes.size() < row * h) bytes.resize(row * h);
        t.download(bytes.data(), row, width, h);
    }
    bool landed(bool) override { return true; }
    void copy(void* dst, std::size_t bytesPerRow) const override {
        for (std::uint32_t y = 0; y < height; ++y)
            std::memcpy(static_cast<std::uint8_t*>(dst) + y * bytesPerRow, bytes.data() + y * row, row);
    }
    void release() override {}
};

}  // namespace

struct Readback::Entry {
    enum class State { Free, Pending, Landed };
    std::unique_ptr<Slot> slot;
    State         state  = State::Free;
    std::uint64_t ticket = 0;
    std::uint32_t width = 0, height = 0;

    void free() {
        if (state != State::Free) slot->release();
        state = State::Free;
    }
};

Readback::Readback(Device& device) {
    for (int i = 0; i < 3; ++i) {
        auto e = std::make_unique<Entry>();
        e->slot = device.impl().staging();
        if (!e->slot) e->slot = std::make_unique<CpuSlot>();
        entries_.push_back(std::move(e));
    }
}

Readback::~Readback() {
    for (auto& e : entries_) {
        try { e->free(); } catch (...) {}   // a lost device: nothing left to wait for
    }
}

std::uint64_t Readback::begin(const Texture& t, std::uint32_t width, std::uint32_t height) {
    // A free buffer; else the oldest landed one nobody took (this copy supersedes
    // it); else the oldest in flight, waited for.
    auto oldest = [&](Entry::State s) {
        Entry* found = nullptr;
        for (auto& e : entries_)
            if (e->state == s && (!found || e->ticket < found->ticket)) found = e.get();
        return found;
    };
    Entry* e = oldest(Entry::State::Free);
    if (!e) e = oldest(Entry::State::Landed);
    if (!e) e = oldest(Entry::State::Pending);
    if (e == held_) held_ = nullptr;
    e->free();
    e->slot->start(t, width, height);
    e->state  = Entry::State::Pending;
    e->ticket = ++tickets_;
    e->width  = width;
    e->height = height;
    return e->ticket;
}

std::uint64_t Readback::newest(bool wait, std::uint32_t& width, std::uint32_t& height) {
    Entry* last = nullptr;
    for (auto& e : entries_)
        if (e->state == Entry::State::Pending && (!last || e->ticket > last->ticket)) last = e.get();
    if (wait && last && last->slot->landed(true)) last->state = Entry::State::Landed;
    for (auto& e : entries_)
        if (e->state == Entry::State::Pending && e->slot->landed(false)) e->state = Entry::State::Landed;

    held_ = nullptr;
    for (auto& e : entries_)
        if (e->state == Entry::State::Landed && e->ticket > taken_ && (!held_ || e->ticket > held_->ticket))
            held_ = e.get();
    for (auto& e : entries_)   // superseded: nobody will ask for these
        if (e->state == Entry::State::Landed && e.get() != held_) e->free();
    width  = held_ ? held_->width : 0;
    height = held_ ? held_->height : 0;
    return held_ ? held_->ticket : 0;
}

std::uint64_t Readback::held(std::uint32_t& width, std::uint32_t& height) const noexcept {
    width  = held_ ? held_->width : 0;
    height = held_ ? held_->height : 0;
    return held_ ? held_->ticket : 0;
}

bool Readback::take(void* dst, std::size_t bytesPerRow) {
    if (!held_) return false;
    held_->slot->copy(dst, bytesPerRow);
    taken_ = held_->ticket;
    held_->free();
    held_ = nullptr;
    return true;
}

}  // namespace orion::gpu

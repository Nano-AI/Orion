// The shim's state, shared by shim.cpp (every platform) and shim_mac.mm (#376).
#pragma once
#include <memory>
#include <unordered_map>
#include <vector>
#include "orion/orion.h"

// A brush stroke as the engine was given it: interleaved x, y and one erase
// flag per dab. The buffers are shared: a stroke that only grew (every pointer
// event of a drag) points into its predecessor's buffers with a larger count,
// so keeping every revision costs the dabs once, not once per event.
struct Stroke {
    std::shared_ptr<std::vector<float>> xy, erase;
    int dabs = 0;
};

struct Shim {
    OrionEngine* engine = nullptr;
    OrionAdjustments adj{};
    // What each mask row's brush holds in the engine, and its revision.
    // Revisions are global and never reused (Engine+Brush.swift's
    // brushRevisions), and every stroke is kept by its revision, so undo can
    // write an old revision back through shim_set and get its dabs with it.
    Stroke held[ORION_MAX_MASK_COMPONENTS];
    unsigned revisions[ORION_MAX_MASK_COMPONENTS]{};
    std::unordered_map<unsigned, Stroke> kept;
    unsigned last_revision = 0;
    // The overlay, applied for one render at a time (shim_render).
    int overlay = 0, overlay_layer = 0;
    // present.mm's last copy out of this engine's textures, which may still be
    // waiting on the GPU for Avalonia: the engine must not render over them first.
    // An id<MTLCommandBuffer>, retained (shim_mac.mm); always empty elsewhere.
    std::shared_ptr<void> reader;
};

// Waits until present.mm has read this engine's picture (shim_mac.mm; a no-op
// where there is no present.mm).
void shim_settle(Shim* s);

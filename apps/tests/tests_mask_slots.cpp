/*  Eight component slots, end to end — decision #209 raised the cap from four.
 *
 *  Nothing else exercises a slot past index 3: before the raise the deepest
 *  fixture in the tree used two components, so the four new slots' plumbing —
 *  the shader's hand-branched texture bindings, the params arrays, the
 *  `develop:linear` input list — could be wrong in any way at all and every
 *  test would stay green. This is the fixture that puts a distinct region
 *  through each of the eight.
 */

#include "harness.h"

void testMaskEightSlotsGpu() {
    section("Eight mask slots (GPU)");

    namespace pipe = orion::pipe;

    static_assert(pipe::kMaxMaskComponents == 8,
                  "this fixture spells eight regions; re-derive it with the cap");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    // A flat frame, same as the layer-break fixture: every comparison is a
    // region against the unmasked base, and a gradient would put the answer in
    // the geometry instead.
    orion::raw::BayerImage img;
    img.width = 192;
    img.height = 96;
    img.samples.assign(std::size_t(192) * 96, 1200);
    img.filters = 0x94949494u;             // RGGB
    img.white = 4095;
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};

    std::unique_ptr<pipe::DevelopPipeline> dev;
    try {
        dev = std::make_unique<pipe::DevelopPipeline>(
            *device, std::string(ORION_SHADER_DIR), img);
    } catch (const std::exception& e) {
        report(false, "develop graph builds", e.what());
        return;
    }

    const std::uint32_t w = dev->outputWidth(), h = dev->outputHeight();
    std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
    const auto patch = [&](float fx, float fy) {
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        const int cx = int(fx * float(w)), cy = int(fy * float(h));
        double sum = 0.0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                sum += double(px[(std::size_t(cy + dy) * w + std::size_t(cx + dx))
                               * 4 + 1]);
        return sum / 25.0;
    };
    const auto centerX = [](int i) { return (float(i) + 0.5f) / 8.0f; };

    // Eight radials in a row, each its own layer, each layer darkened two
    // stops. Distinct regions, so a slot rendering another slot's coverage —
    // the exact failure a wrong texture binding produces — moves a region it
    // should not.
    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};
    adj.maskCount = 8;
    for (int i = 0; i < 8; ++i) {
        auto& c = adj.maskComponents[std::size_t(i)];
        c.kind = 2;
        c.startsLayer = (i > 0);
        c.center[0] = centerX(i);
        c.center[1] = 0.5f;
        c.radius[0] = c.radius[1] = 0.05f;
        c.feather = 0.3f;
        adj.layers[std::size_t(i)].exposureEv = -2.0f;
    }

    dev->apply(adj);
    dev->render();
    const double base = patch(0.5f, 0.06f);
    bool allDark = true;
    double worst = base;
    for (int i = 0; i < 8; ++i) {
        const double v = patch(centerX(i), 0.5f);
        if (v >= base * 0.8) { allDark = false; worst = v; }
    }
    report(allDark, "all eight layers grade their own region",
           "base " + std::to_string(base) + " worst " + std::to_string(worst));

    // The discriminating half: zero the eighth layer's grade and only the
    // eighth region may recover. A stale slot-7 binding — mask7 reading
    // mask3's texture, the params array one short — fails one of these two.
    adj.layers[7].exposureEv = 0.0f;
    dev->apply(adj);
    dev->render();
    const double eighth  = patch(centerX(7), 0.5f);
    const double seventh = patch(centerX(6), 0.5f);
    report(eighth > base * 0.95,
           "the eighth layer's grade is its own — zeroed, its region recovers",
           "base " + std::to_string(base) + " region " + std::to_string(eighth));
    report(seventh < base * 0.8,
           "and its neighbor keeps the grade it still carries",
           "base " + std::to_string(base) + " region " + std::to_string(seventh));
}

// Decision #313: the guided refine chain runs on the slots that end a layer
// and nowhere else. A layer's coverage is the last component of its run
// (`layerTable`), so a chain on any other slot filtered a fold nobody read.
//
// ⚠ No pixel reference for the layer-ending slots: their chains are the same
// nodes on the same inputs they always were, so their pixels are unchanged by
// construction. What can go wrong is the *set* - a chain left off where a
// layer ends (refine silently gone) or left on where none does (the waste) -
// so the set is what this asserts, and that refine still moves the picture.
void testMaskRefineLayerEndsGpu() {
    section("Mask refine runs only where a layer ends (GPU)");
    namespace pipe = orion::pipe;
    try {
        auto device = orion::gpu::Device::create();
        // A hard vertical edge, so the guided filter has something to snap to
        // and a refined mask cannot equal an unrefined one by accident.
        orion::raw::BayerImage img;
        img.width = 192;
        img.height = 96;
        img.samples.resize(std::size_t(192) * 96);
        for (std::size_t i = 0; i < img.samples.size(); ++i)
            img.samples[i] = (i % 192) < 96 ? 600 : 2400;
        img.filters = 0x94949494u;             // RGGB
        img.white = 4095;
        img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
        img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                        0.2126f, 0.7152f, 0.0722f,
                        0.0193f, 0.1192f, 0.9505f};
        pipe::DevelopPipeline dev(*device, std::string(ORION_SHADER_DIR), img);

        // Slots 0-2 one layer (1 and 2 continue it), slot 3 a second layer.
        pipe::Adjustments adj{};
        adj.wb = dev.asShotWhiteBalance();
        adj.maskCount = 4;
        for (int i = 0; i < 4; ++i) {
            auto& c = adj.maskComponents[std::size_t(i)];
            c.kind = 2;
            c.startsLayer = (i == 3);
            c.center[0] = 0.2f + 0.2f * float(i);
            c.center[1] = 0.5f;
            c.radius[0] = c.radius[1] = 0.15f;
            c.feather = 0.0f;
        }
        adj.layers[0].exposureEv = -1.5f;
        adj.layers[1].exposureEv = 1.0f;

        const auto pixels = [&] {
            const auto row = dev.outputWidth() * orion::gpu::bytesPerPixel(dev.output().format());
            std::vector<std::uint8_t> bytes(row * dev.outputHeight());
            dev.output().download(bytes.data(), row, dev.outputWidth(), dev.outputHeight());
            return bytes;
        };
        // White balance nudged on every render so every enabled node is dirty
        // and `lastRun` reports the enabled set, not only what one edit touched.
        int nudge = 0;
        const auto render = [&](pipe::Adjustments a) {
            a.wb.temperatureK += float(++nudge);
            dev.apply(a);
            dev.render();
        };
        const auto ran = [&](const std::string& suffix) {
            std::set<int> slots;
            for (const auto& node : dev.graph().lastRun()) {
                if (!node.executed) continue;
                for (int i = 0; i < pipe::kMaxMaskComponents; ++i)
                    if (node.name == "mask" + std::to_string(i) + ":" + suffix)
                        slots.insert(i);
            }
            return slots;
        };
        const auto show = [](const std::set<int>& s) {
            std::string out;
            for (int i : s) out += std::to_string(i) + " ";
            return out;
        };

        adj.maskRefine = 0.0f;
        render(adj);
        report(ran("refine").empty() && ran("guide prep").empty(),
               "strength zero runs no refine chain", show(ran("refine")));
        const auto unrefined = pixels();

        // The same nudge as the render above, so the two pictures differ by
        // the strength alone. Enabling a chain dirties it, so it still runs.
        adj.maskRefine = 0.5f;
        nudge = 0;
        render(adj);
        report(ran("refine") == std::set<int>{2, 3},
               "refine runs on the two layer ends and nowhere else",
               "ran " + show(ran("refine")));
        report(ran("guide prep") == std::set<int>{2, 3},
               "and so does its guide prep", "ran " + show(ran("guide prep")));
        report(pixels() != unrefined, "the refined picture still differs from strength zero");

        // Move the boundary without touching the strength: slot 2 starts a
        // layer, so slot 1 now ends one. The enable has to follow the set,
        // not only a flip of `refining`.
        adj.maskComponents[2].startsLayer = true;
        adj.layers[2].exposureEv = 0.5f;
        render(adj);
        report(ran("refine") == std::set<int>{1, 2, 3},
               "a moved layer boundary moves the refine set with it",
               "ran " + show(ran("refine")));
        report(ran("guide prep") == std::set<int>{1, 2, 3},
               "and its guide prep", "ran " + show(ran("guide prep")));

        // And back: slot 1 stops ending a layer, so its chain switches off.
        adj.maskComponents[2].startsLayer = false;
        render(adj);
        report(ran("refine") == std::set<int>{2, 3},
               "merging the layers again switches the inner chain off",
               "ran " + show(ran("refine")));
    } catch (const std::exception& e) {
        report(false, "mask refine layer-end fixture completes", e.what());
    }
}

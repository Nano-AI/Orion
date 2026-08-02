/*  orion-bench — where the time goes, and what a write costs.
 *
 *  Two measurements that gate nothing. They are printed because a total cannot
 *  be acted on: the first attempt at making clarity cheaper optimized the
 *  collapse kernels on a hunch and made it slower, and this is what that should
 *  have consulted.
 */
#include "bench.h"

#include "util/ImageWriter.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace bench {

void nodeProfiles(Bench& b) {
    auto& develop = b.develop;

    // ── Where a clarity drag actually goes ────────────────────────────
    //
    // Printed as a breakdown rather than a total because a total cannot be
    // acted on. The first attempt at making this cheaper optimized the
    // collapse kernels on a hunch and made it *slower*; this is what that
    // should have consulted.
    //
    // One command buffer per node, so these do not sum to the batched
    // figure above — they rank, they do not total.
    const auto profileDrag = [&](const char* label,
                                 void (*set)(orion::pipe::Adjustments&)) {
        std::printf("\n%s, node by node\n", label);
        orion::pipe::Adjustments base;
        base.wb = develop.asShotWhiteBalance();
        develop.apply(base);
        develop.render();

        orion::pipe::Adjustments moved = base;
        set(moved);
        develop.apply(moved);

        develop.graph().setProfiling(true);
        develop.render();
        develop.graph().setProfiling(false);

        std::vector<std::pair<double, std::string>> ran;
        double sum = 0.0;
        for (const auto& t : develop.graph().lastRun()) {
            if (!t.executed) continue;
            ran.emplace_back(t.ms, t.name);
            sum += t.ms;
        }
        std::sort(ran.begin(), ran.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        std::printf("  %zu nodes ran, %.2f ms serialized\n", ran.size(), sum);
        for (std::size_t i = 0; i < ran.size() && i < 8; ++i) {
            std::printf("    %-22s %6.2f ms  %4.1f%%\n", ran[i].second.c_str(),
                        ran[i].first, 100.0 * ran[i].first / std::max(sum, 1e-9));
        }
    };

    profileDrag("Clarity", [](orion::pipe::Adjustments& a) { a.clarity = 1.0f; });
    profileDrag("Dehaze",  [](orion::pipe::Adjustments& a) { a.dehaze  = 1.0f; });
}

void exportTiming(Bench& b) {
    auto& develop = b.develop;
    const std::string& path = b.path;
    const std::string& prefix = b.prefix;

    // ── Export ────────────────────────────────────────────────────────
    std::printf("\nExport\n");
    {
        orion::pipe::Adjustments base;
        base.wb = develop.asShotWhiteBalance();
        develop.apply(base);
        develop.render();

        const std::uint32_t ew = develop.outputWidth();
        const std::uint32_t eh = develop.outputHeight();
        const std::size_t rowBytes =
            static_cast<std::size_t>(ew) * 4 * sizeof(std::uint16_t);
        const auto pixels = output16(develop, ew, eh);

        // The same options the app builds, metadata and color space
        // included. The bench used to pass a bare struct, so it measured a
        // write the product never performs — and it would have reported
        // green on an export that carried no EXIF at all.
        using orion::util::ColorSpace;
        struct Case { const char* suffix; orion::util::ExportOptions opts; };
        // ⚠ Designated initializers, not positional. This list was
        // positional and broke the moment two fields were inserted into
        // `ExportOptions` — silently assigning a path to a bit depth, which
        // is the good outcome; the bad one is two fields of the same type
        // swapping places and the bench measuring something else while
        // still exiting 0.
        const Case cases[] = {
            // Metadata::All here on purpose: the point of the probe is
            // that a full block survives the encode, and the app's own
            // default strips location.
            {"-full.jpg", {.format = orion::util::ImageFormat::Jpeg,
                           .quality = 0.92f,
                           .space = ColorSpace::Srgb,
                           .metadataFrom = path,
                           .metadata = orion::util::Metadata::All,
                           .rating = 4}},
            {"-web.jpg",  {.format = orion::util::ImageFormat::Jpeg,
                           .quality = 0.85f,
                           .maxDimension = 2048,
                           .space = ColorSpace::DisplayP3,
                           // The web case is what the panel's defaults
                           // produce: eight bits, and sharpened because it
                           // has been resized.
                           .depth = orion::util::BitDepth::Eight,
                           .sharpen = orion::util::Sharpen::Screen,
                           .metadataFrom = path,
                           .metadata = orion::util::Metadata::All,
                           .rating = 4}},
            {"-full.tif", {.format = orion::util::ImageFormat::Tiff,
                           .quality = 1.0f,
                           .space = ColorSpace::AdobeRgb,
                           .depth = orion::util::BitDepth::Sixteen,
                           .metadataFrom = path,
                           .metadata = orion::util::Metadata::All,
                           .rating = 4}},
            // The print case: resized, eight bits, the heavier sharpening
            // pass. It is the most expensive combination the panel can ask
            // for, so it is the one worth timing.
            {"-print.tif", {.format = orion::util::ImageFormat::Tiff,
                            .quality = 1.0f,
                            .maxDimension = 3000,
                            .space = ColorSpace::AdobeRgb,
                            .depth = orion::util::BitDepth::Eight,
                            .sharpen = orion::util::Sharpen::Print,
                            .metadataFrom = path,
                            .metadata = orion::util::Metadata::All,
                            .rating = 4}},
        };

        for (const auto& c : cases) {
            const std::string out = prefix + c.suffix;
            const auto t = Clock::now();
            orion::util::writeImage(out, pixels.data(), ew, eh, rowBytes, c.opts);
            std::printf("  %-14s %6.0f ms\n", c.suffix, msSince(t));
        }
    }
}

}  // namespace bench

// The export watermark: a one-color coverage mask blended in the writer.
//
// DECISIONS #284. The app lays the mark out and renders the mask; all the
// writer does is Porter-Duff "over" with a fixed color. So these checks are
// about that blend and nothing else - where it lands, by how much, in which
// direction, and that an absent mark changes not one byte.

#include "harness.h"

#include <fstream>
#include <iterator>

namespace {

std::vector<char> fileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// One pixel of a decoded 16-bit frame, as 0..1.
std::array<double, 3> at(const std::vector<std::uint16_t>& px, std::size_t w,
                         std::size_t x, std::size_t y) {
    const std::size_t i = (y * w + x) * 4;
    return {px[i] / 65535.0, px[i + 1] / 65535.0, px[i + 2] / 65535.0};
}

bool write(const std::string& path, const std::vector<std::uint16_t>& px,
           std::uint32_t w, std::uint32_t h, const orion::util::ExportOptions& o,
           const std::string& what) {
    try {
        orion::util::writeImage(path, px.data(), w, h, std::size_t(w) * 8, o);
        return true;
    } catch (const std::exception& e) {
        report(false, "writes " + what, e.what());
        return false;
    }
}

}  // namespace

void testExportWatermark() {
    section("Export watermark");

    using orion::util::BitDepth;
    using orion::util::ColorSpace;
    using orion::util::ImageFormat;

    // A flat, saturated orange: every channel a different distance from the
    // gray, so a blend that went the wrong way in any one of them shows.
    constexpr std::uint32_t kW = 64, kH = 48;
    constexpr double kSrc[3] = {0.8, 0.3, 0.1};
    std::vector<std::uint16_t> px(std::size_t(kW) * kH * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        for (int c = 0; c < 3; ++c) px[i + c] = std::uint16_t(std::lround(kSrc[c] * 65535));
        px[i + 3] = 65535;
    }

    // Coverage 128 over the top-left quadrant only. The top and the left both
    // matter: a mask drawn upside down or mirrored lands in another quadrant.
    constexpr std::uint8_t kCover = 128;
    std::vector<std::uint8_t> mask(std::size_t(kW) * kH, 0);
    for (std::uint32_t y = 0; y < kH / 2; ++y)
        for (std::uint32_t x = 0; x < kW / 2; ++x) mask[y * kW + x] = kCover;

    orion::util::Watermark mark;
    mark.mask = mask.data();
    mark.width = kW;
    mark.height = kH;

    const std::string dir = "/tmp/";

    // ── Where, and by how much ─────────────────────────────────────────────
    orion::util::ExportOptions plain{};
    plain.format = ImageFormat::Png;
    orion::util::ExportOptions marked = plain;
    marked.watermark = mark;

    const std::string plainPath = dir + "orion-wm-plain.png";
    const std::string markPath = dir + "orion-wm-marked.png";
    if (!write(plainPath, px, kW, kH, plain, "the plain frame")) return;
    if (!write(markPath, px, kW, kH, marked, "the watermarked frame")) return;

    std::vector<std::uint16_t> a, b;
    std::size_t aw = 0, ah = 0, bw = 0, bh = 0;
    if (!decode16(plainPath, a, aw, ah) || !decode16(markPath, b, bw, bh)
        || aw != kW || bw != kW || ah != kH || bh != kH) {
        report(false, "the watermarked frames read back");
        return;
    }

    // Under the mask: m * gray + (1 - m) * source, in the file's own encoding.
    const double m = kCover / 255.0;
    const auto under = at(b, bw, 8, 8);
    for (int c = 0; c < 3; ++c) {
        checkNear(under[c], m * 0.5 + (1 - m) * kSrc[c], 0.004,
                  std::string("the covered quadrant moves toward gray, channel ") + "RGB"[c]);
    }

    // Everywhere else, identical to the plain export - checked over the three
    // uncovered quadrants, a pixel clear of the mask's edge.
    bool untouched = true;
    for (std::size_t y = 0; y < kH; ++y) {
        for (std::size_t x = 0; x < kW; ++x) {
            if (x < kW / 2 + 1 && y < kH / 2 + 1) continue;
            const std::size_t i = (y * kW + x) * 4;
            for (int c = 0; c < 3; ++c) untouched &= a[i + c] == b[i + c];
        }
    }
    report(untouched, "outside the mask the watermarked frame is the plain one exactly");

    // A mask read upside down would have covered the bottom-left instead.
    report(at(b, bw, 8, kH - 8) == at(a, aw, 8, kH - 8),
           "the mask's first row is the image's top row");

    // ── An absent mark changes nothing ─────────────────────────────────────
    //
    // ⚠ Through both shortcuts `convert` has - the straight write and the
    // quantise-only redraw - because a watermark counted as "work to do" when
    // it is absent would push every export through a redraw it never needed.
    for (const auto depth : {BitDepth::Sixteen, BitDepth::Eight}) {
        orion::util::ExportOptions base = plain;
        base.depth = depth;
        orion::util::ExportOptions nulled = base;
        nulled.watermark.width = kW;  // a size without a mask is still no mark
        nulled.watermark.height = kH;
        const std::string d = depth == BitDepth::Eight ? "8" : "16";
        const std::string p0 = dir + "orion-wm-none-" + d + "a.png";
        const std::string p1 = dir + "orion-wm-none-" + d + "b.png";
        if (!write(p0, px, kW, kH, base, "without a mark") ||
            !write(p1, px, kW, kH, nulled, "with a null mask")) {
            continue;
        }
        report(fileBytes(p0) == fileBytes(p1) && !fileBytes(p0).empty(),
               "a null mask writes byte-identical files at " + d + " bits");
    }

    // ── Resized, eight bits, and a mask a pixel off ────────────────────────
    //
    // The app renders the mask at the size it predicts; the writer stretches
    // it over whatever size it actually made. Off by one must not matter.
    {
        std::vector<std::uint8_t> full(std::size_t(33) * 25, 255);
        orion::util::ExportOptions o = plain;
        o.maxDimension = 32;
        o.depth = BitDepth::Eight;
        o.watermark = {full.data(), 33, 25, {0.5f, 0.5f, 0.5f}};
        const std::string p = dir + "orion-wm-small.png";
        std::vector<std::uint16_t> s;
        std::size_t sw = 0, sh = 0;
        if (write(p, px, kW, kH, o, "a resized watermarked frame") && decode16(p, s, sw, sh)) {
            report(sw == 32 && sh == 24, "the resized frame keeps its own size");
            const auto c = at(s, sw, sw / 2, sh / 2);
            report(std::abs(c[0] - 0.5) < 0.01 && std::abs(c[2] - 0.5) < 0.01,
                   "a full-coverage mask a pixel off the output size still covers it",
                   std::to_string(c[0]) + ", " + std::to_string(c[2]));
        }
        report(orion::util::encodedSize(px.data(), kW, kH, std::size_t(kW) * 8, o) > 0,
               "the watermarked frame encodes for the size estimate");
    }

    // ── No cast in a wide space ────────────────────────────────────────────
    //
    // The color is stated in sRGB and converted by ColorSync. A gray passed
    // through as raw numbers into Display P3 would still be neutral there - so
    // the check is on the *decoded* result, which is where a mistake in the
    // conversion would tint the mark.
    {
        std::vector<std::uint8_t> full(std::size_t(kW) * kH, 255);
        orion::util::ExportOptions o = plain;
        o.space = ColorSpace::DisplayP3;
        o.watermark = {full.data(), kW, kH, {0.5f, 0.5f, 0.5f}};
        const std::string p = dir + "orion-wm-p3.png";
        std::vector<std::uint16_t> s;
        std::size_t sw = 0, sh = 0;
        if (write(p, px, kW, kH, o, "a Display P3 watermarked frame") && decode16(p, s, sw, sh)) {
            const auto c = at(s, sw, sw / 2, sh / 2);
            const double spread = std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
            report(spread < 0.004, "full coverage in Display P3 decodes to a neutral gray",
                   "spread " + std::to_string(spread));
            checkNear(c[1], 0.5, 0.01, "and to the gray asked for");
        }
    }
}

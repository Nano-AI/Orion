// Output sharpening, on every system. Moved out of tests_io.cpp, where it ran
// only on macOS because it read the file back through ImageIO.
//
// The unsharp mask is the writers' shared CPU half (ExportOps.cpp), so it is
// measured on the pixels themselves and needs no decoder. The one thing a file
// adds is the wiring, checked last through the platform's own writer.
//
// On a step edge, which is the only place an unsharp mask does anything. The
// overshoot either side of it is the mask's signature: asserting on that,
// rather than on "the pixels changed", means a pass that blurred, or that
// added a constant, cannot be mistaken for one that sharpened.

#include "harness.h"

#include "util/ExportOps.h"

#include <algorithm>
#include <cmath>

void testExportSharpening() {
    section("Export: output sharpening");
    using namespace orion::util;

    // ⚠ The edge is in **green only**, and that is what makes the
    // luminance-only claim checkable. Red and blue are flat across the frame,
    // so a sharpener that worked per channel would leave them flat, and one
    // that works on luminance, as this one does, has to move them alongside
    // green. On a neutral edge the two compute the same answer.
    constexpr std::uint32_t kW = 64, kH = 32;
    constexpr std::uint16_t kFlat = 32768;   // room either side to overshoot
    std::vector<std::uint16_t> edge(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            std::uint16_t* p = &edge[(std::size_t(y) * kW + x) * 4];
            p[0] = kFlat;
            p[1] = x < kW / 2 ? 16384 : 49151;
            p[2] = kFlat;
            p[3] = 65535;
        }
    }
    const std::size_t stride = std::size_t(kW) * 4 * sizeof(std::uint16_t);

    const Sharpen modes[] = {Sharpen::None, Sharpen::Screen, Sharpen::Print};
    const char* names[] = {"none", "screen", "print"};
    double overshoot[3], meanLuma[3], flatSwing[3];
    for (int i = 0; i < 3; ++i) {
        auto px = edge;
        unsharpMask(px.data(), kW, kH, stride, unsharpFor(modes[i]));
        // The brightest green on the light side, against where it started, and
        // how far the two channels that never had an edge were moved.
        double peak = 0.0, total = 0.0, swing = 0.0;
        for (std::size_t p = 0; p < px.size(); p += 4) {
            const double r = px[p], g = px[p + 1], b = px[p + 2];
            total += (0.2126 * r + 0.7152 * g + 0.0722 * b) / 65535.0;
            swing = std::max({swing, std::abs(r - kFlat), std::abs(b - kFlat)});
            if ((p / 4) % kW >= kW / 2) peak = std::max(peak, g);
        }
        overshoot[i] = (peak - 49151.0) / 65535.0;
        meanLuma[i] = total / double(kW * kH);
        flatSwing[i] = swing / 65535.0;
    }

    report(overshoot[0] < 1e-4, "None leaves the edge exactly as it was",
           "overshoot " + std::to_string(overshoot[0]));
    report(overshoot[1] > overshoot[0], "Screen overshoots the edge",
           "got " + std::to_string(overshoot[1]));
    report(overshoot[2] > overshoot[1], "Print overshoots more than Screen",
           "got " + std::to_string(overshoot[2]) + " vs " + std::to_string(overshoot[1]));
    for (int i = 1; i < 3; ++i) {
        // ⚠ A high-pass added back moves edges, not the overall brightness: a
        // pass that lifted the frame would pass every check above.
        report(std::abs(meanLuma[i] - meanLuma[0]) < 0.01,
               std::string("sharpening ") + names[i] + " does not move the overall brightness",
               "got " + std::to_string(meanLuma[i]) + " vs " + std::to_string(meanLuma[0]));
        // ⚠ Per-channel sharpening would leave red and blue exactly flat, and
        // put coloured fringes on the high-contrast edges this is aimed at.
        report(flatSwing[i] > 0.005,
               std::string("sharpening ") + names[i] + " moves all three channels together, not each alone",
               "flat-channel swing " + std::to_string(flatSwing[i]));
    }
    report(flatSwing[0] < 1e-4, "None leaves the flat channels exactly flat",
           "swing " + std::to_string(flatSwing[0]));

    // The wiring: this system's writer runs the pass. A sharpened edge has
    // more distinct values than a clean one, so its PNG cannot be the same size.
    ExportOptions plain, print;
    plain.format = print.format = ImageFormat::Png;
    print.sharpen = Sharpen::Print;
    const std::size_t a = encodedSize(edge.data(), kW, kH, stride, plain);
    const std::size_t b = encodedSize(edge.data(), kW, kH, stride, print);
    report(a > 0 && b > 0 && a != b, "the writer sharpens what it encodes",
           std::to_string(a) + " bytes plain, " + std::to_string(b) + " sharpened");
}

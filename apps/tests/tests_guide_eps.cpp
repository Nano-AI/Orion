// What the guided filter's epsilon means in stops.
//
// `research/UNSOURCED.md` §7 carried the guided filter's parameters as
// *reasoned but untested*: the filter itself is He, Sun & Tang, the radius and
// `eps = 0.04` are not, and the note beside them said "eps is about a fifth of
// a stop — below that is texture and noise, above it is an edge."
//
// ⚠ **The reasoning was right and the arithmetic joining it to an edge was
// never written down.** `a = var(I)/(var(I) + eps)` makes `sqrt(eps)` a local
// **standard deviation** of 0.2 stops. A window straddling a step of `h` stops
// half and half has variance `h²/4`, so the *step* that half-passes is
// `2·sqrt(eps)` = 0.4 stops — twice what a reader checking the comment against
// a real edge would have expected.
//
// This drives the shipping kernel with moments computed by hand, which is what
// makes it a measurement rather than the same algebra written twice: the test
// says what `var` a window has and reads back what the GPU decided to pass.

#include "harness.h"

#include <cmath>
#include <vector>

/// The edge threshold, in stops, off the kernel that ships.
void testGuideEpsilonInStops() {
    section("Guided filter — what epsilon means in stops");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/guideAb.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "guideAb");

    // One row, one column per step height. A window straddling a step of `h`
    // stops half and half has mean h/2 and mean-of-squares h²/2.
    const std::vector<double> heights = {0.0, 0.05, 0.1, 0.2, 0.4, 0.8, 1.6, 3.2};
    const auto kW = std::uint32_t(heights.size());
    constexpr std::uint32_t kH = 1;

    auto moments = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RG32Float);
    auto dst     = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RG32Float);

    const auto passAt = [&](float epsilon) {
        std::vector<float> in(std::size_t(kW) * 2);
        for (std::size_t i = 0; i < heights.size(); ++i) {
            const double h = heights[i];
            in[i * 2 + 0] = float(h / 2.0);          // mean(I)
            in[i * 2 + 1] = float(h * h / 2.0);      // mean(I*I)
        }
        moments->upload(in.data(), std::size_t(kW) * 2 * sizeof(float));

        params::GuideAb ga{{kW, kH}, epsilon, 0.0f};
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {moments.get(), dst.get()}, &ga, sizeof ga, kW, kH);
        cb.commitAndWait();

        std::vector<float> out(std::size_t(kW) * 2);
        dst->download(out.data(), std::size_t(kW) * 2 * sizeof(float), kW, kH);

        std::vector<double> a(heights.size());
        for (std::size_t i = 0; i < heights.size(); ++i) a[i] = double(out[i * 2 + 0]);
        return a;
    };

    // ⚠ **The shipped constant, not a copy of it.** `params::kGuideEpsilon` is
    // what `DevelopLocal` hands the node, so changing the product moves this
    // test — which is the whole difference between measuring the filter and
    // restating its arithmetic in a second place.
    const double eps = double(params::kGuideEpsilon);
    const double halfStep = 2.0 * std::sqrt(eps);
    const auto a = passAt(params::kGuideEpsilon);

    for (std::size_t i = 0; i < heights.size(); ++i) {
        std::printf("  step %5.2f stops -> a = %.4f\n", heights[i], a[i]);
    }
    std::printf("  eps %.4f: sd threshold %.3f stops, step threshold %.3f stops\n",
                eps, std::sqrt(eps), halfStep);

    // Flat is flat: no variance, nothing passes, and the filter collapses to
    // the local mean. This is the case that stops highlight recovery haloing.
    report(a[0] < 1e-6,
           "a flat window passes no detail at all",
           std::to_string(a[0]));

    // Monotone, so `a` is a threshold and not a bump.
    bool rising = true;
    for (std::size_t i = 1; i < a.size(); ++i) rising = rising && a[i] > a[i - 1];
    report(rising, "a taller step always passes more than a shorter one");

    // ⚠ The claim the register could not make: **0.4 stops**, not 0.2, is where
    // half the detail survives — the factor of two between a standard deviation
    // and a step height.
    const auto at = [&](double h) {
        for (std::size_t i = 0; i < heights.size(); ++i)
            // ⚠ 1e-6 and not 1e-9: `kGuideEpsilon` is a float, so
            // `2*sqrt(eps)` in double lands 4.5e-9 away from the 0.4 in the
            // list above and an exact-match lookup missed it entirely.
            if (std::fabs(heights[i] - h) < 1e-6) return a[i];
        return -1.0;
    };
    report(std::fabs(at(halfStep) - 0.5) < 0.02,
           "a step of 2*sqrt(eps) stops is where exactly half the detail passes",
           "a = " + std::to_string(at(halfStep)) + " at "
               + std::to_string(halfStep) + " stops");

    // Texture below the threshold is suppressed; an edge well above it is not.
    report(at(0.1) < 0.12,
           "a tenth of a stop is texture and is smoothed away",
           std::to_string(at(0.1)));
    report(at(3.2) > 0.98,
           "three stops is an edge and passes essentially untouched",
           std::to_string(at(3.2)));

    // ⚠ **The control.** Every check above is satisfied by a kernel that
    // ignores `epsilon` and hard-codes a curve in `h`. Running the same steps
    // at four times the epsilon must move the threshold by exactly a factor of
    // two in stops, because the relation is `h = 2*sqrt(eps)`.
    const auto wide = passAt(params::kGuideEpsilon * 4.0f);
    report(std::fabs(wide[4] - 0.2) < 0.02,
           "quadrupling epsilon quarters what a 0.4-stop step passes, so the "
           "kernel is reading the parameter rather than the step",
           "a = " + std::to_string(wide[4]));
    report(std::fabs(wide[5] - 0.5) < 0.02,
           "and moves the half-pass step to 0.8 stops, exactly the factor of "
           "two the square root predicts",
           "a = " + std::to_string(wide[5]));
}

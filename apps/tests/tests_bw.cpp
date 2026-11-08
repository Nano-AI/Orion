// Black and white: `developLinear`'s gray mix on a colour chart.
//
// research/bw-mix.md. Every invariant is read off the kernel's own output on a
// real GPU: the gray is neutral, all sliders at zero is exactly Rec.2020 Y, and
// a slider's gain is 2^(kBwStops * chroma * its normalized band membership).

#include "harness.h"

namespace {

constexpr double kBwStops = 2.0;   // ops/bw_ops.slang
constexpr double kCenters[8] = {0, 30, 60, 120, 180, 240, 285, 320};

/// The mixer's band membership, research/UNSOURCED.md §4: (1 - d/60)^2, normalized.
double membership(double hue, int band) {
    double total = 0, mine = 0;
    for (int i = 0; i < 8; ++i) {
        double d = std::abs(hue - kCenters[i]);
        d = std::min(d, 360.0 - d);
        const double w = std::pow(std::max(0.0, 1.0 - d / 60.0), 2.0);
        total += w;
        if (i == band) mine = w;
    }
    return mine / total;
}

/// HSV with value 0.5 and the given chroma, so (max - min) / max is `chroma`.
std::array<double, 3> patch(double hue, double chroma) {
    const double v = 0.5, c = v * chroma, h = hue / 60.0;
    const double x = c * (1 - std::abs(std::fmod(h, 2.0) - 1));
    std::array<double, 3> rgb{};
    switch (int(h) % 6) {
        case 0: rgb = {c, x, 0}; break;
        case 1: rgb = {x, c, 0}; break;
        case 2: rgb = {0, c, x}; break;
        case 3: rgb = {0, x, c}; break;
        case 4: rgb = {x, 0, c}; break;
        default: rgb = {c, 0, x}; break;
    }
    for (double& ch : rgb) ch += v - c;
    return rgb;
}

}  // namespace

void testBlackWhiteMixGpu() {
    section("Black and white gray mix (GPU)");

    // One column per band centre; rows: chroma 1, chroma 0.5, neutral, and
    // chroma 1 at 4x, a highlight above 1 the mix must weigh like its base.
    constexpr std::uint32_t kW = 8, kH = 4;
    const double chromas[kH] = {1.0, 0.5, 0.0, 1.0};
    const double scales[kH] = {1.0, 1.0, 1.0, 4.0};

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "GPU device available", e.what());
        return;
    }
    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developLinear.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developLinear");
    using orion::gpu::PixelFormat;
    auto src = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto mask = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);

    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    std::vector<double> luma(std::size_t(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            const auto rgb = patch(kCenters[x], chromas[y]);
            for (int c = 0; c < 3; ++c) input[i * 4 + c] = static_cast<__fp16>(rgb[c] * scales[y]);
            input[i * 4 + 3] = 1;
            luma[i] = 0.2627 * double(input[i * 4]) + 0.6780 * double(input[i * 4 + 1]) +
                      0.0593 * double(input[i * 4 + 2]);
        }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    // The guide chain off and exposure at zero, so the kernel's colour path is
    // the identity until the mix: what arrives at it is the chart.
    const auto render = [&](const std::array<float, 8>& mix, bool on) {
        orion::pipe::params::LinearAdjust la{};
        la.size[0] = kW; la.size[1] = kH;
        la.guideSize[0] = kW; la.guideSize[1] = kH;
        la.bwEnabled = on ? 1.0f : 0.0f;
        std::copy(mix.begin(), mix.end(), la.bwMix);
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel,
                    {src.get(), src.get(), src.get(), mask.get(), mask.get(), mask.get(), mask.get(),
                     mask.get(), mask.get(), mask.get(), mask.get(), dst.get()},
                    &la, sizeof la, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH * 4);
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
        return out;
    };

    // Half-float output: an exact result is within one rounding of the truth.
    constexpr double kHalf = 1.0 / 1024.0;
    const auto neutral = [&](const std::vector<__fp16>& o) {
        double worst = 0;
        for (std::size_t i = 0; i < luma.size(); ++i) {
            const double r = o[i * 4], g = o[i * 4 + 1], b = o[i * 4 + 2];
            worst = std::max(worst, (std::max({r, g, b}) - std::min({r, g, b})) / std::max(g, 1e-6));
        }
        return worst;
    };
    // The worst relative error of the gray against Y times `gain(column, row)`.
    const auto against = [&](const std::vector<__fp16>& o, const std::function<double(int, int)>& gain) {
        double worst = 0;
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                const std::size_t i = std::size_t(y) * kW + x;
                const double want = luma[i] * gain(int(x), int(y));
                worst = std::max(worst, std::abs(double(o[i * 4 + 1]) - want) / want);
            }
        return worst;
    };

    const auto colour = render({}, false);
    report(neutral(colour) > 0.5, "the chart is coloured before the switch",
           std::to_string(neutral(colour)));

    const auto plain = render({}, true);
    const double plainErr = against(plain, [](int, int) { return 1.0; });
    report(neutral(plain) == 0 && plainErr <= kHalf,
           "every slider at zero is Rec.2020 Y, with R = G = B",
           "spread " + std::to_string(neutral(plain)) + ", worst " + std::to_string(plainErr));

    // Green's neighbours sit 60 degrees away, where membership is zero, so +1
    // on green is the full two stops on a pure green and nothing at any other
    // band's centre.
    std::array<float, 8> green{};
    green[3] = 1;
    const auto lifted = render(green, true);
    const double greenErr = against(lifted, [&](int x, int y) {
        return std::exp2(kBwStops * chromas[y] * membership(kCenters[x], 3));
    });
    report(neutral(lifted) == 0 && greenErr <= 2 * kHalf && membership(120, 3) == 1.0,
           "+1 on green scales a pure green's gray by 2^2 = 4, half saturated by 2, gray by 1",
           "worst " + std::to_string(greenErr));

    // A band whose neighbours overlap: red at +1 reaches red by its normalized
    // membership, 1 / (1 + 1/4 + 1/9), and orange and magenta by theirs.
    std::array<float, 8> red{};
    red[0] = 1;
    const double redErr = against(render(red, true), [&](int x, int y) {
        return std::exp2(kBwStops * chromas[y] * membership(kCenters[x], 0));
    });
    report(redErr <= 2 * kHalf, "+1 on red weighs each band centre by its normalized membership",
           "worst " + std::to_string(redErr));

    // The weights are a partition of unity: every slider at -1 is two stops
    // down on any fully saturated colour, whatever its band, highlights too.
    std::array<float, 8> all{};
    all.fill(-1);
    const auto lowered = render(all, true);
    const double allErr = against(lowered, [&](int, int y) { return std::exp2(-kBwStops * chromas[y]); });
    report(neutral(lowered) == 0 && allErr <= 2 * kHalf,
           "every slider at -1 is 2^-2 on every saturated band, 2^-1 at half chroma, 1 on gray",
           "worst " + std::to_string(allErr));
}

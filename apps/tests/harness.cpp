#include "harness.h"

#include <map>
#include <utility>

int failures = 0;
int checks = 0;

orion::gpu::Texture& scratchAccum(orion::gpu::Device& device, std::uint32_t w,
                                  std::uint32_t h) {
    // Keyed by size and never freed. The binary lives for seconds, and the
    // alternative — a fresh texture per dispatch — allocates inside loops that
    // run a few hundred times.
    static std::map<std::pair<std::uint32_t, std::uint32_t>,
                    std::unique_ptr<orion::gpu::Texture>> cache;
    const auto key = std::make_pair(w, h);
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.emplace(key,
                           orion::gpu::Texture::create(
                               device, w, h, orion::gpu::PixelFormat::R32Float))
                 .first;
    }
    return *it->second;
}

void report(bool ok, const std::string& what, const std::string& detail) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("  FAIL  %s%s%s\n", what.c_str(),
                detail.empty() ? "" : " \u2014 ", detail.c_str());
}

void checkNear(double got, double want, double tol, const std::string& what) {
    const bool ok = std::abs(got - want) <= tol;
    char detail[128];
    std::snprintf(detail, sizeof detail, "got %.5f, want %.5f (tol %.5f)", got, want, tol);
    report(ok, what, ok ? "" : detail);
}

void section(const char* name) { std::printf("%s\n", name); }

orion::raw::BayerImage noisyGrey(std::uint32_t w, std::uint32_t h, double signal,
                                 double a, double b, unsigned seed) {
    constexpr float kWhite = 16383.0f;
    orion::raw::BayerImage img;
    img.width = w; img.height = h;
    img.filters = 0x94949494u;             // RGGB
    img.white = std::uint16_t(kWhite);
    img.black = {0, 0, 0, 0};
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};
    img.samples.resize(std::size_t(w) * h);
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 1.0);
    const double sigma = std::sqrt(a * signal + b);
    for (auto& s : img.samples)
        s = std::uint16_t(std::clamp(signal + sigma * g(rng), 0.0, 1.0) * kWhite);
    return img;
}

std::vector<std::uint8_t> pixels(const orion::pipe::DevelopPipeline& p) {
    const auto row = p.outputWidth() * orion::gpu::bytesPerPixel(p.output().format());
    std::vector<std::uint8_t> bytes(row * p.outputHeight());
    p.output().download(bytes.data(), row, p.outputWidth(), p.outputHeight());
    return bytes;
}

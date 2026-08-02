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

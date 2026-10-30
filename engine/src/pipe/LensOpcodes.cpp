#include "pipe/LensOpcodes.h"

#include <cmath>
#include <cstring>

namespace orion::pipe {
namespace {

/// A big-endian cursor that refuses to run past its buffer. Opcode lists are
/// always big-endian, whatever the file's own byte order (DNG 1.6 ch. 7).
struct Reader {
    const std::uint8_t* p;
    std::size_t n;
    std::size_t at = 0;
    bool ok = true;

    std::uint32_t u32() {
        if (!ok || n - at < 4) { ok = false; return 0; }
        const auto* b = p + at;
        at += 4;
        return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) |
               (std::uint32_t(b[2]) << 8) | std::uint32_t(b[3]);
    }
    double f64() {
        const std::uint64_t hi = u32(), lo = u32();
        const std::uint64_t bits = (hi << 32) | lo;
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof v);
        return ok && std::isfinite(v) ? v : (ok = false, 0.0);
    }
};

constexpr std::uint32_t kWarpRectilinear   = 1;
constexpr std::uint32_t kFixVignetteRadial = 3;

}  // namespace

DngLensOpcodes parseDngLensOpcodes(const std::vector<std::uint8_t> lists[3]) {
    DngLensOpcodes out;
    for (int list = 0; list < 3; ++list) {
        Reader r{lists[list].data(), lists[list].size()};
        if (r.n == 0) continue;
        const std::uint32_t count = r.u32();
        for (std::uint32_t i = 0; i < count && r.ok; ++i) {
            const std::uint32_t id = r.u32();
            r.u32();   // the DNG version the opcode was defined in
            r.u32();   // flags: optional, skippable for preview
            const std::uint32_t bytes = r.u32();
            if (!r.ok || bytes > r.n - r.at) break;
            Reader param{r.p + r.at, bytes};
            r.at += bytes;

            if (id == kWarpRectilinear && !out.hasWarp) {
                // N coefficient sets of six doubles, then the center. N is 1
                // (all planes) or the plane count — three for an RGB image.
                const std::uint32_t planes = param.u32();
                if (planes != 1 && planes != 3) continue;
                if (bytes != 4 + planes * 48 + 16) continue;
                LensModel& m = out.model;
                double k[3][6]{};
                for (std::uint32_t pl = 0; pl < planes; ++pl) {
                    for (double& v : k[pl]) v = param.f64();
                }
                const double cx = param.f64(), cy = param.f64();
                if (!param.ok) continue;
                for (int ch = 0; ch < 3; ++ch) {
                    const double* s = k[planes == 1 ? 0 : ch];
                    // kr0 + kr1·r² + kr2·r⁴ + kr3·r⁶ into k0, k2, k4, k6.
                    m.radial[ch][0] = float(s[0]);
                    m.radial[ch][1] = 0.0f;
                    m.radial[ch][2] = float(s[1]);
                    m.radial[ch][3] = 0.0f;
                    m.radial[ch][4] = float(s[2]);
                    m.radial[ch][5] = float(s[3]);
                    m.tangential[ch][0] = float(s[4]);
                    m.tangential[ch][1] = float(s[5]);
                }
                m.norm = LensNorm::FarthestPixel;
                m.center[0] = float(cx);
                m.center[1] = float(cy);
                out.hasWarp = true;
            } else if (id == kFixVignetteRadial && !out.hasVignette) {
                if (bytes != 7 * 8) continue;
                double k[7]{};
                for (double& v : k) v = param.f64();
                if (!param.ok) continue;
                LensModel& m = out.model;
                for (int j = 0; j < 5; ++j) m.vig[j] = float(k[j]);
                m.vigNorm = LensNorm::FarthestPixel;
                m.vigCenter[0] = float(k[5]);
                m.vigCenter[1] = float(k[6]);
                m.vigGain = true;
                // The lists run 1, 2, 3 and each in order, which is the order
                // read here: a warp already seen means this saw warped pixels.
                m.vigAtOutput = out.hasWarp;
                out.hasVignette = true;
            }
        }
    }
    return out;
}

}  // namespace orion::pipe

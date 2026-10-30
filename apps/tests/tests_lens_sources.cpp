// Lens corrections from the source that is not lensfun: the DNG opcodes a
// file carries (DJI drones, phones — issue #5).
//
// Every expected value below is computed **from the published formula, written
// out again here in double precision** — never through LensGeometry.h — so a
// mistake in the shared geometry cannot agree with itself. The DNG warp is
// DNG 1.6 chapter 7 as printed.

#include "harness.h"

#include "Engine.h"
#include "pipe/LensOpcodes.h"
#include "util/DngWriter.h"

#include <libraw/libraw.h>

#include <cstdint>
#include <filesystem>

namespace {

using orion::pipe::LensModel;
namespace lens = orion::pipe::lens;

constexpr std::uint32_t kW = 256, kH = 192;

// ── Building opcode lists by hand, big-endian, per DNG 1.6 chapter 7 ───────

void be32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
}
void be64(std::vector<std::uint8_t>& b, double d) {
    std::uint64_t v = 0;
    std::memcpy(&v, &d, sizeof v);
    be32(b, std::uint32_t(v >> 32));
    be32(b, std::uint32_t(v));
}
std::vector<std::uint8_t> opcode(std::uint32_t id, const std::vector<std::uint8_t>& params,
                                 std::uint32_t flags = 0) {
    std::vector<std::uint8_t> b;
    be32(b, id);
    be32(b, 0x01030000);   // defined in DNG 1.3.0.0
    be32(b, flags);
    be32(b, std::uint32_t(params.size()));
    b.insert(b.end(), params.begin(), params.end());
    return b;
}
std::vector<std::uint8_t> warpOp(int planes, const double k[][6], double cx, double cy) {
    std::vector<std::uint8_t> p;
    be32(p, std::uint32_t(planes));
    for (int i = 0; i < planes; ++i) for (int j = 0; j < 6; ++j) be64(p, k[i][j]);
    be64(p, cx);
    be64(p, cy);
    return opcode(1, p);
}
std::vector<std::uint8_t> vignetteOp(const double k[5], double cx, double cy) {
    std::vector<std::uint8_t> p;
    for (int j = 0; j < 5; ++j) be64(p, k[j]);
    be64(p, cx);
    be64(p, cy);
    return opcode(3, p);
}
std::vector<std::uint8_t> opList(std::initializer_list<std::vector<std::uint8_t>> ops) {
    std::vector<std::uint8_t> b;
    be32(b, std::uint32_t(ops.size()));
    for (const auto& o : ops) b.insert(b.end(), o.begin(), o.end());
    return b;
}

// ── The two published formulas, independently ──────────────────────────────

/// DNG WarpRectilinear for one plane, pixel-index coordinates in and out.
void dngWarp(const double k[6], double chx, double chy, double x, double y,
             double& xs, double& ys) {
    const double cx = chx * (kW - 1), cy = chy * (kH - 1);
    const double mx = std::max(std::abs(0 - cx), std::abs(kW - 1 - cx));
    const double my = std::max(std::abs(0 - cy), std::abs(kH - 1 - cy));
    const double m = std::sqrt(mx * mx + my * my);
    const double dx = (x - cx) / m, dy = (y - cy) / m;
    const double r2 = dx * dx + dy * dy;
    const double f = k[0] + k[1] * r2 + k[2] * r2 * r2 + k[3] * r2 * r2 * r2;
    const double xt = k[4] * (2 * dx * dy) + k[5] * (r2 + 2 * dx * dx);
    const double yt = k[5] * (2 * dx * dy) + k[4] * (r2 + 2 * dy * dy);
    xs = cx + m * (f * dx + xt);
    ys = cy + m * (f * dy + yt);
}

// ── The GPU ─────────────────────────────────────────────────────────────────

struct Gpu {
    std::unique_ptr<orion::gpu::Device> device;
    std::unique_ptr<orion::gpu::Library> library;
    std::unique_ptr<orion::gpu::Kernel> kernel;
    std::unique_ptr<orion::gpu::Texture> src, dst;

    bool ok() const { return kernel != nullptr; }

    /// Renders `input` (RGBA fp16) through the lens kernel.
    std::vector<__fp16> run(const orion::pipe::params::Lens& p, const std::vector<__fp16>& input) {
        src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH * 4);
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
        return out;
    }
};

Gpu makeGpu() {
    Gpu g;
    try {
        g.device = orion::gpu::Device::create();
        g.library = orion::gpu::Library::createFromFile(
            *g.device, std::string(ORION_SHADER_DIR) + "/lensCorrect.metallib");
        g.kernel = orion::gpu::Kernel::create(*g.device, *g.library, "lensCorrect");
        g.src = orion::gpu::Texture::create(*g.device, kW, kH, orion::gpu::PixelFormat::RGBA16Float);
        g.dst = orion::gpu::Texture::create(*g.device, kW, kH, orion::gpu::PixelFormat::RGBA16Float);
    } catch (const std::exception& e) {
        report(false, "Metal device and lens kernel available", e.what());
        g.kernel.reset();
    }
    return g;
}

/// Every channel carries its texel's own x (axis 0) or y (axis 1), encoded so
/// that a bilinear fetch at texel coordinate t reads back exactly t.
std::vector<__fp16> ramp(int axis) {
    std::vector<__fp16> v(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double t = axis == 0 ? (x + 0.5) / kW : (y + 0.5) / kH;
            for (int c = 0; c < 3; ++c) v[(std::size_t(y) * kW + x) * 4 + c] = __fp16(t);
            v[(std::size_t(y) * kW + x) * 4 + 3] = 1;
        }
    }
    return v;
}
double decode(const std::vector<__fp16>& out, std::uint32_t x, std::uint32_t y, int c, int axis) {
    return double(out[(std::size_t(y) * kW + x) * 4 + c]) * (axis == 0 ? kW : kH) - 0.5;
}

/// The worst disagreement, in pixels, between where the kernel read each
/// channel from and where `want(c, x, y, sx, sy)` says it should have.
/// Pixels whose expected source falls outside the frame are skipped: the
/// clamp answers for them, not the warp.
template <class F>
double worstFetch(Gpu& g, const orion::pipe::params::Lens& p, F want) {
    const auto ox = g.run(p, ramp(0));
    const auto oy = g.run(p, ramp(1));
    double worst = 0.0;
    for (std::uint32_t y = 0; y < kH; y += 7) {
        for (std::uint32_t x = 0; x < kW; x += 7) {
            for (int c = 0; c < 3; ++c) {
                double sx = 0, sy = 0;
                want(c, double(x), double(y), sx, sy);
                if (sx < 0 || sy < 0 || sx > kW - 1 || sy > kH - 1) continue;
                worst = std::max({worst, std::abs(decode(ox, x, y, c, 0) - sx),
                                  std::abs(decode(oy, x, y, c, 1) - sy)});
            }
        }
    }
    return worst;
}

}  // namespace

void testDngLensOpcodes() {
    section("Lens correction from DNG opcodes (WarpRectilinear, FixVignetteRadial)");

    // ── Parsing ──────────────────────────────────────────────────────────
    const double one[1][6] = {{1.0, -0.06, 0.012, -0.002, 0.0008, -0.0005}};
    std::vector<std::uint8_t> lists[3];
    const double vig[5] = {0.3, 0.1, 0.0, 0.0, 0.0};
    lists[1] = opList({vignetteOp(vig, 0.5, 0.5)});
    lists[2] = opList({opcode(9, {1, 2, 3, 4}), warpOp(1, one, 0.47, 0.53)});
    auto parsed = orion::pipe::parseDngLensOpcodes(lists);
    report(parsed.hasWarp && parsed.hasVignette, "both lens opcodes are found past an unknown one");
    checkNear(parsed.model.radial[0][2], -0.06, 1e-7, "kr1 lands on the r² term");
    checkNear(parsed.model.radial[2][5], -0.002, 1e-7, "kr3 lands on the r⁶ term, every plane");
    checkNear(parsed.model.tangential[1][0], 0.0008, 1e-7, "kt0 is the first tangential term");
    report(parsed.model.vigGain && !parsed.model.vigAtOutput,
           "a vignette in OpcodeList2 is a gain at the source position");

    lists[1].clear();
    lists[2] = opList({warpOp(1, one, 0.47, 0.53), vignetteOp(vig, 0.5, 0.5)});
    report(orion::pipe::parseDngLensOpcodes(lists).model.vigAtOutput,
           "a vignette after the warp is evaluated at the output position");

    // ── Hostile bytes must find nothing and read nothing out of bounds ──
    {
        std::vector<std::uint8_t> bad[3];
        auto truncated = opList({warpOp(1, one, 0.5, 0.5)});
        truncated.resize(truncated.size() - 9);
        bad[2] = truncated;
        bool none = !orion::pipe::parseDngLensOpcodes(bad).hasWarp;
        bad[2] = opList({warpOp(1, one, 0.5, 0.5)});
        bad[2][16] = 0xff;   // parameter byte count far past the buffer
        none = none && !orion::pipe::parseDngLensOpcodes(bad).hasWarp;
        const double two[2][6] = {{1, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0}};
        bad[2] = opList({warpOp(2, two, 0.5, 0.5)});   // N must be 1 or the plane count
        none = none && !orion::pipe::parseDngLensOpcodes(bad).hasWarp;
        const double nan[1][6] = {{std::nan(""), 0, 0, 0, 0, 0}};
        bad[2] = opList({warpOp(1, nan, 0.5, 0.5)});
        none = none && !orion::pipe::parseDngLensOpcodes(bad).hasWarp;
        bad[2] = {0xff, 0xff, 0xff, 0xff};   // four billion opcodes, none present
        none = none && !orion::pipe::parseDngLensOpcodes(bad).hasWarp;
        report(none, "truncated, oversized, malformed and non-finite opcode lists find no warp");
    }

    // ── On the GPU: the kernel's fetch is the specification's formula ────
    Gpu g = makeGpu();
    if (!g.ok()) return;

    // Three planes, red and blue scaled against green: a camera's own lateral
    // chromatic aberration, which is what N = 3 exists for.
    const double three[3][6] = {{1.003, -0.06, 0.012, -0.002, 0.0008, -0.0005},
                                {1.0,   -0.06, 0.012, -0.002, 0.0008, -0.0005},
                                {0.996, -0.05, 0.010, -0.001, 0.0008, -0.0005}};
    lists[1].clear();
    lists[2] = opList({warpOp(3, three, 0.47, 0.53)});
    parsed = orion::pipe::parseDngLensOpcodes(lists);
    auto p = lens::resolve(parsed.model, kW, kH);
    const double worst = worstFetch(g, p, [&](int c, double x, double y, double& sx, double& sy) {
        dngWarp(three[c], 0.47, 0.53, x, y, sx, sy);
    });
    report(worst < 0.15, "WarpRectilinear on the GPU matches DNG 1.6 per plane, to fp16",
           "worst " + std::to_string(worst) + " px");

    // And the host's forward map, which the autoscale and region-of-interest
    // rendering stand on, is the same map.
    double worstHost = 0.0;
    for (int c = 0; c < 3; ++c) {
        for (double y = 3; y < kH; y += 37) for (double x = 5; x < kW; x += 41) {
            float fx = 0, fy = 0;
            lens::fetchPoint(p, c, float(x + 0.5), float(y + 0.5), fx, fy);
            double sx = 0, sy = 0;
            dngWarp(three[c], 0.47, 0.53, x, y, sx, sy);
            worstHost = std::max({worstHost, std::abs(fx - sx), std::abs(fy - sy)});
        }
    }
    report(worstHost < 1e-3, "lens::fetchPoint is the DNG formula",
           "worst " + std::to_string(worstHost) + " px");

    // FixVignetteRadial is a gain, multiplied in: g = 1 + k0·r² + … at the
    // distance from its own center over the farthest-pixel radius.
    lists[2] = opList({vignetteOp(vig, 0.4, 0.6)});
    p = lens::resolve(orion::pipe::parseDngLensOpcodes(lists).model, kW, kH);
    std::vector<__fp16> flat(std::size_t(kW) * kH * 4, __fp16(0.25));
    const auto vout = g.run(p, flat);
    double worstGain = 0.0;
    for (const auto& [x, y] : {std::pair{0u, 0u}, {kW - 1, kH - 1}, {kW / 3, kH / 4}, {101u, 140u}}) {
        const double cx = 0.4 * (kW - 1), cy = 0.6 * (kH - 1);
        const double mx = std::max(cx, kW - 1 - cx), my = std::max(cy, kH - 1 - cy);
        const double r2 = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (mx * mx + my * my);
        const double gain = 1 + vig[0] * r2 + vig[1] * r2 * r2;
        worstGain = std::max(worstGain,
                             std::abs(double(vout[(std::size_t(y) * kW + x) * 4]) - 0.25 * gain));
    }
    report(worstGain < 1e-3, "FixVignetteRadial multiplies by the specification's gain",
           "worst " + std::to_string(worstGain));
}

void testLensInverseMap() {
    section("Lens geometry: the inverse map undoes the forward map");

    const double outward[1][6] = {{1.02, 0.05, -0.01, 0.002, 0.001, -0.0007}};
    std::vector<std::uint8_t> lists[3];
    lists[2] = opList({warpOp(1, outward, 0.45, 0.55)});

    struct Case { const char* name; LensModel model; float ca; };
    const Case cases[] = {
        {"DNG", orion::pipe::parseDngLensOpcodes(lists).model, 0.0f},
        {"lensfun with manual fringing", lens::ptlens(0.01f, -0.2f, 0.02f), 0.002f},
    };
    for (const auto& c : cases) {
        auto p = lens::resolve(c.model, 6000, 4000);
        p.caRed = c.ca;
        p.caBlue = -c.ca;
        p.scale = lens::autoScale(p);
        double worst = 0.0;
        bool converged = true;
        for (int ch = 0; ch < 3; ++ch) {
            for (float y = 0.5f; y < 4000; y += 777.0f) for (float x = 0.5f; x < 6000; x += 999.0f) {
                float fx = 0, fy = 0, bx = 0, by = 0;
                lens::fetchPoint(p, ch, x, y, fx, fy);
                converged = lens::outputPoint(p, ch, fx, fy, bx, by) && converged;
                worst = std::max({worst, double(std::abs(bx - x)), double(std::abs(by - y))});
            }
        }
        report(converged && worst < 0.05,
               std::string(c.name) + ": outputPoint(fetchPoint(q)) is q on a 24 MP frame",
               "worst " + std::to_string(worst) + " px, scale " + std::to_string(p.scale));
    }
}

void testDngLensOpcodesThroughLibRaw() {
    section("A DNG's own lens opcodes reach the engine through LibRaw");
#if !LIBRAW_COMPILE_CHECK_VERSION_NOTLESS(0, 22)
    std::printf("  skipped: LibRaw %s keeps no DNG opcodes (0.22 does; Ubuntu 24.04 ships 0.21)\n",
                LIBRAW_VERSION_STR);
    return;
#endif

    const auto path = (std::filesystem::temp_directory_path() / "orion-lens-opcodes.dng").string();
    std::vector<float> rgb(std::size_t(kW) * kH * 3, 0.4f);
    const double one[1][6] = {{1.0, -0.06, 0.012, -0.002, 0.0008, -0.0005}};
    const double vig[5] = {0.3, 0.1, 0.0, 0.0, 0.0};
    const auto list3 = opList({warpOp(1, one, 0.5, 0.5), vignetteOp(vig, 0.5, 0.5)});

    orion::util::DngLinearImage img;
    img.width = kW;
    img.height = kH;
    img.rgb = rgb.data();
    img.xyzToCam = {0.6640f, -0.1847f, -0.0503f, -0.5238f, 1.3465f, 0.1916f,
                    -0.0879f, 0.1636f, 0.6271f};   // as tests_dng.cpp
    img.asShotNeutral = {0.4548f, 1.0f, 0.7130f};
    // ⚠ Not "DJI FC8482": with that Make LibRaw leaves this synthetic linear
    // DNG's color matrix at zero (its DJI handling, not ours), and the opcode
    // path is what is under test. The body tests_dng.cpp already uses.
    img.camera = "SONY ILCE-7RM3";
    img.opcodeList3 = list3;
    try {
        orion::util::writeDngLinear(path, img);
    } catch (const std::exception& e) {
        report(false, "lens-opcode DNG writes", e.what());
        return;
    }

    const auto info = orion::raw::readInfo(path);
    report(info.dngOpcodes[2] == list3, "LibRaw hands OpcodeList3 over byte for byte",
           std::to_string(info.dngOpcodes[2].size()) + " bytes");

    try {
        orion::Engine engine;
        engine.setHeadless(true);
        engine.openRaw(path);
        const auto& p = engine.lensProfile();
        report(p.found && p.source == orion::pipe::LensSource::DngOpcode,
               "the engine takes the file's own correction ahead of any database",
               p.lens);
        checkNear(p.model.radial[0][2], -0.06, 1e-7, "and it is the correction the file carries");

        // A hand-picked lensfun lens overrides it; clearing the choice brings
        // the file's own back rather than falling through to lensfun.
        const auto names = orion::Engine::lensDatabaseNames();
        if (!names.empty() && engine.setLensChoice(names.front())) {
            report(engine.lensProfile().source == orion::pipe::LensSource::Lensfun,
                   "a lens chosen by hand overrides the opcodes");
        }
        engine.setLensChoice("");
        report(engine.lensProfile().source == orion::pipe::LensSource::DngOpcode,
               "clearing the choice restores the file's own correction");
    } catch (const std::exception& e) {
        report(false, "the engine opens the lens-opcode DNG", e.what());
    }
    std::filesystem::remove(path);
}

void testLensSourceOnRealSony() {
    section("A real Sony raw still gets the lensfun answer");

    // Skipped where the sample is not linked in (`tools/worktree-setup.sh`).
    const std::string path = std::string(ORION_DATA_DIR) + "/../samples/_PIC8220.ARW";
    if (!std::filesystem::is_regular_file(path)) {
        std::printf("  (skipped: no %s)\n", path.c_str());
        return;
    }
    try {
        orion::Engine engine;
        engine.setHeadless(true);
        engine.openRaw(path);
        const auto& info = engine.info();
        report(info.dngOpcodes[0].empty() && info.dngOpcodes[1].empty() && info.dngOpcodes[2].empty(),
               "an ARW carries no DNG opcode lists");
        const orion::pipe::LensDatabase db(std::string(ORION_DATA_DIR) + "/lensfun");
        const auto want = db.lookup(info.lens, info.focalLength, info.aperture);
        const auto& got = engine.lensProfile();
        // No opcodes in an ARW, so the answer is lensfun's, found or not.
        report(got.found == want.found && got.lens == want.lens &&
               got.source == (want.found ? orion::pipe::LensSource::Lensfun : orion::pipe::LensSource::None),
               "the engine's profile is the lensfun database's own answer",
               "lens '" + info.lens + "' -> '" + got.lens + "'");
        std::printf("  file lens '%s', profile found %d\n", info.lens.c_str(), got.found ? 1 : 0);
        const auto names = orion::Engine::lensDatabaseNames();
        if (!names.empty() && engine.setLensChoice(names.front())) {
            report(engine.lensProfile().source == orion::pipe::LensSource::Lensfun &&
                   engine.lensProfile().found, "a hand-picked lens is lensfun's");
        }
    } catch (const std::exception& e) {
        report(false, "the engine opens the Sony sample", e.what());
    }
}

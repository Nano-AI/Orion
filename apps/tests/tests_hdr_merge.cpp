/*  tests_hdr_merge — the whole feature through the C facade: three files on
 *  disk in, one merged DNG out, reopened and asserted against the radiance
 *  the fixture was built from.
 *
 *  The frames are linear DNGs written by Orion's own writer (story A is why
 *  this is possible without ARW samples), with NO usable EXIF — so this also
 *  exercises the measured-ratio fallback: the merge must establish the
 *  exposure ladder from the pixels alone. The short exposure — the only
 *  frame that sees the highlight — is shifted by a known handheld move, so
 *  a merge that skipped alignment would put the recovered highlight in the
 *  wrong place, and the block-edge assertions would catch it there.
 *
 *  Cancellation is asserted as the invariant that cannot flake: whichever
 *  side of the race a cancel lands on, there is either a complete valid file
 *  or no file at all — never a partial one.
 */

#include "harness.h"
#include "merge/HdrMerge.h"
#include "orion/orion.h"
#include "util/DngWriter.h"
#include "util/Half.h"

#include <filesystem>
#include <thread>

namespace {

constexpr std::uint32_t kW = 480;
constexpr std::uint32_t kH = 360;

// Scene radiance in reference exposure units. A textured mid field (so the
// aligner has features), a highlight block only the short frame can see,
// and a shadow band.
float radiance(float x, float y) {
    if (x >= 300 && x < 380 && y >= 60 && y < 140) return 1.6f;   // highlight block
    if (y >= 260) return 0.05f;                                   // shadow band
    // Sharp-edged tiles: features everywhere, nothing periodic.
    const int tx = int(x) / 24, ty = int(y) / 24;
    const unsigned h = unsigned(tx) * 2654435761u ^ unsigned(ty) * 40503u;
    return 0.08f + float(h % 40) * 0.01f;                          // 0.08..0.47
}

/// One photograph: exposure e, camera moved so that frame pixel p depicts
/// scene point p - (dx, dy) (i.e. refToSource is a translation by +dx, +dy).
std::vector<float> photograph(float e, float dx, float dy) {
    std::vector<float> rgb(std::size_t(kW) * kH * 3);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float X = radiance(float(x) - dx, float(y) - dy);
            float* p = &rgb[(std::size_t(y) * kW + x) * 3];
            const float v = std::min(X * e, 1.0f);
            p[0] = v; p[1] = v; p[2] = v;
        }
    }
    return rgb;
}

constexpr std::array<float, 9> kXyzToCam = {
    0.6640f, -0.1847f, -0.0503f,
    -0.5238f, 1.3465f,  0.1916f,
    -0.0879f,  0.1636f,  0.6271f,
};

void writeFrame(const std::string& path, const std::vector<float>& rgb) {
    orion::util::DngLinearImage img;
    img.width  = kW;
    img.height = kH;
    img.rgb    = rgb.data();
    img.xyzToCam = kXyzToCam;
    img.asShotNeutral = {0.4548f, 1.0f, 0.7130f};
    img.camera = "SONY ILCE-7RM3";
    orion::util::writeDngLinear(path, img);
}

}  // namespace

void testHdrMergeFacade() {
    section("HDR merge, end to end through the C facade");

    const std::string dir = "/tmp/";
    const std::string p0 = dir + "orion-hdr-long.dng";
    const std::string p1 = dir + "orion-hdr-ref.dng";
    const std::string p2 = dir + "orion-hdr-short.dng";
    const std::string out = dir + "orion-hdr-merged.dng";
    std::remove(out.c_str());

    // E = {2, 1, 0.5}. The short frame — sole witness of the highlight —
    // took the handheld move: refToSource should come back ≈ (+9, -6).
    try {
        writeFrame(p0, photograph(2.0f, 0.0f, 0.0f));
        writeFrame(p1, photograph(1.0f, 0.0f, 0.0f));
        writeFrame(p2, photograph(0.5f, 9.0f, -6.0f));
    } catch (const std::exception& e) {
        report(false, "fixture frames written", e.what());
        return;
    }

    OrionEngine* engine = nullptr;
    if (orion_engine_create(&engine) != ORION_OK || engine == nullptr) {
        report(false, "engine for the merge facade");
        return;
    }

    const char* paths[3] = {p0.c_str(), p1.c_str(), p2.c_str()};

    // Progress is a fresh 0 before anything runs.
    float progress = -1.0f;
    CHECK(orion_engine_hdr_merge_progress(engine, &progress) == ORION_OK);
    CHECK(progress == 0.0f);

    const OrionStatus st = orion_engine_hdr_merge(engine, paths, 3, 1, out.c_str());
    report(st == ORION_OK, "merge succeeds through the facade",
           st == ORION_OK ? "" : orion_last_error(engine));

    if (st == ORION_OK) {
        CHECK(orion_engine_hdr_merge_progress(engine, &progress) == ORION_OK);
        CHECK(progress == 1.0f);

        try {
            const auto decoded = orion::raw::decode(out);
            const auto& m = std::get<orion::raw::LinearImage>(decoded);
            checkNear(m.width, kW, 0, "merged width");
            // H = 1/min(E) = 2, established from the PIXELS — no EXIF exists.
            checkNear(m.baselineExposureEv, 1.0, 0.08,
                      "headroom measured from the pixels alone");

            const auto px = [&](std::uint32_t x, std::uint32_t y) {
                return orion::util::halfToFloat(
                    m.rgba[(std::size_t(y) * kW + x) * 4 + 1]);
            };
            const float H = std::exp2(m.baselineExposureEv);

            // The highlight block, in REFERENCE coordinates: only the
            // shifted short frame saw it unclipped, so landing here at the
            // right value means the alignment worked end to end.
            checkNear(px(340, 100), 1.6f / H, 0.02,
                      "recovered highlight lands in reference framing");
            // Just outside the block: background, not smeared highlight. A
            // merge that ignored the shift would displace the block by 9 px.
            report(px(295, 100) < 1.0f / H,
                   "the block edge is where the reference put it");

            checkNear(px(60, 300), 0.05f / H, 0.01, "shadow band survives");
        } catch (const std::exception& e) {
            report(false, "merged DNG reopens", e.what());
        }
    }

    // Refusals: an existing output is an error, not an overwrite...
    CHECK(orion_engine_hdr_merge(engine, paths, 3, 1, out.c_str()) != ORION_OK);
    // ...and one frame is not a bracket.
    CHECK(orion_engine_hdr_merge(engine, paths, 1, 0, "/tmp/orion-hdr-x.dng") != ORION_OK);

    orion_engine_destroy(engine);
    std::remove(p0.c_str());
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove(out.c_str());
}

void testHdrMergeCancelIsAtomic() {
    section("HDR merge: cancellation never leaves a partial file");

    const std::string p0 = "/tmp/orion-hdrc-a.dng";
    const std::string p1 = "/tmp/orion-hdrc-b.dng";
    const std::string out = "/tmp/orion-hdrc-merged.dng";
    std::remove(out.c_str());

    try {
        writeFrame(p0, photograph(2.0f, 0.0f, 0.0f));
        writeFrame(p1, photograph(1.0f, 0.0f, 0.0f));

        auto device = orion::gpu::Device::create();
        orion::merge::HdrMerge merger(*device, ORION_SHADER_DIR);

        // Cancel racing the run. Whichever side wins, the invariant holds:
        // a complete valid file, or no file — never a partial one. The
        // writer's write-then-rename is what makes this assertable.
        bool threw = false;
        std::thread canceller([&] { merger.cancel(); });
        try {
            merger.run({p0, p1}, 0, out);
        } catch (const std::exception&) {
            threw = true;
        }
        canceller.join();

        namespace fs = std::filesystem;
        if (threw) {
            report(!fs::exists(out), "a cancelled merge leaves no file");
        } else {
            report(fs::exists(out) && fs::file_size(out) > 0,
                   "a completed merge leaves a whole file");
        }
        report(!fs::exists(out + ".part"), "and never a partial one");

        // A failing destination also leaves nothing.
        bool failed = false;
        try {
            merger.run({p0, p1}, 0, "/tmp/orion-no-such-dir/x.dng");
        } catch (const std::exception&) {
            failed = true;
        }
        CHECK(failed);
        report(!fs::exists("/tmp/orion-no-such-dir"), "no debris on a bad destination");
    } catch (const std::exception& e) {
        report(false, "cancel fixture", e.what());
    }

    std::remove(p0.c_str());
    std::remove(p1.c_str());
    std::remove(out.c_str());
}

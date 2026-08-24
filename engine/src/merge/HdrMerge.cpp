/*  HdrMerge — see the header for the contract and the policy decisions.
 *
 *  Memory shape at the a7riii's 42 MP, three frames: each decoded frame is
 *  demosaiced on the GPU one at a time (~1.1 GiB transient, reused), and its
 *  float RGB kept on the CPU (~0.5 GiB each). The accumulation happens
 *  inside merge(), which allocates the output once. Peak is roughly
 *  0.5 GiB x frames + 1.6 GiB working — deliberately nowhere near the
 *  develop graph's footprint (#162), which this path never touches.
 */

#include "merge/HdrMerge.h"

#include "merge/Align.h"
#include "merge/Merge.h"
#include "merge/MergeRender.h"
#include "raw/NoiseProfile.h"
#include "raw/RawImage.h"
#include "util/DngWriter.h"
#include "util/Half.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <variant>

namespace orion::merge {
namespace {

/// One decoded frame, in the merge's currency.
struct Loaded {
    std::vector<float> rgb;         // own-normalized linear camera RGB
    std::uint32_t width = 0, height = 0;
    float exifLight = 0.0f;         // t·ISO/N², 0 when EXIF is unusable
    float noiseA = 0.0f, noiseB = 0.0f;
    // Reference-frame metadata for the container.
    std::array<float, 4> camMul{};
    std::array<float, 9> camToXyz{};
    std::string camera;
};

}  // namespace

HdrMerge::HdrMerge(gpu::Device& device, std::string shaderDir)
    : device_(device), shaderDir_(std::move(shaderDir)) {}

void HdrMerge::step(float progress) {
    if (cancelled_.load(std::memory_order_relaxed)) {
        throw std::runtime_error("merge cancelled");
    }
    progress_.store(progress, std::memory_order_relaxed);
}

float HdrMerge::run(const std::vector<std::string>& paths, int referenceIndex,
                    const std::string& outputPath) {
    cancelled_.store(false, std::memory_order_relaxed);
    progress_.store(0.0f, std::memory_order_relaxed);

    if (paths.size() < 2) {
        throw std::runtime_error("a merge needs at least two frames");
    }
    if (referenceIndex < 0 || referenceIndex >= int(paths.size())) {
        throw std::runtime_error("reference index out of range");
    }
    if (std::filesystem::exists(outputPath)) {
        // Collision policy belongs to the caller (the UI picks fresh names);
        // silently replacing a photograph is not this class's call to make.
        throw std::runtime_error("output already exists: " + outputPath);
    }

    // ── Decode and demosaic, one frame at a time ──────────────────────────
    MergeRender render(device_, shaderDir_);
    std::vector<Loaded> frames(paths.size());

    for (std::size_t i = 0; i < paths.size(); ++i) {
        step(0.05f + 0.55f * float(i) / float(paths.size()));

        Loaded& f = frames[i];
        const auto info = raw::readInfo(paths[i]);
        f.exifLight = lightGathered(info.shutter, info.isoSpeed, info.aperture);

        auto decoded = raw::decode(paths[i]);
        if (auto* bayer = std::get_if<raw::BayerImage>(&decoded)) {
            const auto profile = raw::estimateNoise(*bayer);
            if (profile.measured) {
                f.noiseA = profile.a;
                f.noiseB = profile.b;
            }
            f.width = bayer->width;
            f.height = bayer->height;
            f.camMul = bayer->camMul;
            f.camToXyz = bayer->camToXyz;
            f.camera = bayer->camera;
            f.rgb = render.demosaic(*bayer);
        } else {
            // A linear DNG merges too — it is the same scene-linear camera
            // RGB, just already demosaiced. (This is also what makes the
            // merge testable end-to-end from synthetic files.)
            const auto& linear = std::get<raw::LinearImage>(decoded);
            f.width = linear.width;
            f.height = linear.height;
            f.camMul = linear.camMul;
            f.camToXyz = linear.camToXyz;
            f.camera = linear.camera;
            f.rgb.resize(linear.pixelCount() * 3);
            for (std::size_t p = 0; p < linear.pixelCount(); ++p) {
                f.rgb[p * 3 + 0] = util::halfToFloat(linear.rgba[p * 4 + 0]);
                f.rgb[p * 3 + 1] = util::halfToFloat(linear.rgba[p * 4 + 1]);
                f.rgb[p * 3 + 2] = util::halfToFloat(linear.rgba[p * 4 + 2]);
            }
        }

        if (f.width != frames[0].width || f.height != frames[0].height) {
            throw std::runtime_error(
                "the frames are different sizes — a bracket comes from one camera");
        }
    }

    const auto ref = std::size_t(referenceIndex);

    // ── Provisional exposure ratios (EXIF), then alignment ────────────────
    // The aligner only needs E to normalize its proxies, and its log domain
    // shrugs off a wrong constant — so EXIF (or 1.0 when absent) is enough
    // here, and the measured ratio afterwards runs on aligned taps.
    std::vector<Frame> merged(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        Frame& m = merged[i];
        m.width = frames[i].width;
        m.height = frames[i].height;
        m.rgb = frames[i].rgb.data();
        m.noiseA = frames[i].noiseA;
        m.noiseB = frames[i].noiseB;
        m.exposureRatio =
            (frames[i].exifLight > 0.0f && frames[ref].exifLight > 0.0f)
                ? frames[i].exifLight / frames[ref].exifLight
                : 1.0f;
    }

    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (i == ref) continue;
        step(0.62f + 0.10f * float(i) / float(frames.size()));
        const AlignResult a = align(merged[i], merged[ref]);
        // A refusal degrades to identity: the deghost gate then discards
        // whatever genuinely disagrees, and the merge falls back toward the
        // reference rather than warping by a guess.
        if (a.ok) merged[i].refToSource = a.refToSource;
    }

    // ── Final exposure ratios: EXIF checked against the pixels ────────────
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (i == ref) continue;
        const float exif = merged[i].exposureRatio;
        const float measured = measuredExposureRatio(merged[i], merged[ref]);
        const float resolved = resolveExposureRatio(
            frames[i].exifLight > 0.0f ? exif : 0.0f, measured);
        if (resolved <= 0.0f) {
            throw std::runtime_error(
                "cannot establish the exposure ratio for " + paths[i]);
        }
        merged[i].exposureRatio = resolved;
    }
    merged[ref].exposureRatio = 1.0f;

    step(0.72f);
    Result result = merge(merged, referenceIndex);

    // ── The container ─────────────────────────────────────────────────────
    step(0.95f);
    const Loaded& r = frames[ref];
    util::DngLinearImage out;
    out.width = result.width;
    out.height = result.height;
    out.rgb = result.rgb.data();
    out.xyzToCam = r.camToXyz;
    // AsShotNeutral is the camera-space color of a neutral patch: the
    // reciprocals of the gains, normalized so green is 1.
    const float g = r.camMul[1] != 0.0f ? r.camMul[1] : 1.0f;
    for (int c = 0; c < 3; ++c) {
        out.asShotNeutral[std::size_t(c)] =
            r.camMul[std::size_t(c)] != 0.0f ? g / r.camMul[std::size_t(c)] : 1.0f;
    }
    out.baselineExposureEv = result.headroomEv;
    out.camera = r.camera;

    util::writeDngLinear(outputPath, out);

    progress_.store(1.0f, std::memory_order_relaxed);
    return result.headroomEv;
}

}  // namespace orion::merge

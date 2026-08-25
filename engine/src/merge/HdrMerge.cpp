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
#include "util/ImageWriter.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    int flip = 0;
};

/// A small viewable rendering of the merge, for the DNG's embedded preview.
/// Not the develop pipeline's rendering and not trying to be: an embedded
/// preview is the camera-JPEG of the file — WB, the camera matrix into sRGB,
/// a gamma curve, done. The full fidelity lives in the raw data one IFD over.
struct Preview {
    std::vector<std::uint8_t> jpeg;
    std::uint32_t width = 0, height = 0;
};

bool invert3(const float m[9], float out[9]) {
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7])
                    - m[1] * (m[3] * m[8] - m[5] * m[6])
                    + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12f) return false;
    const float k = 1.0f / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * k;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * k;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * k;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * k;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * k;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * k;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * k;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * k;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * k;
    return true;
}

Preview buildPreview(const Result& result, const Loaded& ref) {
    Preview p;

    // Camera -> sRGB: XYZ->sRGB times the inverse of the file's XYZ->camera,
    // rows normalized so a white-balanced neutral stays neutral — the same
    // dcraw convention the develop pipeline applies for its working space.
    constexpr float kXyzToSrgb[9] = {
         3.2404542f, -1.5371385f, -0.4985314f,
        -0.9692660f,  1.8760108f,  0.0415560f,
         0.0556434f, -0.2040259f,  1.0572252f,
    };
    float camToXyz[9], m[9];
    float xyzToCam[9];
    std::copy(ref.camToXyz.begin(), ref.camToXyz.end(), xyzToCam);
    if (!invert3(xyzToCam, camToXyz)) return p;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r * 3 + c] = kXyzToSrgb[r * 3 + 0] * camToXyz[0 * 3 + c]
                         + kXyzToSrgb[r * 3 + 1] * camToXyz[1 * 3 + c]
                         + kXyzToSrgb[r * 3 + 2] * camToXyz[2 * 3 + c];
        }
    }
    for (int r = 0; r < 3; ++r) {
        const float sum = m[r * 3] + m[r * 3 + 1] + m[r * 3 + 2];
        if (std::abs(sum) > 1e-9f) {
            for (int c = 0; c < 3; ++c) m[r * 3 + c] /= sum;
        }
    }

    const float g = ref.camMul[1] != 0.0f ? ref.camMul[1] : 1.0f;
    const float wb[3] = {
        ref.camMul[0] != 0.0f ? ref.camMul[0] / g : 1.0f, 1.0f,
        ref.camMul[2] != 0.0f ? ref.camMul[2] / g : 1.0f};

    // Downsample to ~1024 on the long edge, in sensor orientation first.
    const std::uint32_t longEdge = std::max(result.width, result.height);
    const std::uint32_t step = std::max(1u, (longEdge + 1023) / 1024);
    const std::uint32_t sw = result.width / step;
    const std::uint32_t sh = result.height / step;
    if (sw == 0 || sh == 0) return p;

    // The stored data is radiance / H; the preview shows it at intended
    // brightness, exactly as BaselineExposure instructs a renderer to.
    const float gain = std::exp2(result.headroomEv);

    std::vector<float> small(std::size_t(sw) * sh * 3);
    for (std::uint32_t y = 0; y < sh; ++y) {
        for (std::uint32_t x = 0; x < sw; ++x) {
            float acc[3] = {0, 0, 0};
            for (std::uint32_t dy = 0; dy < step; ++dy) {
                const std::size_t row =
                    (std::size_t(y) * step + dy) * result.width;
                for (std::uint32_t dx = 0; dx < step; ++dx) {
                    const float* px =
                        result.rgb.data() + (row + std::size_t(x) * step + dx) * 3;
                    acc[0] += px[0]; acc[1] += px[1]; acc[2] += px[2];
                }
            }
            const float inv = gain / float(step * step);
            float cam[3] = {acc[0] * inv * wb[0], acc[1] * inv * wb[1],
                            acc[2] * inv * wb[2]};
            // The common neutral ceiling, linearize.slang's argument in
            // miniature: a pixel clipped in every frame arrives with its
            // channels cut at different levels, and unclamped it renders in
            // the white-balance gains' color. The ceiling after the gains is
            // gain·min(wb) = gain, since wb is normalized to green.
            for (float& c : cam) c = std::min(c, gain);
            float* out = small.data() + (std::size_t(y) * sw + x) * 3;
            for (int c = 0; c < 3; ++c) {
                const float lin = m[c * 3 + 0] * cam[0] + m[c * 3 + 1] * cam[1]
                                + m[c * 3 + 2] * cam[2];
                // A camera-JPEG-ish shoulder: the recovered headroom (up to
                // 2^headroom above white) rolls smoothly toward paper white,
                // and the mids land where a preview is expected to sit —
                // bare gamma rendered a stop and a half under the develop
                // path.
                out[c] = 1.0f - std::exp(-3.0f * std::max(lin, 0.0f));
            }
        }
    }

    // Bake the rotation into the pixels. Thumbnail consumers pass the JPEG
    // stream around without the TIFF tag it sat under, so an upright
    // preview is the only kind that is upright everywhere.
    const int quarters = ref.flip == 3 ? 2 : ref.flip == 5 ? 3
                       : ref.flip == 6 ? 1 : 0;
    const bool swaps = (quarters % 2) != 0;
    p.width  = swaps ? sh : sw;
    p.height = swaps ? sw : sh;

    std::vector<std::uint16_t> rgba(std::size_t(p.width) * p.height * 4);
    for (std::uint32_t dy = 0; dy < p.height; ++dy) {
        for (std::uint32_t dx = 0; dx < p.width; ++dx) {
            std::uint32_t sx = dx, sy = dy;
            switch (quarters) {
                case 1: sx = dy;            sy = sh - 1 - dx; break;
                case 2: sx = sw - 1 - dx;   sy = sh - 1 - dy; break;
                case 3: sx = sw - 1 - dy;   sy = dx;          break;
                default: break;
            }
            const float* px = small.data() + (std::size_t(sy) * sw + sx) * 3;
            std::uint16_t* out =
                rgba.data() + (std::size_t(dy) * p.width + dx) * 4;
            for (int c = 0; c < 3; ++c) {
                // sRGB gamma over the toned light.
                const float v = std::clamp(px[c], 0.0f, 1.0f);
                const float e = v <= 0.0031308f
                    ? v * 12.92f
                    : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
                out[std::size_t(c)] = std::uint16_t(e * 65535.0f + 0.5f);
            }
            out[3] = 65535;
        }
    }

    p.jpeg = util::encodeJpeg(rgba.data(), p.width, p.height,
                              std::size_t(p.width) * 4 * sizeof(std::uint16_t),
                              0.85f);
    return p;
}

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
            f.flip = bayer->flip;
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
            f.flip = linear.flip;
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
    // The reference frames the picture, so its orientation is the picture's.
    out.flip = r.flip;

    // The embedded preview — the filmstrip's and Finder's view of the file.
    // Allowed to fail: a DNG without one is inconvenient, not wrong.
    const Preview preview = buildPreview(result, r);
    if (!preview.jpeg.empty()) {
        out.previewJpeg      = preview.jpeg.data();
        out.previewJpegBytes = preview.jpeg.size();
        out.previewWidth     = preview.width;
        out.previewHeight    = preview.height;
    }

    util::writeDngLinear(outputPath, out);

    progress_.store(1.0f, std::memory_order_relaxed);
    return result.headroomEv;
}

}  // namespace orion::merge

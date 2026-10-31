/*  Stacker — see the header for the contract and the policy decisions.
 *
 *  Memory shape at 24 MP, 20 frames: the held half planes (144 MB each,
 *  2.9 GB), plus one frame in flight — its decoded image (192 MB as a linear
 *  DNG's RGBA halves), its float RGB (288 MB), detectStars' transients
 *  (~310 MB) — plus, in Noise mode, the reference's float (288 MB).
 *
 *  Calibration (Calibrate.h) comes first: a master is a median, so every
 *  dark (then every flat) is held at once as floats — 96 MB a mosaic at
 *  24 MP, 288 MB a linear DNG's RGB. ponytail: 20 linear-DNG flats are 5.8 GB;
 *  a median over strips of rows would cap it if anyone stacks that many.
 *  Afterwards only the two masters stay.
 */

#include "stack/Stacker.h"

#include "merge/Align.h"
#include "merge/MergeRender.h"
#include "merge/Preview.h"
#include "raw/RawImage.h"
#include "stack/Calibrate.h"
#include "stack/SkySplit.h"
#include "stack/StarAlign.h"
#include "util/DngWriter.h"
#include "util/Half.h"
#include "util/Utf8Path.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace orion::stack {
namespace {

/// The reference's metadata, for the container.
struct Meta {
    std::uint32_t width = 0, height = 0;
    std::array<float, 4> camMul{};
    std::array<float, 9> camToXyz{};
    std::string camera;
    int flip = 0;
};

std::vector<float> rgbOf(const raw::LinearImage& linear) {
    std::vector<float> rgb(linear.pixelCount() * 3);
    for (std::size_t p = 0; p < linear.pixelCount(); ++p)
        for (std::size_t c = 0; c < 3; ++c) rgb[p * 3 + c] = util::halfToFloat(linear.rgba[p * 4 + c]);
    return rgb;
}

/// The master dark and flat gain, in the samples of the kind of frame they were built from: a Bayer
/// raw's black-subtracted mosaic, or a linear DNG's RGB. Empty when no darks (flats) were given.
/// Sizes are the sensor's (decoded, before any turn): a transposed frame has the same sample count.
struct Masters {
    std::vector<float> dark, gain;
    int bayer = -1;   // -1: none built; else whether they are mosaics
    std::uint32_t width = 0, height = 0;

    /// A mosaic's clipped samples are kept by storeMosaic; a linear DNG's clip is 1.
    void apply(std::vector<float>& samples, bool isBayer, std::uint32_t w, std::uint32_t h) const {
        if (bayer < 0) return;
        if (bayer != int(isBayer))
            throw std::runtime_error("the dark and flat frames are not the same kind of raw as the lights");
        if (w != width || h != height) throw std::runtime_error("the dark and flat frames do not match the lights' size");
        calibrate(samples.data(), samples.size(), dark, gain, isBayer ? std::numeric_limits<float>::infinity() : 1.0f);
    }
};

/// One calibration frame as samples (see Masters), and each sample's channel when asked.
std::vector<float> samplesOf(const std::string& path, Masters& m, std::vector<std::uint8_t>* channel) {
    auto decoded = raw::decode(path);
    const auto* bayer = std::get_if<raw::BayerImage>(&decoded);
    const auto* linear = std::get_if<raw::LinearImage>(&decoded);
    const std::uint32_t w = bayer ? bayer->width : linear->width, h = bayer ? bayer->height : linear->height;
    if (m.bayer >= 0 && m.bayer != int(bayer != nullptr))
        throw std::runtime_error("the dark and flat frames mix mosaic raws and linear DNGs");
    if (m.bayer >= 0 && (w != m.width || h != m.height))
        throw std::runtime_error("the dark and flat frames are not all the same size");
    m.bayer = bayer != nullptr;
    m.width = w;
    m.height = h;
    if (bayer) {
        if (channel) *channel = channelsOf(*bayer);
        return mosaicOf(*bayer);
    }
    auto rgb = rgbOf(*linear);
    if (channel) {
        channel->resize(rgb.size());
        for (std::size_t i = 0; i < rgb.size(); ++i) (*channel)[i] = std::uint8_t(i % 3);
    }
    return rgb;
}

/// One file to own-normalized float RGB (1.0 = clip), camera-native, calibrated by the masters
/// (a mosaic before demosaic); the decoded image dies on return.
std::vector<float> decodeFrame(const std::string& path, merge::MergeRender& render, const Masters& masters, Meta& meta) {
    auto decoded = raw::decode(path);
    if (auto* bayer = std::get_if<raw::BayerImage>(&decoded)) {
        meta = {bayer->width, bayer->height, bayer->camMul, bayer->camToXyz, bayer->camera, bayer->flip};
        if (masters.bayer >= 0) {
            auto mosaic = mosaicOf(*bayer);
            masters.apply(mosaic, true, bayer->width, bayer->height);
            storeMosaic(*bayer, mosaic);
        }
        return render.demosaic(*bayer);
    }
    // A linear DNG stacks too (and is what makes the stack testable from synthetic files).
    const auto& linear = std::get<raw::LinearImage>(decoded);
    meta = {linear.width, linear.height, linear.camMul, linear.camToXyz, linear.camera, linear.flip};
    auto rgb = rgbOf(linear);
    masters.apply(rgb, false, linear.width, linear.height);
    return rgb;
}

std::vector<std::uint16_t> toHalf(const std::vector<float>& rgb) {
    std::vector<std::uint16_t> half(rgb.size());
    for (std::size_t p = 0; p < rgb.size(); ++p) half[p] = util::floatToHalf(rgb[p]);
    return half;
}

}  // namespace

Stacker::Stacker(gpu::Device& device, std::string shaderDir)
    : device_(device), shaderDir_(std::move(shaderDir)) {}

void Stacker::step(float progress) {
    if (cancelled_.load(std::memory_order_relaxed)) throw std::runtime_error("stack cancelled");
    progress_.store(progress, std::memory_order_relaxed);
}

void Stacker::run(const Request& rq) {
    cancelled_.store(false, std::memory_order_relaxed);
    progress_.store(0.0f, std::memory_order_relaxed);
    skipped_.store(0, std::memory_order_relaxed);

    const std::size_t n = rq.paths.size();
    if (n < 2) throw std::runtime_error("a stack needs at least two frames");
    if (rq.reference < 0 || std::size_t(rq.reference) >= n) throw std::runtime_error("reference index out of range");
    if (std::filesystem::exists(util::utf8Path(rq.output)))
        throw std::runtime_error("output already exists: " + rq.output);
    const bool landscape = rq.mode == Mode::Landscape;
    if (landscape && (rq.skyMatte.empty() || rq.matteWidth <= 0 || rq.matteHeight <= 0 ||
                      rq.skyMatte.size() != std::size_t(rq.matteWidth) * std::size_t(rq.matteHeight)))
        throw std::runtime_error("a landscape stack needs a sky matte");
    // Refuse a mixed set before paying for any decode (metadata only); the decode loop re-checks size.
    const auto ref = std::size_t(rq.reference);
    const raw::RawInfo refInfo = raw::readInfo(rq.paths[ref]);
    const auto named = [](const std::string& c) { return c.find_first_not_of(' ') != std::string::npos; };
    const auto otherCamera = [&](const raw::RawInfo& info) {
        return named(info.camera) && named(refInfo.camera) && info.camera != refInfo.camera;
    };
    for (const auto& path : rq.paths) {
        const raw::RawInfo info = raw::readInfo(path);
        if (info.width != refInfo.width || info.height != refInfo.height)
            throw std::runtime_error("the frames are different sizes — a stack comes from one camera");
        if (otherCamera(info))
            throw std::runtime_error("the frames come from different cameras: " + refInfo.camera + ", " + info.camera);
    }
    // readInfo's size is as displayed, and a dark shot with the camera turned is the same sensor: either way round.
    // (Calibration is in sensor space, before any turn; the masters re-check the decoded sensor size.)
    for (const auto* list : {&rq.darks, &rq.flats}) {
        const std::string what = list == &rq.darks ? "dark" : "flat";
        for (const auto& path : *list) {
            const raw::RawInfo info = raw::readInfo(path);
            if (!(info.width == refInfo.width && info.height == refInfo.height) &&
                !(info.width == refInfo.height && info.height == refInfo.width))
                throw std::runtime_error("the " + what + " frames do not match the lights' size");
            if (otherCamera(info)) throw std::runtime_error("the " + what + " frames do not match the lights' camera");
        }
    }

    // ── The masters, progress 0–0.05; each list is held whole only while its median needs it ──
    Masters masters;
    const std::size_t calTotal = rq.darks.size() + rq.flats.size();
    std::size_t calDone = 0;
    std::vector<std::uint8_t> channel;   // the flat's, for its per-channel mean
    const auto median = [&](const std::vector<std::string>& paths, bool flat) {
        std::vector<std::vector<float>> frames;
        for (const auto& path : paths) {
            step(0.05f * float(calDone++) / float(calTotal));
            frames.push_back(samplesOf(path, masters, flat && frames.empty() ? &channel : nullptr));
        }
        return medianOf(frames);
    };
    masters.dark = median(rq.darks, false);
    if (!rq.flats.empty()) masters.gain = flatGain(median(rq.flats, true), masters.dark, channel);

    // ── Decode and register, one frame at a time, the reference first ─────
    merge::MergeRender render(device_, shaderDir_);
    Meta refMeta;
    std::vector<float> refFloat;     // Noise mode: kept until every frame is aligned
    std::vector<Star> refStars;      // Stars and Landscape
    std::vector<float> matte;        // Landscape: the sky on the sensor frame
    std::vector<std::vector<std::uint16_t>> planes;
    std::vector<Frame> frames;

    for (std::size_t k = 0; k < n; ++k) {
        step(0.05f + 0.70f * float(k) / float(n));
        const std::size_t i = k == 0 ? ref : (k <= ref ? k - 1 : k);   // the reference, then the rest in order
        Meta meta;
        std::vector<float> rgb = decodeFrame(rq.paths[i], render, masters, meta);
        if (k == 0) {
            refMeta = meta;
        } else if (meta.width != refMeta.width || meta.height != refMeta.height) {
            throw std::runtime_error("the frames are different sizes — a stack comes from one camera");
        }
        const std::uint32_t W = meta.width, H = meta.height;

        Frame f{W, H, nullptr, persp::identity(), persp::identity()};
        if (k == 0) {
            if (landscape) matte = skyMatteToFrame(rq.skyMatte.data(), rq.matteWidth, rq.matteHeight, W, H, meta.flip);
            if (rq.mode == Mode::Noise) refFloat = std::move(rgb);
            else refStars = detectStars(rgb.data(), W, H, landscape ? matte.data() : nullptr);
        } else {
            merge::AlignResult a;
            if (rq.mode == Mode::Noise) {
                merge::Frame mf, mr;
                mf.width = mr.width = W;
                mf.height = mr.height = H;
                mf.rgb = rgb.data();
                mr.rgb = refFloat.data();
                a = merge::align(mf, mr);
            } else {
                a = alignStars(detectStars(rgb.data(), W, H, landscape ? matte.data() : nullptr), refStars);
            }
            if (!a.ok) {
                std::fprintf(stderr, "orion stack: skipped %s: did not register (%d inliers)\n",
                             rq.paths[i].c_str(), a.inliers);
                skipped_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            f.refToSource = a.refToSource;   // the ground (Landscape) stays identity: the camera did not move
        }

        // Halves only for a frame that registered; the Noise reference's float now lives in refFloat.
        planes.push_back(toHalf(k == 0 && rq.mode == Mode::Noise ? refFloat : rgb));
        frames.push_back(f);
    }
    refFloat = {};
    if (frames.size() < 2) throw std::runtime_error("no frame registered against the reference — nothing to stack");
    for (std::size_t j = 0; j < frames.size(); ++j) frames[j].rgb = planes[j].data();

    step(0.8f);
    const Result result = integrate(frames, 0, landscape ? matte.data() : nullptr, rq.options);
    frames = {};   // they point into planes
    planes = {};

    // ── The container, as HdrMerge's ──────────────────────────────────────
    step(0.95f);
    util::DngLinearImage out;
    out.width = refMeta.width;
    out.height = refMeta.height;
    out.rgb = result.rgb.data();
    out.xyzToCam = refMeta.camToXyz;
    // AsShotNeutral: the reciprocals of the gains, normalized so green is 1.
    const float g = refMeta.camMul[1] != 0.0f ? refMeta.camMul[1] : 1.0f;
    for (std::size_t c = 0; c < 3; ++c)
        out.asShotNeutral[c] = refMeta.camMul[c] != 0.0f ? g / refMeta.camMul[c] : 1.0f;
    out.baselineExposureEv = 0.0f;   // a stack is normalized to the reference: no headroom bought
    out.camera = refMeta.camera;
    out.flip = refMeta.flip;

    // Allowed to fail: a DNG without a preview is inconvenient, not wrong.
    const merge::Preview preview = merge::buildPreview(result.rgb, refMeta.width, refMeta.height, 0.0f,
                                                       refMeta.camMul, refMeta.camToXyz, refMeta.flip);
    if (!preview.jpeg.empty()) {
        out.previewJpeg = preview.jpeg.data();
        out.previewJpegBytes = preview.jpeg.size();
        out.previewWidth = preview.width;
        out.previewHeight = preview.height;
    }
    util::writeDngLinear(rq.output, out);
    progress_.store(1.0f, std::memory_order_relaxed);
}

}  // namespace orion::stack

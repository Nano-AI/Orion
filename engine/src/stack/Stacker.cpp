/*  Stacker — see the header for the contract and the policy decisions.
 *
 *  Memory shape, any frame count: one frame in flight — its decoded mosaic,
 *  the GPU demosaic pool (26 B/px), its float RGB (12), its halves (6),
 *  detectStars' transients (~13) — then, integrating, the result (12) and a
 *  band of every frame (128 MB plus the rows a warp reaches past it). On
 *  macOS a freed large block stays in malloc's cache and in the footprint
 *  (UNSOURCED §32), so the shapes are kept few and the same every frame.
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
#include "util/Memory.h"
#include "util/Utf8Path.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
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

/// What a band aims to hold across every frame, and its shortest height (#410, UNSOURCED §32): 128 MB is
/// small beside the in-flight frame and big enough that a band's reads are long and few.
constexpr std::uint64_t kBandBytes = 128ull << 20;
constexpr std::uint32_t kMinBandRows = 16;

/// The registered frames' half planes on disk, frame after frame, in the system temp directory; the
/// file goes with this object, on success, failure and cancel alike.
class Spill {
public:
    Spill(const std::filesystem::path& dir, std::uint32_t width, std::uint32_t height)
        : row_(std::uint64_t(width) * 3), values_(row_ * height) {
        std::random_device rd;
        char name[64];
        std::snprintf(name, sizeof name, "orion-stack-%08x%08x.tmp", rd(), rd());
        path_ = dir / name;
        file_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        if (!file_) throw std::runtime_error("could not create the stack's temporary file in " + util::utf8String(dir));
#ifndef _WIN32
        // Unlinked while open: the system reclaims it even if the process dies. (Windows cannot; the destructor does.)
        std::error_code ec;
        std::filesystem::remove(path_, ec);
#endif
    }
    Spill(const Spill&) = delete;
    Spill& operator=(const Spill&) = delete;
    ~Spill() {
        file_.close();
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    void write(std::size_t frame, const std::vector<std::uint16_t>& halves) {
        file_.seekp(std::streamoff(frame * values_ * 2));
        file_.write(reinterpret_cast<const char*>(halves.data()), std::streamsize(halves.size() * 2));
        if (!file_) throw std::runtime_error("could not write the stack's temporary file (is the disk full?)");
    }

    /// `rows` rows of `frame` from `firstRow`, width*3 halves a row, into dst.
    void read(std::size_t frame, std::uint32_t firstRow, std::uint32_t rows, std::uint16_t* dst) {
        file_.seekg(std::streamoff((frame * values_ + firstRow * row_) * 2));
        file_.read(reinterpret_cast<char*>(dst), std::streamsize(rows * row_ * 2));
        if (!file_) throw std::runtime_error("could not read back the stack's temporary file");
    }

private:
    std::filesystem::path path_;
    std::fstream file_;
    std::uint64_t row_, values_;   // halves a row, a frame
};

}  // namespace

std::uint64_t estimateBytes(std::uint64_t pixels, std::size_t lights, std::size_t darks, std::size_t flats) noexcept {
    // Bytes a pixel, measured (research/UNSOURCED.md §32): the stack's own peak footprint, frames streamed
    // (#410), is flat in the frame count: 54.7 at 42 MP noise (2 to 19 frames alike), 67 stars, and 62.2
    // and 70.6 on 24 MP links, whose daylight frames the memory compressor cannot shrink. 70.6 x 1.1 = 78.
    // A light adds the rows its warp reaches past each band, 0.1 (64 rows of a 4000-row frame, 6 B each).
    // Calibration frames: 4 each of the longer list while its median is taken, and the two masters (from
    // the code, unmeasured; a linear DNG's RGB is 12).
    constexpr std::uint64_t kFixed = 78, kCalibration = 4;
    const std::uint64_t calibration = darks + flats > 0 ? kCalibration * (std::max(darks, flats) + 2) : 0;
    return pixels * (kFixed + calibration) + pixels * lights / 10;
}

std::uint64_t spillBytes(std::uint64_t pixels, std::size_t lights) noexcept { return pixels * 6 * lights; }

std::uint64_t estimateBytes(const std::string& light, std::size_t lights, std::size_t darks, std::size_t flats) {
    const raw::RawInfo info = raw::readInfo(light);
    return estimateBytes(std::uint64_t(info.width) * info.height, lights, darks, flats);
}

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
    // Before any other read: what this run needs against what is available now (0 = the OS will not say).
    if (!rq.force) {
        const std::uint64_t need = estimateBytes(std::uint64_t(refInfo.width) * refInfo.height, n, rq.darks.size(),
                                                 rq.flats.size());
        const std::uint64_t have = rq.availableBytes ? rq.availableBytes : util::availableMemory();
        if (have > 0 && need > have) {
            char sentence[200];
            std::snprintf(sentence, sizeof sentence,
                          "These %zu frames need about %.1f GB of memory and about %.1f GB is free. "
                          "Close other apps to free memory, or proceed anyway.",
                          n, double(need) / 1e9, double(have) / 1e9);
            throw NotEnoughMemory(sentence);
        }
    }
    // And the disk the frames spill to, which forcing does not conjure up.
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    {
        const std::uint64_t need = spillBytes(std::uint64_t(refInfo.width) * refInfo.height, n);
        std::error_code ec;
        const std::uint64_t have = rq.availableDisk ? rq.availableDisk : std::filesystem::space(tempDir, ec).available;
        if (!ec && need > have) {
            char sentence[300];
            std::snprintf(sentence, sizeof sentence,
                          "These %zu frames need about %.1f GB of temporary disk space and about %.1f GB is free on "
                          "the disk that holds %s. Free some space there, or stack fewer frames.",
                          n, double(need) / 1e9, double(have) / 1e9, util::utf8String(tempDir).c_str());
            throw std::runtime_error(sentence);
        }
    }
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
    // Each registered frame is measured against the reference while its halves are in memory, then
    // spilled to disk; only one frame is ever held whole. Scoped to the loop: the GPU demosaic pool
    // (26 B/px) is freed before integration (on Apple silicon it is the memory availableMemory reports).
    std::optional<Spill> spill;      // made once the first frame's sensor size is known
    std::optional<merge::MergeRender> render;
    render.emplace(device_, shaderDir_);
    Meta refMeta;
    merge::RefProxy refProxy;        // Noise mode: the reference as the aligner sees it, not its full float
    std::vector<Star> refStars;      // Stars and Landscape
    std::vector<float> matte;        // Landscape: the sky on the sensor frame
    std::vector<std::uint16_t> halves;   // the frame in flight, one buffer for all of them
    Reference refStats;
    std::vector<Norm> norms;
    std::vector<Frame> frames;       // their warps; the pixels are in the spill

    for (std::size_t k = 0; k < n; ++k) {
        step(0.05f + 0.70f * float(k) / float(n));
        const std::size_t i = k == 0 ? ref : (k <= ref ? k - 1 : k);   // the reference, then the rest in order
        Meta meta;
        std::vector<float> rgb = decodeFrame(rq.paths[i], *render, masters, meta);
        if (k == 0) {
            refMeta = meta;
        } else if (meta.width != refMeta.width || meta.height != refMeta.height) {
            throw std::runtime_error("the frames are different sizes — a stack comes from one camera");
        }
        const std::uint32_t W = meta.width, H = meta.height;
        merge::Frame mf;   // the aligner's view of this frame
        mf.width = W;
        mf.height = H;
        mf.rgb = rgb.data();

        Frame f{W, H, nullptr, persp::identity(), persp::identity()};
        if (k == 0) {
            spill.emplace(tempDir, W, H);
            if (landscape) matte = skyMatteToFrame(rq.skyMatte.data(), rq.matteWidth, rq.matteHeight, W, H, meta.flip);
            if (rq.mode == Mode::Noise) refProxy = merge::referenceProxy(mf);
            else refStars = detectStars(rgb.data(), W, H, landscape ? matte.data() : nullptr);
        } else {
            const merge::AlignResult a =
                rq.mode == Mode::Noise
                    ? merge::align(mf, refProxy)
                    : alignStars(detectStars(rgb.data(), W, H, landscape ? matte.data() : nullptr), refStars, W, H);
            if (!a.ok) {
                std::fprintf(stderr, "orion stack: skipped %s: did not register (%d inliers)\n",
                             rq.paths[i].c_str(), a.inliers);
                skipped_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            f.refToSource = a.refToSource;   // the ground (Landscape) stays identity: the camera did not move
        }

        // Halves only for a frame that registered: measured, spilled, and the buffer reused.
        halves.resize(rgb.size());
        for (std::size_t p = 0; p < rgb.size(); ++p) halves[p] = util::floatToHalf(rgb[p]);
        f.rgb = halves.data();
        if (k == 0) refStats = measureReference(f, landscape ? matte.data() : nullptr);
        norms.push_back(measureFrame(f, refStats, rq.options.weightByNoise));
        spill->write(frames.size(), halves);
        f.rgb = nullptr;
        frames.push_back(f);
    }
    refProxy = {};
    halves = {};
    render.reset();
    if (frames.size() < 2) throw std::runtime_error("no frame registered against the reference — nothing to stack");
    const Rejection used = rq.options.rejection == Rejection::Auto ? autoRejection(int(frames.size())) : rq.options.rejection;
    if (rq.options.weightByNoise) capWeights(norms);

    // ── Integrate in bands of rows, each frame read for just the rows its warp reaches ──
    const std::uint32_t W = refMeta.width, H = refMeta.height;
    const std::size_t m = frames.size();
    const auto reads = [&](const Frame& f, std::uint32_t y0, std::uint32_t y1) {
        Rows r = rowsRead(f.refToSource, W, H, y0, y1);
        if (landscape) {
            const Rows g = rowsRead(f.groundToSource, W, H, y0, y1);
            r = {std::min(r.first, g.first), std::max(r.last, g.last)};
        }
        return r;
    };
    // The rows a frame holds for bands of `rows`: its widest band's, the band plus the warp's reach past it.
    const auto mostRows = [&](const Frame& f, std::uint32_t rows) {
        std::uint32_t most = 0;
        for (std::uint32_t y0 = 0; y0 < H; y0 += rows) {
            const Rows r = reads(f, y0, std::min(H, y0 + rows));
            most = std::max(most, r.last - r.first);
        }
        return most;
    };
    const auto heldBytes = [&](std::uint32_t rows) {
        std::uint64_t bytes = 0;
        for (const Frame& f : frames) bytes += std::uint64_t(mostRows(f, rows)) * W * 6;
        return bytes;
    };
    // The tallest band (16 rows doubled) whose holdings fit the band budget, or twice what the shortest band
    // holds when a turned field's reach dominates: taller bands then re-read each row under twice, for at most
    // twice the memory, where short ones would re-read the reach once a band.
    std::uint32_t bandRows = std::min(kMinBandRows, H);
    const std::uint64_t budget = std::max(rq.bandBytes ? rq.bandBytes : kBandBytes, 2 * heldBytes(bandRows));
    while (bandRows < H && heldBytes(std::min(H, bandRows * 2)) <= budget) bandRows = std::min(H, bandRows * 2);
    // The estimate could not know the warps: a field turned far enough holds more than it allowed for.
    const std::uint64_t holding = heldBytes(bandRows);
    if (!rq.force && holding > kBandBytes) {
        const std::uint64_t have = rq.availableBytes ? rq.availableBytes : util::availableMemory();
        const std::uint64_t need = holding + std::uint64_t(W) * H * 12;   // and the result
        if (have > 0 && need > have) {
            char sentence[200];
            std::snprintf(sentence, sizeof sentence,
                          "These %zu frames need about %.1f GB of memory and about %.1f GB is free. "
                          "Close other apps to free memory, or proceed anyway.",
                          n, double(need) / 1e9, double(have) / 1e9);
            throw NotEnoughMemory(sentence);
        }
    }
    // One buffer a frame, sized for its widest band, so the band loop allocates nothing.
    std::vector<std::vector<std::uint16_t>> held(m);
    for (std::size_t j = 0; j < m; ++j) held[j].resize(std::size_t(mostRows(frames[j], bandRows)) * W * 3);
    std::vector<float> out(std::size_t(W) * H * 3);
    for (std::uint32_t y0 = 0; y0 < H; y0 += bandRows) {
        step(0.8f + 0.15f * float(y0) / float(H));
        const std::uint32_t y1 = std::min(H, y0 + bandRows);
        for (std::size_t j = 0; j < m; ++j) {
            const Rows r = reads(frames[j], y0, y1);
            spill->read(j, r.first, r.last - r.first, held[j].data());
            frames[j].firstRow = r.first;
            frames[j].rgb = held[j].data();
        }
        integrateRows(frames, 0, landscape ? matte.data() : nullptr, refStats, norms, used, y0, y1,
                      out.data() + std::size_t(y0) * W * 3);
    }
    frames = {};   // they point into held
    held = {};
    spill.reset();   // its disk back before the DNG is written

    // ── The container, as HdrMerge's ──────────────────────────────────────
    step(0.95f);
    util::DngLinearImage dng;
    dng.width = refMeta.width;
    dng.height = refMeta.height;
    dng.rgb = out.data();
    dng.xyzToCam = refMeta.camToXyz;
    // AsShotNeutral: the reciprocals of the gains, normalized so green is 1.
    const float g = refMeta.camMul[1] != 0.0f ? refMeta.camMul[1] : 1.0f;
    for (std::size_t c = 0; c < 3; ++c)
        dng.asShotNeutral[c] = refMeta.camMul[c] != 0.0f ? g / refMeta.camMul[c] : 1.0f;
    dng.baselineExposureEv = 0.0f;   // a stack is normalized to the reference: no headroom bought
    dng.camera = refMeta.camera;
    dng.flip = refMeta.flip;

    // Allowed to fail: a DNG without a preview is inconvenient, not wrong.
    const merge::Preview preview = merge::buildPreview(out, refMeta.width, refMeta.height, 0.0f,
                                                       refMeta.camMul, refMeta.camToXyz, refMeta.flip);
    if (!preview.jpeg.empty()) {
        dng.previewJpeg = preview.jpeg.data();
        dng.previewJpegBytes = preview.jpeg.size();
        dng.previewWidth = preview.width;
        dng.previewHeight = preview.height;
    }
    util::writeDngLinear(rq.output, dng);
    progress_.store(1.0f, std::memory_order_relaxed);
}

}  // namespace orion::stack

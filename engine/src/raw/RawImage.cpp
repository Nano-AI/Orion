#include "raw/RawImage.h"

#include "util/Half.h"

#include <libraw/libraw.h>

#include <cmath>

#include <memory>
#include <stdexcept>
#include <string>

namespace orion::raw {
namespace {

[[noreturn]] void fail(const char* what, int code) {
    throw std::runtime_error(std::string(what) + ": " + libraw_strerror(code));
}

char channelLetter(Channel c) {
    switch (c) {
        case Channel::R:  return 'R';
        case Channel::G:  return 'G';
        case Channel::B:  return 'B';
        case Channel::G2: return 'G';
    }
    return '?';
}

}  // namespace

std::string BayerImage::patternString() const {
    return {channelLetter(channelAt(0, 0)), channelLetter(channelAt(1, 0)),
            channelLetter(channelAt(0, 1)), channelLetter(channelAt(1, 1))};
}

RawInfo readInfo(const std::string& path) {
    // Heap, not stack. A LibRaw instance carries the whole imgdata structure —
    // hundreds of kilobytes — and this runs on Swift's cooperative thread pool
    // where each stack is 544K. On the stack it overflows before doing any work.
    auto owned = std::make_unique<LibRaw>();
    LibRaw& proc = *owned;

    if (proc.open_file(path.c_str()) != LIBRAW_SUCCESS) {
        throw std::runtime_error("could not open " + path);
    }

    const auto& sizes = proc.imgdata.sizes;
    const auto& idata = proc.imgdata.idata;
    const auto& other = proc.imgdata.other;

    // A quarter turn means the frame is displayed with width and height
    // swapped, and a browser wants the displayed shape.
    const bool swaps = (sizes.flip == 5 || sizes.flip == 6);

    RawInfo info;
    info.width       = swaps ? sizes.height : sizes.width;
    info.height      = swaps ? sizes.width  : sizes.height;
    info.camera      = std::string(idata.make) + " " + idata.model;
    info.lens        = proc.imgdata.lens.Lens;
    info.isoSpeed    = other.iso_speed;
    info.shutter     = other.shutter;
    info.aperture    = other.aperture;
    info.focalLength = other.focal_len;
    info.timestamp   = static_cast<std::int64_t>(other.timestamp);

    proc.recycle();
    return info;
}

int quarterTurnsFor(int flip) noexcept {
    switch (flip) {
        case 3: return 2;   // 180
        case 5: return 3;   // 90 anticlockwise
        case 6: return 1;   // 90 clockwise
        default: return 0;
    }
}

Thumbnail extractThumbnail(const std::string& path) {
    // Heap for the same reason as readInfo: this runs off the main thread.
    auto owned = std::make_unique<LibRaw>();
    LibRaw& proc = *owned;

    if (proc.open_file(path.c_str()) != LIBRAW_SUCCESS) return {};
    if (proc.unpack_thumb() != LIBRAW_SUCCESS) { proc.recycle(); return {}; }

    const auto& t = proc.imgdata.thumbnail;

    // Only the JPEG form is useful directly; the bitmap forms would need
    // wrapping in a container before anything could display them.
    if (t.tformat != LIBRAW_THUMBNAIL_JPEG || t.thumb == nullptr || t.tlength == 0) {
        proc.recycle();
        return {};
    }

    // `sizes.flip`, not anything on the thumbnail struct: `libraw_thumbnail_t`
    // has no orientation of its own, and the preview stream carries none
    // either (see the header). Read before recycle(), which clears it.
    Thumbnail out;
    out.jpeg.assign(t.thumb, t.thumb + t.tlength);
    out.flip = proc.imgdata.sizes.flip;
    proc.recycle();
    return out;
}

std::array<std::uint16_t, 4> BayerImage::blackLevels(unsigned black,
                                                     const unsigned* cblack,
                                                     std::uint32_t filters) noexcept {
    std::array<std::uint16_t, 4> out{};

    const unsigned rows = cblack[4], cols = cblack[5];
    const unsigned cells = rows * cols;

    if (rows == 2 && cols == 2) {
        // Each pattern cell belongs to whichever CFA channel sits at that
        // position, which is what makes this exact rather than an average.
        BayerImage probe;
        probe.filters = filters;
        unsigned value[4] = {0, 0, 0, 0};
        bool seen[4] = {false, false, false, false};
        for (unsigned y = 0; y < 2; ++y) {
            for (unsigned x = 0; x < 2; ++x) {
                const int c = static_cast<int>(probe.channelAt(x, y));
                // Two greens share channel 1 in LibRaw's mask unless the file
                // distinguishes them; taking the first is what FC would give.
                if (!seen[c]) { value[c] = cblack[6 + y * 2 + x]; seen[c] = true; }
            }
        }
        for (int c = 0; c < 4; ++c) {
            const unsigned offset = seen[c] ? value[c] : (seen[1] ? value[1] : 0u);
            out[c] = static_cast<std::uint16_t>(black + cblack[c] + offset);
        }
        return out;
    }

    unsigned mean = 0;
    if (cells > 0) {
        for (unsigned i = 0; i < cells; ++i) mean += cblack[6 + i];
        mean /= cells;
    }
    for (int c = 0; c < 4; ++c) {
        out[c] = static_cast<std::uint16_t>(black + cblack[c] + mean);
    }
    return out;
}

namespace {

/// Open + unpack, shared by both decoders.
///
/// ⚠ The flag matters: LibRaw's default is to flatten floating-point data to
/// 16-bit integers at unpack, which would quietly throw away the shadow
/// precision a merged HDR DNG exists to carry. Measured in tests_dng.cpp —
/// with the default options float3_image stays null and color3_image gets the
/// quantized copy. Harmless for integer sources, so it is cleared always.
void openUnpacked(LibRaw& proc, const std::string& path) {
    if (int rc = proc.open_file(path.c_str()); rc != LIBRAW_SUCCESS) {
        fail("could not open raw file", rc);
    }
    proc.imgdata.rawparams.options &=
        ~static_cast<unsigned>(LIBRAW_RAWOPTIONS_CONVERTFLOAT_TO_INT);
    if (int rc = proc.unpack(); rc != LIBRAW_SUCCESS) {
        fail("could not unpack raw data", rc);
    }
}

/// The mosaic path: everything decodeBayer always did, from the format checks
/// down. Assumes raw_image is present.
BayerImage buildBayer(LibRaw& proc) {
    const auto& sizes = proc.imgdata.sizes;
    const auto& idata = proc.imgdata.idata;
    const auto& color = proc.imgdata.color;

    // ── What this decoder can and cannot take ──────────────────────────────
    //
    // Each of these produces a *specific* message rather than a wrong picture.
    // The demosaic assumes a 2x2 mosaic of three colors; every case below
    // breaks that assumption in a different way, and two of them would render
    // silently — a scrambled X-Trans frame and a four-color sensor's cast both
    // look like a bug in the pipeline rather than an unsupported file.
    if (idata.filters == 9) {
        proc.recycle();
        throw std::runtime_error(
            "X-Trans sensors use a 6x6 mosaic, and Orion's demosaic is Bayer "
            "only. Fujifilm X-Trans support needs the Markesteijn algorithm — "
            "it is on the roadmap, not in this build");
    }
    if (idata.filters == 0) {
        proc.recycle();
        throw std::runtime_error("this file has no color filter array");
    }
    // cdesc names the four mosaic positions. Anything but RGB-with-two-greens
    // (RGBE on some early bodies, CMYG on older camcorder sensors) needs a
    // different matrix path as well as a different demosaic.
    if (const std::string desc(idata.cdesc);
        desc.find('E') != std::string::npos || desc.find('C') != std::string::npos ||
        desc.find('M') != std::string::npos || desc.find('Y') != std::string::npos) {
        proc.recycle();
        throw std::runtime_error(
            "this sensor's filter array is " + desc +
            ", not RGB. Orion supports three-color Bayer sensors");
    }

    BayerImage out;
    out.width  = sizes.width;
    out.height = sizes.height;
    out.camera = std::string(idata.make) + " " + idata.model;
    out.flip   = sizes.flip;

    // Copy the visible area out of the padded sensor buffer, dropping the
    // masked border LibRaw keeps for black-level estimation.
    out.samples.resize(out.pixelCount());
    const std::uint16_t* src = proc.imgdata.rawdata.raw_image;
    for (std::uint32_t y = 0; y < out.height; ++y) {
        const std::uint16_t* row =
            src + static_cast<std::size_t>(y + sizes.top_margin) * sizes.raw_width
                + sizes.left_margin;
        std::copy_n(row, out.width, out.samples.begin() +
                    static_cast<std::size_t>(y) * out.width);
    }

    // 'filters' is defined against the raw buffer origin; shift it so it is
    // correct against our cropped visible-area origin instead.
    out.filters = idata.filters;
    if (sizes.left_margin & 1) out.filters = (out.filters >> 2) | (out.filters << 30);
    if (sizes.top_margin & 1)  out.filters = (out.filters >> 8) | (out.filters << 24);

    out.black = BayerImage::blackLevels(color.black, color.cblack, out.filters);
    out.white = static_cast<std::uint16_t>(color.maximum);

    for (int c = 0; c < 4; ++c) out.camMul[c] = color.cam_mul[c];

    // LibRaw stores cam_xyz as XYZ->camera; we want camera->XYZ, so transpose
    // is not enough — but for the 3x3 case rgb_cam already carries the useful
    // camera->sRGB form. Keep cam_xyz here and invert in the color node.
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out.camToXyz[r * 3 + c] = color.cam_xyz[r][c];
    }

    proc.recycle();
    return out;
}

/// The linear path: a demosaiced three-channel DNG, floating-point or
/// integer. Assumes one of float3_image / color3_image is present.
LinearImage buildLinear(LibRaw& proc) {
    const auto& sizes = proc.imgdata.sizes;
    const auto& idata = proc.imgdata.idata;
    const auto& color = proc.imgdata.color;
    const auto& rd    = proc.imgdata.rawdata;

    LinearImage out;
    out.width  = sizes.width;
    out.height = sizes.height;
    out.camera = std::string(idata.make) + " " + idata.model;
    out.flip   = sizes.flip;

    const float baseline = color.dng_levels.baseline_exposure;
    out.baselineExposureEv = std::isfinite(baseline) ? baseline : 0.0f;

    for (int c = 0; c < 4; ++c) out.camMul[c] = color.cam_mul[c];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out.camToXyz[r * 3 + c] = color.cam_xyz[r][c];
    }

    // Copy the visible area, converting to the fp16 RGBA layout an
    // RGBA16Float texture uploads directly. Alpha is a constant 1.0.
    constexpr std::uint16_t kHalfOne = 0x3c00;
    out.rgba.resize(out.pixelCount() * 4);

    // Integer linear data is normalized against the file's own white level so
    // both kinds mean the same thing downstream: 1.0 at the ceiling.
    const float invMax =
        1.0f / static_cast<float>(color.maximum > 0 ? color.maximum : 65535);

    for (std::uint32_t y = 0; y < out.height; ++y) {
        const std::size_t srcRow =
            (static_cast<std::size_t>(y) + sizes.top_margin) * sizes.raw_width
            + sizes.left_margin;
        std::uint16_t* dst = out.rgba.data() + static_cast<std::size_t>(y) * out.width * 4;
        if (rd.float3_image != nullptr) {
            for (std::uint32_t x = 0; x < out.width; ++x) {
                const float* px = rd.float3_image[srcRow + x];
                dst[x * 4 + 0] = util::floatToHalf(px[0]);
                dst[x * 4 + 1] = util::floatToHalf(px[1]);
                dst[x * 4 + 2] = util::floatToHalf(px[2]);
                dst[x * 4 + 3] = kHalfOne;
            }
        } else {
            for (std::uint32_t x = 0; x < out.width; ++x) {
                const std::uint16_t* px = rd.color3_image[srcRow + x];
                dst[x * 4 + 0] = util::floatToHalf(static_cast<float>(px[0]) * invMax);
                dst[x * 4 + 1] = util::floatToHalf(static_cast<float>(px[1]) * invMax);
                dst[x * 4 + 2] = util::floatToHalf(static_cast<float>(px[2]) * invMax);
                dst[x * 4 + 3] = kHalfOne;
            }
        }
    }

    proc.recycle();
    return out;
}

}  // namespace

BayerImage decodeBayer(const std::string& path) {
    auto owned = std::make_unique<LibRaw>();
    LibRaw& proc = *owned;
    openUnpacked(proc, path);

    if (proc.imgdata.rawdata.raw_image == nullptr) {
        const bool demosaiced = proc.imgdata.rawdata.color3_image != nullptr ||
                                proc.imgdata.rawdata.color4_image != nullptr ||
                                proc.imgdata.rawdata.float3_image != nullptr;
        proc.recycle();
        if (demosaiced) {
            throw std::runtime_error(
                "this file is already demosaiced (a linear DNG or a Foveon "
                "frame). Orion develops mosaic data — open the camera's "
                "original raw file instead");
        }
        throw std::runtime_error("no mosaic data in this file");
    }
    return buildBayer(proc);
}

DecodedImage decode(const std::string& path) {
    auto owned = std::make_unique<LibRaw>();
    LibRaw& proc = *owned;
    openUnpacked(proc, path);

    const auto& rd = proc.imgdata.rawdata;
    if (rd.raw_image != nullptr) return buildBayer(proc);
    if (rd.float3_image != nullptr || rd.color3_image != nullptr) {
        return buildLinear(proc);
    }
    proc.recycle();
    throw std::runtime_error("no mosaic data in this file");
}

BayerImage decimate(const BayerImage& image, int scale) {
    if (scale < 2 || (scale % 2) != 0) return image;

    // Whole cells only. A mosaic whose width is not a multiple of the stride
    // loses its last partial cell rather than sampling across the edge, which
    // would take the phase with it.
    // One output *pixel* stands for `scale` input pixels per axis, so one
    // output *cell* — two pixels — stands for 2*scale input pixels, which is
    // `scale` input cells. Getting this ratio wrong is how the first version
    // produced a mosaic twice the intended size while every phase check still
    // passed.
    const int cell = scale;
    const std::uint32_t outCellsX = image.width  / std::uint32_t(2 * scale);
    const std::uint32_t outCellsY = image.height / std::uint32_t(2 * scale);
    if (outCellsX == 0 || outCellsY == 0) return image;

    BayerImage out = image;
    out.width  = outCellsX * 2;
    out.height = outCellsY * 2;
    out.samples.assign(std::size_t(out.width) * out.height, 0);

    for (std::uint32_t J = 0; J < outCellsY; ++J) {
        for (std::uint32_t I = 0; I < outCellsX; ++I) {
            // The four positions within a CFA cell, each averaged over the
            // cell x cell block of input cells this output cell stands for.
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    std::uint32_t sum = 0;
                    std::uint32_t n = 0;
                    for (int cy = 0; cy < cell; ++cy) {
                        for (int cx = 0; cx < cell; ++cx) {
                            const std::uint32_t sx =
                                (I * std::uint32_t(cell) + std::uint32_t(cx)) * 2
                                + std::uint32_t(dx);
                            const std::uint32_t sy =
                                (J * std::uint32_t(cell) + std::uint32_t(cy)) * 2
                                + std::uint32_t(dy);
                            if (sx >= image.width || sy >= image.height) continue;
                            sum += image.samples[std::size_t(sy) * image.width + sx];
                            ++n;
                        }
                    }
                    const std::uint32_t ox = I * 2 + std::uint32_t(dx);
                    const std::uint32_t oy = J * 2 + std::uint32_t(dy);
                    out.samples[std::size_t(oy) * out.width + ox] =
                        static_cast<std::uint16_t>(n ? sum / n : 0);
                }
            }
        }
    }

    // ⚠ `filters` is deliberately carried across unchanged. The output starts
    // on the same cell boundary as the input and every sample keeps its phase,
    // so the pattern is the same pattern — recomputing it would be inventing a
    // second source of truth for something that did not change.
    return out;
}

LinearImage decimate(const LinearImage& image, int scale) {
    if (scale < 2 || (scale % 2) != 0) return image;

    const std::uint32_t outW = image.width  / std::uint32_t(scale);
    const std::uint32_t outH = image.height / std::uint32_t(scale);
    if (outW == 0 || outH == 0) return image;

    LinearImage out = image;
    out.width  = outW;
    out.height = outH;
    out.rgba.assign(out.pixelCount() * 4, 0);

    constexpr std::uint16_t kHalfOne = 0x3c00;
    const float inv = 1.0f / static_cast<float>(scale * scale);

    for (std::uint32_t oy = 0; oy < outH; ++oy) {
        for (std::uint32_t ox = 0; ox < outW; ++ox) {
            float sum[3] = {0.0f, 0.0f, 0.0f};
            for (int dy = 0; dy < scale; ++dy) {
                const std::size_t row =
                    (std::size_t(oy) * scale + std::size_t(dy)) * image.width;
                for (int dx = 0; dx < scale; ++dx) {
                    const std::uint16_t* px =
                        image.rgba.data() + (row + std::size_t(ox) * scale + std::size_t(dx)) * 4;
                    sum[0] += util::halfToFloat(px[0]);
                    sum[1] += util::halfToFloat(px[1]);
                    sum[2] += util::halfToFloat(px[2]);
                }
            }
            std::uint16_t* dst =
                out.rgba.data() + (std::size_t(oy) * outW + ox) * 4;
            dst[0] = util::floatToHalf(sum[0] * inv);
            dst[1] = util::floatToHalf(sum[1] * inv);
            dst[2] = util::floatToHalf(sum[2] * inv);
            dst[3] = kHalfOne;
        }
    }
    return out;
}

}  // namespace orion::raw

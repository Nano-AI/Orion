#include "raw/RawImage.h"

#include <libraw/libraw.h>

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
    LibRaw proc;
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

std::vector<std::uint8_t> extractThumbnail(const std::string& path) {
    LibRaw proc;
    if (proc.open_file(path.c_str()) != LIBRAW_SUCCESS) return {};
    if (proc.unpack_thumb() != LIBRAW_SUCCESS) { proc.recycle(); return {}; }

    const auto& t = proc.imgdata.thumbnail;

    // Only the JPEG form is useful directly; the bitmap forms would need
    // wrapping in a container before anything could display them.
    if (t.tformat != LIBRAW_THUMBNAIL_JPEG || t.thumb == nullptr || t.tlength == 0) {
        proc.recycle();
        return {};
    }

    std::vector<std::uint8_t> out(t.thumb, t.thumb + t.tlength);
    proc.recycle();
    return out;
}

BayerImage decodeBayer(const std::string& path) {
    LibRaw proc;

    if (int rc = proc.open_file(path.c_str()); rc != LIBRAW_SUCCESS) {
        fail("could not open raw file", rc);
    }
    if (int rc = proc.unpack(); rc != LIBRAW_SUCCESS) {
        fail("could not unpack raw data", rc);
    }

    const auto& sizes = proc.imgdata.sizes;
    const auto& idata = proc.imgdata.idata;
    const auto& color = proc.imgdata.color;

    if (proc.imgdata.rawdata.raw_image == nullptr) {
        throw std::runtime_error(
            "no Bayer mosaic in this file — Foveon or already-demosaiced sources "
            "are not supported");
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

    // Black level: a global offset plus a per-channel trim. LibRaw can also
    // carry a 2D pattern in cblack[4]x cblack[5]; averaging it is close enough
    // for M0 and avoids a per-pixel table. Revisit if banding shows up.
    unsigned patternMean = 0;
    if (const unsigned cells = color.cblack[4] * color.cblack[5]; cells > 0) {
        for (unsigned i = 0; i < cells; ++i) patternMean += color.cblack[6 + i];
        patternMean /= cells;
    }
    for (int c = 0; c < 4; ++c) {
        out.black[c] = static_cast<std::uint16_t>(color.black + color.cblack[c] + patternMean);
    }
    out.white = static_cast<std::uint16_t>(color.maximum);

    for (int c = 0; c < 4; ++c) out.camMul[c] = color.cam_mul[c];

    // LibRaw stores cam_xyz as XYZ->camera; we want camera->XYZ, so transpose
    // is not enough — but for the 3x3 case rgb_cam already carries the useful
    // camera->sRGB form. Keep cam_xyz here and invert in the colour node.
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out.camToXyz[r * 3 + c] = color.cam_xyz[r][c];
    }

    proc.recycle();
    return out;
}

}  // namespace orion::raw

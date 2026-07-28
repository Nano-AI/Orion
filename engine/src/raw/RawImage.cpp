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

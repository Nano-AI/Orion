/*  DNG writer — see DngWriter.h for what and why.
 *
 *  Sources, by section (full citations in research/hdr-merge.md):
 *    - TIFF 6.0 (Adobe, 1992): header, IFD layout, field types, the rule that
 *      entries sort ascending by tag and that a value wider than four bytes
 *      lives outside the IFD at an even offset (§2 "TIFF Structure").
 *    - DNG 1.4 (Adobe, 2012): LinearRaw photometric value (chapter 4,
 *      PhotometricInterpretation = 34892), DNGVersion/DNGBackwardVersion
 *      semantics (chapter 2), ColorMatrix1 as XYZ->camera under
 *      CalibrationIlluminant1 (chapter 6 "Mapping Camera Color Space to CIE
 *      XYZ Space"), AsShotNeutral (chapter 6), BaselineExposure (chapter 5),
 *      and floating-point sample support: 16-bit IEEE with
 *      SampleFormat = 3 requires readers at 1.4, hence both version tags say
 *      1.4.0.0 (chapter 3 "Floating Point Data").
 *
 *  Defaults deliberately relied on rather than written: BlackLevel (0) and
 *  WhiteLevel (1.0 for floating-point data, DNG 1.4 chapter 4) — the samples
 *  are written already normalized to that contract.
 */

#include "util/DngWriter.h"

#include "util/Half.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace orion::util {
namespace {

// TIFF 6.0 field types, §2.
constexpr std::uint16_t kByte      = 1;
constexpr std::uint16_t kAscii     = 2;
constexpr std::uint16_t kShort     = 3;
constexpr std::uint16_t kLong      = 4;
constexpr std::uint16_t kRational  = 5;
constexpr std::uint16_t kSRational = 10;

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, static_cast<std::uint16_t>(v & 0xffff));
    put16(out, static_cast<std::uint16_t>(v >> 16));
}

/// One IFD entry, payload held as its final little-endian bytes. Whether the
/// payload rides inline or in the external data area is decided at layout
/// time from its size alone.
struct Entry {
    std::uint16_t tag  = 0;
    std::uint16_t type = 0;
    std::uint32_t count = 0;
    std::vector<std::uint8_t> data;
};

Entry shortEntry(std::uint16_t tag, std::initializer_list<std::uint16_t> vs) {
    Entry e{tag, kShort, static_cast<std::uint32_t>(vs.size()), {}};
    for (auto v : vs) put16(e.data, v);
    return e;
}

Entry longEntry(std::uint16_t tag, std::uint32_t v) {
    Entry e{tag, kLong, 1, {}};
    put32(e.data, v);
    return e;
}

Entry byteEntry(std::uint16_t tag, std::initializer_list<std::uint8_t> vs) {
    Entry e{tag, kByte, static_cast<std::uint32_t>(vs.size()), {}};
    e.data.assign(vs);
    return e;
}

Entry asciiEntry(std::uint16_t tag, const std::string& s) {
    Entry e{tag, kAscii, static_cast<std::uint32_t>(s.size() + 1), {}};
    e.data.assign(s.begin(), s.end());
    e.data.push_back(0);
    return e;
}

/// den = 10000 matches the precision Adobe's own converter writes color
/// matrices at; the values arrive from LibRaw at four decimal places anyway.
Entry srationalEntry(std::uint16_t tag, const float* vs, std::size_t n,
                     std::int32_t den = 10000) {
    Entry e{tag, kSRational, static_cast<std::uint32_t>(n), {}};
    for (std::size_t i = 0; i < n; ++i) {
        const auto num =
            static_cast<std::int32_t>(std::lround(static_cast<double>(vs[i]) * den));
        put32(e.data, static_cast<std::uint32_t>(num));
        put32(e.data, static_cast<std::uint32_t>(den));
    }
    return e;
}

Entry rationalEntry(std::uint16_t tag, const float* vs, std::size_t n,
                    std::uint32_t den = 1000000) {
    Entry e{tag, kRational, static_cast<std::uint32_t>(n), {}};
    for (std::size_t i = 0; i < n; ++i) {
        const double v = std::max(0.0, static_cast<double>(vs[i]));
        put32(e.data, static_cast<std::uint32_t>(std::lround(v * den)));
        put32(e.data, den);
    }
    return e;
}

}  // namespace

void writeDngLinear(const std::string& path, const DngLinearImage& image) {
    if (image.rgb == nullptr || image.width == 0 || image.height == 0) {
        throw std::runtime_error("DNG write: no image data");
    }

    const std::size_t pixels = std::size_t(image.width) * image.height;
    const std::uint32_t stripBytes =
        static_cast<std::uint32_t>(pixels * 3 * sizeof(std::uint16_t));

    // Make and Model help LibRaw name the camera; UniqueCameraModel is the
    // DNG-blessed identity. Split "SONY ILCE-7RM3" on the first space.
    std::string make = image.camera, model;
    if (const auto space = image.camera.find(' '); space != std::string::npos) {
        make  = image.camera.substr(0, space);
        model = image.camera.substr(space + 1);
    }
    if (model.empty()) model = image.camera;

    // ── The IFD, entries ascending by tag (TIFF 6.0 §2 requires it) ────────
    std::vector<Entry> entries;
    entries.push_back(longEntry(254, 0));                   // NewSubfileType: full-resolution image
    entries.push_back(longEntry(256, image.width));         // ImageWidth
    entries.push_back(longEntry(257, image.height));        // ImageLength
    entries.push_back(shortEntry(258, {16, 16, 16}));       // BitsPerSample
    entries.push_back(shortEntry(259, {1}));                // Compression: none
    entries.push_back(shortEntry(262, {34892}));            // Photometric: LinearRaw
    entries.push_back(asciiEntry(271, make));               // Make
    entries.push_back(asciiEntry(272, model));              // Model
    entries.push_back(longEntry(273, 0));                   // StripOffsets — patched at layout
    entries.push_back(shortEntry(274, {1}));                // Orientation: as stored
    entries.push_back(shortEntry(277, {3}));                // SamplesPerPixel
    entries.push_back(longEntry(278, image.height));        // RowsPerStrip: one strip
    entries.push_back(longEntry(279, stripBytes));          // StripByteCounts
    entries.push_back(shortEntry(284, {1}));                // PlanarConfiguration: chunky
    entries.push_back(shortEntry(339, {3, 3, 3}));          // SampleFormat: IEEE float
    entries.push_back(byteEntry(50706, {1, 4, 0, 0}));      // DNGVersion
    entries.push_back(byteEntry(50707, {1, 4, 0, 0}));      // DNGBackwardVersion — FP data is a 1.4 feature
    entries.push_back(asciiEntry(50708, image.camera));     // UniqueCameraModel
    entries.push_back(srationalEntry(50721, image.xyzToCam.data(), 9));       // ColorMatrix1
    entries.push_back(rationalEntry(50728, image.asShotNeutral.data(), 3));   // AsShotNeutral
    entries.push_back(srationalEntry(50730, &image.baselineExposureEv, 1, 100));  // BaselineExposure
    entries.push_back(shortEntry(50778, {21}));             // CalibrationIlluminant1: D65

    // ── Layout: header | IFD | external values | strip ─────────────────────
    const std::uint32_t ifdOffset = 8;
    const std::uint32_t ifdBytes =
        2 + static_cast<std::uint32_t>(entries.size()) * 12 + 4;
    std::uint32_t cursor = ifdOffset + ifdBytes;

    std::vector<std::uint32_t> valueOffsets(entries.size(), 0);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].data.size() > 4) {
            cursor += cursor & 1;  // values live at even offsets (TIFF 6.0 §2)
            valueOffsets[i] = cursor;
            cursor += static_cast<std::uint32_t>(entries[i].data.size());
        }
    }
    cursor += cursor & 1;
    const std::uint32_t stripOffset = cursor;

    for (auto& e : entries) {  // patch StripOffsets now the layout is known
        if (e.tag == 273) { e.data.clear(); put32(e.data, stripOffset); }
    }

    // ── Serialize everything but the strip ─────────────────────────────────
    std::vector<std::uint8_t> head;
    head.reserve(stripOffset);
    head.push_back('I'); head.push_back('I');  // little-endian
    put16(head, 42);
    put32(head, ifdOffset);

    put16(head, static_cast<std::uint16_t>(entries.size()));
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        put16(head, e.tag);
        put16(head, e.type);
        put32(head, e.count);
        if (e.data.size() <= 4) {
            head.insert(head.end(), e.data.begin(), e.data.end());
            head.resize(head.size() + (4 - e.data.size()), 0);
        } else {
            put32(head, valueOffsets[i]);
        }
    }
    put32(head, 0);  // no next IFD

    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].data.size() > 4) {
            head.resize(valueOffsets[i], 0);  // even-offset padding
            head.insert(head.end(), entries[i].data.begin(), entries[i].data.end());
        }
    }
    head.resize(stripOffset, 0);

    // ── Samples: clamp, quantize to half, one row at a time ────────────────
    // The clamp is the contract from the header: 1.0 is the ceiling and the
    // gain above it rides in BaselineExposure. NaN would poison a merge
    // average silently, so it dies here instead.
    std::vector<std::uint16_t> row(std::size_t(image.width) * 3);

    // Write to a sibling and rename: a throw mid-write must not leave a
    // truncated file that the library scan would then try to open forever.
    const std::string partial = path + ".part";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("DNG write: cannot create " + partial);
        out.write(reinterpret_cast<const char*>(head.data()),
                  static_cast<std::streamsize>(head.size()));

        for (std::uint32_t y = 0; y < image.height; ++y) {
            const float* src = image.rgb + std::size_t(y) * image.width * 3;
            for (std::size_t i = 0; i < row.size(); ++i) {
                float v = src[i];
                if (!std::isfinite(v)) {
                    std::remove(partial.c_str());
                    throw std::runtime_error("DNG write: non-finite sample");
                }
                row[i] = floatToHalf(std::clamp(v, 0.0f, 1.0f));
            }
            out.write(reinterpret_cast<const char*>(row.data()),
                      static_cast<std::streamsize>(row.size() * sizeof(std::uint16_t)));
        }
        if (!out) {
            out.close();
            std::remove(partial.c_str());
            throw std::runtime_error("DNG write: write failed for " + path);
        }
    }
    if (std::rename(partial.c_str(), path.c_str()) != 0) {
        std::remove(partial.c_str());
        throw std::runtime_error("DNG write: cannot move into place " + path);
    }
}

}  // namespace orion::util

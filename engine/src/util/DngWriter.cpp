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

    // ── The IFDs, entries ascending by tag (TIFF 6.0 §2 requires it) ──────
    //
    // Two shapes. Without a preview: one IFD carrying everything, the layout
    // story A shipped. With one: the standard DNG arrangement — IFD0 holds
    // the JPEG preview plus the camera metadata and points at the raw image
    // through SubIFDs (330), which is where every mainstream DNG keeps it
    // and therefore the shape thumbnailers actually look in.
    const bool withPreview =
        image.previewJpeg != nullptr && image.previewJpegBytes > 0 &&
        image.previewWidth > 0 && image.previewHeight > 0;

    // LibRaw flip -> TIFF Orientation (the exact inverse of the mapping
    // LibRaw applies when reading): none->1, 180->3, 90 CCW->8, 90 CW->6.
    const std::uint16_t orientation =
        image.flip == 3 ? 3 : image.flip == 5 ? 8 : image.flip == 6 ? 6 : 1;

    // The raw image's own tags — a full IFD alone, or the SubIFD.
    std::vector<Entry> raw;
    raw.push_back(longEntry(254, withPreview ? 0 : 0));     // NewSubfileType: the full-resolution image
    raw.push_back(longEntry(256, image.width));             // ImageWidth
    raw.push_back(longEntry(257, image.height));            // ImageLength
    raw.push_back(shortEntry(258, {16, 16, 16}));           // BitsPerSample
    raw.push_back(shortEntry(259, {1}));                    // Compression: none
    raw.push_back(shortEntry(262, {34892}));                // Photometric: LinearRaw
    raw.push_back(longEntry(273, 0));                       // StripOffsets — patched at layout
    raw.push_back(shortEntry(274, {orientation}));          // Orientation
    raw.push_back(shortEntry(277, {3}));                    // SamplesPerPixel
    raw.push_back(longEntry(278, image.height));            // RowsPerStrip: one strip
    raw.push_back(longEntry(279, stripBytes));              // StripByteCounts
    raw.push_back(shortEntry(284, {1}));                    // PlanarConfiguration: chunky
    raw.push_back(shortEntry(339, {3, 3, 3}));              // SampleFormat: IEEE float

    // The camera metadata, which belongs in IFD0 whichever shape is written.
    std::vector<Entry> meta;
    meta.push_back(asciiEntry(271, make));                  // Make
    meta.push_back(asciiEntry(272, model));                 // Model
    meta.push_back(byteEntry(50706, {1, 4, 0, 0}));         // DNGVersion
    meta.push_back(byteEntry(50707, {1, 4, 0, 0}));         // DNGBackwardVersion — FP data is a 1.4 feature
    meta.push_back(asciiEntry(50708, image.camera));        // UniqueCameraModel
    meta.push_back(srationalEntry(50721, image.xyzToCam.data(), 9));       // ColorMatrix1
    meta.push_back(rationalEntry(50728, image.asShotNeutral.data(), 3));   // AsShotNeutral
    meta.push_back(srationalEntry(50730, &image.baselineExposureEv, 1, 100));  // BaselineExposure
    meta.push_back(shortEntry(50778, {21}));                // CalibrationIlluminant1: D65

    std::vector<Entry> ifd0;
    std::vector<Entry> sub;
    if (withPreview) {
        // The preview IFD: an ordinary JPEG-compressed TIFF image whose one
        // strip is the whole JFIF stream — how DNG previews are stored.
        ifd0.push_back(longEntry(254, 1));                  // NewSubfileType: reduced-resolution
        ifd0.push_back(longEntry(256, image.previewWidth));
        ifd0.push_back(longEntry(257, image.previewHeight));
        ifd0.push_back(shortEntry(258, {8, 8, 8}));
        ifd0.push_back(shortEntry(259, {7}));               // Compression: JPEG
        ifd0.push_back(shortEntry(262, {6}));               // Photometric: YCbCr
        ifd0.push_back(longEntry(273, 0));                  // StripOffsets — patched
        ifd0.push_back(shortEntry(277, {3}));
        ifd0.push_back(longEntry(278, image.previewHeight));
        ifd0.push_back(longEntry(279, std::uint32_t(image.previewJpegBytes)));
        ifd0.push_back(longEntry(330, 0));                  // SubIFDs — patched
        for (auto& e : meta) ifd0.push_back(std::move(e));
        sub = std::move(raw);
    } else {
        ifd0 = std::move(raw);
        for (auto& e : meta) ifd0.push_back(std::move(e));
    }
    std::sort(ifd0.begin(), ifd0.end(),
              [](const Entry& a, const Entry& b) { return a.tag < b.tag; });
    std::sort(sub.begin(), sub.end(),
              [](const Entry& a, const Entry& b) { return a.tag < b.tag; });

    // ── Layout: header | IFD0 | values | subIFD | values | jpeg | strip ───
    const auto ifdBytes = [](const std::vector<Entry>& ifd) {
        return std::uint32_t(2 + ifd.size() * 12 + 4);
    };
    const auto externalBytes = [](const std::vector<Entry>& ifd) {
        std::uint32_t total = 0;
        for (const auto& e : ifd) {
            if (e.data.size() > 4) total += std::uint32_t(e.data.size() + 1) & ~1u;
        }
        return total;
    };

    const std::uint32_t ifd0Offset = 8;
    const std::uint32_t subOffset =
        sub.empty() ? 0
                    : ifd0Offset + ifdBytes(ifd0) + externalBytes(ifd0);
    std::uint32_t cursor = sub.empty()
        ? ifd0Offset + ifdBytes(ifd0) + externalBytes(ifd0)
        : subOffset + ifdBytes(sub) + externalBytes(sub);

    cursor += cursor & 1;
    const std::uint32_t jpegOffset = cursor;
    if (withPreview) cursor += std::uint32_t(image.previewJpegBytes);
    cursor += cursor & 1;
    const std::uint32_t stripOffset = cursor;

    for (auto& e : ifd0) {
        if (e.tag == 273) { e.data.clear(); put32(e.data, withPreview ? jpegOffset : stripOffset); }
        if (e.tag == 330) { e.data.clear(); put32(e.data, subOffset); }
    }
    for (auto& e : sub) {
        if (e.tag == 273) { e.data.clear(); put32(e.data, stripOffset); }
    }

    // ── Serialize everything but the strip ─────────────────────────────────
    std::vector<std::uint8_t> head;
    head.reserve(stripOffset);
    head.push_back('I'); head.push_back('I');  // little-endian
    put16(head, 42);
    put32(head, ifd0Offset);

    const auto serializeIfd = [&](const std::vector<Entry>& ifd,
                                  std::uint32_t ifdOffset) {
        // Values wider than four bytes live after the IFD at even offsets.
        std::uint32_t valueCursor = ifdOffset + ifdBytes(ifd);
        std::vector<std::uint32_t> valueOffsets(ifd.size(), 0);
        for (std::size_t i = 0; i < ifd.size(); ++i) {
            if (ifd[i].data.size() > 4) {
                valueCursor += valueCursor & 1;
                valueOffsets[i] = valueCursor;
                valueCursor += std::uint32_t(ifd[i].data.size());
            }
        }
        head.resize(ifdOffset, 0);
        put16(head, std::uint16_t(ifd.size()));
        for (std::size_t i = 0; i < ifd.size(); ++i) {
            const Entry& e = ifd[i];
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
        for (std::size_t i = 0; i < ifd.size(); ++i) {
            if (ifd[i].data.size() > 4) {
                head.resize(valueOffsets[i], 0);
                head.insert(head.end(), ifd[i].data.begin(), ifd[i].data.end());
            }
        }
    };

    serializeIfd(ifd0, ifd0Offset);
    if (!sub.empty()) serializeIfd(sub, subOffset);

    if (withPreview) {
        head.resize(jpegOffset, 0);
        head.insert(head.end(), image.previewJpeg,
                    image.previewJpeg + image.previewJpegBytes);
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

// What an export off macOS says about itself (ImageWriterPortable.cpp): the
// colour space its ICC profile declares and the camera EXIF and XMP it carries.
//
// Read back by parsers written here from the container specs (JPEG markers,
// PNG chunks, JPEG XL boxes, a TIFF directory walk), by libtiff for the TIFF,
// by libjxl for JPEG XL's profile and by Little CMS for each profile's
// colorants, not by the OpenCV that wrote the JPEG and PNG.

#include "harness.h"

#include "util/TiffIfd.h"

#include <jxl/decode_cxx.h>
#include <lcms2.h>
#include <opencv2/imgcodecs.hpp>
#include <tiffio.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

namespace {

using orion::util::ColorSpace;
using orion::util::ImageFormat;
using Bytes = std::vector<std::uint8_t>;
using Chunks = std::vector<std::pair<std::string, Bytes>>;

constexpr std::uint32_t kW = 64, kH = 48;

std::string scratch(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

Bytes bytesOf(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

std::uint32_t be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/// JPEG's marker segments before the scan, named by marker: "E1", "E2".
Chunks jpegSegments(const Bytes& b) {
    Chunks out;
    for (std::size_t i = 2; i + 4 <= b.size() && b[i] == 0xFF && b[i + 1] != 0xDA;) {
        const std::size_t len = std::size_t(b[i + 2]) << 8 | b[i + 3];
        if (len < 2 || i + 2 + len > b.size()) break;
        char name[3];
        std::snprintf(name, sizeof name, "%02X", b[i + 1]);
        out.emplace_back(name, Bytes(b.begin() + long(i) + 4, b.begin() + long(i + 2 + len)));
        i += 2 + len;
    }
    return out;
}

/// PNG's chunks (PNG 3rd ed. §5.3), or JPEG XL's ISO BMFF boxes.
Chunks pngChunks(const Bytes& b) {
    Chunks out;
    for (std::size_t i = 8; i + 12 <= b.size();) {
        const std::size_t len = be32(&b[i]);
        if (i + 12 + len > b.size()) break;
        out.emplace_back(std::string(b.begin() + long(i) + 4, b.begin() + long(i) + 8),
                         Bytes(b.begin() + long(i) + 8, b.begin() + long(i + 8 + len)));
        i += 12 + len;
    }
    return out;
}
Chunks jxlBoxes(const Bytes& b) {
    Chunks out;
    for (std::size_t i = 0; i + 8 <= b.size();) {
        const std::size_t len = be32(&b[i]);
        if (len < 8 || i + len > b.size()) break;
        out.emplace_back(std::string(b.begin() + long(i) + 4, b.begin() + long(i) + 8),
                         Bytes(b.begin() + long(i) + 8, b.begin() + long(i + len)));
        i += len;
    }
    return out;
}

const Bytes* find(const Chunks& c, const std::string& name, const char* prefix = "",
                  std::size_t prefixLen = 0) {
    for (const auto& [n, body] : c) {
        if (n == name && body.size() >= prefixLen && std::memcmp(body.data(), prefix, prefixLen) == 0) {
            return &body;
        }
    }
    return nullptr;
}

/// The ICC profile a file embeds, by its own container's rules.
Bytes embeddedIcc(const std::string& path, ImageFormat f) {
    const Bytes b = bytesOf(path);
    if (f == ImageFormat::Jpeg) {   // APP2 "ICC_PROFILE\0", sequence, count (ICC.1 annex B)
        Bytes icc;
        for (const auto& [n, body] : jpegSegments(b)) {
            if (n == "E2" && body.size() > 14 && std::memcmp(body.data(), "ICC_PROFILE", 12) == 0) {
                icc.insert(icc.end(), body.begin() + 14, body.end());
            }
        }
        return icc;
    }
    if (f == ImageFormat::Png) {    // iCCP: name, NUL, method, zlib stream
        const Chunks chunks = pngChunks(b);
        const Bytes* c = find(chunks, "iCCP");
        if (c == nullptr) return {};
        const auto nul = std::find(c->begin(), c->end(), 0);
        if (nul == c->end() || c->end() - nul <= 2) return {};
        Bytes icc(1 << 20);
        uLongf size = uLongf(icc.size());
        const auto* z = &*(nul + 2);
        if (uncompress(icc.data(), &size, z, uLong(c->end() - (nul + 2))) != Z_OK) return {};
        icc.resize(size);
        return icc;
    }
    if (f == ImageFormat::Tiff) {
        Bytes icc;
        if (TIFF* t = TIFFOpen(path.c_str(), "r")) {
            std::uint32_t n = 0;
            void* data = nullptr;
            if (TIFFGetField(t, TIFFTAG_ICCPROFILE, &n, &data) == 1) {
                icc.assign(static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + n);
            }
            TIFFClose(t);
        }
        return icc;
    }
    auto dec = JxlDecoderMake(nullptr);
    JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_COLOR_ENCODING);
    JxlDecoderSetInput(dec.get(), b.data(), b.size());
    JxlDecoderCloseInput(dec.get());
    std::size_t n = 0;
    if (JxlDecoderProcessInput(dec.get()) != JXL_DEC_COLOR_ENCODING ||
        JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &n) != JXL_DEC_SUCCESS) {
        return {};
    }
    Bytes icc(n);
    if (JxlDecoderGetColorAsICCProfile(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, icc.data(), n)
            != JXL_DEC_SUCCESS) return {};
    return icc;
}

/// A profile's colorants as chromaticities under D65, its red curve at 0.5.
/// The colorants are stored adapted to D50 by Bradford (ICC.1 annex E), which
/// Little CMS undoes here.
struct Declared { double x[3] = {}, y[3] = {}; double half = 0; bool ok = false; };
Declared declared(const Bytes& icc) {
    Declared d;
    cmsHPROFILE p = cmsOpenProfileFromMem(icc.data(), cmsUInt32Number(icc.size()));
    if (p == nullptr) return d;
    const cmsCIExyY d65xyY = {0.3127, 0.3290, 1.0};
    cmsCIEXYZ d65;
    cmsxyY2XYZ(&d65, &d65xyY);
    const cmsTagSignature tags[3] = {cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag};
    d.ok = true;
    for (int i = 0; i < 3; ++i) {
        const auto* v = static_cast<const cmsCIEXYZ*>(cmsReadTag(p, tags[i]));
        cmsCIEXYZ un;
        if (v == nullptr || !cmsAdaptToIlluminant(&un, cmsD50_XYZ(), &d65, v)) { d.ok = false; break; }
        const double sum = un.X + un.Y + un.Z;
        d.x[i] = un.X / sum;
        d.y[i] = un.Y / sum;
    }
    const auto* trc = static_cast<const cmsToneCurve*>(cmsReadTag(p, cmsSigRedTRCTag));
    d.ok = d.ok && trc != nullptr;
    if (trc != nullptr) d.half = cmsEvalToneCurveFloat(trc, 0.5f);
    cmsCloseProfile(p);
    return d;
}

/// One tag of a TIFF-structured block (an EXIF block or a whole TIFF), found
/// by walking IFD0 and the directory `pointer` names (0 for IFD0 itself).
/// Little-endian only, which is what the writer produces.
struct Tag { std::uint16_t type = 0; std::uint32_t count = 0; const std::uint8_t* value = nullptr; };
Tag tagIn(const Bytes& t, std::uint16_t pointer, std::uint16_t tag) {
    const auto rd16 = [&](std::size_t o) { return std::uint32_t(t[o] | t[o + 1] << 8); };
    const auto rd32 = [&](std::size_t o) { return rd16(o) | rd16(o + 2) << 16; };
    const auto lookup = [&](std::size_t ifd, std::uint16_t want) -> Tag {
        if (ifd + 2 > t.size()) return {};
        for (std::uint32_t i = 0, n = rd16(ifd); i < n; ++i) {
            const std::size_t e = ifd + 2 + std::size_t(i) * 12;
            if (e + 12 > t.size()) return {};
            if (rd16(e) != want) continue;
            const std::uint16_t type = std::uint16_t(rd16(e + 2));
            const std::uint32_t count = rd32(e + 4);
            const std::size_t width = (type == 3 ? 2 : type == 4 ? 4 : type == 5 ? 8 : 1) * std::size_t(count);
            const std::size_t at = width <= 4 ? e + 8 : rd32(e + 8);
            if (at + width > t.size()) return {};
            return {type, count, &t[at]};
        }
        return {};
    };
    if (t.size() < 8 || t[0] != 'I' || t[1] != 'I') return {};
    std::size_t ifd = rd32(4);
    if (pointer != 0) {
        const Tag p = lookup(ifd, pointer);
        if (p.value == nullptr) return {};
        ifd = std::size_t(p.value[0] | p.value[1] << 8 | p.value[2] << 16 | std::uint32_t(p.value[3]) << 24);
    }
    return lookup(ifd, tag);
}
std::string text(const Tag& t) {
    return t.type == 2 && t.count > 0 ? std::string(reinterpret_cast<const char*>(t.value), t.count - 1) : "";
}
double rational(const Tag& t, int i = 0) {
    if (t.type != 5 || t.value == nullptr) return -1;
    const auto r = [&](int k) { const auto* p = t.value + 8 * i + 4 * k; return double(p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24); };
    return r(0) / r(1);
}
std::uint32_t number(const Tag& t) {
    if (t.value == nullptr) return 0;
    return t.type == 3 ? std::uint32_t(t.value[0] | t.value[1] << 8)
                       : std::uint32_t(t.value[0] | t.value[1] << 8 | t.value[2] << 16 | std::uint32_t(t.value[3]) << 24);
}

/// The EXIF block and XMP packet a JPEG, PNG or JPEG XL carries.
std::pair<Bytes, std::string> carried(const std::string& path, ImageFormat f) {
    const Bytes b = bytesOf(path);
    static const char kXmp[] = "http://ns.adobe.com/xap/1.0/";
    Bytes exif;
    std::string xmp;
    if (f == ImageFormat::Jpeg) {
        const Chunks s = jpegSegments(b);
        if (const Bytes* e = find(s, "E1", "Exif\0", 6)) exif.assign(e->begin() + 6, e->end());
        if (const Bytes* x = find(s, "E1", kXmp, sizeof kXmp)) xmp.assign(x->begin() + sizeof kXmp, x->end());
    } else if (f == ImageFormat::Png) {
        const Chunks c = pngChunks(b);
        if (const Bytes* e = find(c, "eXIf")) exif = *e;
        static const char kKey[] = "XML:com.adobe.xmp";
        if (const Bytes* x = find(c, "tEXt", kKey, sizeof kKey)) xmp.assign(x->begin() + sizeof kKey, x->end());
    } else {
        const Chunks c = jxlBoxes(b);
        if (const Bytes* e = find(c, "Exif"); e != nullptr && e->size() > 4) exif.assign(e->begin() + 4, e->end());
        if (const Bytes* x = find(c, "xml ")) xmp.assign(x->begin(), x->end());
    }
    return {exif, xmp};
}

/// A small linear DNG with the camera facts an ARW carries, an EXIF IFD and a
/// GPS IFD, for LibRaw to read as it reads a camera's file. The DNG writer
/// writes no EXIF, and the sample frames carry no GPS.
void writeCameraDng(const std::string& path) {
    using namespace orion::util::tiff;
    constexpr std::uint32_t n = 32;   // LibRaw takes nothing under 22 pixels a side
    const float matrix[9] = {0.6640f, -0.1847f, -0.0503f, -0.5238f, 1.3465f, 0.1916f, -0.0879f, 0.1636f, 0.6271f};
    Ifd ifd0 = {longEntry(254, 0), longEntry(256, n), longEntry(257, n), shortEntry(258, {16, 16, 16}),
                shortEntry(259, {1}), shortEntry(262, {34892}), longEntry(273, 0), asciiEntry(271, "SONY"),
                asciiEntry(272, "ILCE-7RM3"), shortEntry(274, {6}), shortEntry(277, {3}), longEntry(278, n),
                longEntry(279, n * n * 6), shortEntry(284, {1}), byteEntry(50706, {1, 4, 0, 0}),
                asciiEntry(50708, "SONY ILCE-7RM3"), srationalEntry(50721, matrix, 9), shortEntry(50778, {21})};
    const std::uint8_t version[4] = {'0', '2', '3', '2'};
    Ifd exif = {rationalEntry(33434, {{1, 250}}), rationalEntry(33437, {{56, 10}}), shortEntry(34855, {400}),
                bytesEntry(36864, kUndefined, version, 4), asciiEntry(36867, "2024:05:01 10:20:30"),
                rationalEntry(37386, {{350, 10}}), asciiEntry(42036, "FE 35mm F1.8")};
    Ifd gps = {byteEntry(0, {2, 3, 0, 0}), asciiEntry(1, "N"), rationalEntry(2, {{51, 1}, {30, 1}, {1234, 100}}),
               asciiEntry(3, "W"), rationalEntry(4, {{0, 1}, {7, 1}, {3900, 100}}), byteEntry(5, {0}),
               rationalEntry(6, {{4500, 100}})};
    Bytes file = layout(std::move(ifd0), std::move(exif), std::move(gps));
    file.resize(file.size() + n * n * 6, 0x40);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
}

std::vector<std::uint16_t> frame() {
    std::vector<std::uint16_t> px(std::size_t(kW) * kH * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = 65535;    // saturated sRGB red, where the spaces disagree most
        px[i + 1] = 0;
        px[i + 2] = 0;
        px[i + 3] = 65535;
    }
    return px;
}

const char* extension(ImageFormat f) {
    return f == ImageFormat::Jpeg ? ".jpg" : f == ImageFormat::Png ? ".png" : f == ImageFormat::Tiff ? ".tif" : ".jxl";
}

}  // namespace

void testExportColourTags() {
    section("Export colour spaces (Windows/Linux writer)");
    using namespace orion::util;
    const auto px = frame();
    const std::size_t stride = std::size_t(kW) * 8;

    // The published chromaticities: IEC 61966-2-1 (sRGB), SMPTE EG 432-1 (the
    // P3 primaries of Apple's Display P3), Adobe RGB (1998) v2005-05 §4.3.1.
    // Red's curve at 0.5: the sRGB formula, or Adobe's gamma of 563/256.
    struct Space { ColorSpace space; const char* name; double x[3], y[3]; double half; };
    const double srgbHalf = std::pow((0.5 + 0.055) / 1.055, 2.4);
    const Space spaces[] = {
        {ColorSpace::Srgb, "sRGB", {0.64, 0.30, 0.15}, {0.33, 0.60, 0.06}, srgbHalf},
        {ColorSpace::DisplayP3, "Display P3", {0.680, 0.265, 0.150}, {0.320, 0.690, 0.060}, srgbHalf},
        {ColorSpace::AdobeRgb, "Adobe RGB", {0.64, 0.21, 0.15}, {0.33, 0.71, 0.06}, std::pow(0.5, 563.0 / 256.0)},
    };
    for (const auto& sp : spaces) {
        for (ImageFormat f : {ImageFormat::Jpeg, ImageFormat::Png, ImageFormat::Tiff, ImageFormat::Jxl}) {
            if (sp.space != ColorSpace::DisplayP3 && f != ImageFormat::Png) continue;
            ExportOptions o;
            o.format = f;
            o.space = sp.space;
            const std::string path = scratch(std::string("orion-space-") + sp.name[0] + extension(f));
            const std::string what = std::string(sp.name) + " " + extension(f);
            try { writeImage(path, px.data(), kW, kH, stride, o); }
            catch (const std::exception& e) { report(false, what + " writes", e.what()); continue; }
            const Declared d = declared(embeddedIcc(path, f));
            double worst = d.ok ? 0 : 1;
            for (int i = 0; i < 3 && d.ok; ++i) {
                worst = std::max({worst, std::abs(d.x[i] - sp.x[i]), std::abs(d.y[i] - sp.y[i])});
            }
            report(worst < 0.0015, what + ": the embedded profile declares the published primaries",
                   "worst xy error " + std::to_string(worst));
            report(std::abs(d.half - sp.half) < 0.002, what + ": and the published curve",
                   "red curve at 0.5 is " + std::to_string(d.half));
            report(encodedSize(px.data(), kW, kH, stride, o) > 0, what + ": the size readout encodes");
        }
    }

    // Converted, not relabelled: sRGB's red, expressed in each wider space.
    // Display P3: (0.9175, 0.2003, 0.1386), as CSS Color 4's conversion gives.
    // Adobe RGB shares sRGB's red primary and white, so only red moves: to
    // (0.2126 / 0.2974)^(256/563) = 0.8586, the two red luminances' ratio.
    struct Want { ColorSpace space; double rgb[3]; const char* name; };
    for (const Want& w : {Want{ColorSpace::Srgb, {1, 0, 0}, "sRGB"},
                          Want{ColorSpace::DisplayP3, {0.9175, 0.2003, 0.1386}, "Display P3"},
                          Want{ColorSpace::AdobeRgb, {0.8586, 0, 0}, "Adobe RGB"}}) {
        ExportOptions o;
        o.format = ImageFormat::Png;
        o.space = w.space;
        const std::string path = scratch("orion-convert.png");
        try { writeImage(path, px.data(), kW, kH, stride, o); }
        catch (const std::exception& e) { report(false, std::string(w.name) + " writes", e.what()); continue; }
        const cv::Mat back = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (back.empty() || back.depth() != CV_16U) { report(false, std::string(w.name) + " reads back"); continue; }
        const cv::Vec3w v = back.at<cv::Vec3w>(10, 10);   // BGR
        const double got[3] = {v[2] / 65535.0, v[1] / 65535.0, v[0] / 65535.0};
        report(std::abs(got[0] - w.rgb[0]) < 0.002 && std::abs(got[1] - w.rgb[1]) < 0.002 &&
                   std::abs(got[2] - w.rgb[2]) < 0.002,
               std::string("sRGB red is converted into ") + w.name,
               std::to_string(got[0]) + " " + std::to_string(got[1]) + " " + std::to_string(got[2]));
    }
}

void testExportMetadataTags() {
    section("Export metadata (Windows/Linux writer)");
    using namespace orion::util;
    const std::string source = scratch("orion-camera.dng");
    writeCameraDng(source);
    orion::raw::RawInfo info;
    try { info = orion::raw::readInfo(source); }
    catch (const std::exception& e) { report(false, "LibRaw opens the camera DNG", e.what()); return; }
    report(info.make == "Sony" && info.shutter > 0 && info.gps.parsed,
           "LibRaw reads the camera DNG's make, exposure and GPS", info.make);

    const auto px = frame();
    const std::size_t stride = std::size_t(kW) * 8;
    for (ImageFormat f : {ImageFormat::Jpeg, ImageFormat::Png, ImageFormat::Tiff, ImageFormat::Jxl}) {
        for (Metadata policy : {Metadata::NoLocation, Metadata::All, Metadata::None}) {
            ExportOptions o;
            o.format = f;
            o.metadataFrom = source;
            o.metadata = policy;
            o.rating = 3;
            o.maxDimension = 32;
            const std::string path = scratch(std::string("orion-meta-") + char('0' + int(policy)) + extension(f));
            const std::string what = std::string(extension(f) + 1) +
                (policy == Metadata::All ? ", keep all" : policy == Metadata::None ? ", strip everything" : "");
            try { writeImage(path, px.data(), kW, kH, stride, o); }
            catch (const std::exception& e) { report(false, what + " writes", e.what()); continue; }

            Bytes exif;
            std::string xmp;
            if (f == ImageFormat::Tiff) {
                exif = bytesOf(path);   // a TIFF is its own EXIF structure; libtiff reads it below
                if (TIFF* t = TIFFOpen(path.c_str(), "r")) {
                    char* field = nullptr;
                    std::uint64_t exifAt = 0;
                    float exposure = 0;
                    const bool hasMake = TIFFGetField(t, TIFFTAG_MAKE, &field) == 1;
                    const std::string make = hasMake ? field : "";   // reading the EXIF IFD frees it
                    const bool read = TIFFGetField(t, TIFFTAG_EXIFIFD, &exifAt) == 1 &&
                                      TIFFReadEXIFDirectory(t, exifAt) &&
                                      TIFFGetField(t, EXIFTAG_EXPOSURETIME, &exposure) == 1;
                    report(policy == Metadata::None ? !hasMake
                                                    : hasMake && make == info.make && read && std::abs(exposure - 0.004f) < 1e-6f,
                           what + ": libtiff reads the Make and the EXIF ExposureTime");
                    TIFFClose(t);
                }
                const Tag x = tagIn(exif, 0, 700);
                if (x.value != nullptr) xmp.assign(reinterpret_cast<const char*>(x.value), x.count);
            } else {
                std::tie(exif, xmp) = carried(path, f);
            }

            const bool camera = policy != Metadata::None;
            report(text(tagIn(exif, 0, 305)) == "Orion" && number(tagIn(exif, 0, 274)) == 1,
                   what + ": Software is Orion and Orientation is 1");
            report(camera ? text(tagIn(exif, 0, 271)) == info.make && text(tagIn(exif, 0, 272)) == "ILCE-7RM3"
                          : tagIn(exif, 0, 271).value == nullptr,
                   what + (camera ? ": Make and Model are the camera's" : ": no Make"));
            if (camera) {
                const Tag lens = tagIn(exif, 34665, 42036);
                report(rational(tagIn(exif, 34665, 33434)) == 1.0 / 250 && rational(tagIn(exif, 34665, 33437)) == 5.6 &&
                           number(tagIn(exif, 34665, 34855)) == 400 && rational(tagIn(exif, 34665, 37386)) == 35.0 &&
                           text(tagIn(exif, 34665, 36867)) == "2024:05:01 10:20:30" && text(lens) == "FE 35mm F1.8",
                       what + ": exposure, aperture, ISO, focal length, date and lens survive");
                report(number(tagIn(exif, 34665, 40962)) == 32 && number(tagIn(exif, 34665, 40963)) == 24,
                       what + ": the EXIF pixel size is the export's");
            }
            const Tag lat = tagIn(exif, 34853, 2);
            const bool gps = policy == Metadata::All;
            report(gps ? text(tagIn(exif, 34853, 1)) == "N" && rational(lat, 0) == 51 && rational(lat, 2) == 12.34 &&
                             text(tagIn(exif, 34853, 3)) == "W"
                       : tagIn(exif, 0, 34853).value == nullptr,
                   what + (gps ? ": the GPS position survives" : ": no GPS"));
            report(xmp.find("xmp:Rating=\"3\"") != std::string::npos &&
                       xmp.find("xmp:CreatorTool=\"Orion\"") != std::string::npos &&
                       (xmp.find("tiff:Model=\"ILCE-7RM3\"") != std::string::npos) == camera,
                   what + ": the XMP carries the rating" + (camera ? " and the camera" : ""));
        }
    }
}

void testExportTiffDeflate() {
    section("Export TIFF compression (Windows/Linux writer)");
    using namespace orion::util;
    // Wide enough for several strips; ramps and a square wave, so the
    // predictor and the deflate both have structure to work on.
    constexpr std::uint32_t w = 640, h = 400;
    std::vector<std::uint16_t> px(std::size_t(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            std::uint16_t* p = &px[(std::size_t(y) * w + x) * 4];
            p[0] = std::uint16_t(x * 100 + y);
            p[1] = std::uint16_t(y * 160);
            p[2] = ((x / 7 + y / 5) & 1) ? 52000 : 9000;
            p[3] = 65535;
        }
    for (BitDepth depth : {BitDepth::Sixteen, BitDepth::Eight}) {
        const bool wide = depth == BitDepth::Sixteen;
        const std::string what = wide ? "16-bit TIFF" : "8-bit TIFF";
        ExportOptions o;
        o.format = ImageFormat::Tiff;
        o.depth = depth;
        const std::string path = scratch(wide ? "orion-deflate-16.tif" : "orion-deflate-8.tif");
        try { writeImage(path, px.data(), w, h, std::size_t(w) * 8, o); }
        catch (const std::exception& e) { report(false, what + " writes", e.what()); continue; }

        bool same = false;
        std::uint16_t compression = 0;
        std::uint32_t strips = 0;
        if (TIFF* t = TIFFOpen(path.c_str(), "r")) {
            TIFFGetField(t, TIFFTAG_COMPRESSION, &compression);
            strips = TIFFNumberOfStrips(t);
            std::vector<std::uint8_t> row(TIFFScanlineSize(t));
            same = row.size() == std::size_t(w) * 3 * (wide ? 2 : 1);
            for (std::uint32_t y = 0; y < h && same; ++y) {
                same = TIFFReadScanline(t, row.data(), y) == 1;
                for (std::uint32_t x = 0; x < w && same; ++x)
                    for (int c = 0; c < 3 && same; ++c) {
                        const std::uint16_t want = px[(std::size_t(y) * w + x) * 4 + c];
                        const std::size_t i = std::size_t(x) * 3 + c;
                        same = wide ? (row[2 * i] | row[2 * i + 1] << 8) == want
                                    : row[i] == std::lround(want * 255.0 / 65535.0);
                    }
            }
            TIFFClose(t);
        }
        report(compression == 8 && strips > 1, what + " is deflated, in several strips",
               "compression " + std::to_string(compression) + ", " + std::to_string(strips) + " strips");
        report(same, what + ": libtiff decodes it pixel for pixel");
        const auto bytes = std::filesystem::file_size(path);
        const std::size_t raw = std::size_t(w) * h * 3 * (wide ? 2 : 1);
        report(bytes < raw / 2, what + " is under half its uncompressed size",
               std::to_string(bytes) + " of " + std::to_string(raw) + " bytes");
    }
}

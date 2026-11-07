#include "util/ExportMetadata.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <exception>
#include <utility>

namespace orion::util {
namespace {

using Rational = std::pair<std::uint32_t, std::uint32_t>;

/// 1/250 as 1/250, not 4000/1000000: a shorter exposure than a second is
/// written as a unit fraction whenever one is within 0.1% of it.
Rational exposureRational(float seconds) {
    if (seconds < 1.0f) {
        const auto den = std::uint32_t(std::lround(1.0 / seconds));
        if (den > 0 && std::abs(1.0 / den - seconds) <= 1e-3 * seconds) return {1, den};
        return {std::uint32_t(std::lround(seconds * 1e6)), 1000000};
    }
    return {std::uint32_t(std::lround(seconds * 10.0)), 10};
}

Rational tenths(float v) { return {std::uint32_t(std::lround(std::max(0.0f, v) * 10.0f)), 10}; }

std::string rationalText(Rational r) { return std::to_string(r.first) + "/" + std::to_string(r.second); }

/// LibRaw read DateTimeOriginal with mktime, so localtime gives it back.
std::string cameraClock(std::int64_t timestamp) {
    if (timestamp <= 0) return {};
    const std::time_t t = std::time_t(timestamp);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &t) != 0) return {};
#else
    if (localtime_r(&t, &local) == nullptr) return {};
#endif
    char text[20];
    std::snprintf(text, sizeof text, "%04d:%02d:%02d %02d:%02d:%02d", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    return text;
}

/// Degrees and minutes whole, seconds to a hundredth, as cameras write them.
tiff::Entry dms(std::uint16_t tag, const float* v) {
    return tiff::rationalEntry(tag, {{std::uint32_t(std::lround(v[0])), 1},
                                     {std::uint32_t(std::lround(v[1])), 1},
                                     {std::uint32_t(std::lround(v[2] * 100.0f)), 100}});
}

/// XML text with every character outside printable ASCII as a character
/// reference. A byte that does not start a whole UTF-8 sequence becomes '?'.
std::string escaped(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2
                        : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0 || s.size() - i <= std::size_t(extra)) {
            out += '?';
            ++i;
            continue;
        }
        std::uint32_t cp = extra == 0 ? c : c & (0x3Fu >> extra);
        for (int k = 1; k <= extra; ++k) cp = cp << 6 | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        i += std::size_t(extra) + 1;
        if (cp == '&') out += "&amp;";
        else if (cp == '<') out += "&lt;";
        else if (cp == '>') out += "&gt;";
        else if (cp == '"') out += "&quot;";
        else if (cp >= 0x20 && cp < 0x7F) out += char(cp);
        else {
            char ref[16];
            std::snprintf(ref, sizeof ref, "&#x%X;", unsigned(cp));
            out += ref;
        }
    }
    return out;
}

}  // namespace

CaptureFacts captureFacts(const ExportOptions& o) {
    CaptureFacts f;
    f.rating = o.rating >= 0 ? std::clamp(o.rating, 0, 5) : -1;
    if (o.metadata == Metadata::None) {
        f.software = true;
        return f;
    }
    if (o.metadataFrom.empty()) return f;
    f.software = true;
    try {
        const raw::RawInfo info = raw::readInfo(o.metadataFrom);
        f.make = info.make;
        f.model = info.model;
        f.lens = info.lens;
        f.taken = cameraClock(info.timestamp);
        f.exposure = info.shutter;
        f.fNumber = info.aperture;
        f.iso = info.isoSpeed;
        f.focalLength = info.focalLength;
        if (o.metadata == Metadata::All) f.gps = info.gps;
    } catch (const std::exception&) {
        // Not a RAW LibRaw reads: the file says only who developed it.
    }
    return f;
}

void exifDirectories(const CaptureFacts& f, std::uint32_t width, std::uint32_t height,
                     tiff::Ifd& ifd0, tiff::Ifd& exif, tiff::Ifd& gps) {
    using namespace tiff;
    if (!f.hasExif()) return;
    if (!f.make.empty()) ifd0.push_back(asciiEntry(271, f.make));
    if (!f.model.empty()) ifd0.push_back(asciiEntry(272, f.model));
    ifd0.push_back(shortEntry(274, {1}));                           // Orientation
    if (f.software) ifd0.push_back(asciiEntry(305, "Orion"));      // Software

    const std::uint8_t version[4] = {'0', '2', '3', '2'};
    exif.push_back(bytesEntry(36864, kUndefined, version, 4));      // ExifVersion
    if (f.exposure > 0) exif.push_back(rationalEntry(33434, {exposureRational(f.exposure)}));
    if (f.fNumber > 0) exif.push_back(rationalEntry(33437, {tenths(f.fNumber)}));
    if (f.iso > 0) {                                                // PhotographicSensitivity
        exif.push_back(shortEntry(34855, {std::uint16_t(std::min(f.iso, 65535.0f))}));
    }
    if (!f.taken.empty()) exif.push_back(asciiEntry(36867, f.taken));  // DateTimeOriginal
    if (f.focalLength > 0) exif.push_back(rationalEntry(37386, {tenths(f.focalLength)}));
    exif.push_back(longEntry(40962, width));                        // PixelXDimension
    exif.push_back(longEntry(40963, height));                       // PixelYDimension
    if (!f.lens.empty()) exif.push_back(asciiEntry(42036, f.lens)); // LensModel

    if (f.gps.parsed) {
        gps.push_back(byteEntry(0, {2, 3, 0, 0}));                  // GPSVersionID
        gps.push_back(asciiEntry(1, std::string(1, f.gps.latRef == 'S' ? 'S' : 'N')));
        gps.push_back(dms(2, f.gps.latitude));
        gps.push_back(asciiEntry(3, std::string(1, f.gps.lonRef == 'W' ? 'W' : 'E')));
        gps.push_back(dms(4, f.gps.longitude));
        gps.push_back(byteEntry(5, {std::uint8_t(f.gps.altRef == 1 ? 1 : 0)}));
        const float altitude = std::abs(f.gps.altitude);
        gps.push_back(rationalEntry(6, &altitude, 1, 100));
    }
}

std::vector<std::uint8_t> exifBlock(const CaptureFacts& f, std::uint32_t width,
                                    std::uint32_t height) {
    if (!f.hasExif()) return {};
    tiff::Ifd ifd0, exif, gps;
    exifDirectories(f, width, height, ifd0, exif, gps);
    return tiff::layout(std::move(ifd0), std::move(exif), std::move(gps));
}

std::string xmpPacket(const CaptureFacts& f) {
    if (!f.any()) return {};
    std::string attrs;
    const auto attr = [&](const char* name, const std::string& value) {
        if (!value.empty()) attrs += std::string("\n   ") + name + "=\"" + escaped(value) + "\"";
    };
    if (f.software) attr("xmp:CreatorTool", "Orion");
    if (f.rating >= 0) attr("xmp:Rating", std::to_string(f.rating));
    attr("tiff:Make", f.make);
    attr("tiff:Model", f.model);
    if (f.hasExif()) attr("tiff:Orientation", "1");
    if (f.exposure > 0) attr("exif:ExposureTime", rationalText(exposureRational(f.exposure)));
    if (f.fNumber > 0) attr("exif:FNumber", rationalText(tenths(f.fNumber)));
    if (f.focalLength > 0) attr("exif:FocalLength", rationalText(tenths(f.focalLength)));
    if (f.taken.size() == 19) {   // XMP dates are ISO 8601
        std::string iso = f.taken;
        iso[4] = iso[7] = '-';
        iso[10] = 'T';
        attr("exif:DateTimeOriginal", iso);
    }
    attr("exifEX:LensModel", f.lens);
    std::string iso;
    if (f.iso > 0) {
        iso = "\n   <exif:ISOSpeedRatings><rdf:Seq><rdf:li>" +
              std::to_string(std::lround(f.iso)) + "</rdf:li></rdf:Seq></exif:ISOSpeedRatings>\n  ";
    }
    return "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
           "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
           " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
           "  <rdf:Description rdf:about=\"\"\n"
           "   xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
           "   xmlns:tiff=\"http://ns.adobe.com/tiff/1.0/\"\n"
           "   xmlns:exif=\"http://ns.adobe.com/exif/1.0/\"\n"
           "   xmlns:exifEX=\"http://cipa.jp/exif/1.0/\"" +
           attrs + (iso.empty() ? "/>\n" : ">" + iso + "</rdf:Description>\n") +
           " </rdf:RDF>\n"
           "</x:xmpmeta>\n"
           "<?xpacket end=\"w\"?>";
}

}  // namespace orion::util

#include "util/TiffIfd.h"

#include <algorithm>
#include <cmath>

namespace orion::util::tiff {

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, static_cast<std::uint16_t>(v & 0xffff));
    put16(out, static_cast<std::uint16_t>(v >> 16));
}

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

Entry bytesEntry(std::uint16_t tag, std::uint16_t type, const std::uint8_t* data, std::size_t n) {
    Entry e{tag, type, static_cast<std::uint32_t>(n), {}};
    e.data.assign(data, data + n);
    return e;
}

Entry asciiEntry(std::uint16_t tag, const std::string& s) {
    Entry e{tag, kAscii, static_cast<std::uint32_t>(s.size() + 1), {}};
    e.data.assign(s.begin(), s.end());
    e.data.push_back(0);
    return e;
}

Entry rationalEntry(std::uint16_t tag,
                    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> vs) {
    Entry e{tag, kRational, static_cast<std::uint32_t>(vs.size()), {}};
    for (const auto& [num, den] : vs) {
        put32(e.data, num);
        put32(e.data, den);
    }
    return e;
}

Entry rationalEntry(std::uint16_t tag, const float* vs, std::size_t n, std::uint32_t den) {
    Entry e{tag, kRational, static_cast<std::uint32_t>(n), {}};
    for (std::size_t i = 0; i < n; ++i) {
        const double v = std::max(0.0, static_cast<double>(vs[i]));
        put32(e.data, static_cast<std::uint32_t>(std::lround(v * den)));
        put32(e.data, den);
    }
    return e;
}

Entry srationalEntry(std::uint16_t tag, const float* vs, std::size_t n, std::int32_t den) {
    Entry e{tag, kSRational, static_cast<std::uint32_t>(n), {}};
    for (std::size_t i = 0; i < n; ++i) {
        const auto num =
            static_cast<std::int32_t>(std::lround(static_cast<double>(vs[i]) * den));
        put32(e.data, static_cast<std::uint32_t>(num));
        put32(e.data, static_cast<std::uint32_t>(den));
    }
    return e;
}

std::uint32_t ifdBytes(const Ifd& ifd) {
    return std::uint32_t(2 + ifd.size() * 12 + 4);
}

std::uint32_t externalBytes(const Ifd& ifd) {
    std::uint32_t total = 0;
    for (const auto& e : ifd) {
        if (e.data.size() > 4) total += std::uint32_t(e.data.size() + 1) & ~1u;
    }
    return total;
}

void sortByTag(Ifd& ifd) {
    std::sort(ifd.begin(), ifd.end(), [](const Entry& a, const Entry& b) { return a.tag < b.tag; });
}

void writeIfd(std::vector<std::uint8_t>& out, const Ifd& ifd, std::uint32_t offset) {
    std::uint32_t valueCursor = offset + ifdBytes(ifd);
    std::vector<std::uint32_t> valueOffsets(ifd.size(), 0);
    for (std::size_t i = 0; i < ifd.size(); ++i) {
        if (ifd[i].data.size() > 4) {
            valueCursor += valueCursor & 1;
            valueOffsets[i] = valueCursor;
            valueCursor += std::uint32_t(ifd[i].data.size());
        }
    }
    out.resize(offset, 0);
    put16(out, std::uint16_t(ifd.size()));
    for (std::size_t i = 0; i < ifd.size(); ++i) {
        const Entry& e = ifd[i];
        put16(out, e.tag);
        put16(out, e.type);
        put32(out, e.count);
        if (e.data.size() <= 4) {
            out.insert(out.end(), e.data.begin(), e.data.end());
            out.resize(out.size() + (4 - e.data.size()), 0);
        } else {
            put32(out, valueOffsets[i]);
        }
    }
    put32(out, 0);
    for (std::size_t i = 0; i < ifd.size(); ++i) {
        if (ifd[i].data.size() > 4) {
            out.resize(valueOffsets[i], 0);
            out.insert(out.end(), ifd[i].data.begin(), ifd[i].data.end());
        }
    }
}

std::vector<std::uint8_t> layout(Ifd ifd0, Ifd exif, Ifd gps, const std::vector<std::uint32_t>& strips) {
    if (!exif.empty()) ifd0.push_back(longEntry(34665, 0));
    if (!gps.empty()) ifd0.push_back(longEntry(34853, 0));
    sortByTag(ifd0);
    sortByTag(exif);
    sortByTag(gps);

    const auto span = [](const Ifd& ifd) { return ifdBytes(ifd) + externalBytes(ifd); };
    const std::uint32_t ifd0Offset = 8;
    const std::uint32_t exifOffset = ifd0Offset + span(ifd0);
    const std::uint32_t gpsOffset = exifOffset + (exif.empty() ? 0 : span(exif));
    std::uint32_t end = gpsOffset + (gps.empty() ? 0 : span(gps));
    end += end & 1;

    for (auto& e : ifd0) {
        const std::uint32_t at = e.tag == 34665 ? exifOffset : e.tag == 34853 ? gpsOffset : 0;
        if (at != 0) { e.data.clear(); put32(e.data, at); }
        if (e.tag == 273) {
            e.data.clear();
            std::uint32_t offset = end;
            for (std::uint32_t i = 0; i < e.count; ++i) {
                put32(e.data, offset);
                if (i < strips.size()) offset += strips[i];
            }
        }
    }

    std::vector<std::uint8_t> out;
    out.reserve(end);
    out.push_back('I'); out.push_back('I');
    put16(out, 42);
    put32(out, ifd0Offset);
    writeIfd(out, ifd0, ifd0Offset);
    if (!exif.empty()) writeIfd(out, exif, exifOffset);
    if (!gps.empty()) writeIfd(out, gps, gpsOffset);
    out.resize(end, 0);
    return out;
}

}  // namespace orion::util::tiff

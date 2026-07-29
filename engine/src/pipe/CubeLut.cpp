#include "pipe/CubeLut.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace orion::pipe {
namespace {

/// A line, trimmed. Comments are handled by the caller, because in this format
/// they are whole *lines* and not trailing text.
///
/// The specification, section 5.8: "The file may, at any location, include any
/// number of comment lines. Each comment line shall be formatted as follows:
/// `# <text>`". A '#' part-way through a line is therefore *not* a comment —
/// and a look really can be called `TITLE "Look #3"`, which a mid-line rule
/// would silently truncate to `Look`.
///
/// Leading whitespace before the '#' is tolerated on read even though the spec
/// says a comment line has none: a data line cannot begin with '#', so nothing
/// is ambiguous, and refusing a file over indentation helps nobody.
[[nodiscard]] std::string_view trim(std::string_view line) {
    // Trailing \r matters: the spec is explicit that the separator is \n and
    // that files using \r as the separator "are not valid cube files", but a
    // stray \r from CRLF is another matter and every field would otherwise end
    // in a character the number parser stops at.
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    };
    while (!line.empty() && isSpace(line.front())) line.remove_prefix(1);
    while (!line.empty() && isSpace(line.back()))  line.remove_suffix(1);
    return line;
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

[[nodiscard]] bool toFloat(std::string_view s, float& out) {
    // from_chars for float is not available everywhere libc++ ships, so this
    // goes through strtod on a bounded copy rather than assuming.
    const std::string copy(s);
    char* end = nullptr;
    const double v = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

[[nodiscard]] bool toInt(std::string_view s, int& out) {
    const std::string copy(s);
    char* end = nullptr;
    const long v = std::strtol(copy.c_str(), &end, 10);
    if (end == copy.c_str() || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

[[nodiscard]] bool equalsIgnoringCase(std::string_view a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

/// Lifts a 1D LUT onto the 3D grid.
///
/// A 1D LUT applies the same curve to each channel independently, which is
/// exactly a separable 3D LUT — so evaluating it at the grid nodes loses
/// nothing at those nodes, and what the interpolation does between them is the
/// same thing it does for any other LUT. One code path in the shader, at the
/// cost of a table that is larger than the file was.
[[nodiscard]] std::vector<float> liftOneDimensional(const std::vector<float>& oneD,
                                                    int entries, int grid) {
    std::vector<float> out(static_cast<std::size_t>(grid) * grid * grid * 3);

    const auto sample = [&](float t, int channel) {
        const float x = std::clamp(t, 0.0f, 1.0f) * float(entries - 1);
        const int   i0 = std::min(int(x), entries - 1);
        const int   i1 = std::min(i0 + 1, entries - 1);
        const float f  = x - float(i0);
        const float a = oneD[static_cast<std::size_t>(i0) * 3 + channel];
        const float b = oneD[static_cast<std::size_t>(i1) * 3 + channel];
        return a + (b - a) * f;
    };

    std::size_t w = 0;
    for (int b = 0; b < grid; ++b) {
        for (int g = 0; g < grid; ++g) {
            for (int r = 0; r < grid; ++r) {
                out[w++] = sample(float(r) / float(grid - 1), 0);
                out[w++] = sample(float(g) / float(grid - 1), 1);
                out[w++] = sample(float(b) / float(grid - 1), 2);
            }
        }
    }
    return out;
}

}  // namespace

CubeLutResult parseCube(std::string_view text) {
    CubeLutResult result;
    CubeLut lut;

    int  declared3d = 0;
    int  declared1d = 0;
    std::vector<float> values;
    int  lineNumber = 0;

    const auto fail = [&](const std::string& why) {
        result.ok = false;
        result.error = "line " + std::to_string(lineNumber) + ": " + why;
        return result;
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view raw =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
        ++lineNumber;

        const std::string_view line = trim(raw);
        // Blank lines are not in the spec's list of what a line may be
        // (section 5.3), but its own reference reader in Annex B skips them,
        // and files in circulation contain them. Tolerated on read.
        if (line.empty()) continue;
        if (line.front() == '#') continue;

        const auto tok = split(line);
        if (tok.empty()) continue;

        if (equalsIgnoringCase(tok[0], "TITLE")) {
            // The rest of the line, with surrounding quotes removed.
            const auto start = line.find(tok[0]) + tok[0].size();
            std::string_view rest = trim(line.substr(start));
            if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
                rest = rest.substr(1, rest.size() - 2);
            }
            lut.title = std::string(rest);
            continue;
        }

        if (equalsIgnoringCase(tok[0], "LUT_3D_SIZE")) {
            if (tok.size() < 2 || !toInt(tok[1], declared3d)) {
                return fail("LUT_3D_SIZE needs one integer");
            }
            if (declared3d < 2 || declared3d > kMaxCubeSize) {
                return fail("LUT_3D_SIZE " + std::to_string(declared3d) +
                            " is outside the 2.." + std::to_string(kMaxCubeSize) +
                            " Orion accepts");
            }
            continue;
        }

        if (equalsIgnoringCase(tok[0], "LUT_1D_SIZE")) {
            if (tok.size() < 2 || !toInt(tok[1], declared1d)) {
                return fail("LUT_1D_SIZE needs one integer");
            }
            if (declared1d < 2 || declared1d > 65536) {
                return fail("LUT_1D_SIZE " + std::to_string(declared1d) +
                            " is outside 2..65536");
            }
            continue;
        }

        if (equalsIgnoringCase(tok[0], "DOMAIN_MIN") ||
            equalsIgnoringCase(tok[0], "DOMAIN_MAX")) {
            const bool isMin = equalsIgnoringCase(tok[0], "DOMAIN_MIN");
            if (tok.size() < 4) return fail(std::string(tok[0]) + " needs three numbers");
            for (int c = 0; c < 3; ++c) {
                float v = 0.0f;
                if (!toFloat(tok[static_cast<std::size_t>(c) + 1], v)) {
                    return fail(std::string(tok[0]) + " has a value that is not a number");
                }
                (isMin ? lut.domainMin : lut.domainMax)[static_cast<std::size_t>(c)] = v;
            }
            continue;
        }

        // Anything else has to be a data row.
        if (tok.size() != 3) {
            return fail("expected three numbers or a keyword, found " +
                        std::to_string(tok.size()) + " fields");
        }
        for (int c = 0; c < 3; ++c) {
            float v = 0.0f;
            if (!toFloat(tok[static_cast<std::size_t>(c)], v)) {
                return fail("'" + std::string(tok[static_cast<std::size_t>(c)]) +
                            "' is not a number");
            }
            values.push_back(v);
        }
    }

    if (declared3d == 0 && declared1d == 0) {
        result.error = "no LUT_3D_SIZE or LUT_1D_SIZE — this is not a .cube file";
        return result;
    }
    if (declared3d != 0 && declared1d != 0) {
        result.error = "declares both LUT_3D_SIZE and LUT_1D_SIZE";
        return result;
    }

    for (int c = 0; c < 3; ++c) {
        if (!(lut.domainMax[static_cast<std::size_t>(c)] >
              lut.domainMin[static_cast<std::size_t>(c)])) {
            result.error = "DOMAIN_MAX is not above DOMAIN_MIN";
            return result;
        }
    }

    if (declared3d != 0) {
        const std::size_t want =
            static_cast<std::size_t>(declared3d) * declared3d * declared3d * 3;
        if (values.size() != want) {
            result.error = "LUT_3D_SIZE " + std::to_string(declared3d) + " needs " +
                           std::to_string(want / 3) + " rows, found " +
                           std::to_string(values.size() / 3);
            return result;
        }
        lut.size = declared3d;
        lut.data = std::move(values);
    } else {
        const std::size_t want = static_cast<std::size_t>(declared1d) * 3;
        if (values.size() != want) {
            result.error = "LUT_1D_SIZE " + std::to_string(declared1d) + " needs " +
                           std::to_string(declared1d) + " rows, found " +
                           std::to_string(values.size() / 3);
            return result;
        }
        lut.size = kOneDimensionalLift;
        lut.data = liftOneDimensional(values, declared1d, kOneDimensionalLift);
        lut.wasOneDimensional = true;
    }

    result.ok = lut.valid();
    if (!result.ok) result.error = "internal: table did not come out the declared size";
    result.lut = std::move(lut);
    return result;
}

}  // namespace orion::pipe

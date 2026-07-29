#include "pipe/LensDatabase.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace orion::pipe {
namespace {

/// A lens name with the noise taken out.
///
/// EXIF and the database rarely agree on spelling: `FE 24-70mm F2.8 GM` against
/// `Sony FE 24-70mm f/2.8 GM`, `E PZ 16-50mm F3.5-5.6 OSS` against
/// `Sony E PZ 16-50mm f/3.5-5.6 OSS`. Lowercasing and dropping everything that
/// is not a letter or a digit collapses the punctuation, the `f/` and the case
/// all at once, and leaves the numbers — which are the part that actually
/// identifies the lens — intact.
std::string normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        if (std::isalnum(ch)) out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

/// The value of `name="..."` inside one tag, or `fallback`.
float attribute(const std::string& tag, const char* name, float fallback) {
    const std::string key = std::string(name) + "=\"";
    const auto at = tag.find(key);
    if (at == std::string::npos) return fallback;
    try {
        return std::stof(tag.substr(at + key.size()));
    } catch (...) {
        return fallback;
    }
}

std::string textAttribute(const std::string& tag, const char* name) {
    const std::string key = std::string(name) + "=\"";
    const auto at = tag.find(key);
    if (at == std::string::npos) return {};
    const auto start = at + key.size();
    const auto end = tag.find('"', start);
    return end == std::string::npos ? std::string{} : tag.substr(start, end - start);
}

/// The text between `<tag>` and `</tag>`, ignoring the localized copies that
/// carry an `xml:lang` attribute — those are translations of the same name and
/// matching against them would report a German spelling for an English file.
std::string element(const std::string& block, const char* tag) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    const auto at = block.find(open);
    if (at == std::string::npos) return {};
    const auto end = block.find(close, at);
    if (end == std::string::npos) return {};
    return block.substr(at + open.size(), end - at - open.size());
}

/// Linear interpolation between the two calibrations that bracket `x`, and the
/// nearest endpoint outside the range.
///
/// ⚠️ lensfun's own interpolation is cubic in a transformed variable. Linear is
/// a deliberate simplification: calibrations are dense in focal length (seven
/// points across an 18–55 zoom above), the coefficients vary smoothly between
/// them, and the difference is far below what the correction itself is worth.
/// Stated here rather than implied — see research/lens-corrections.md.
void interpolate(const std::vector<LensDatabase::Calibration>& points, float x,
                 float out[3]) {
    if (points.empty()) return;
    if (points.size() == 1 || x <= points.front().focal) {
        for (int i = 0; i < 3; ++i) out[i] = points.front().coeff[i];
        return;
    }
    if (x >= points.back().focal) {
        for (int i = 0; i < 3; ++i) out[i] = points.back().coeff[i];
        return;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (x > points[i].focal) continue;
        const auto& lo = points[i - 1];
        const auto& hi = points[i];
        const float span = hi.focal - lo.focal;
        const float t = span > 0.0f ? (x - lo.focal) / span : 0.0f;
        for (int j = 0; j < 3; ++j) out[j] = lo.coeff[j] + t * (hi.coeff[j] - lo.coeff[j]);
        return;
    }
}

}  // namespace

LensDatabase::LensDatabase(const std::string& directory) {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.path().extension() != ".xml") continue;
        std::ifstream in(entry.path());
        if (!in) continue;
        std::ostringstream buffer;
        buffer << in.rdbuf();
        parse(buffer.str());
    }

    // Sorted once so every lookup is a binary-searchable walk rather than a
    // sort per photo.
    for (auto& lens : lenses_) {
        const auto byFocal = [](const Calibration& a, const Calibration& b) {
            return a.focal < b.focal;
        };
        std::sort(lens.distortion.begin(), lens.distortion.end(), byFocal);
        std::sort(lens.vignetting.begin(), lens.vignetting.end(), byFocal);
    }
}

void LensDatabase::parse(const std::string& xml) {
    std::size_t at = 0;
    while ((at = xml.find("<lens>", at)) != std::string::npos) {
        const auto end = xml.find("</lens>", at);
        if (end == std::string::npos) break;
        const std::string block = xml.substr(at, end - at);
        at = end + 1;

        Lens lens;
        lens.maker = element(block, "maker");
        lens.model = element(block, "model");
        if (lens.model.empty()) continue;
        lens.normalized = normalize(lens.maker + lens.model);

        // Distortion. poly3 stores k₁ in `k1`; ptlens stores a, b, c. They are
        // the same polynomial — poly3 is ptlens with a and c at zero — so both
        // land in the same three coefficients and the shader needs one model.
        std::size_t d = 0;
        while ((d = block.find("<distortion ", d)) != std::string::npos) {
            const auto tagEnd = block.find('>', d);
            if (tagEnd == std::string::npos) break;
            const std::string tag = block.substr(d, tagEnd - d);
            d = tagEnd;

            const std::string model = textAttribute(tag, "model");
            Calibration c;
            c.focal = attribute(tag, "focal", 0.0f);
            if (model == "ptlens") {
                c.coeff[0] = attribute(tag, "a", 0.0f);
                c.coeff[1] = attribute(tag, "b", 0.0f);
                c.coeff[2] = attribute(tag, "c", 0.0f);
            } else if (model == "poly3") {
                c.coeff[1] = attribute(tag, "k1", 0.0f);
            } else {
                // poly5 and linear exist in five entries between them. Skipping
                // is honest; approximating them with a model they are not would
                // put a wrong correction under a label that says "measured".
                continue;
            }
            if (c.focal > 0.0f) lens.distortion.push_back(c);
        }

        // Vignetting, at the focus distance the calibration calls infinity.
        //
        // Three axes are published — focal, aperture, distance — and Orion
        // interpolates one. Distance is fixed at the largest calibrated value
        // because a landscape or a portrait is nearer infinity than the 0.25 m
        // macro end, and aperture takes the nearest calibrated stop. Both are
        // stated in the research entry as simplifications.
        float bestDistance = 0.0f;
        std::size_t v = 0;
        while ((v = block.find("<vignetting ", v)) != std::string::npos) {
            const auto tagEnd = block.find('>', v);
            if (tagEnd == std::string::npos) break;
            bestDistance = std::max(bestDistance,
                                    attribute(block.substr(v, tagEnd - v), "distance", 0.0f));
            v = tagEnd;
        }

        v = 0;
        while ((v = block.find("<vignetting ", v)) != std::string::npos) {
            const auto tagEnd = block.find('>', v);
            if (tagEnd == std::string::npos) break;
            const std::string tag = block.substr(v, tagEnd - v);
            v = tagEnd;

            if (textAttribute(tag, "model") != "pa") continue;
            if (attribute(tag, "distance", 0.0f) < bestDistance) continue;

            Calibration c;
            c.focal = attribute(tag, "focal", 0.0f);
            c.aperture = attribute(tag, "aperture", 0.0f);
            c.coeff[0] = attribute(tag, "k1", 0.0f);
            c.coeff[1] = attribute(tag, "k2", 0.0f);
            c.coeff[2] = attribute(tag, "k3", 0.0f);
            if (c.focal > 0.0f) lens.vignetting.push_back(c);
        }

        if (!lens.distortion.empty() || !lens.vignetting.empty()) {
            lenses_.push_back(std::move(lens));
        }
    }
}

LensProfile LensDatabase::lookup(const std::string& lensName,
                                 float focalLength, float aperture) const {
    LensProfile profile;
    if (lensName.empty() || lenses_.empty()) return profile;

    const std::string want = normalize(lensName);

    // Eight characters of signal before containment is trusted. "50mm" is
    // inside a hundred entries and identifies none of them, and a confident
    // wrong profile is worse than no profile: it distorts the frame and says
    // it measured it.
    if (want.size() < 8) return profile;

    // Longest containment either way. A database name usually carries the maker
    // the EXIF string omits, so the EXIF name sits *inside* it; the reverse
    // happens when EXIF is more specific than the entry. Longest wins because a
    // short name is inside many entries — "e30mmf35macro" would otherwise match
    // the first 30 mm lens in the file.
    const Lens* best = nullptr;
    std::size_t bestLength = 0;
    for (const auto& lens : lenses_) {
        const bool contains = lens.normalized.find(want) != std::string::npos ||
                              want.find(lens.normalized) != std::string::npos;
        if (!contains) continue;
        if (lens.normalized.size() > bestLength) {
            best = &lens;
            bestLength = lens.normalized.size();
        }
    }
    if (best == nullptr) return profile;

    profile.found = true;
    profile.maker = best->maker;
    profile.lens = best->model;
    profile.approximate = normalize(best->maker + best->model) != want;

    interpolate(best->distortion, focalLength, profile.poly);

    // Vignetting: interpolated in focal length within each calibrated aperture,
    // then between apertures **in the reciprocal**.
    //
    // This used to take the nearest calibrated aperture, on the reasoning that
    // falloff changes fast between wide open and a stop down so a value between
    // two stops is not a value at either. lensfun disagrees, and it is the
    // authority here: `lfLens::InterpolateVignetting` interpolates aperture
    // rather than selecting, and does it against **1/aperture** — which is the
    // right variable, because vignetting tracks the entrance pupil and the
    // f-number is a ratio, so equal steps in 1/N are equal steps in what the
    // lens is actually doing. Nearest-stop also means a lens calibrated at f/2.8
    // and f/5.6 renders f/4 and f/5.5 identically and then jumps.
    //
    // lensfun weights by inverse distance over three axes; this interpolates
    // linearly between the two bracketing apertures, which is the same thing in
    // one dimension. Focus distance stays at infinity — see the note above.
    if (!best->vignetting.empty()) {
        std::vector<float> stops;
        for (const auto& c : best->vignetting) {
            if (std::find(stops.begin(), stops.end(), c.aperture) == stops.end()) {
                stops.push_back(c.aperture);
            }
        }
        std::sort(stops.begin(), stops.end());

        const auto atStop = [&](float stop, float out[3]) {
            std::vector<Calibration> points;
            for (const auto& c : best->vignetting) {
                if (c.aperture == stop) points.push_back(c);
            }
            interpolate(points, focalLength, out);
        };

        if (stops.size() == 1 || aperture <= stops.front()) {
            atStop(stops.front(), profile.vignette);
        } else if (aperture >= stops.back()) {
            atStop(stops.back(), profile.vignette);
        } else {
            std::size_t hi = 1;
            while (hi < stops.size() && stops[hi] < aperture) ++hi;

            float below[3]{}, above[3]{};
            atStop(stops[hi - 1], below);
            atStop(stops[hi],     above);

            // Linear in 1/N, not in N.
            const float ra = 1.0f / std::max(aperture, 1e-3f);
            const float r0 = 1.0f / std::max(stops[hi - 1], 1e-3f);
            const float r1 = 1.0f / std::max(stops[hi],     1e-3f);
            const float t  = (std::abs(r1 - r0) < 1e-9f) ? 0.0f : (ra - r0) / (r1 - r0);

            for (int i = 0; i < 3; ++i) {
                profile.vignette[i] = below[i] + (above[i] - below[i]) * t;
            }
        }
    }

    return profile;
}

}  // namespace orion::pipe

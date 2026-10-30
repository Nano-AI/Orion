#include "pipe/LensLcp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace orion::pipe {
namespace {

/// `stCamera:Name`'s value, in either spelling RDF/XML allows: an attribute
/// (`stCamera:Name="v"`, what Adobe's own files use) or an element
/// (`<stCamera:Name>v</stCamera:Name>`). Empty when absent.
std::string value(const std::string& text, const char* name) {
    const std::string attr = std::string(name) + "=\"";
    if (const auto at = text.find(attr); at != std::string::npos) {
        const auto start = at + attr.size();
        const auto end = text.find('"', start);
        if (end != std::string::npos) return text.substr(start, end - start);
    }
    const std::string open = std::string("<") + name + ">";
    if (const auto at = text.find(open); at != std::string::npos) {
        const auto start = at + open.size();
        const auto end = text.find('<', start);
        if (end != std::string::npos) return text.substr(start, end - start);
    }
    return {};
}

float number(const std::string& text, const char* name, float fallback) {
    const std::string v = value(text, name);
    if (v.empty()) return fallback;
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    return end != v.c_str() && std::isfinite(f) ? f : fallback;
}

/// The next `<tag` that really is `tag` — `<stCamera:VignetteModel` must not
/// stop at `<stCamera:VignetteModelParam1>`.
std::size_t findOpen(const std::string& text, const std::string& open, std::size_t from) {
    for (auto at = text.find(open, from); at != std::string::npos; at = text.find(open, at + 1)) {
        const char next = at + open.size() < text.size() ? text[at + open.size()] : '\0';
        if (next == '>' || next == '/' || next == ' ' || next == '\n' || next == '\r' ||
            next == '\t') return at;
    }
    return std::string::npos;
}

/// Where element `tag` starts and ends, including a self-closing form.
/// {npos, npos} when absent.
std::pair<std::size_t, std::size_t> span(const std::string& text, const std::string& tag,
                                         std::size_t from = 0) {
    const std::string open = "<" + tag;
    if (const auto at = findOpen(text, open, from); at != std::string::npos) {
        const auto tagEnd = text.find('>', at);
        if (tagEnd == std::string::npos) return {std::string::npos, std::string::npos};
        if (text[tagEnd - 1] == '/') return {at, tagEnd + 1};
        // Nested elements of the same name (an rdf:li inside an rdf:li) close
        // in order, so count them.
        int depth = 1;
        auto cursor = tagEnd + 1;
        while (depth > 0) {
            const auto o = findOpen(text, open, cursor);
            const auto c = text.find("</" + tag + ">", cursor);
            if (c == std::string::npos) return {std::string::npos, std::string::npos};
            if (o != std::string::npos && o < c) {
                const auto oEnd = text.find('>', o);
                if (oEnd == std::string::npos) return {std::string::npos, std::string::npos};
                if (text[oEnd - 1] != '/') ++depth;   // a self-closed one opens nothing
                cursor = oEnd + 1;
            }
            else { --depth; cursor = c + tag.size() + 3; }
        }
        return {at, cursor};
    }
    return {std::string::npos, std::string::npos};
}

/// `text` with element `tag` cut out, so a nested model's FocalLengthX is not
/// read as its parent's.
std::string without(std::string text, const std::string& tag) {
    for (;;) {
        const auto [a, b] = span(text, tag);
        if (a == std::string::npos) return text;
        text.erase(a, b - a);
    }
}

/// Linear in focal length between the two calibrations that bracket it, each
/// taken at the calibrated aperture nearest in 1/N (lensfun's variable for the
/// same reason: equal steps in 1/N are equal steps in the entrance pupil).
template <std::size_t N>
bool blend(const std::vector<const AdobeLensProfiles::Sub*>& subs,
           float (AdobeLensProfiles::Sub::*field)[N], float focal, float aperture, float out[N]) {
    if (subs.empty()) return false;
    const auto apertureCost = [&](const AdobeLensProfiles::Sub* s) {
        if (aperture <= 0.0f || s->aperture <= 0.0f) return 0.0f;
        return std::abs(1.0f / s->aperture - 1.0f / aperture);
    };
    // Nearest-aperture representative at each calibrated focal length.
    std::vector<const AdobeLensProfiles::Sub*> line;
    for (const auto* s : subs) {
        auto it = std::find_if(line.begin(), line.end(),
                               [&](const auto* l) { return l->focal == s->focal; });
        if (it == line.end()) line.push_back(s);
        else if (apertureCost(s) < apertureCost(*it)) *it = s;
    }
    std::sort(line.begin(), line.end(), [](auto* a, auto* b) { return a->focal < b->focal; });

    const auto copy = [&](const AdobeLensProfiles::Sub* s) {
        for (std::size_t i = 0; i < N; ++i) out[i] = (s->*field)[i];
    };
    if (line.size() == 1 || focal <= line.front()->focal) { copy(line.front()); return true; }
    if (focal >= line.back()->focal) { copy(line.back()); return true; }
    for (std::size_t i = 1; i < line.size(); ++i) {
        if (focal > line[i]->focal) continue;
        const auto* lo = line[i - 1];
        const auto* hi = line[i];
        const float t = (focal - lo->focal) / (hi->focal - lo->focal);
        for (std::size_t j = 0; j < N; ++j) {
            out[j] = (lo->*field)[j] + t * ((hi->*field)[j] - (lo->*field)[j]);
        }
        return true;
    }
    copy(line.back());
    return true;
}

}  // namespace

AdobeLensProfiles::AdobeLensProfiles(const std::vector<std::string>& directories) {
    namespace fs = std::filesystem;
    for (const auto& dir : directories) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        // Sorted, so which of two profiles for one lens wins does not depend on
        // the order the filesystem happens to list them in.
        std::vector<fs::path> paths;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) break;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".lcp") paths.push_back(it->path());
        }
        std::sort(paths.begin(), paths.end());
        // ponytail: every profile is parsed on first use (Engine holds this in a
        // function-local static). A full Camera Raw install is a few thousand
        // files; pre-filter by camera name if that first open is ever measured slow.
        for (const auto& path : paths) {
            std::ifstream in(path);
            if (!in) continue;
            std::ostringstream buffer;
            buffer << in.rdbuf();
            addText(buffer.str());
        }
    }
}

std::vector<std::string> AdobeLensProfiles::defaultDirectories() {
    constexpr const char* kProfiles = "/Library/Application Support/Adobe/CameraRaw/LensProfiles";
    std::vector<std::string> dirs;
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        dirs.push_back(std::string(home) + kProfiles);
    }
    dirs.push_back(kProfiles);
    return dirs;
}

void AdobeLensProfiles::addText(const std::string& xml) {
    const std::size_t file = files_++;
    std::size_t from = 0;
    for (;;) {
        const auto [a, b] = span(xml, "rdf:li", from);
        if (a == std::string::npos) break;
        from = b;
        const std::string li = xml.substr(a, b - a);

        const auto [pa, pb] = span(li, "stCamera:PerspectiveModel");
        std::string persp = pa == std::string::npos ? std::string{} : li.substr(pa, pb - pa);
        const std::string meta = pa == std::string::npos ? li : li.substr(0, pa) + li.substr(pb);

        Sub s;
        s.file = file;
        // CameraRawProfile="False" is calibrated on the camera's JPEGs, which
        // have the camera's own correction already baked in.
        std::string raw = value(meta, "stCamera:CameraRawProfile");
        std::transform(raw.begin(), raw.end(), raw.begin(), ::tolower);
        if (raw == "false") continue;
        s.camera = normalizeLensName(value(meta, "stCamera:Make") + value(meta, "stCamera:Model"));
        const std::string lensName = value(meta, "stCamera:Lens");
        s.lens = normalizeLensName(lensName);
        s.name = value(meta, "stCamera:LensPrettyName");
        if (s.name.empty()) s.name = lensName;
        if (s.camera.empty()) continue;
        s.focal = number(meta, "stCamera:FocalLength", 0.0f);
        // ApertureValue is APEX: N = 2^(Av / 2).
        const float av = number(meta, "stCamera:ApertureValue", -1.0f);
        s.aperture = av >= 0.0f ? std::exp2(av * 0.5f) : 0.0f;
        s.distance = number(meta, "stCamera:FocusDistance", 0.0f);

        if (!persp.empty()) {
            const auto [va, vb] = span(persp, "stCamera:VignetteModel");
            const std::string vig = va == std::string::npos ? std::string{} : persp.substr(va, vb - va);
            for (const char* nested : {"stCamera:VignetteModel", "stCamera:ChromaticRedGreenModel",
                                       "stCamera:ChromaticBlueGreenModel", "stCamera:ChromaticGreenModel"}) {
                persp = without(persp, nested);
            }
            // The image center defaults to 0.5 when absent (the format's own
            // default); a focal length has none and is required.
            const float fx = number(persp, "stCamera:FocalLengthX", 0.0f);
            const float fy = number(persp, "stCamera:FocalLengthY", fx);
            const float cx = number(persp, "stCamera:ImageXCenter", 0.5f);
            const float cy = number(persp, "stCamera:ImageYCenter", 0.5f);
            if (fx > 0.0f && fy > 0.0f) {
                s.hasPersp = true;
                const float p[9] = {fx, fy, cx, cy,
                                    number(persp, "stCamera:RadialDistortParam1", 0.0f),
                                    number(persp, "stCamera:RadialDistortParam2", 0.0f),
                                    number(persp, "stCamera:RadialDistortParam3", 0.0f),
                                    number(persp, "stCamera:TangentialDistortParam1", 0.0f),
                                    number(persp, "stCamera:TangentialDistortParam2", 0.0f)};
                std::copy(p, p + 9, s.persp);
            }
            if (!vig.empty() && s.hasPersp) {
                // The vignette model may restate the camera intrinsics; when it
                // does not, they are the perspective model's.
                s.hasVig = true;
                const float v[7] = {number(vig, "stCamera:FocalLengthX", fx),
                                    number(vig, "stCamera:FocalLengthY", fy),
                                    number(vig, "stCamera:ImageXCenter", cx),
                                    number(vig, "stCamera:ImageYCenter", cy),
                                    number(vig, "stCamera:VignetteModelParam1", 0.0f),
                                    number(vig, "stCamera:VignetteModelParam2", 0.0f),
                                    number(vig, "stCamera:VignetteModelParam3", 0.0f)};
                std::copy(v, v + 7, s.vig);
            }
        }
        if (s.hasPersp) subs_.push_back(std::move(s));
    }
}

LensProfile AdobeLensProfiles::lookup(const std::string& camera, const std::string& lens,
                                      float focalLength, float aperture) const {
    LensProfile profile;
    const std::string body = normalizeLensName(camera);
    const std::string glass = normalizeLensName(lens);
    if (body.empty()) return profile;

    // The first file that has this body (and, where both record one, this
    // lens) is the profile; other files describing it are not blended in.
    std::vector<const Sub*> subs;
    for (const auto& s : subs_) {
        if (s.camera != body) continue;
        if (!s.lens.empty() && !glass.empty() && s.lens != glass) continue;
        if (!subs.empty() && s.file != subs.front()->file) continue;
        subs.push_back(&s);
    }
    if (subs.empty()) return profile;

    // Focus distance: the farthest calibrated, as the lensfun path does — a
    // landscape or a portrait is nearer infinity than the close-up end.
    float far = 0.0f;
    for (const auto* s : subs) far = std::max(far, s->distance);
    std::erase_if(subs, [&](const Sub* s) { return s->distance < far; });

    float p[9]{};
    blend<9>(subs, &Sub::persp, focalLength, aperture, p);
    std::vector<const Sub*> withVig;
    for (const auto* s : subs) if (s->hasVig) withVig.push_back(s);
    float v[7]{};
    const bool vig = blend<7>(withVig, &Sub::vig, focalLength, aperture, v);

    LensModel& m = profile.model;
    m.norm = LensNorm::MaxDimension;
    m.focal[0] = p[0]; m.focal[1] = p[1];
    m.center[0] = p[2]; m.center[1] = p[3];
    for (int ch = 0; ch < 3; ++ch) {
        // 1 + k1·r² + k2·r⁴ + k3·r⁶ into k0, k2, k4, k6; Adobe's k4, k5 are
        // DNG's kt0, kt1 term for term.
        m.radial[ch][0] = 1.0f;
        m.radial[ch][2] = p[4];
        m.radial[ch][4] = p[5];
        m.radial[ch][5] = p[6];
        m.tangential[ch][0] = p[7];
        m.tangential[ch][1] = p[8];
    }
    if (vig) {
        m.vigNorm = LensNorm::MaxDimension;
        m.vigFocal[0] = v[0]; m.vigFocal[1] = v[1];
        m.vigCenter[0] = v[2]; m.vigCenter[1] = v[3];
        m.vig[0] = v[4]; m.vig[1] = v[5]; m.vig[2] = v[6];
    }

    profile.found = true;
    profile.source = LensSource::AdobeLcp;
    profile.lens = subs.front()->name;
    profile.approximate = false;
    return profile;
}

}  // namespace orion::pipe

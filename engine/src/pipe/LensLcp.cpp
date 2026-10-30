#include "pipe/LensLcp.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlreader.h>

#include <algorithm>
#include <xlocale.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace orion::pipe {
namespace {

namespace fs = std::filesystem;

/// No network, no entity expansion (an LCP has no business with either), and
/// no complaints on stderr about a file a user dropped in the folder.
constexpr int kXmlOptions = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING;

/// A whole profile is tens of kilobytes; anything this large is not one.
constexpr std::uintmax_t kMaxFileBytes = 4u << 20;

std::string str(const xmlChar* s) { return s ? reinterpret_cast<const char*>(s) : std::string{}; }

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

bool named(const xmlNode* n, const char* local) {
    return n && n->type == XML_ELEMENT_NODE &&
           xmlStrcmp(n->name, reinterpret_cast<const xmlChar*>(local)) == 0;
}

/// Where an RDF resource's properties can sit: on the element itself
/// (`rdf:parseType="Resource"`, or attributes on it) or on an `rdf:Description`
/// child (what Adobe writes). Matching is by local name, whatever the prefix.
std::vector<const xmlNode*> facets(const xmlNode* n) {
    std::vector<const xmlNode*> out{n};
    for (const xmlNode* c = n->children; c; c = c->next) {
        if (named(c, "Description")) out.push_back(c);
    }
    return out;
}

/// The property `name` of resource `n`, as an attribute or as a child
/// element's text — the two spellings RDF/XML allows. Empty when absent.
/// Only `n`'s own properties: a nested model's FocalLengthX is a property of
/// that model, one level further down, and is never seen here.
std::string prop(const xmlNode* n, const char* name) {
    for (const xmlNode* f : facets(n)) {
        // xmlGetProp ignores namespaces, which is what is wanted here.
        if (xmlChar* v = xmlGetProp(f, reinterpret_cast<const xmlChar*>(name))) {
            std::string s = str(v);
            xmlFree(v);
            return trim(s);
        }
        for (const xmlNode* c = f->children; c; c = c->next) {
            if (!named(c, name)) continue;
            xmlChar* v = xmlNodeGetContent(c);
            std::string s = str(v);
            xmlFree(v);
            return trim(s);
        }
    }
    return {};
}

const xmlNode* child(const xmlNode* n, const char* name) {
    for (const xmlNode* f : facets(n)) {
        for (const xmlNode* c = f->children; c; c = c->next) {
            if (named(c, name)) return c;
        }
    }
    return nullptr;
}

const xmlNode* find(const xmlNode* n, const char* name) {
    for (; n; n = n->next) {
        if (named(n, name)) return n;
        if (const xmlNode* hit = find(n->children, name)) return hit;
    }
    return nullptr;
}

float number(const xmlNode* n, const char* name, float fallback) {
    // The "C" locale, created once: plain strtof reads "0.72" as 0 under a
    // locale whose decimal separator is a comma. (std::from_chars for float
    // needs macOS 26; Orion's floor is 14.)
    static const locale_t c = newlocale(LC_ALL_MASK, "C", nullptr);
    const std::string v = prop(n, name);
    if (v.empty()) return fallback;
    char* end = nullptr;
    const float f = strtof_l(v.c_str(), &end, c);
    return end != v.c_str() && std::isfinite(f) ? f : fallback;
}

/// A file's camera body, reading only as far as its first Make and Model.
/// An LCP describes one body and one lens, every sub-profile included (Adobe
/// Lens Profile Creator User Guide, 2010), so the first answer is the file's.
std::string readBody(const std::string& path) {
    xmlTextReaderPtr reader = xmlReaderForFile(path.c_str(), nullptr, kXmlOptions);
    if (!reader) return {};
    std::string make, model;
    const auto take = [&](const std::string& local, const std::string& value) {
        if (local == "Make" && make.empty()) make = trim(value);
        if (local == "Model" && model.empty()) model = trim(value);
    };
    while ((make.empty() || model.empty()) && xmlTextReaderRead(reader) == 1) {
        if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) continue;
        const std::string local = str(xmlTextReaderConstLocalName(reader));
        // The camera's own fields precede the models; past them there is
        // nothing more to learn.
        if (local == "PerspectiveModel") break;
        if (local == "Make" || local == "Model") {
            xmlChar* text = xmlTextReaderReadString(reader);
            take(local, str(text));
            xmlFree(text);
        }
        while (xmlTextReaderMoveToNextAttribute(reader) == 1) {
            take(str(xmlTextReaderConstLocalName(reader)), str(xmlTextReaderConstValue(reader)));
        }
    }
    xmlFreeTextReader(reader);
    return model.empty() ? std::string{} : AdobeLensProfiles::bodyKey(make, model);
}

/// Every raw sub-profile in one file.
std::vector<AdobeLensProfiles::Sub> parseFile(const std::string& path) {
    std::vector<AdobeLensProfiles::Sub> subs;
    std::error_code ec;
    if (fs::file_size(path, ec) > kMaxFileBytes || ec) return subs;
    xmlDocPtr doc = xmlReadFile(path.c_str(), nullptr, kXmlOptions);
    if (!doc) return subs;

    const xmlNode* profiles = find(xmlDocGetRootElement(doc), "CameraProfiles");
    const xmlNode* seq = profiles ? child(profiles, "Seq") : nullptr;
    for (const xmlNode* li = seq ? seq->children : nullptr; li; li = li->next) {
        if (!named(li, "li")) continue;
        // CameraRawProfile="False" was calibrated on the camera's JPEGs, which
        // have the camera's own correction baked in already.
        std::string raw = prop(li, "CameraRawProfile");
        std::transform(raw.begin(), raw.end(), raw.begin(), ::tolower);
        if (raw == "false") continue;
        const xmlNode* persp = child(li, "PerspectiveModel");
        if (!persp) continue;

        AdobeLensProfiles::Sub s;
        s.body = AdobeLensProfiles::bodyKey(prop(li, "Make"), prop(li, "Model"));
        const std::string lensName = prop(li, "Lens");
        s.lens = normalizeLensName(lensName);
        s.name = prop(li, "LensPrettyName");
        if (s.name.empty()) s.name = lensName;
        s.focal = number(li, "FocalLength", 0.0f);
        // ApertureValue is APEX: N = 2^(Av / 2).
        const float av = number(li, "ApertureValue", -1.0f);
        s.aperture = av >= 0.0f ? std::exp2(av * 0.5f) : 0.0f;
        s.distance = number(li, "FocusDistance", 0.0f);

        // The centre defaults to 0.5 when absent; a focal length has no
        // default and is required.
        const float fx = number(persp, "FocalLengthX", 0.0f);
        const float fy = number(persp, "FocalLengthY", fx);
        const float cx = number(persp, "ImageXCenter", 0.5f);
        const float cy = number(persp, "ImageYCenter", 0.5f);
        if (!(fx > 0.0f && fy > 0.0f)) continue;
        const float p[9] = {fx, fy, cx, cy,
                            number(persp, "RadialDistortParam1", 0.0f),
                            number(persp, "RadialDistortParam2", 0.0f),
                            number(persp, "RadialDistortParam3", 0.0f),
                            number(persp, "TangentialDistortParam1", 0.0f),
                            number(persp, "TangentialDistortParam2", 0.0f)};
        std::copy(p, p + 9, s.persp);

        if (const xmlNode* vig = child(persp, "VignetteModel")) {
            // It may restate the camera intrinsics; when it does not, they are
            // the perspective model's.
            s.hasVig = true;
            const float v[7] = {number(vig, "FocalLengthX", fx), number(vig, "FocalLengthY", fy),
                                number(vig, "ImageXCenter", cx), number(vig, "ImageYCenter", cy),
                                number(vig, "VignetteModelParam1", 0.0f),
                                number(vig, "VignetteModelParam2", 0.0f),
                                number(vig, "VignetteModelParam3", 0.0f)};
            std::copy(v, v + 7, s.vig);
        }
        subs.push_back(std::move(s));
    }
    xmlFreeDoc(doc);
    return subs;
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

AdobeLensProfiles::AdobeLensProfiles(std::vector<std::string> directories)
    : directories_(std::move(directories)) {
    xmlInitParser();   // once, before any thread uses the parser
    indexer_ = std::thread([this] { index(); });
}

AdobeLensProfiles::~AdobeLensProfiles() {
    stop_ = true;
    if (indexer_.joinable()) indexer_.join();
}

void AdobeLensProfiles::index() {
    // ⚠ A background thread: nothing may escape it, or the process ends.
    try {
        std::vector<std::string> paths;
        for (const auto& dir : directories_) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            const std::size_t first = paths.size();
            // The user guide's own words: either folder "or any of their
            // sub-folders".
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end && !stop_; it.increment(ec)) {
                std::string ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                std::error_code sizeEc;
                if (ext == ".lcp" && it->file_size(sizeEc) <= kMaxFileBytes && !sizeEc) {
                    paths.push_back(it->path().string());
                }
            }
            // Sorted within each folder, user folder first, so which of two
            // files for one body wins does not depend on the filesystem's order.
            std::sort(paths.begin() + std::ptrdiff_t(first), paths.end());
        }
        for (const auto& path : paths) {
            if (stop_) break;
            const std::string body = readBody(path);
            if (body.empty()) continue;
            std::lock_guard lock(mutex_);
            bodies_[body].push_back(path);
        }
    } catch (...) {
        // An index that failed part way is an index with fewer bodies in it.
    }
    {
        std::lock_guard lock(mutex_);
        ready_ = true;
    }
    readyChanged_.notify_all();
}

void AdobeLensProfiles::waitUntilReady() const {
    std::unique_lock lock(mutex_);
    readyChanged_.wait(lock, [this] { return ready_.load(); });
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

std::string AdobeLensProfiles::bodyKey(const std::string& make, const std::string& model) {
    // The make's first word is the brand: "NIKON CORPORATION", "OLYMPUS
    // IMAGING CORP." and "Nikon" all say nikon / olympus. A model that repeats
    // the brand ("NIKON D850", "Canon EOS 5D Mark IV") loses it.
    // ponytail: a rebranded maker (Pentax bodies sold as RICOH IMAGING) still
    // differs; add an alias table when a real profile shows the need.
    const std::string brand = normalizeLensName(make.substr(0, make.find(' ')));
    std::string body = normalizeLensName(model);
    if (!brand.empty() && body.rfind(brand, 0) == 0 && body.size() > brand.size()) {
        body.erase(0, brand.size());
    }
    return brand + "|" + body;
}

const std::vector<AdobeLensProfiles::Sub>& AdobeLensProfiles::parsed(const std::string& path) const {
    // Called with mutex_ held.
    auto it = cache_.find(path);
    if (it == cache_.end()) it = cache_.emplace(path, parseFile(path)).first;
    return it->second;
}

LensProfile AdobeLensProfiles::lookup(const std::string& make, const std::string& model,
                                      const std::string& lens, float focalLength,
                                      float aperture) const {
    LensProfile profile;
    // Never wait for the index: this photo gets lensfun instead.
    if (!ready_ || model.empty()) return profile;
    const std::string body = bodyKey(make, model);
    const std::string glass = normalizeLensName(lens);

    std::lock_guard lock(mutex_);
    const auto files = bodies_.find(body);
    if (files == bodies_.end()) return profile;
    const bool oneFile = files->second.size() == 1;

    // The first file with a fitting sub-profile is the profile; another file
    // describing the same lens is not blended in.
    std::vector<const Sub*> subs;
    for (const auto& path : files->second) {
        for (const auto& s : parsed(path)) {
            if (s.body != body) continue;
            if (glass.empty()) {
                // No lens name on the photo: only a profile with none either,
                // or the body's single file. Never whichever lens sorts first.
                if (!s.lens.empty() && !oneFile) continue;
            } else if (!s.lens.empty() && s.lens != glass) {
                continue;
            }
            subs.push_back(&s);
        }
        if (!subs.empty()) break;
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

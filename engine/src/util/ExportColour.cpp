#include "util/ExportColour.h"

#include <lcms2.h>
#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace orion::util {
namespace {

struct ProfileCloser { void operator()(void* p) const { cmsCloseProfile(p); } };
struct CurveFreer { void operator()(cmsToneCurve* c) const { cmsFreeToneCurve(c); } };
struct MluFreer { void operator()(cmsMLU* m) const { cmsMLUfree(m); } };
using Profile = std::unique_ptr<void, ProfileCloser>;
using Curve = std::unique_ptr<cmsToneCurve, CurveFreer>;

/// IEC 61966-2-1's curve, tabulated: a v2 profile has no parametric curve
/// type, and 1024 entries put it within 1e-6 of the formula.
Curve srgbCurve() {
    const cmsFloat64Number params[5] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
    const Curve exact(cmsBuildParametricToneCurve(nullptr, 4, params));
    if (!exact) throw std::runtime_error("lcms: sRGB curve");
    cmsUInt16Number table[1024];
    for (int i = 0; i < 1024; ++i) {
        const double v = cmsEvalToneCurveFloat(exact.get(), cmsFloat32Number(i / 1023.0));
        table[i] = cmsUInt16Number(std::lround(std::clamp(v, 0.0, 1.0) * 65535.0));
    }
    return Curve(cmsBuildTabulatedToneCurve16(nullptr, 1024, table));
}

Profile build(ColorSpace space) {
    const cmsCIExyY d65 = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries;
    Curve curve;
    const char* name = "sRGB IEC61966-2.1";
    switch (space) {
        case ColorSpace::Srgb:
            primaries = {{0.64, 0.33, 1.0}, {0.30, 0.60, 1.0}, {0.15, 0.06, 1.0}};
            curve = srgbCurve();
            break;
        case ColorSpace::DisplayP3:
            primaries = {{0.680, 0.320, 1.0}, {0.265, 0.690, 1.0}, {0.150, 0.060, 1.0}};
            curve = srgbCurve();
            name = "Display P3";
            break;
        case ColorSpace::AdobeRgb:
            primaries = {{0.64, 0.33, 1.0}, {0.21, 0.71, 1.0}, {0.15, 0.06, 1.0}};
            curve = Curve(cmsBuildGamma(nullptr, 563.0 / 256.0));
            name = "Adobe RGB (1998) compatible";
            break;
    }
    if (!curve) throw std::runtime_error("lcms: tone curve");
    cmsToneCurve* curves[3] = {curve.get(), curve.get(), curve.get()};
    Profile p(cmsCreateRGBProfile(&d65, &primaries, curves));
    if (!p) throw std::runtime_error("lcms: could not build a profile");

    cmsSetProfileVersion(p.get(), 2.4);
    // v2 states the white the profile was made for; the colorants stay
    // adapted to D50, and the chad tag cmsCreateRGBProfile wrote says how.
    cmsCIEXYZ white;
    cmsxyY2XYZ(&white, &d65);
    const std::unique_ptr<cmsMLU, MluFreer> desc(cmsMLUalloc(nullptr, 1));
    if (!desc || !cmsMLUsetASCII(desc.get(), "en", "US", name) ||
        !cmsWriteTag(p.get(), cmsSigProfileDescriptionTag, desc.get()) ||
        !cmsWriteTag(p.get(), cmsSigMediaWhitePointTag, &white)) {
        throw std::runtime_error("lcms: profile tags");
    }
    return p;
}

}  // namespace

std::vector<std::uint8_t> iccProfile(ColorSpace space) {
    const Profile p = build(space);
    cmsUInt32Number size = 0;
    if (!cmsSaveProfileToMem(p.get(), nullptr, &size) || size == 0) {
        throw std::runtime_error("lcms: could not size the profile");
    }
    std::vector<std::uint8_t> out(size);
    if (!cmsSaveProfileToMem(p.get(), out.data(), &size)) {
        throw std::runtime_error("lcms: could not save the profile");
    }
    out.resize(size);
    return out;
}

void convertFromSrgb(std::uint16_t* rgba, std::uint32_t width, std::uint32_t height,
                     std::size_t bytesPerRow, ColorSpace space) {
    if (space == ColorSpace::Srgb || width == 0 || height == 0) return;
    const Profile from = build(ColorSpace::Srgb);
    const Profile to = build(space);
    // NOCACHE makes one transform safe to share between threads (lcms2 API,
    // "Multithreading"). In place, the fourth sample is never written.
    const cmsHTRANSFORM t = cmsCreateTransform(from.get(), TYPE_RGBA_16, to.get(), TYPE_RGBA_16,
                                               INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOCACHE);
    if (t == nullptr) throw std::runtime_error("lcms: could not create the transform");
    auto* base = reinterpret_cast<std::uint8_t*>(rgba);
    cv::parallel_for_(cv::Range(0, int(height)), [&](const cv::Range& rows) {
        std::uint8_t* first = base + std::size_t(rows.start) * bytesPerRow;
        cmsDoTransformLineStride(t, first, first, width, cmsUInt32Number(rows.size()),
                                 cmsUInt32Number(bytesPerRow), cmsUInt32Number(bytesPerRow), 0, 0);
    });
    cmsDeleteTransform(t);
}

}  // namespace orion::util

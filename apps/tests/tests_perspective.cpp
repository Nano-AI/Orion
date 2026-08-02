// Perspective correction — the homography, the composition, and the zoom.
//
// research/perspective.md. Hartley & Zisserman, *Multiple View Geometry in
// Computer Vision*, 2nd ed., CUP 2004, §2.3, §4.1, §4.1.2, §4.4.4.
//
// Every check here exists because the thing it asserts fails invisibly. A
// transposed matrix, a dropped perspective divide and a second sampling pass
// all render a picture; two of the three render a *plausible* picture.

#include "harness.h"

#include "pipe/Perspective.h"

namespace {

namespace persp = orion::pipe::persp;

constexpr std::uint32_t kW = 96, kH = 64;

/// Coordinates as color, so every output pixel names the source pixel it came
/// from and the GPU's answer can be compared with the host's exactly.
std::vector<__fp16> coordinateFrame(std::uint32_t w, std::uint32_t h) {
    std::vector<__fp16> input(std::size_t(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = (std::size_t(y) * w + x) * 4;
            input[i + 0] = static_cast<__fp16>(x);
            input[i + 1] = static_cast<__fp16>(y);
            input[i + 2] = 0;
            input[i + 3] = 1;
        }
    }
    return input;
}

void loadMatrix(orion::pipe::params::Geometry& g, const persp::Matrix3& h) {
    g.perspectiveOn = persp::isIdentity(h) ? 0u : 1u;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) g.perspective[r][c] = h.m[r * 3 + c];
        g.perspective[r][3] = 0.0f;
    }
}

}  // namespace

/// The pieces that must be *exactly* right, on the host, before anything is
/// dispatched.
void testPerspectiveMath() {
    section("Perspective — the matrix");

    // 1. Neutral is exactly neutral. Not "close to the identity": a control at
    //    zero has to leave the picture byte-for-byte alone, and every baseline
    //    in every suite depends on it.
    const auto neutral = persp::keystone({});
    CHECK(persp::isIdentity(neutral));
    report(persp::autoScale(neutral) == 1.0f,
           "auto-scale on a neutral correction is exactly 1.0");
    CHECK(persp::isIdentity(persp::compose({}, float(kW), float(kH))));
    CHECK(persp::isIdentity(persp::inverse(neutral)));

    // And at a *real* frame's dimensions, which is where this can actually
    // fail. `inTexels` conjugates by T and T-inverse; T·I·T⁻¹ is the identity in
    // exact arithmetic and need not be in float, and 1/3012 is not a number
    // float can hold. The fixture sizes above are powers of two and would let a
    // missing guard through.
    CHECK(persp::isIdentity(persp::inTexels(persp::identity(), 6024.0f, 4024.0f)));
    CHECK(persp::isIdentity(persp::compose({}, 6024.0f, 4024.0f)));
    CHECK(persp::isIdentity(persp::compose({}, 4023.0f, 6021.0f)));

    // 2. The DLT reproduces the correspondences it was given. H&Z §4.1: four
    //    points in general position determine the homography, so it must pass
    //    through all four and not merely near them.
    {
        const float from[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        const float to[4][2]   = {{-0.7f, -1.0f}, {0.7f, -1.0f},
                                  {-1.3f,  1.0f}, {1.3f,  1.0f}};
        persp::Matrix3 h{};
        report(persp::fourPoint(from, to, h), "the four-point solve succeeds");

        double worst = 0.0;
        for (int i = 0; i < 4; ++i) {
            float x = from[i][0], y = from[i][1];
            persp::apply(h, x, y);
            worst = std::max(worst, std::abs(double(x) - to[i][0]));
            worst = std::max(worst, std::abs(double(y) - to[i][1]));
        }
        report(worst < 1e-5, "the solved homography hits all four correspondences",
               "worst " + std::to_string(worst));
    }

    // 3. Three collinear points have no homography, and the solve says so
    //    rather than returning a plausible matrix. H&Z §4.1's "general
    //    position" is a precondition, not a footnote.
    {
        const float from[4][2] = {{-1, 0}, {0, 0}, {1, 0}, {0, 1}};
        const float to[4][2]   = {{-1, 0}, {0, 0}, {1, 0}, {0, 1}};
        persp::Matrix3 h{};
        report(!persp::fourPoint(from, to, h),
               "a degenerate correspondence is refused, not approximated");
    }

    // 4. The correction does what its name says: with a positive vertical, the
    //    destination's top row comes from a *narrower* strip of the source than
    //    its bottom row. That is the whole mechanism — the top is magnified,
    //    which pulls converging verticals apart.
    {
        const auto h = persp::keystone({0.8f, 0.0f, 0.0f});
        float topX = 1.0f, topY = -1.0f;
        float botX = 1.0f, botY = 1.0f;
        persp::apply(h, topX, topY);
        persp::apply(h, botX, botY);
        report(topX < 0.98f && botX > 1.02f,
               "vertical fills the top row from a narrower strip than the bottom",
               "top " + std::to_string(topX) + ", bottom " + std::to_string(botX));
    }

    // 5. Horizontal is the same statement with the axes exchanged. Asserted
    //    separately because a keystone that only ever moves one axis is exactly
    //    what a copied-and-pasted second control looks like.
    {
        const auto h = persp::keystone({0.0f, 0.8f, 0.0f});
        float leftX = -1.0f, leftY = 1.0f;
        float rightX = 1.0f, rightY = 1.0f;
        persp::apply(h, leftX, leftY);
        persp::apply(h, rightX, rightY);
        report(leftY < 0.98f && rightY > 1.02f,
               "horizontal fills the left column from a narrower strip than the right",
               "left " + std::to_string(leftY) + ", right " + std::to_string(rightY));
    }

    // 6. Auto-scale actually brings the overreach back. Both halves matter: a
    //    scale below 1 that still leaves a corner outside is no better than
    //    none, and an unconditional zoom would magnify a frame that did not
    //    need it.
    {
        const auto h = persp::keystone({1.0f, 0.0f, 0.0f});
        const float s = persp::autoScale(h);
        report(s < 1.0f && s > 0.5f, "a full-travel keystone pays a zoom",
               "s = " + std::to_string(s));

        bool inside = true;
        const float corners[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        for (const auto& c : corners) {
            float x = c[0] * s, y = c[1] * s;
            persp::apply(h, x, y);
            if (x < -1.001f || x > 1.001f || y < -1.001f || y > 1.001f) inside = false;
        }
        report(inside, "at that zoom every corner of the output lands inside the frame");
    }

    // 7. The inverse is an inverse. This is what `fromFrame` uses to draw a
    //    spot, and an inverse that is nearly right puts every spot handle a few
    //    pixels off the dust it is covering.
    {
        const auto h = persp::compose({0.6f, -0.4f, 0.25f}, float(kW), float(kH));
        const auto inv = persp::inverse(h);
        double worst = 0.0;
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 5; ++j) {
                const float x0 = float(kW) * (0.1f + 0.2f * float(i));
                const float y0 = float(kH) * (0.1f + 0.2f * float(j));
                float x = x0, y = y0;
                persp::apply(h, x, y);
                persp::apply(inv, x, y);
                worst = std::max(worst, std::abs(double(x) - x0));
                worst = std::max(worst, std::abs(double(y) - y0));
            }
        }
        report(worst < 1e-2, "H then H-inverse is the identity, in texels",
               "worst " + std::to_string(worst) + " px");
    }

    // 8. The Jacobian is the map's actual derivative, checked against a finite
    //    difference. It is what carries a gradient mask's angle, and an angle
    //    that is wrong turns the mask without moving it — which reads as taste.
    {
        const auto h = persp::compose({0.5f, 0.3f, 0.0f}, float(kW), float(kH));
        const float x = float(kW) * 0.3f, y = float(kH) * 0.7f;
        const auto j = persp::jacobian(h, x, y);

        constexpr float e = 0.05f;
        float ax = x + e, ay = y, bx = x - e, by = y;
        persp::apply(h, ax, ay);
        persp::apply(h, bx, by);
        const float dax = (ax - bx) / (2 * e), day = (ay - by) / (2 * e);

        float cx = x, cy = y + e, dx = x, dy = y - e;
        persp::apply(h, cx, cy);
        persp::apply(h, dx, dy);
        const float dbx = (cx - dx) / (2 * e), dby = (cy - dy) / (2 * e);

        checkNear(j.a, dax, 1e-3, "Jacobian dx/dx matches a finite difference");
        checkNear(j.c, day, 1e-3, "Jacobian dy/dx matches a finite difference");
        checkNear(j.b, dbx, 1e-3, "Jacobian dx/dy matches a finite difference");
        checkNear(j.d, dby, 1e-3, "Jacobian dy/dy matches a finite difference");
    }
}

/// The shader and the host must compute the *same* transform.
///
/// If they disagree the auto-scale is right for a warp nobody performs — the
/// trap `pipe/LensGeometry.h` names for its polynomial, arriving here through a
/// matrix instead. The fixture carries its coordinates as color, so each output
/// pixel names the source pixel the kernel actually fetched.
///
/// The keystone is asymmetric in both axes on purpose: a transposed matrix, a
/// dropped homogeneous divide and a row read from `.w` instead of `.z` all
/// survive a symmetric one.
void testPerspectiveShaderMatchesHost() {
    section("Perspective — the shader agrees with the host");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    const auto input = coordinateFrame(kW, kH);
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    const auto h = persp::compose({0.55f, -0.30f, 0.0f}, float(kW), float(kH));
    report(!persp::isIdentity(h), "the fixture's correction is not the identity");

    orion::pipe::params::Geometry p{};
    p.outSize[0] = kW; p.outSize[1] = kH;
    p.inSize[0]  = kW; p.inSize[1]  = kH;
    p.cropSize[0] = 1.0f; p.cropSize[1] = 1.0f;
    p.pivot[0] = 0.5f; p.pivot[1] = 0.5f;
    loadMatrix(p, h);

    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, kW, kH);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

    double worst = 0.0;
    int sampled = 0;
    for (std::uint32_t y = 4; y + 4 < kH; y += 3) {
        for (std::uint32_t x = 4; x + 4 < kW; x += 3) {
            // What the host says the kernel should have fetched: the shader's
            // own `r = u·W − 0.5` with the crop at full frame, then H.
            float fx = (float(x) + 0.5f) - 0.5f;
            float fy = (float(y) + 0.5f) - 0.5f;
            persp::apply(h, fx, fy);
            if (fx < 1.0f || fy < 1.0f || fx > kW - 2.0f || fy > kH - 2.0f) continue;

            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            worst = std::max(worst, std::abs(double(out[i + 0]) - double(fx)));
            worst = std::max(worst, std::abs(double(out[i + 1]) - double(fy)));
            ++sampled;
        }
    }

    report(sampled > 100, "the comparison sampled the frame",
           "n = " + std::to_string(sampled));
    // Half a texel of bilinear positioning, plus half-float's own resolution at
    // these magnitudes. A transposed matrix misses by tens of pixels.
    report(worst < 0.6, "every sampled pixel came from where the host says it should",
           "worst " + std::to_string(worst) + " px");
}

/// A neutral control is bit-identical to a build with no perspective in it.
///
/// The flag is what buys that, and this is the check that the *matrix* path is
/// also a true identity when handed one — so the two branches cannot drift.
///
/// ⚠ Run over a straightened, cropped frame rather than a bare one, because the
/// perspective sits between the straighten and the turns and an identity in the
/// wrong place is still an identity.
void testPerspectiveIdentity() {
    section("Perspective — neutral is bit-identical");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    const auto input = coordinateFrame(kW, kH);
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    orion::pipe::params::Geometry base{};
    base.inSize[0] = kW; base.inSize[1] = kH;
    base.quarterTurns = 1;
    base.straightenRad = 7.0f * 3.14159265358979f / 180.0f;
    base.cropOrigin[0] = 0.12f; base.cropOrigin[1] = 0.08f;
    base.cropSize[0]   = 0.70f; base.cropSize[1]   = 0.75f;
    base.pivot[0] = 0.5f; base.pivot[1] = 0.5f;
    base.outSize[0] = std::uint32_t(float(kH) * 0.70f);
    base.outSize[1] = std::uint32_t(float(kW) * 0.75f);

    const auto render = [&](const orion::pipe::params::Geometry& p) {
        auto dst = orion::gpu::Texture::create(*device, p.outSize[0], p.outSize[1],
                                               orion::gpu::PixelFormat::RGBA16Float);
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p,
                    p.outSize[0], p.outSize[1]);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(p.outSize[0]) * p.outSize[1] * 4);
        dst->download(out.data(), std::size_t(p.outSize[0]) * 4 * sizeof(__fp16),
                      p.outSize[0], p.outSize[1]);
        return out;
    };

    auto off = base;
    off.perspectiveOn = 0u;
    const auto without = render(off);

    auto on = base;
    loadMatrix(on, persp::identity());
    on.perspectiveOn = 1u;      // forced: `loadMatrix` would have cleared it
    const auto identity = render(on);

    // Bit-for-bit, not "near". The half-float patterns are compared as the
    // integers they are, so a one-ulp drift is a failure and not a rounding.
    bool same = without.size() == identity.size();
    std::size_t firstBad = 0;
    for (std::size_t i = 0; same && i < without.size(); ++i) {
        if (std::memcmp(&without[i], &identity[i], sizeof(__fp16)) != 0) {
            same = false;
            firstBad = i;
        }
    }
    report(same, "the identity matrix renders bit-identically to no matrix at all",
           same ? "" : "first difference at component " + std::to_string(firstBad));

    // And the picture is not empty, or the comparison above is two black
    // frames agreeing.
    double sum = 0.0;
    for (std::size_t i = 0; i < without.size(); i += 4) sum += double(without[i + 3]);
    report(sum > double(without.size()) * 0.1,
           "the fixture rendered something to compare");
}

/// Converging verticals come out parallel.
///
/// The fixture is the picture the feature exists for, built so the answer is
/// arithmetic rather than judgement: two straight lines that converge toward
/// the top of the frame by exactly the amount one full unit of the vertical
/// control undoes. Corrected, the gap between them at the top row must equal
/// the gap at the bottom.
void testPerspectiveConvergingLines() {
    section("Perspective — converging verticals come out parallel");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    constexpr std::uint32_t w = 256, h = 256;

    // The building. Two lines that lean in toward the top by the same amount a
    // vertical of +1 spreads them by, drawn white on black. `kLean` is
    // `persp::kCornerTravel`, which is what makes the correction exact rather
    // than approximately right — the fixture and the control are the same
    // number, stated once.
    const float lean = orion::pipe::persp::kCornerTravel;
    std::vector<__fp16> input(std::size_t(w) * h * 4, __fp16(0));
    const auto edgeAt = [&](float base, float y) {
        // y = 0 at the top. The source's half-width at row y, relative to the
        // frame center, is (1 - lean) at the top and (1 + lean) at the bottom.
        const float t = (y + 0.5f) / float(h);              // 0..1 down the frame
        const float half = (1.0f - lean) + 2.0f * lean * t;
        return float(w) * 0.5f + (base - float(w) * 0.5f) * half;
    };
    constexpr float kLeft = 64.0f, kRight = 192.0f;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (float base : {kLeft, kRight}) {
            const float cx = edgeAt(base, float(y));
            for (int d = -1; d <= 1; ++d) {
                const int x = int(cx + 0.5f) + d;
                if (x < 0 || x >= int(w)) continue;
                const std::size_t i = (std::size_t(y) * w + std::size_t(x)) * 4;
                input[i + 0] = input[i + 1] = input[i + 2] = __fp16(1);
                input[i + 3] = __fp16(1);
            }
        }
    }

    auto src = orion::gpu::Texture::create(*device, w, h,
                                           orion::gpu::PixelFormat::RGBA16Float);
    src->upload(input.data(), std::size_t(w) * 4 * sizeof(__fp16));

    const auto spacingAt = [&](const std::vector<__fp16>& img, std::uint32_t row) {
        // The centroid of the lit pixels either side of the frame's center.
        double lSum = 0, lWeight = 0, rSum = 0, rWeight = 0;
        for (std::uint32_t x = 0; x < w; ++x) {
            const double v = double(img[(std::size_t(row) * w + x) * 4]);
            if (v < 0.25) continue;
            if (x < w / 2) { lSum += v * x; lWeight += v; }
            else           { rSum += v * x; rWeight += v; }
        }
        if (lWeight < 0.5 || rWeight < 0.5) return -1.0;
        return (rSum / rWeight) - (lSum / lWeight);
    };

    const auto renderWith = [&](float vertical) {
        orion::pipe::params::Geometry p{};
        p.outSize[0] = w; p.outSize[1] = h;
        p.inSize[0]  = w; p.inSize[1]  = h;
        p.cropSize[0] = 1.0f; p.cropSize[1] = 1.0f;
        p.pivot[0] = 0.5f; p.pivot[1] = 0.5f;
        loadMatrix(p, persp::compose({vertical, 0.0f, 0.0f}, float(w), float(h)));

        auto dst = orion::gpu::Texture::create(*device, w, h,
                                               orion::gpu::PixelFormat::RGBA16Float);
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, w, h);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(w) * h * 4);
        dst->download(out.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        return out;
    };

    // Uncorrected, the lines converge: the gap near the top is much smaller
    // than the gap near the bottom. Asserted first, or "parallel" below could
    // be passing on a fixture that was never crooked.
    {
        const auto plain = renderWith(0.0f);
        const double top = spacingAt(plain, 16);
        const double bottom = spacingAt(plain, h - 17);
        report(top > 0 && bottom > 0, "the uncorrected fixture has both lines in view");
        report(bottom - top > 20.0,
               "the fixture's verticals really do converge",
               "top " + std::to_string(top) + " px, bottom " + std::to_string(bottom));
    }

    // Corrected. Auto-scale zooms, so both spacings grow by the same factor —
    // what is asserted is that they *agree with each other*, which no zoom can
    // manufacture.
    {
        const auto fixed = renderWith(1.0f);
        const double top = spacingAt(fixed, 16);
        const double bottom = spacingAt(fixed, h - 17);
        report(top > 0 && bottom > 0, "the corrected fixture has both lines in view");

        const double drift = std::abs(bottom - top);
        report(drift < 2.0,
               "corrected, the verticals are parallel",
               "top " + std::to_string(top) + " px, bottom " + std::to_string(bottom) +
               ", drift " + std::to_string(drift));

        // And the sign is the one a photographer expects: correcting a frame
        // shot looking up must not need a negative slider.
        const auto wrongWay = renderWith(-1.0f);
        const double wTop = spacingAt(wrongWay, 16);
        const double wBottom = spacingAt(wrongWay, h - 17);
        report(wTop < 0 || wBottom < 0 || std::abs(wBottom - wTop) > drift + 10.0,
               "the opposite sign makes it worse, not better");
    }
}

/// The whole transform in one pass keeps more of the picture than the same
/// transform in two.
///
/// This is the reason perspective is a matrix inside the geometry node rather
/// than a node of its own (`geometry.slang`'s opening comment, decision #40,
/// Wolberg §7). Bilinear on bilinear is a triangle filter convolved with
/// itself; the second sampling costs high frequencies that no later stage gets
/// back. Measured rather than argued.
void testPerspectiveOneResample() {
    section("Perspective — composed, not chained");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    constexpr std::uint32_t w = 192, h = 192;

    // A fine sinusoidal grating. Fine because that is where a second resample
    // shows, and a sinusoid rather than a bar chart because a hard edge under
    // bilinear is dominated by where the samples happen to land.
    std::vector<__fp16> input(std::size_t(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const float v = 0.5f + 0.45f * std::sin(6.2831853f * float(x) / 5.0f);
            const std::size_t i = (std::size_t(y) * w + x) * 4;
            input[i + 0] = input[i + 1] = input[i + 2] = __fp16(v);
            input[i + 3] = __fp16(1);
        }
    }

    auto src = orion::gpu::Texture::create(*device, w, h,
                                           orion::gpu::PixelFormat::RGBA16Float);
    src->upload(input.data(), std::size_t(w) * 4 * sizeof(__fp16));

    const float angle = 6.0f * 3.14159265358979f / 180.0f;
    const auto matrix = persp::compose({0.45f, 0.2f, 0.0f}, float(w), float(h));

    orion::pipe::params::Geometry basis{};
    basis.outSize[0] = w; basis.outSize[1] = h;
    basis.inSize[0]  = w; basis.inSize[1]  = h;
    basis.cropSize[0] = 1.0f; basis.cropSize[1] = 1.0f;
    basis.pivot[0] = 0.5f; basis.pivot[1] = 0.5f;

    const auto run = [&](const orion::pipe::params::Geometry& p,
                         orion::gpu::Texture& in) {
        auto dst = orion::gpu::Texture::create(*device, p.outSize[0], p.outSize[1],
                                               orion::gpu::PixelFormat::RGBA16Float);
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {&in, dst.get()}, &p, sizeof p,
                    p.outSize[0], p.outSize[1]);
        cb.commitAndWait();
        return dst;
    };

    const auto readBack = [&](orion::gpu::Texture& t) {
        std::vector<__fp16> out(std::size_t(w) * h * 4);
        t.download(out.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        return out;
    };

    // One pass: straighten and perspective composed, as the product does.
    auto composedParams = basis;
    composedParams.straightenRad = angle;
    loadMatrix(composedParams, matrix);
    const auto composed = readBack(*run(composedParams, *src));

    // Two passes, deliberately in the order that makes them *the same
    // transform*: the shader applies the straighten and then H, so undoing that
    // as a chain means H first into an intermediate and the straighten second.
    auto pass1 = basis;
    loadMatrix(pass1, matrix);
    auto intermediate = run(pass1, *src);

    auto pass2 = basis;
    pass2.straightenRad = angle;
    const auto chained = readBack(*run(pass2, *intermediate));

    // Mean absolute difference between horizontal neighbours — the bench's
    // Detail metric, over the interior so the transparent margins cannot vote.
    const auto acutance = [&](const std::vector<__fp16>& img) {
        double sum = 0.0;
        int n = 0;
        for (std::uint32_t y = h / 4; y < 3 * h / 4; ++y) {
            for (std::uint32_t x = w / 4; x + 1 < 3 * w / 4; ++x) {
                const std::size_t a = (std::size_t(y) * w + x) * 4;
                if (double(img[a + 3]) < 0.99) continue;
                sum += std::abs(double(img[a + 4]) - double(img[a]));
                ++n;
            }
        }
        return n > 0 ? sum / n : 0.0;
    };

    const double one = acutance(composed);
    const double two = acutance(chained);

    report(one > 0.01 && two > 0.01, "both renders carry the grating",
           "one pass " + std::to_string(one) + ", two " + std::to_string(two));
    report(one > two * 1.05,
           "one sampling pass keeps more detail than two",
           "one pass " + std::to_string(one) + " against " + std::to_string(two) +
           " (" + std::to_string(one / std::max(two, 1e-9)) + "x)");
}

/// Auto-scale leaves no transparent corner.
///
/// `sampleBilinear` returns transparent black outside the frame, so a corner
/// that reaches past it is not a soft artifact — it is a hole, and it goes
/// straight into an export.
void testPerspectiveAutoScaleFillsTheFrame() {
    section("Perspective — auto-scale fills the frame");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    constexpr std::uint32_t w = 128, h = 96;
    std::vector<__fp16> input(std::size_t(w) * h * 4, __fp16(1));
    auto src = orion::gpu::Texture::create(*device, w, h,
                                           orion::gpu::PixelFormat::RGBA16Float);
    src->upload(input.data(), std::size_t(w) * 4 * sizeof(__fp16));

    const auto worstAlpha = [&](float v, float hz, float aspect, bool zoom) {
        auto m = persp::keystone({v, hz, aspect});
        if (zoom) {
            const float s = persp::autoScale(m);
            const persp::Matrix3 scale{{s, 0, 0, 0, s, 0, 0, 0, 1}};
            m = persp::multiply(m, scale);
        }
        m = persp::inTexels(m, float(w), float(h));

        orion::pipe::params::Geometry p{};
        p.outSize[0] = w; p.outSize[1] = h;
        p.inSize[0]  = w; p.inSize[1]  = h;
        p.cropSize[0] = 1.0f; p.cropSize[1] = 1.0f;
        p.pivot[0] = 0.5f; p.pivot[1] = 0.5f;
        loadMatrix(p, m);

        auto dst = orion::gpu::Texture::create(*device, w, h,
                                               orion::gpu::PixelFormat::RGBA16Float);
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, w, h);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(w) * h * 4);
        dst->download(out.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);

        double worst = 1.0;
        for (std::size_t i = 3; i < out.size(); i += 4) {
            worst = std::min(worst, double(out[i]));
        }
        return worst;
    };

    // Without the zoom the frame is holed. Asserted first: without it, "no
    // black corners" would pass on a correction that never reached an edge.
    report(worstAlpha(1.0f, 0.0f, 0.0f, false) < 0.5,
           "a full-travel keystone without the zoom leaves the frame holed");

    // With it, every pixel of every combination reads real data.
    struct Case { float v, h, a; const char* name; };
    for (const Case& c : {Case{1.0f, 0.0f, 0.0f, "vertical +1"},
                          Case{-1.0f, 0.0f, 0.0f, "vertical -1"},
                          Case{0.0f, 1.0f, 0.0f, "horizontal +1"},
                          Case{0.7f, 0.7f, 0.0f, "both at 0.7"},
                          Case{0.5f, -0.5f, 1.0f, "with a full aspect"},
                          Case{0.5f, -0.5f, -1.0f, "with a full aspect, other way"}}) {
        const double worst = worstAlpha(c.v, c.h, c.a, true);
        report(worst > 0.99,
               std::string("auto-scale leaves no empty pixel — ") + c.name,
               "worst alpha " + std::to_string(worst));
    }
}

/// A mask, a brush dab and a spot all follow the corrected picture.
///
/// The failure this stops is the one this project fears most: not a crash, a
/// *plausible* wrong answer. A mask that lands half a subject away still looks
/// like a mask.
void testPerspectiveMaskGeometry() {
    section("Perspective — masks and spots follow");

    namespace mask = orion::pipe::mask;

    const float frameW = 600.0f, frameH = 400.0f;
    const auto h = persp::compose({0.7f, -0.35f, 0.15f}, frameW, frameH);
    const auto inv = persp::inverse(h);
    report(!persp::isIdentity(h), "the fixture's correction is not the identity");

    const mask::Crop crop{0.15f, 0.10f, 0.60f, 0.70f};
    constexpr int turns = 1;
    const float straighten = 5.0f * 3.14159265358979324f / 180.0f;

    // 1. A neutral matrix changes nothing at all — the same guarantee the
    //    shader's flag gives, on this side of the boundary.
    {
        const mask::Placement p{0.37f, 0.62f, 0.4f};
        const auto with = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                        frameW, frameH, nullptr);
        const auto persp0 = persp::identity();
        const auto also = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                        frameW, frameH, &persp0);
        report(with.centerX == also.centerX && with.centerY == also.centerY &&
               with.angle == also.angle && with.scale == also.scale,
               "an identity matrix is exactly the same as no matrix");
    }

    // 2. It actually moves the mask. Without this the round trip below would
    //    pass on a transform that does nothing.
    {
        const mask::Placement p{0.30f, 0.25f, 0.0f};
        const auto plain = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                         frameW, frameH, nullptr);
        const auto moved = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                         frameW, frameH, &h);
        const double d = std::hypot(double(plain.centerX) - moved.centerX,
                                    double(plain.centerY) - moved.centerY);
        report(d > 0.01, "the correction moves the mask center",
               "by " + std::to_string(d) + " of the frame");
        report(std::abs(moved.scale - 1.0f) > 1e-3,
               "and rescales what a length near it means",
               "scale " + std::to_string(moved.scale));
    }

    // 3. `fromFrame` undoes `toFrame`, with the inverse matrix and in the
    //    reverse order. A spot is stored in frame coordinates and drawn through
    //    this; an inverse that is nearly right puts every handle off its dust.
    {
        double worst = 0.0;
        for (int i = 1; i < 6; ++i) {
            for (int j = 1; j < 6; ++j) {
                const mask::Placement p{0.15f * float(i), 0.15f * float(j), 0.3f};
                const auto f = mask::toFrame(p, crop, turns, straighten,
                                             0.45f, 0.45f, frameW, frameH, &h);
                const auto back = mask::fromFrame(f, crop, turns, straighten,
                                                  0.45f, 0.45f, frameW, frameH, &inv);
                worst = std::max(worst, std::abs(double(back.centerX) - p.centerX));
                worst = std::max(worst, std::abs(double(back.centerY) - p.centerY));
                worst = std::max(worst, std::abs(double(back.angle) - p.angle));
            }
        }
        report(worst < 2e-3, "toFrame and fromFrame round-trip under a correction",
               "worst " + std::to_string(worst));
    }

    // 4. A mask center goes where the *picture* went. This is the check that
    //    ties the two halves together: the geometry node maps a displayed point
    //    to a source texel, and `mask::toFrame` must agree with it, because the
    //    mask is applied to that source.
    //
    //    Composed by hand from the shader's own steps rather than by calling
    //    the shader, so the check is readable — and the shader is separately
    //    pinned against the host matrix in `testPerspectiveShaderMatchesHost`.
    {
        const mask::Placement p{0.62f, 0.30f, 0.0f};
        const auto got = mask::toFrame(p, crop, 0, 0.0f, 0.5f, 0.5f,
                                       frameW, frameH, &h);

        // The shader, with no turns and no straighten: crop, then H, in texels.
        const float u = crop.x + p.centerX * crop.w;
        const float v = crop.y + p.centerY * crop.h;
        float rx = u * frameW - 0.5f;
        float ry = v * frameH - 0.5f;
        persp::apply(h, rx, ry);

        checkNear(double(got.centerX) * frameW - 0.5, double(rx), 0.05,
                  "a mask center lands on the texel the geometry node fetched (x)");
        checkNear(double(got.centerY) * frameH - 0.5, double(ry), 0.05,
                  "a mask center lands on the texel the geometry node fetched (y)");
    }
}

/// The correction through the *product*, not the kernel.
///
/// `testPerspectiveShaderMatchesHost` dispatches the geometry kernel with
/// parameters it sets itself, so it can never see the wiring — the same split
/// that let dehaze be deleted with every instrument green, and the reason
/// `testGrainWiring` exists beside `testGrainGpu`. This drives
/// `DevelopPipeline` and asserts what would actually break.
void testPerspectiveWiring() {
    section("Perspective wiring");

    namespace pipe = orion::pipe;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    // A ramp in both axes, so a warp in either direction is visible. A flat
    // patch would be bit-identical under any transform at all, which is the
    // fixture that turns this whole file into a formality.
    orion::raw::BayerImage img;
    img.width = 64;
    img.height = 64;
    img.samples.resize(std::size_t(64) * 64);
    for (std::uint32_t y = 0; y < 64; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) {
            img.samples[std::size_t(y) * 64 + x] =
                static_cast<std::uint16_t>(200 + x * 30 + y * 20);
        }
    }
    img.filters = 0x94949494u;             // RGGB
    img.white = 4095;
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};

    std::unique_ptr<pipe::DevelopPipeline> dev;
    try {
        dev = std::make_unique<pipe::DevelopPipeline>(
            *device, std::string(ORION_SHADER_DIR), img);
    } catch (const std::exception& e) {
        report(false, "develop graph builds", e.what());
        return;
    }

    const std::uint32_t w = dev->outputWidth(), h = dev->outputHeight();
    const auto frame = [&] {
        std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
        dev->output().download(px.data(), std::size_t(w) * 4, w, h);
        return px;
    };
    const auto ranNodes = [&] {
        int n = 0;
        for (const auto& node : dev->graph().lastRun()) if (node.executed) ++n;
        return n;
    };

    pipe::Adjustments adj{};
    adj.wb = pipe::WhiteBalance{};

    // 1. Neutral is bit-identical, through the whole graph.
    dev->apply(adj);
    dev->render();
    const auto cold = frame();

    // A *warm* render, because the first one is cold and would compare sixty
    // nodes against one. `testGrainWiring`'s first draft failed on exactly this.
    adj.exposureEv = 0.25f;
    dev->apply(adj);
    dev->render();
    adj.exposureEv = 0.0f;
    dev->apply(adj);
    dev->render();
    const auto warm = frame();
    report(warm == cold, "a warm render with no perspective matches the cold one");

    adj.perspectiveVertical = 0.0f;
    adj.perspectiveHorizontal = 0.0f;
    adj.perspectiveAspect = 0.0f;
    dev->apply(adj);
    dev->render();
    report(frame() == cold,
           "all three controls at zero are bit-identical to never setting them");

    // 2. And it is not a fixture that cannot move.
    adj.perspectiveVertical = 0.5f;
    dev->apply(adj);
    const double warpMs = dev->render();
    const int warpNodes = ranNodes();
    const auto warped = frame();
    report(warped != cold, "the vertical control moves the picture");

    // 3. One node, which is the whole architectural claim.
    //
    // Perspective lives inside the sampling pass that was already there. A
    // second geometry node — the obvious way to build this, and the one that
    // resamples twice — would show up here as two, and it would soften the
    // picture in a way no assertion about pixels catches.
    report(warpNodes == 1,
           "a perspective tick runs exactly one node",
           std::to_string(warpNodes) + " nodes, " + std::to_string(warpMs) + " ms");

    // 4. Each control does something of its own. Three fields of the same type,
    //    assigned by hand in five files between the slider and the kernel — a
    //    transposed pair compiles.
    adj.perspectiveVertical = 0.0f;
    adj.perspectiveHorizontal = 0.5f;
    dev->apply(adj);
    dev->render();
    const auto horizontal = frame();
    report(horizontal != cold && horizontal != warped,
           "horizontal moves the picture, and differently from vertical");

    adj.perspectiveHorizontal = 0.0f;
    adj.perspectiveAspect = 0.5f;
    dev->apply(adj);
    dev->render();
    const auto aspect = frame();
    report(aspect != cold && aspect != warped && aspect != horizontal,
           "aspect moves the picture, and differently from either keystone");

    // 5. Back to zero is back to the beginning.
    adj.perspectiveAspect = 0.0f;
    dev->apply(adj);
    dev->render();
    report(frame() == cold, "returning all three to zero restores the exact frame");

    // 6. And a spot follows.
    //
    // `displayedToFrame` is a second call site with its own copy of the
    // argument list, and it is the one a spot is placed through — dust sits on
    // the sensor, so a spot is converted once when it is placed rather than on
    // every render. A correction that reached the picture and not this call
    // would put every spot placed afterwards on the wrong piece of dust, and a
    // misplaced heal still looks like a heal.
    const auto plainSpot = dev->displayedToFrame(0.30f, 0.72f);

    adj.perspectiveVertical = 0.5f;
    dev->apply(adj);
    dev->render();
    const auto movedSpot = dev->displayedToFrame(0.30f, 0.72f);
    const double spotShift = std::hypot(double(movedSpot.first) - plainSpot.first,
                                        double(movedSpot.second) - plainSpot.second);
    report(spotShift > 0.005,
           "a spot's displayed-to-frame conversion follows the correction",
           "moved " + std::to_string(spotShift) + " of the frame");

    // And back out again lands where it started, which is what draws the
    // handle. The inverse is a separate matrix; using H where H-inverse belongs
    // is a mistake that survives every check that only looks at one direction.
    const auto back = dev->frameToDisplayed(movedSpot.first, movedSpot.second);
    checkNear(double(back.first), 0.30, 2e-3,
              "and the inverse brings it back to the displayed point (x)");
    checkNear(double(back.second), 0.72, 2e-3,
              "and the inverse brings it back to the displayed point (y)");

    adj.perspectiveVertical = 0.0f;
    dev->apply(adj);
    dev->render();
}

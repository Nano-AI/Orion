// Orientation, display neutrality, crop and straighten, on the GPU.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

void testOrientGpu() {
    section("Orientation (GPU)");

    constexpr std::uint32_t kW = 8, kH = 5;
    constexpr std::uint32_t kSquare = 8;   // max(kW, kH), as DevelopPipeline does

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
    auto dst = orion::gpu::Texture::create(*device, kSquare, kSquare,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // Each pixel carries its own coordinates, so a misplaced sample is
    // immediately identifiable rather than just "looking wrong".
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = static_cast<__fp16>(x);
            input[i + 1] = static_cast<__fp16>(y);
            input[i + 2] = 0;
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    for (std::uint32_t turns = 0; turns < 4; ++turns) {
        const bool swaps = (turns % 2) != 0;
        const std::uint32_t ow = swaps ? kH : kW;
        const std::uint32_t oh = swaps ? kW : kH;

        orion::pipe::params::Geometry p{};
        p.outSize[0] = ow;
        p.outSize[1] = oh;
        p.inSize[0]  = kW;
        p.inSize[1]  = kH;
        p.quarterTurns = turns;
        p.straightenRad = 0.0f;
        p.cropOrigin[0] = 0.0f; p.cropOrigin[1] = 0.0f;
        p.cropSize[0]   = 1.0f; p.cropSize[1]   = 1.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, ow, oh);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(ow) * oh * 4);
        dst->download(out.data(), std::size_t(ow) * 4 * sizeof(__fp16), ow, oh);

        bool allCorrect = true;
        std::string firstBad;

        for (std::uint32_t oy = 0; oy < oh && allCorrect; ++oy) {
            for (std::uint32_t ox = 0; ox < ow && allCorrect; ++ox) {
                std::uint32_t sx = 0, sy = 0;
                switch (turns) {
                    case 1: sx = oy;              sy = kH - 1 - ox; break;
                    case 2: sx = kW - 1 - ox;     sy = kH - 1 - oy; break;
                    case 3: sx = kW - 1 - oy;     sy = ox;          break;
                    default: sx = ox;             sy = oy;          break;
                }

                const std::size_t i = (std::size_t(oy) * ow + ox) * 4;
                const auto gotX = static_cast<int>(float(out[i + 0]) + 0.5f);
                const auto gotY = static_cast<int>(float(out[i + 1]) + 0.5f);

                if (gotX != int(sx) || gotY != int(sy)) {
                    allCorrect = false;
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                                  "out(%u,%u) holds src(%d,%d), expected src(%u,%u)",
                                  ox, oy, gotX, gotY, sx, sy);
                    firstBad = buf;
                }
            }
        }

        report(allCorrect,
               "rotation " + std::to_string(turns * 90) + " maps every pixel",
               firstBad);

        // A quarter turn must actually change the framing — catches the case
        // where the kernel silently falls through to identity.
        if (turns != 0) {
            const auto x0 = static_cast<int>(float(out[0]) + 0.5f);
            const auto y0 = static_cast<int>(float(out[1]) + 0.5f);
            report(!(x0 == 0 && y0 == 0),
                   "rotation " + std::to_string(turns * 90) + " moves the origin");
        }
    }
}

// ── Neutrality of the display transform ────────────────────────────────────

/// Gray in, gray out.
///
/// This is the single most valuable check in the file. A tone mapper's inset
/// and outset matrices must each preserve the achromatic axis; if they do not,
/// *every* pixel picks up a cast and the image looks subtly wrong in a way that
/// is easy to mistake for a white balance problem. An earlier pair of matrices
/// mapped neutral to roughly (0.84, 0.94, 1.22) and cast the whole image purple.
void testDisplayNeutrality() {
    section("Display transform neutrality");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developDisplay.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developDisplay");

    // A ramp of neutral grays spanning 12 stops, from deep shadow to well
    // above diffuse white.
    constexpr std::uint32_t kN = 24;
    std::vector<__fp16> input(std::size_t(kN) * 4);
    std::vector<float> levels(kN);
    for (std::uint32_t i = 0; i < kN; ++i) {
        const float ev = -10.0f + 12.0f * float(i) / float(kN - 1);
        const float v = std::pow(2.0f, ev);
        levels[i] = v;
        input[i * 4 + 0] = static_cast<__fp16>(v);
        input[i * 4 + 1] = static_cast<__fp16>(v);
        input[i * 4 + 2] = static_cast<__fp16>(v);
        input[i * 4 + 3] = 1;
    }

    auto src = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA8Unorm);
    src->upload(input.data(), std::size_t(kN) * 4 * sizeof(__fp16));

    // An identity LUT, so the curve stage is a pass-through.
    auto lut = orion::gpu::Texture::create(*device, orion::pipe::kCurveResolution,
                                           orion::pipe::kCurveRows,
                                           orion::gpu::PixelFormat::R32Float);
    const auto lutData = orion::pipe::buildCurveLut({});
    lut->upload(lutData.data(), orion::pipe::kCurveResolution * sizeof(float));

    orion::pipe::params::Display p{};
    p.contrast = 1.0f;
    p.pivot = -2.5f;
    p.curveIdentity = 1u;
    p.resolution = orion::pipe::kCurveResolution;
    p.size[0] = kN;
    p.size[1] = 1;

    orion::gpu::CommandBuffer cb(*device);
    // The display kernel binds the creative LUT after the curve LUT. It is
    // never sampled here — lutSize stays zero — but the binding has to exist or
    // every texture after it shifts by one, which is silent and total.
    auto cubeStub = orion::gpu::Texture::create(*device, 2, 4,
                                                orion::gpu::PixelFormat::RGBA32Float);
    cb.dispatch(*kernel, {src.get(), lut.get(), cubeStub.get(), dst.get()},
                &p, sizeof p, kN, 1);
    cb.commitAndWait();

    std::vector<std::uint8_t> out(std::size_t(kN) * 4);
    dst->download(out.data(), std::size_t(kN) * 4, kN, 1);

    int worst = 0;
    float worstLevel = 0.0f;
    bool monotone = true;
    int previousGreen = -1;

    for (std::uint32_t i = 0; i < kN; ++i) {
        const int r = out[i * 4 + 0], g = out[i * 4 + 1], b = out[i * 4 + 2];
        const int spread = std::max({r, g, b}) - std::min({r, g, b});
        if (spread > worst) { worst = spread; worstLevel = levels[i]; }
        if (g < previousGreen) monotone = false;
        previousGreen = g;
    }

    char detail[160];
    std::snprintf(detail, sizeof detail,
                  "worst channel spread %d/255 at scene level %.4f", worst, worstLevel);
    // 2/255 covers 8-bit rounding; anything beyond that is a genuine cast.
    report(worst <= 2, "neutral gray stays neutral through the display transform",
           worst <= 2 ? "" : detail);

    report(monotone, "brighter input never produces a darker output");

    // Middle gray must land near the middle of the display range. This is what
    // catches a double encode: applying a transfer function on top of AgX put
    // 0.18 at 189/255 instead of about 128.
    const float target = 0.18f;
    std::uint32_t nearest = 0;
    for (std::uint32_t i = 1; i < kN; ++i) {
        if (std::abs(levels[i] - target) < std::abs(levels[nearest] - target)) nearest = i;
    }
    const int mid = out[nearest * 4 + 1];
    std::snprintf(detail, sizeof detail, "scene %.3f -> %d/255", levels[nearest], mid);
    report(mid > 95 && mid < 165, "middle gray lands mid-range", detail);
}

/// Crop selects the requested region, not merely a smaller one.
///
/// An off-by-one or a transposed origin here shows up as "the crop works but
/// grabs the wrong part of the frame", which is easy to miss by eye on a
/// uniform subject.
void testCropGpu() {
    section("Crop (GPU)");

    constexpr std::uint32_t kW = 16, kH = 16;

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

    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = static_cast<__fp16>(x);
            input[i + 1] = static_cast<__fp16>(y);
            input[i + 2] = 0;
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    // Right half, bottom half: origin (0.5, 0.5), size (0.5, 0.5).
    const std::uint32_t ow = kW / 2, oh = kH / 2;
    orion::pipe::params::Geometry p{};
    p.outSize[0] = ow; p.outSize[1] = oh;
    p.inSize[0]  = kW; p.inSize[1]  = kH;
    p.quarterTurns = 0;
    p.straightenRad = 0.0f;
    p.cropOrigin[0] = 0.5f; p.cropOrigin[1] = 0.5f;
    p.cropSize[0]   = 0.5f; p.cropSize[1]   = 0.5f;

    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, ow, oh);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(ow) * oh * 4);
    dst->download(out.data(), std::size_t(ow) * 4 * sizeof(__fp16), ow, oh);

    // The crop's top-left must be the source's center, within half a pixel of
    // bilinear positioning.
    const double x0 = double(out[0]);
    const double y0 = double(out[1]);
    checkNear(x0, 8.0, 0.6, "crop origin lands at the requested column");
    checkNear(y0, 8.0, 0.6, "crop origin lands at the requested row");

    // And the far corner must reach the source's far corner.
    const std::size_t last = (std::size_t(oh - 1) * ow + (ow - 1)) * 4;
    checkNear(double(out[last + 0]), 15.0, 0.6, "crop reaches the right edge");
    checkNear(double(out[last + 1]), 15.0, 0.6, "crop reaches the bottom edge");

    // A full-frame crop must be an exact pass-through.
    p.cropOrigin[0] = 0.0f; p.cropOrigin[1] = 0.0f;
    p.cropSize[0]   = 1.0f; p.cropSize[1]   = 1.0f;
    p.outSize[0] = kW; p.outSize[1] = kH;

    orion::gpu::CommandBuffer cb2(*device);
    cb2.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, kW, kH);
    cb2.commitAndWait();

    std::vector<__fp16> full(std::size_t(kW) * kH * 4);
    dst->download(full.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

    bool identity = true;
    for (std::uint32_t i = 0; i < kW * kH; ++i) {
        if (std::abs(double(full[i * 4]) - double(input[i * 4])) > 0.51 ||
            std::abs(double(full[i * 4 + 1]) - double(input[i * 4 + 1])) > 0.51) {
            identity = false;
            break;
        }
    }
    report(identity, "a full-frame crop is a pass-through");
}

/// The straighten preview must agree with what committing the crop produces.
///
/// While the crop tool is open the geometry node renders onto a canvas larger
/// than the frame, so cropOrigin and cropSize describe the canvas rather than
/// the user's rectangle. The pivot was derived from those two, which meant the
/// preview turned the picture about the frame center and the committed render
/// turned it about the crop center. With an off-center crop the two disagree,
/// and the picture you got was not the picture the white box had shown.
void testStraightenPivot() {
    section("Straighten pivot (GPU)");

    constexpr std::uint32_t kW = 64, kH = 64;
    constexpr float kCanvas = 1.42f;
    constexpr float kAngle  = 8.0f * 3.14159265358979f / 180.0f;

    // An off-center crop — the case the two paths disagreed on.
    constexpr float cx = 0.10f, cy = 0.15f, cw = 0.40f, ch = 0.35f;

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

    // Coordinates as color, so every output pixel names the source pixel it
    // came from and the two renders can be compared exactly.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = static_cast<__fp16>(x);
            input[i + 1] = static_cast<__fp16>(y);
            input[i + 2] = 0;
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    orion::pipe::params::Geometry base{};
    base.inSize[0] = kW; base.inSize[1] = kH;
    base.quarterTurns  = 0;
    base.straightenRad = kAngle;
    // The frame's center, which is what the app sends. An off-center crop is
    // the case the two paths used to disagree on.
    base.pivot[0] = 0.5f;
    base.pivot[1] = 0.5f;

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

    // The committed render: the crop rectangle alone.
    orion::pipe::params::Geometry commit = base;
    commit.cropOrigin[0] = cx; commit.cropOrigin[1] = cy;
    commit.cropSize[0]   = cw; commit.cropSize[1]   = ch;
    commit.outSize[0] = static_cast<std::uint32_t>(kW * cw);
    commit.outSize[1] = static_cast<std::uint32_t>(kH * ch);
    const auto committed = render(commit);

    // The preview: the whole enlarged canvas.
    orion::pipe::params::Geometry preview = base;
    preview.cropOrigin[0] = 0.5f - kCanvas * 0.5f;
    preview.cropOrigin[1] = 0.5f - kCanvas * 0.5f;
    preview.cropSize[0]   = kCanvas;
    preview.cropSize[1]   = kCanvas;
    preview.outSize[0] = static_cast<std::uint32_t>(kW * kCanvas);
    preview.outSize[1] = static_cast<std::uint32_t>(kH * kCanvas);
    const auto previewed = render(preview);

    // Where the crop rectangle lands inside the preview canvas.
    const double pw = preview.outSize[0], ph = preview.outSize[1];
    const double originU = (cx - preview.cropOrigin[0]) / kCanvas;
    const double originV = (cy - preview.cropOrigin[1]) / kCanvas;

    double worst = 0.0;
    int sampled = 0;
    for (std::uint32_t oy = 2; oy + 2 < commit.outSize[1]; oy += 4) {
        for (std::uint32_t ox = 2; ox + 2 < commit.outSize[0]; ox += 4) {
            // Same point of the crop, addressed in the preview's canvas.
            const double u = originU + (double(ox) + 0.5) / commit.outSize[0]
                                       * (cw / kCanvas);
            const double v = originV + (double(oy) + 0.5) / commit.outSize[1]
                                       * (ch / kCanvas);
            const auto px = std::uint32_t(u * pw);
            const auto py = std::uint32_t(v * ph);
            if (px >= preview.outSize[0] || py >= preview.outSize[1]) continue;

            const std::size_t a = (std::size_t(oy) * commit.outSize[0] + ox) * 4;
            const std::size_t b = (std::size_t(py) * preview.outSize[0] + px) * 4;

            worst = std::max(worst, std::abs(double(committed[a]) - double(previewed[b])));
            worst = std::max(worst,
                             std::abs(double(committed[a + 1]) - double(previewed[b + 1])));
            ++sampled;
        }
    }

    report(sampled > 20, "the comparison sampled the crop", "n = " + std::to_string(sampled));

    // A pixel of slack for the two grids landing on different sample points.
    report(worst <= 1.5,
           "the straighten preview shows what committing produces",
           "worst disagreement " + std::to_string(worst) + " px");

    // And with no crop the pivot is the frame center, which must leave the
    // center pixel exactly where it started.
    orion::pipe::params::Geometry centered{};
    centered.inSize[0] = kW; centered.inSize[1] = kH;
    centered.outSize[0] = kW; centered.outSize[1] = kH;
    centered.straightenRad = kAngle;
    centered.cropSize[0] = 1.0f; centered.cropSize[1] = 1.0f;
    centered.pivot[0] = 0.5f; centered.pivot[1] = 0.5f;
    const auto whole = render(centered);

    const std::size_t mid = (std::size_t(kH / 2) * kW + kW / 2) * 4;
    checkNear(double(whole[mid + 0]), kW / 2.0, 0.6,
              "rotating about the center leaves the center column put");
    checkNear(double(whole[mid + 1]), kH / 2.0, 0.6,
              "rotating about the center leaves the center row put");
}

/// The noise estimator, against noise we made ourselves.
///
/// A fitted a and b are only useful if they are close to the truth, and a
/// synthetic frame is the only place the truth is known. The generator is a
/// smooth ramp plus Poisson-Gaussian noise at a chosen a and b.

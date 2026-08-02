#include "Engine.h"

#include "pipe/AutoEnhance.h"
#include "ResourcePaths.h"

#include <stdexcept>
#include <algorithm>
#include <vector>

namespace orion {

Engine::Engine() : device_(gpu::Device::create()) {}

Engine::~Engine() = default;

const pipe::LensDatabase& Engine::lensDatabase() {
    // Function-local static: parsed on the first open, never on a path where
    // nothing asks for it, and thread-safe initialization is the language's
    // problem rather than ours.
    static const pipe::LensDatabase db(res::dataDir() + "/lensfun");
    return db;
}

void Engine::openRaw(const std::string& path) {
    const auto image = raw::decodeBayer(path);
    sourcePath_ = path;

    // The lens name lives in the metadata rather than the mosaic, so it takes a
    // second read of the file — cheap next to the decode that just happened.
    const auto info = raw::readInfo(path);
    lensProfile_ = lensDatabase().lookup(info.lens, info.focalLength, info.aperture);

    // Reuse the compiled graph whenever the new frame has the same shape.
    // Rebuilding recompiles sixteen shaders and reallocates roughly 2.5 GiB of
    // textures — for a folder of frames from one camera, all of that is
    // identical work repeated per photo.
    // The preview mosaic. Decimated once here rather than per render: it is
    // the same pixels every time, and at 24 MP the decimation is not free.
    const auto small = raw::decimate(image, kPreviewScale);

    if (develop_ && develop_->canReload(image)) {
        develop_->reload(image);
        if (preview_ && preview_->canReload(small)) preview_->reload(small);
        camera_ = image.camera;
        return;
    }

    // Replace only once decode and construction have both succeeded, so a
    // failed open leaves the previously open image intact.
    auto next = std::make_unique<pipe::DevelopPipeline>(*device_, res::shaderDir(), image);

    // ⚠ Built after the full graph and allowed to fail. A machine that cannot
    // find room for the preview's intermediates should still be able to edit
    // the photograph — degrade-then-refine is a comfort, and losing it is not
    // a reason to refuse the file. `renderPreview` reports zero when there is
    // none and the caller falls back to the full graph.
    std::unique_ptr<pipe::DevelopPipeline> smallNext;
    try {
        smallNext = std::make_unique<pipe::DevelopPipeline>(
            *device_, res::shaderDir(), small);
        // ⚠ One grain field at two resolutions, not two realisations of it.
        // The plate is addressed in frame coordinates, so the preview has to
        // say how many frame pixels each of its pixels covers or it will show
        // the per-pixel variance where the settled render averages sixteen —
        // and read an order of magnitude grainier than the picture it previews.
        smallNext->setGridStep(static_cast<float>(kPreviewScale));
    } catch (const std::exception&) {
        smallNext.reset();
    }

    develop_ = std::move(next);
    preview_ = std::move(smallNext);
    camera_  = image.camera;
}

void Engine::setAdjustments(const pipe::Adjustments& adj) {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->apply(adj);
    // ⚠ Both, always. See the note in the header: a preview left stale is the
    // graph that gets read the instant the drag ends.
    if (preview_) preview_->apply(adj);
}

double Engine::renderPreview() {
    if (!preview_) return 0.0;
    return preview_->render();
}

void Engine::setBrushStroke(int component, const float* xy,
                            const float* erase, int count) {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->setBrushStroke(component, xy, erase, count);
    // Dabs are normalized displayed coordinates and the nib is a fraction of
    // the displayed picture, so the same stroke means the same thing on a
    // quarter-linear mosaic. Nothing is scaled here on purpose.
    if (preview_) preview_->setBrushStroke(component, xy, erase, count);
}

bool Engine::setMaskMatte(int component, const float* alpha, int width, int height) {
    if (!develop_) throw std::runtime_error("no image open");
    const bool ok = develop_->setMaskMatte(component, alpha, width, height);
    // The full graph's answer is the one the caller gets. The preview's
    // allocation is derived from the frame's aspect and can round a texel
    // differently, so a matte it refuses costs the drag its matte and nothing
    // else — the same bargain as building the preview graph at all.
    if (ok && preview_) preview_->setMaskMatte(component, alpha, width, height);
    return ok;
}

void Engine::setCreativeLut(const pipe::CubeLut& lut) {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->setCreativeLut(lut);
    if (preview_) preview_->setCreativeLut(lut);
}

void Engine::clearCreativeLut() {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->clearCreativeLut();
    if (preview_) preview_->clearCreativeLut();
}

const pipe::DevelopPipeline& Engine::previewDevelop() const {
    if (!preview_) throw std::runtime_error("no preview graph");
    return *preview_;
}

double Engine::render() {
    if (!develop_) throw std::runtime_error("no image open");
    return develop_->render();
}

void Engine::sampleAt(float u, float v, float* outDisplay, float* outScene) const {
    if (outDisplay) outDisplay[0] = outDisplay[1] = outDisplay[2] = 0.0f;
    if (outScene)   outScene[0]   = outScene[1]   = outScene[2]   = 0.0f;
    if (!develop_) return;

    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();
    if (w == 0 || h == 0) return;

    const auto clamp01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
    const auto x = static_cast<std::uint32_t>(clamp01(u) * float(w - 1));
    const auto y = static_cast<std::uint32_t>(clamp01(v) * float(h - 1));

    // What is on screen.
    //
    // ⚠️ Format-aware, and it was not. The tail of the graph writes eight bits
    // for the screen and sixteen only around an export, and this read the
    // texture as `__fp16` either way. Reading an RGBA8Unorm texture as half
    // float does not fail — it reinterprets four bytes as two halves and
    // returns nonsense, including NaN, which is the same trap already recorded
    // in the bench's `output16`.
    //
    // The consequence was not a wrong color, it was a *silent* one: the
    // color-mixer eyedropper asks for this pixel's hue, `TargetedAdjust.hue`
    // correctly refuses a pixel with no hue to speak of, and NaN reads as no
    // hue. So the eyedropper picked nothing, said nothing, and looked broken.
    if (outDisplay) {
        const auto& tex = develop_->output();
        if (tex.format() == gpu::PixelFormat::RGBA16Float) {
            __fp16 px[4]{};
            tex.readPixel(x, y, px);
            for (int i = 0; i < 3; ++i) {
                outDisplay[i] = std::clamp(float(px[i]), 0.0f, 1.0f);
            }
        } else {
            std::uint8_t px[4]{};
            tex.readPixel(x, y, px);
            for (int i = 0; i < 3; ++i) {
                outDisplay[i] = float(px[i]) / 255.0f;
            }
        }
    }

    // The unedited scene color. The reference image is the whole frame, before
    // the geometry node — uncropped, unturned and unstraightened — so the
    // displayed point has to be carried into it.
    //
    // ⚠️ **Through `displayedToFrame`, which is the transform a mask goes
    // through, and not a hand-rolled inverse.** This undid the quarter turn and
    // nothing else, so on a cropped photograph it read whatever sat at that
    // fraction of the *uncropped* frame: click the yellow car, get the hue of
    // the tarmac above it. Silent, because the color-mixer band it picks is
    // plausible either way, and invisible to `repro/eyedropper-color-mixer.txt`
    // because that scenario never crops. A straighten was wrong the same way.
    if (outScene) {
        const std::uint32_t rw = develop_->width();
        const std::uint32_t rh = develop_->height();

        const auto f = develop_->displayedToFrame(clamp01(u), clamp01(v));
        const auto sx = static_cast<std::uint32_t>(clamp01(f.first)  * float(rw - 1));
        const auto sy = static_cast<std::uint32_t>(clamp01(f.second) * float(rh - 1));

        __fp16 px[4]{};
        develop_->referenceImage().readPixel(sx, sy, px);

        // Scene-linear and unbounded. Normalize by the peak: the caller wants
        // hue, and hue is scale-invariant.
        const float peak = std::max({float(px[0]), float(px[1]), float(px[2]), 1e-6f});
        for (int i = 0; i < 3; ++i) outScene[i] = std::max(float(px[i]), 0.0f) / peak;
    }
}

void Engine::autoEnhance(pipe::Adjustments& adj) {
    if (!develop_) return;

    namespace ae = pipe::auto_enhance;
    constexpr std::uint32_t kBins = 256;

    std::vector<std::uint32_t> rgb(std::size_t(kBins) * 3);
    std::vector<std::uint32_t> combined(kBins);

    const auto measureNow = [&]() {
        develop_->apply(adj);
        develop_->render();
        histogram(rgb.data(), kBins);
        ae::combine(rgb.data(), kBins, combined.data());
        return ae::measure(combined.data(), kBins, ae::kClipPerSide);
    };

    // Auto owns five controls and **replaces** them, so it puts them back to
    // their defaults before it measures anything. planning/DECISIONS.md #66.
    //
    // Without this the button is not a function of the photograph: press one
    // leaves the frame corrected, press two measures that corrected frame,
    // derives a different look from it, and therefore solves the tone controls
    // differently. Measured before the change, on one frame: +2.25 EV with lift
    // 0.16, then +2.99 EV with lift 0.00. It also made the comment below false
    // on every press after the first — the look was responding to the
    // correction, which is exactly what that comment says it must not do.
    //
    // Only these five. White balance, contrast, the crop, the curve and
    // everything else the photographer set stays in place and is measured
    // *through*, because those are not Auto's to discard.
    adj.exposureEv = 0.0f;
    adj.whites     = 0.0f;
    adj.blacks     = 0.0f;
    adj.fusion     = 0.0f;
    adj.clarity    = 0.0f;

    // Measure the photograph as it stands first. The look controls respond to
    // the picture the photographer took, not to the correction about to be
    // applied to it — deriving them afterwards would let the solver's own work
    // decide how much shadow lift the frame "needs".
    const ae::Controls look = ae::look(measureNow());
    adj.fusion  = look.fusion;
    adj.clarity = look.clarity;

    // Then solve, with the look already in place, because it moves the
    // histogram the solver is reading.
    ae::Controls c{};

    // Stop when the median arrives rather than after a fixed count. Every step
    // undershoots — see kDamping — so a frame far from the anchor needs more
    // steps than a frame near it, and a flat count either short-changes the
    // first or makes the second pay for renders it does not need.
    for (int pass = 0; pass < ae::kMaxPasses; ++pass) {
        adj.exposureEv = c.exposureEv;
        adj.blacks     = c.blacks;
        adj.whites     = c.whites;

        const ae::Stats s = measureNow();
        if (std::abs(s.median - ae::kMidGray) < ae::kSettled) break;
        c = ae::refine(c, s);
    }

    adj.exposureEv = c.exposureEv;
    adj.blacks     = c.blacks;
    adj.whites     = c.whites;
    develop_->apply(adj);
    develop_->render();
}

void Engine::histogram(std::uint32_t* out, std::uint32_t bins) const {
    if (out == nullptr || bins == 0) return;
    std::fill_n(out, std::size_t(bins) * 3, 0u);
    if (!develop_) return;

    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();
    if (w == 0 || h == 0) return;

    // Format-aware: the tail is eight bits for the screen and sixteen only
    // around an export, and the histogram is a screen reader.
    const std::vector<float> pixels = readOutputFloat(w, h);

    // A prime stride decorrelates from any repeating structure in the image,
    // so a picket fence cannot alias into a false peak.
    constexpr std::size_t kStride = 31;
    const std::size_t count = std::size_t(w) * h;

    for (std::size_t i = 0; i < count; i += kStride) {
        for (std::uint32_t c = 0; c < 3; ++c) {
            const float v = std::clamp(pixels[i * 4 + c], 0.0f, 1.0f);
            const std::uint32_t bin = std::min<std::uint32_t>(
                bins - 1, static_cast<std::uint32_t>(v * float(bins)));
            ++out[c * bins + bin];
        }
    }
}

/// Retargets the graph's tail for the duration of a read, and puts it back.
///
/// The display and geometry nodes write eight bits for the screen, because the
/// drawable is `bgra8Unorm` and anything wider is bytes moved for precision
/// nothing can show — worth 3.4 ms of a 16 ms budget. Export is the one
/// consumer that can use sixteen, so it asks, and pays the reallocation.
///
/// ⚠️ **An eight-bit export must take the narrow graph, not the wide one.**
/// The narrow tail is the path that dithers (`ops/dither_ops.slang`), and
/// rounding a smooth sixteen-bit sky down to eight without one is the banding
/// the dither exists to prevent. So the depth the file is going to have decides
/// which graph renders it, and an eight-bit export is byte-for-byte what the
/// screen shows.
namespace {
struct WideOutputFor {
    pipe::DevelopPipeline& pipeline;
    const bool was;

    WideOutputFor(pipe::DevelopPipeline& p, bool wide)
        : pipeline(p), was(p.wideOutput()) {
        pipeline.setWideOutput(wide);
    }
    ~WideOutputFor() {
        // Reallocates, so it can throw, and a throwing destructor during
        // unwinding terminates the process. Failing to narrow again costs
        // latency; terminating costs the user's work.
        try {
            pipeline.setWideOutput(was);
        } catch (...) {
        }
    }
    WideOutputFor(const WideOutputFor&) = delete;
    WideOutputFor& operator=(const WideOutputFor&) = delete;
};
}  // namespace

void Engine::exportImage(const std::string& path, const util::ExportOptions& options) {
    if (!develop_) throw std::runtime_error("no image open");

    // Sixteen bits end to end when the file will hold sixteen, and the screen's
    // own dithered eight when it will not. Restored either way.
    WideOutputFor wide(*develop_, options.depth == util::BitDepth::Sixteen);

    // Make sure what we write matches what is on screen.
    develop_->render();

    // The orientation node's texture is square; only the oriented rectangle
    // inside it holds pixels.
    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();

    util::ExportOptions o = options;
    // Only the real write carries metadata. The size estimate does not, and
    // the difference is a few hundred bytes against a file measured in
    // megabytes — but it is a difference, so it is stated rather than hidden.
    o.metadataFrom = sourcePath_;

    const auto pixels = readOutput16(w, h);
    util::writeImage(path, pixels.data(), w, h,
                     static_cast<std::size_t>(w) * 4 * sizeof(std::uint16_t), o);
}

std::size_t Engine::exportedSize(const util::ExportOptions& options) {
    if (!develop_) throw std::runtime_error("no image open");

    // The estimate has to encode the same pixels the real write will — which
    // includes the depth, since eight bits is roughly half the PNG.
    WideOutputFor wide(*develop_, options.depth == util::BitDepth::Sixteen);
    develop_->render();

    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();

    const auto pixels = readOutput16(w, h);
    return util::encodedSize(pixels.data(), w, h,
                             static_cast<std::size_t>(w) * 4 * sizeof(std::uint16_t),
                             options);
}

/// The output as 16-bit unsigned, which is what the image formats want.
///
/// The graph ends in half float. Converting here rather than making the graph
/// write integers keeps the pipeline in one numeric world, and half float is
/// the format Metal guarantees is read-write.
std::vector<float> Engine::readOutputFloat(std::uint32_t w, std::uint32_t h) const {
    const std::size_t count = static_cast<std::size_t>(w) * h * 4;
    std::vector<float> out(count);

    if (develop_->output().format() == gpu::PixelFormat::RGBA16Float) {
        std::vector<__fp16> half(count);
        develop_->output().download(half.data(),
                                    static_cast<std::size_t>(w) * 4 * sizeof(__fp16), w, h);
        for (std::size_t i = 0; i < count; ++i) out[i] = static_cast<float>(half[i]);
    } else {
        std::vector<std::uint8_t> bytes(count);
        develop_->output().download(bytes.data(), static_cast<std::size_t>(w) * 4, w, h);
        for (std::size_t i = 0; i < count; ++i) out[i] = bytes[i] / 255.0f;
    }
    return out;
}

std::vector<std::uint16_t> Engine::readOutput16(std::uint32_t w, std::uint32_t h) const {
    const std::size_t count = static_cast<std::size_t>(w) * h * 4;
    std::vector<std::uint16_t> out(count);

    // ⚠️ The tail is not always half float. An eight-bit export renders through
    // the narrow graph on purpose — that is the path that dithers — and reading
    // `RGBA8Unorm` as half float would give noise, not a picture.
    if (develop_->output().format() != gpu::PixelFormat::RGBA16Float) {
        std::vector<std::uint8_t> bytes(count);
        develop_->output().download(bytes.data(), static_cast<std::size_t>(w) * 4, w, h);
        // 257, not 256: it maps 255 to 65535 exactly, so quantising back to
        // eight bits in the writer returns the byte the graph produced. Any
        // other scale would round some values to a neighbour and undo the
        // dither this path exists for.
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = static_cast<std::uint16_t>(bytes[i] * 257u);
        }
        return out;
    }

    std::vector<__fp16> half(count);
    develop_->output().download(half.data(),
                                static_cast<std::size_t>(w) * 4 * sizeof(__fp16), w, h);
    for (std::size_t i = 0; i < count; ++i) {
        const float v = std::clamp(float(half[i]), 0.0f, 1.0f);
        out[i] = static_cast<std::uint16_t>(v * 65535.0f + 0.5f);
    }
    return out;
}

const pipe::DevelopPipeline& Engine::develop() const {
    if (!develop_) throw std::runtime_error("no image open");
    return *develop_;
}

pipe::DevelopPipeline& Engine::developMutable() {
    if (!develop_) throw std::runtime_error("no image open");
    return *develop_;
}

}  // namespace orion

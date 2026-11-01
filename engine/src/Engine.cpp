#include "Engine.h"

#include "pipe/AutoEnhance.h"
#include "pipe/LensGeometry.h"
#include "pipe/LensOpcodes.h"
#include "ResourcePaths.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <variant>
#include <vector>

namespace orion {

namespace {

/// The last resort's words (#406): which effect the card refused, what the
/// graph needs at once and what the card gave it before refusing, both
/// counting the graph's own resident plates. Keeps "out of GPU memory", which
/// the shell keys its shedding on.
gpu::OutOfMemory outOfMemory(const pipe::DevelopPipeline& graph, std::uint64_t free) {
    const auto gb = [](std::uint64_t b) {
        char text[32];
        std::snprintf(text, sizeof text, "%.1f GB", double(b) / 1e9);
        return std::string(text);
    };
    return gpu::OutOfMemory("out of GPU memory: rendering " + graph.failedEffect() +
                            " needs " + gb(graph.neededBytes()) + ", " + gb(free) + " free");
}

}  // namespace

Engine::Engine() : device_(gpu::Device::create()) {
    // Eager, so hdrMergeProgress/hdrMergeCancel from another thread never
    // race a lazy construction inside run().
    hdrMerge_ = std::make_unique<merge::HdrMerge>(*device_, res::shaderDir());
    stacker_ = std::make_unique<stack::Stacker>(*device_, res::shaderDir());
}

Engine::~Engine() {
    // The Dawn device outlives this engine while another holds it (#392).
    develop_.reset(); preview_.reset(); region_.reset(); display_.reset(); samples_.reset(); hdrMerge_.reset();
    device_->reclaim();
}

const pipe::LensDatabase& Engine::lensDatabase() {
    // Function-local static: parsed on the first open, never on a path where
    // nothing asks for it, and thread-safe initialization is the language's
    // problem rather than ours.
    static const pipe::LensDatabase db(res::dataDir() + "/lensfun");
    return db;
}

pipe::LensProfile Engine::automaticLensProfile() const {
    // Precedence: the file's own DNG opcodes, then lensfun (#372-#374; Adobe
    // .lcp support was withdrawn pending a patent decision, branch
    // lcp-adobe-profiles). The opcodes are the manufacturer's correction for
    // this exact frame, written knowing its focal length and crop, so nothing
    // measured elsewhere outranks them.
    const auto dng = pipe::parseDngLensOpcodes(info_.dngOpcodes);
    if (dng.hasWarp || dng.hasVignette) {
        pipe::LensProfile p;
        p.found = true;
        p.source = pipe::LensSource::DngOpcode;
        p.model = dng.model;
        p.lens = dng.hasWarp && dng.hasVignette ? "Embedded in the file"
               : dng.hasWarp ? "Embedded in the file (distortion)"
                             : "Embedded in the file (vignetting)";
        return p;
    }
    return lensDatabase().lookup(info_.lens, info_.focalLength, info_.aperture);
}

void Engine::setHeadless(bool on) noexcept {
    headless_ = on;
    // The release policy can follow an open graph; the preview cannot be
    // unbuilt, which is why the header asks for this before `openRaw`.
    if (develop_) develop_->graph().setReleaseEveryRender(on);
}

void Engine::openRaw(const std::string& path) {
    // A refusal's ceiling belongs to the photo that met it (#406).
    device_->forgetOutOfMemory();
    // Either a mosaic or a demosaiced linear DNG — a merged HDR frame comes
    // back through this same door, which is the point of writing one.
    auto decoded = raw::decode(path);
    sourcePath_ = path;

    // The lens name lives in the metadata rather than the mosaic, so it takes a
    // second read of the file — cheap next to the decode that just happened.
    info_ = raw::readInfo(path);
    // A choice belongs to the photograph that was open, not to the next one.
    lensChoice_.clear();
    lensProfile_ = automaticLensProfile();
    openDecoded(std::move(decoded));
}

void Engine::openDecoded(raw::DecodedImage decoded) {
    // One body for both kinds of source: every operation below — decimate,
    // canReload, reload, the pipeline constructor — is overloaded for the
    // mosaic and the linear image, and a mosaic graph never reloads a linear
    // frame or vice versa (the two build different node sets).
    std::visit([&](const auto& image) {
        // Reuse the compiled graph whenever the new frame has the same shape.
        // Rebuilding recompiles sixteen shaders and reallocates roughly 2.5 GiB
        // of textures — for a folder of frames from one camera, all of that is
        // identical work repeated per photo.
        if (develop_ && develop_->canReload(image)) {
            develop_->reload(image);
            // ⚠ Headless never built a preview, and never decimates for one:
            // the decimation is a second, smaller copy of the mosaic nobody
            // would render (#312).
            if (preview_ && !headless_) {
                const auto small = raw::decimate(image, kPreviewScale);
                if (preview_->canReload(small)) preview_->reload(small);
            }
            develop_->graph().setReleaseEveryRender(headless_);
            camera_ = image.camera;
            return;
        }

        // Replace only once decode and construction have both succeeded, so a
        // failed open leaves the previously open image intact.
        auto next = std::make_unique<pipe::DevelopPipeline>(*device_, res::shaderDir(), image);
        next->graph().setReleaseEveryRender(headless_);
        if (headless_) {
            develop_ = std::move(next);
            preview_.reset();
            camera_  = image.camera;
            return;
        }

        // The preview image. Decimated once here rather than per render: it is
        // the same pixels every time, and at 24 MP the decimation is not free.
        const auto small = raw::decimate(image, kPreviewScale);

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
    }, decoded);

    // The region graph cuts its windows from this (#22). Kept only for a
    // mosaic in an interactive engine; the old window is stale either way.
    auto* mosaic = std::get_if<raw::BayerImage>(&decoded);
    mosaic_ = mosaic && !headless_
        ? std::make_shared<const raw::BayerImage>(std::move(*mosaic)) : nullptr;
    regionLoaded_ = false;
    // A new photograph: the region graph's texture still holds the last one,
    // and a canvas asking for it before the next region would show that.
    region_.reset();
    regionStep_ = 0;
    fullReleased_ = false;
    fullCurrent_ = previewCurrent_ = regionCurrent_ = false;
}

float Engine::hdrMerge(const std::vector<std::string>& paths, int referenceIndex,
                       const std::string& outputPath) {
    return hdrMerge_->run(paths, referenceIndex, outputPath);
}

float Engine::hdrMergeProgress() const noexcept {
    return hdrMerge_->progress();
}

void Engine::hdrMergeCancel() noexcept {
    hdrMerge_->cancel();
}

std::vector<std::string> Engine::lensDatabaseNames() {
    return lensDatabase().names();
}

bool Engine::setLensChoice(const std::string& makerModel) {
    if (makerModel.empty()) {
        // Back to what the file says, which may be nothing at all.
        lensChoice_.clear();
        lensProfile_ = automaticLensProfile();
        return true;
    }

    const auto picked = lensDatabase().lookupExact(makerModel, info_.focalLength, info_.aperture);
    if (!picked.found) return false;

    lensChoice_ = makerModel;
    lensProfile_ = picked;
    return true;
}

void Engine::setAdjustments(const pipe::Adjustments& adj) {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->apply(adj);
    // ⚠ Both, always. See the note in the header: a preview left stale is the
    // graph that gets read the instant the drag ends.
    if (preview_) preview_->apply(adj);
    fullCurrent_ = previewCurrent_ = regionCurrent_ = false;
    // The region graph applies these itself when it renders: it is rebuilt or
    // reloaded as the view moves, and an apply now could be for a window
    // nobody will look at.
    adj_ = adj;
}

namespace {
/// Region graph pixels of margin around what the screen shows: the supports
/// of the nodes `regionSupports` allows, added, since each reads the last's
/// output. Smoothness's two starlet scales at a quarter of the size, 2·(4+8)
/// quarter pixels = 96; the four full-size scales 2·(1+2+4+8) = 30; its down-
/// and upsampling 8; demosaic and non-local means under 8 each; the lens's
/// chromatic aberration up to ~11 at full slider on 24 MP; sharpening 3. About
/// 165; 192 leaves room. A texel this far inside the window is computed from
/// the same neighbours as in the whole frame; the edge clamp is past it.
constexpr std::uint32_t kRegionHalo = 192;
/// The window's origin is a multiple of this many region pixels, so every
/// downsampled grid in the graph (Smoothness's quarter, the CFA's two) lands
/// on the same phase as the whole frame's.
constexpr std::uint32_t kRegionAlign = 16;
/// The window's size is rounded up to this many region pixels, so a pan
/// reloads the same graph rather than building a new one.
constexpr std::uint32_t kRegionBucket = 256;
}  // namespace

double Engine::renderRegion(const Region& v, bool interacting) {
    if (!develop_ || !mosaic_ || headless_) return -1.0;
    if (!pipe::DevelopPipeline::regionSupports(adj_)) return -1.0;
    if (!(v.w > 0.0f && v.h > 0.0f) || v.outW == 0 || v.outH == 0) return -1.0;
    const raw::BayerImage& img = *mosaic_;

    // Frame pixels per screen pixel decides the step: the largest power of two
    // that still gives the screen at least one region pixel per pixel, so the
    // region is never softer than the screen. Capped at eight.
    const double ratio = std::min(double(v.w) * develop_->outputWidth() / v.outW,
                                  double(v.h) * develop_->outputHeight() / v.outH);
    std::uint32_t step = 1;
    while (step < 8 && step * 2 <= ratio * 1.001) step *= 2;   // 4.0 computed as 3.9999 is 4
    // The denoise chain's supports are fixed in graph pixels (the starlet's
    // dilations, the NLM window), so at a coarser step the same slider would
    // reach further across the picture and the look would change with the
    // zoom. Those edits render at step 1: the full render's look, at a cost
    // still bounded by the window — never more than the whole graph.
    if (adj_.denoiseLuma > 0.0f || adj_.denoiseColor > 0.0f || adj_.denoiseNlm > 0.0f ||
        std::fabs(adj_.texture) > 1e-4f) {
        // Mid-drag, a step-1 window this far out costs several times the
        // preview (4.7-6.5 ms a frame at 50% on 24 MP, against 0.8): a latency
        // regression. The drag takes the preview, as it always did.
        if (interacting && step > 1) return -1.0;
        step = 1;
    }

    // What the geometry node samples for the region, then what the lens
    // fetches for that: the window must cover both, because the nodes before
    // the lens see its source and the nodes after it see its output. The edge
    // suffices — both maps are continuous and one-to-one on the picture.
    const float W = float(img.width), H = float(img.height);
    float x0 = W, y0 = H, x1 = 0.0f, y1 = 0.0f;
    const auto grow = [&](float x, float y) {
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
    };
    const pipe::params::Lens* lens = develop_->lensParams();
    constexpr int kEdge = 32;
    for (int i = 0; i <= kEdge; ++i) {
        const float t = float(i) / kEdge;
        const float pts[4][2] = {{v.x + t * v.w, v.y}, {v.x + t * v.w, v.y + v.h},
                                 {v.x, v.y + t * v.h}, {v.x + v.w, v.y + t * v.h}};
        for (const auto& p : pts) {
            const auto f = develop_->sampledFrom(p[0], p[1]);
            const float fx = f.first * W, fy = f.second * H;
            grow(fx, fy);
            for (int ch = 0; lens && ch < 3; ++ch) {
                float sx = 0.0f, sy = 0.0f;
                pipe::lens::fetchPoint(*lens, ch, fx, fy, sx, sy);
                grow(sx, sy);
            }
        }
    }

    // In region pixels from here: the whole frame at this step (decimate's
    // whole cells), the margin, the alignment, then a bucketed size that fits.
    const std::uint32_t fw = step == 1 ? img.width  : img.width  / (2 * step) * 2;
    const std::uint32_t fh = step == 1 ? img.height : img.height / (2 * step) * 2;
    const auto span = [&](float lo, float hi, std::uint32_t whole,
                          std::uint32_t& origin, std::uint32_t& size) {
        const float a = std::floor(lo / float(step)) - float(kRegionHalo);
        const float b = std::ceil(hi / float(step)) + float(kRegionHalo);
        std::uint32_t o = a <= 0.0f ? 0u : std::uint32_t(a) / kRegionAlign * kRegionAlign;
        const std::uint32_t e = b <= 0.0f ? 0u : std::min(whole, std::uint32_t(b));
        size = (std::max(e, o + kRegionAlign) - o + kRegionBucket - 1) / kRegionBucket * kRegionBucket;
        if (size >= whole) { origin = 0; size = whole; return; }
        // At the far edge: the aligned origin at or before `whole - size`, and
        // the window then runs to the edge itself. Stopping at `o + size`
        // would drop the last `whole % 16` rows — a transparent strip — and
        // the size stays put along the edge, so panning there still reloads.
        if (o + size > whole) {
            o = (whole - size) / kRegionAlign * kRegionAlign;
            size = whole - o;
        }
        origin = o;
    };
    // Past fit the canvas shows regions and nothing on screen reads the full
    // graph: its textures go back (#318's release), ~3.3 GB at 24 MP, before
    // the window allocates, not after: at 42 MP beside a desktop, the full
    // graph an export's size estimate had just rendered left every region
    // out of memory (#401). The next full render (export, an edit the region
    // refuses) rebuilds them.
    if (!fullReleased_) {
        develop_->graph().releaseOutputs();
        device_->reclaim();
        fullReleased_ = true;
        fullCurrent_ = false;
    }
    std::uint32_t ox = 0, oy = 0, ww = 0, wh = 0;
    span(x0, x1, fw, ox, ww);
    span(y0, y1, fh, oy, wh);

    // Built when the step or the size changes, reloaded when the window moves.
    // A window is a crop of the mosaic on whole cells, then the preview's own
    // decimation — so region pixel i is pixel i + origin of the whole frame
    // decimated by `step`, exactly.
    const bool fits = region_ && regionStep_ == int(step) &&
                      region_->width() == ww && region_->height() == wh;
    if (!fits || !regionLoaded_ || ox != regionX_ || oy != regionY_) {
        regionLoaded_ = false;   // until the load below has finished
        auto sub = raw::window(img, ox * step, oy * step, ww * step, wh * step);
        if (step > 1) sub = raw::decimate(sub, int(step));
        if (fits && region_->canReload(sub)) {
            region_->reload(sub);
        } else {
            region_ = std::make_unique<pipe::DevelopPipeline>(*device_, res::shaderDir(), sub);
            region_->setGridStep(float(step));
            if (lut_) region_->setCreativeLut(*lut_);
            regionView_ = {};
        }
        region_->setFrameWindow({ox, oy, fw, fh});
        // The frame's noise, not the window's: denoise must not depend on
        // what is on screen.
        region_->setNoiseProfile(develop_->noiseProfile());
        regionStep_ = int(step);
        regionX_ = ox; regionY_ = oy;
        regionLoaded_ = true;
    }
    if (std::memcmp(&regionView_, &v, sizeof v) != 0) {
        region_->setOutputRegion({v.x, v.y, v.w, v.h, v.outW, v.outH});
        regionView_ = v;
    }
    region_->apply(adj_);
    double ms = 0.0;
    try {
        ms = region_->render();
    } catch (...) {
        // A window that failed (out of memory, a lost device) keeps nothing:
        // the next one starts from empty rather than beside this one's
        // half-allocated graph (#401).
        region_.reset();
        regionLoaded_ = regionCurrent_ = false;
        regionView_ = {};
        device_->reclaim();
        throw;
    }
    regionCurrent_ = true;
    return ms;
}

const pipe::DevelopPipeline& Engine::regionDevelop() const {
    if (!region_) throw std::runtime_error("no region graph");
    return *region_;
}

double Engine::renderPreview() {
    if (!preview_) return 0.0;
    const double ms = preview_->render();
    previewCurrent_ = true;
    return ms;
}

void Engine::releaseRenderMemory() noexcept {
    if (develop_) develop_->graph().releaseOutputs();
    if (preview_) preview_->graph().releaseOutputs();
    if (region_)  region_->graph().releaseOutputs();
    if (display_) display_->release();   // the kernel stays compiled
    samples_.reset();
    device_->reclaim();
}

pipe::DisplayCopy& Engine::displayCopy() {
    if (!display_) display_ = std::make_unique<pipe::DisplayCopy>(*device_, res::shaderDir());
    return *display_;
}

void Engine::setBrushStroke(int component, const float* xy,
                            const float* erase, int count) {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->setBrushStroke(component, xy, erase, count);
    // Dabs are normalized displayed coordinates and the nib is a fraction of
    // the displayed picture, so the same stroke means the same thing on a
    // quarter-linear mosaic. Nothing is scaled here on purpose.
    if (preview_) preview_->setBrushStroke(component, xy, erase, count);
    fullCurrent_ = previewCurrent_ = regionCurrent_ = false;
}

bool Engine::setMaskMatte(int component, const float* alpha, int width, int height) {
    if (!develop_) throw std::runtime_error("no image open");
    const bool ok = develop_->setMaskMatte(component, alpha, width, height);
    // The full graph's answer is the one the caller gets. The preview's
    // allocation is derived from the frame's aspect and can round a texel
    // differently, so a matte it refuses costs the drag its matte and nothing
    // else — the same bargain as building the preview graph at all.
    if (ok && preview_) preview_->setMaskMatte(component, alpha, width, height);
    fullCurrent_ = previewCurrent_ = regionCurrent_ = false;
    return ok;
}

void Engine::setCreativeLut(const pipe::CubeLut& lut) {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->setCreativeLut(lut);
    if (preview_) preview_->setCreativeLut(lut);
    lut_ = std::make_shared<const pipe::CubeLut>(lut);
    if (region_) region_->setCreativeLut(lut);
    fullCurrent_ = previewCurrent_ = regionCurrent_ = false;
}

void Engine::clearCreativeLut() {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->clearCreativeLut();
    if (preview_) preview_->clearCreativeLut();
    lut_.reset();
    if (region_) region_->clearCreativeLut();
    fullCurrent_ = previewCurrent_ = regionCurrent_ = false;
}

const pipe::DevelopPipeline& Engine::previewDevelop() const {
    if (!preview_) throw std::runtime_error("no preview graph");
    return *preview_;
}

double Engine::render() {
    if (!develop_) throw std::runtime_error("no image open");
    // Out of GPU memory part way, the graph keeps what it did get and starves
    // the preview and every other open photo: give it back, as a release would.
    const auto giveBack = [&] {
        develop_->graph().releaseOutputs();
        // A region graph, if any, is not what is wanted now: the next region rebuilds it (#401).
        if (region_) region_->graph().releaseOutputs();
        regionCurrent_ = false;
        device_->reclaim();
        fullReleased_ = true;
        fullCurrent_ = false;
    };
    double ms = 0.0;
    try {
        ms = develop_->render();
    } catch (const gpu::OutOfMemory&) {
        // ⚠ What ran out was the plan's guess at the card (#392): Dawn cannot
        // say what is free, so until a refusal the rule budgets to 2.5 GB.
        // The refusal set the device's ceiling, so once more from empty is
        // planned to it, frugal where the cache will not fit (#406). Lean, one
        // render, only if that fails too.
        giveBack();
        try {
            ms = develop_->render();
        } catch (const gpu::OutOfMemory&) {
            giveBack();
            ms = renderLean(giveBack);
        } catch (...) {
            giveBack();
            throw;
        }
    } catch (...) {
        giveBack();
        throw;
    }
    fullReleased_ = false;
    fullCurrent_ = true;
    return ms;
}

const pipe::DevelopPipeline& Engine::measured() {
    // The full graph, unless the canvas has been showing regions since the
    // last edit and only the preview — the small whole-frame pass — has seen
    // it (#22). A readout a quarter of the size is right; a full-size one of
    // the previous edit is not.
    if (((!fullCurrent_ && regionCurrent_) || fullReleased_) && preview_) {
        // Mid-drag at 100% the region renders and the preview does not; the
        // released full graph has no output at all. Render the small pass now
        // (~1 ms for a tick, 7 ms from the mosaic) rather than hand that out.
        if (!previewCurrent_) renderPreview();
        return *preview_;
    }
    // No preview graph (it could not be built): the full one, rebuilt if released.
    if (fullReleased_) render();
    return *develop_;
}

void Engine::sampleAt(float u, float v, float* outDisplay, float* outScene) {
    if (outDisplay) outDisplay[0] = outDisplay[1] = outDisplay[2] = 0.0f;
    if (outScene)   outScene[0]   = outScene[1]   = outScene[2]   = 0.0f;
    if (!develop_) return;

    const pipe::DevelopPipeline& shown = measured();
    const std::uint32_t w = shown.outputWidth();
    const std::uint32_t h = shown.outputHeight();
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
        const auto& tex = shown.output();
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
    // From the same graph as the display read: past fit the full one may be
    // older than the edit (#22), and its reference with it.
    if (outScene) {
        const std::uint32_t rw = shown.width();
        const std::uint32_t rh = shown.height();

        const auto f = shown.displayedToFrame(clamp01(u), clamp01(v));
        const auto sx = static_cast<std::uint32_t>(clamp01(f.first)  * float(rw - 1));
        const auto sy = static_cast<std::uint32_t>(clamp01(f.second) * float(rh - 1));

        __fp16 px[4]{};
        shown.referenceImage().readPixel(sx, sy, px);

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

    // ⚠ Through render(), not the graph's own: it is what gives the memory back
    // and retries lean when the card runs out (#392). The look turns on
    // clarity, whose 24 MP full graph peaks at 2.3 GB on its own, beside
    // whatever the cache kept from the edit before, and the graph's own render
    // failed there with nothing to say why and the cache still held.
    const auto measureNow = [&]() {
        develop_->apply(adj);
        render();
        previewCurrent_ = regionCurrent_ = false;   // neither has had these
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
    ae::Controls c{};
    const pipe::Adjustments before = adj_;
    try {
        const ae::Controls look = ae::look(measureNow());
        adj.fusion  = look.fusion;
        adj.clarity = look.clarity;

        // Then solve, with the look already in place, because it moves the
        // histogram the solver is reading.
        //
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
        adj_ = adj;
        render();
    } catch (...) {
        // A measuring pass's values are nobody's edit: the caller's go back.
        adj_ = before;
        develop_->apply(adj_);
        throw;
    }
}

void Engine::histogram(std::uint32_t* out, std::uint32_t bins) {
    if (out == nullptr || bins == 0) return;
    std::fill_n(out, std::size_t(bins) * 3, 0u);
    if (!develop_) return;

    const pipe::DevelopPipeline& d = measured();
    const std::uint32_t w = d.outputWidth();
    const std::uint32_t h = d.outputHeight();
    if (w == 0 || h == 0) return;

    // A prime stride decorrelates from any repeating structure in the image,
    // so a picket fence cannot alias into a false peak.
    constexpr std::size_t kStride = 31;
    const std::size_t count = std::size_t(w) * h;
    const auto add = [&](std::uint32_t c, float pixel) {
        const float v = std::clamp(pixel, 0.0f, 1.0f);
        const std::uint32_t bin = std::min<std::uint32_t>(
            bins - 1, static_cast<std::uint32_t>(v * float(bins)));
        ++out[c * bins + bin];
    };

    // ⚠ **Sampled on the GPU, read back small (#393).** A full readback of
    // a 24 MP frame to keep one pixel in 31 cost 82 ms on the UI thread under
    // Dawn on Linux, every settled render. histogram_sample gathers the same
    // pixels in the output's own format, so the bins are unchanged.
    const std::uint32_t n = std::uint32_t((count + kStride - 1) / kStride);
    const std::uint32_t tw = std::min<std::uint32_t>(n, 2048), th = (n + tw - 1) / tw;
    const gpu::PixelFormat format = d.output().format();
    if (!sampleKernel_) {
        sampleLibrary_ = gpu::Library::createFromFile(*device_, res::shaderDir() + "/histogramSample.metallib");
        sampleKernel_ = gpu::Kernel::create(*device_, *sampleLibrary_, "histogramSample");
    }
    if (!samples_ || samples_->width() != tw || samples_->height() != th || samples_->format() != format)
        samples_ = gpu::Texture::create(*device_, tw, th, format);   // kept: no allocation a render
    gpu::Texture* samples = samples_.get();
    struct {
        std::uint32_t size[2], width, count, stride, unused;
    } p{{tw, th}, w, std::uint32_t(count), std::uint32_t(kStride), 0};
    static_assert(sizeof p == 24);
    gpu::CommandBuffer cb(*device_);
    cb.dispatch(*sampleKernel_, {&d.output(), samples}, &p, sizeof p, tw, th);
    cb.commitAndWait();

    // Read only in the output's native format. Expanding every pixel to float
    // used another 16 bytes per pixel even though only one in 31 is sampled.
    if (format == gpu::PixelFormat::RGBA16Float) {
        std::vector<__fp16> pixels(std::size_t(tw) * th * 4);
        samples->download(pixels.data(), std::size_t(tw) * 4 * sizeof(__fp16), tw, th);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::uint32_t c = 0; c < 3; ++c)
                add(c, static_cast<float>(pixels[i * 4 + c]));
        }
    } else {
        std::vector<std::uint8_t> pixels(std::size_t(tw) * th * 4);
        samples->download(pixels.data(), std::size_t(tw) * 4, tw, th);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::uint32_t c = 0; c < 3; ++c)
                add(c, pixels[i * 4 + c] / 255.0f);
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
    const bool changed;
    bool restored = false;

    WideOutputFor(pipe::DevelopPipeline& p, bool wide)
        : pipeline(p), was(p.wideOutput()), changed(wide != was) {
        if (changed) pipeline.setWideOutput(wide);
    }
    void restore() {
        if (!changed || restored) return;
        if (pipeline.wideOutput() != was) pipeline.setWideOutput(was);
        pipeline.render();
        restored = true;
    }
    ~WideOutputFor() {
        try { restore(); } catch (...) {} // preserve an earlier export error
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
    renderForExport();

    // The orientation node's texture can be larger than the picture (it grows
    // with room to spare, #399); only the oriented rectangle inside it holds pixels.
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
    wide.restore();
    rereleaseFull();
}

void Engine::renderForExport() {
    // Out of memory part way, the graph keeps every texture it did get, and
    // past fit no region frees them again (they are already "released"):
    // at 42 MP beside a desktop, a size estimate that failed so left 2.2 GB
    // behind and every region after it out of memory (#401). Give it back.
    const auto giveBack = [&] {
        develop_->graph().releaseOutputs();
        device_->reclaim();
        fullReleased_ = true;
        fullCurrent_ = false;
    };
    // Out of memory, once more planned to the ceiling the refusal set, then
    // lean, as the canvas's render does (#392, #406): an export fails only
    // where the canvas would, and says why the same way.
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            develop_->render();
            return;
        } catch (const gpu::OutOfMemory&) {
            giveBack();
        } catch (...) {
            giveBack();
            throw;
        }
    }
    renderLean(giveBack);
}

double Engine::renderLean(const std::function<void()>& giveBack) {
    // One render, not the photo's from then on (#392's lean retry was sticky
    // because the cache refilled; the memory rule now plans each render to the
    // ceiling the refusal set, #406). The headless engine stays lean (#312).
    struct Restore {
        pipe::Pipeline& graph;
        bool was;
        ~Restore() { graph.setReleaseEveryRender(was); }
    } restore{develop_->graph(), headless_};
    develop_->graph().setReleaseEveryRender(true);
    // What the card can give this graph: the most the retry reached, less
    // what the rest of the process held when it started.
    device_->resetPeak();
    const std::uint64_t start = device_->peakBytes();
    const std::uint64_t others = start - std::min<std::uint64_t>(
        start, develop_->graph().allocatedBytes());
    try {
        return develop_->render();
    } catch (const gpu::OutOfMemory&) {
        const std::uint64_t reached = device_->peakBytes();
        giveBack();
        throw outOfMemory(*develop_, reached - std::min(reached, others));
    } catch (...) {
        giveBack();
        throw;
    }
}

void Engine::rereleaseFull() noexcept {
    // An export past fit renders the full graph directly; give its ~3.3 GB
    // back again, as the region did (#22). The next export rebuilds them.
    if (fullReleased_ && develop_) {
        develop_->graph().releaseOutputs();
        device_->reclaim();
    }
}

std::size_t Engine::exportedSize(const util::ExportOptions& options) {
    std::uint32_t w = 0, h = 0;
    const auto pixels = exportPixels(options, w, h);
    return util::encodedSize(pixels.data(), w, h,
                             static_cast<std::size_t>(w) * 4 * sizeof(std::uint16_t), options);
}

std::vector<std::uint16_t> Engine::exportPixels(const util::ExportOptions& options,
                                                std::uint32_t& w, std::uint32_t& h) {
    if (!develop_) throw std::runtime_error("no image open");

    // The estimate has to encode the same pixels the real write will — which
    // includes the depth, since eight bits is roughly half the PNG.
    WideOutputFor wide(*develop_, options.depth == util::BitDepth::Sixteen);
    renderForExport();

    w = develop_->outputWidth();
    h = develop_->outputHeight();
    auto pixels = readOutput16(w, h);
    wide.restore();
    rereleaseFull();
    return pixels;
}

/// The output as 16-bit unsigned, which is what the image formats want.
/// Wide export reads half float; narrow export widens screen bytes.
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

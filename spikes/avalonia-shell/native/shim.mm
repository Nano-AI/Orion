// Spike glue between Orion's C facade and C#. Not product code.
//
// Two things the facade does not offer yet, and a second UI needs:
//  - the default look. contrast 1.45 and friends live in Engine.swift, so this
//    file has to repeat them. That duplication is the case for moving shared
//    state below the facade before a real port.
//  - a picture Avalonia can composite. The facade hands out an id<MTLTexture>;
//    present.mm copies it on the GPU into an IOSurface that Avalonia's Metal
//    renderer imports (#317, decision #354), and shim_read copies it to the CPU
//    for everything that has no GPU interop: headless, OpenGL, a failed import.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <vector>
#include "orion/orion.h"

// A brush stroke as the engine was given it: interleaved x, y and one erase
// flag per dab. The buffers are shared: a stroke that only grew (every pointer
// event of a drag) points into its predecessor's buffers with a larger count,
// so keeping every revision costs the dabs once, not once per event.
struct Stroke {
    std::shared_ptr<std::vector<float>> xy, erase;
    int dabs = 0;
};

struct Shim {
    OrionEngine* engine = nullptr;
    OrionAdjustments adj{};
    // What each mask row's brush holds in the engine, and its revision.
    // Revisions are global and never reused (Engine+Brush.swift's
    // brushRevisions), and every stroke is kept by its revision, so undo can
    // write an old revision back through shim_set and get its dabs with it.
    Stroke held[ORION_MAX_MASK_COMPONENTS];
    unsigned revisions[ORION_MAX_MASK_COMPONENTS]{};
    std::unordered_map<unsigned, Stroke> kept;
    unsigned last_revision = 0;
    // The overlay, applied for one render at a time (shim_render).
    int overlay = 0, overlay_layer = 0;
    // present.mm's last copy out of this engine's textures, which may still be
    // waiting on the GPU for Avalonia: the engine must not render over them first.
    id<MTLCommandBuffer> reader = nil;
};

// Waits until present.mm has read this engine's picture. Its copy waits on the
// GPU only for a hand-off Avalonia has already submitted (SharedPicture keeps
// one in flight), so this returns within a frame; in practice it is long done.
// ponytail: an export through orion_engine_export directly does not settle; a
// copy lands within a frame of its hand-off, long before a click. Route it here if that changes.
static void settle(Shim* s) {
    [s->reader waitUntilCompleted];
    s->reader = nil;
}

static void defaults(OrionAdjustments& a) {
    // Mirrors Engine.swift's non-zero initial values (lines 132-475).
    a.contrast = 1.45f;
    a.process = 2;
    a.crop_w = 1; a.crop_h = 1;
    a.lut_strength = 1;
    a.grain_size = 1.5f;
    a.vignette_field_angle = 45;
    a.sharpen_radius = 1;
}

// Every scalar and float[N] member of OrionAdjustments, by its C name, then
// every OrionMaskComponent member as mask_components.<name>[row]. fields.inc
// and mask_fields.inc are generated from orion.h on each build (gen_fields.py).
// The mask rows are in the table so a snapshot of the table is a snapshot of
// the mask group: EditHistory undoes a mask edit without knowing masks exist.
struct Field { const char* name; int length; bool isInt; size_t offset; size_t stride; };
constexpr bool float_kind = false, int_kind = true;
#define F(n, k) {#n, 1, k##_kind, offsetof(OrionAdjustments, n), 4},
#define A(n, len) {#n, len, false, offsetof(OrionAdjustments, n), 4},
static const Field kFields[] = {
#include "fields.inc"
#undef F
#define F(n, k) {"mask_components." #n, ORION_MAX_MASK_COMPONENTS, k##_kind,                    \
                 offsetof(OrionAdjustments, mask_components) + offsetof(OrionMaskComponent, n), \
                 sizeof(OrionMaskComponent)},
#include "mask_fields.inc"
    // The shim's own: shim_set lays the stroke kept for the revision it is given.
    F(brush_revision, int)
};
#undef F
#undef A
static const char kRevision[] = "mask_components.brush_revision";
// One row of the mask group by its short name, for shim_mask_set.
#define F(n, k) {#n, 1, k##_kind, offsetof(OrionMaskComponent, n), 4},
static const Field kMaskFields[] = {
#include "mask_fields.inc"
};
#undef F

template <size_t N>
static const Field* find(const Field (&table)[N], const char* name) {
    for (auto& f : table) if (std::strcmp(f.name, name) == 0) return &f;
    return nullptr;
}
static const Field* find(const char* name) { return find(kFields, name); }

// Writes a 4-byte field, int or float as the table says.
static void put(void* p, const Field* f, float value) {
    if (f->isInt) *static_cast<int32_t*>(p) = int32_t(std::lround(value));
    else *static_cast<float*>(p) = value;
}

// The four OrionCurveChannels by Session.Curve's numbering: master, R, G, B.
static OrionCurveChannel* channel(OrionAdjustments& a, int c) {
    switch (c) {
        case 0: return &a.curve_master;
        case 1: return &a.curve_red;
        case 2: return &a.curve_green;
        case 3: return &a.curve_blue;
        default: return nullptr;
    }
}

// 4-byte slot for field[index], or null when the name or index is wrong.
static void* slot(OrionAdjustments& a, const char* name, int index, const Field** out) {
    const Field* f = find(name);
    if (!f || index < 0 || index >= f->length) return nullptr;
    *out = f;
    return reinterpret_cast<char*>(&a) + f->offset + size_t(index) * f->stride;
}

// Hands one row's stroke to the engine. Does not bump the revision.
static bool lay(Shim* s, int row, const Stroke& st) {
    const bool any = st.dabs > 0;
    return orion_engine_set_brush_stroke(s->engine, row, any ? st.xy->data() : nullptr,
                                         any ? st.erase->data() : nullptr, st.dabs) == ORION_OK;
}

// Whether `st` holds exactly these dabs (`erase` null means none erases).
static bool holds(const Stroke& st, const float* xy, const float* erase, int dabs, int upTo) {
    if (upTo > st.dabs || upTo > dabs) return false;
    for (int i = 0; i < upTo; ++i)
        if ((*st.xy)[2 * i] != xy[2 * i] || (*st.xy)[2 * i + 1] != xy[2 * i + 1] ||
            (*st.erase)[i] != (erase ? erase[i] : 0.0f)) return false;
    return true;
}

// The stroke for these dabs: the held one grown in place when it is a prefix
// and nothing kept later dabs in its buffers, else a fresh copy.
static Stroke grown(const Stroke& held, const float* xy, const float* erase, int dabs) {
    Stroke st;
    const bool grows = held.dabs > 0 && dabs > held.dabs && held.xy->size() == size_t(held.dabs) * 2 &&
                       holds(held, xy, erase, dabs, held.dabs);
    st.xy = grows ? held.xy : std::make_shared<std::vector<float>>();
    st.erase = grows ? held.erase : std::make_shared<std::vector<float>>();
    for (int i = grows ? held.dabs : 0; i < dabs; ++i) {
        st.xy->push_back(xy[2 * i]);
        st.xy->push_back(xy[2 * i + 1]);
        st.erase->push_back(erase ? erase[i] : 0.0f);
    }
    st.dabs = dabs;
    return st;
}

// Undo wrote a revision back: lay the stroke it names. 0 is the empty stroke.
static bool restore(Shim* s, int row, unsigned revision) {
    if (revision == s->revisions[row]) return true;
    Stroke st;
    if (revision != 0) {
        auto it = s->kept.find(revision);
        if (it == s->kept.end()) return false;
        st = it->second;
    }
    if (!lay(s, row, st)) return false;
    s->held[row] = st;
    s->revisions[row] = revision;
    return true;
}

extern "C" {

Shim* shim_open(const char* path) {
    auto* s = new Shim;
    if (orion_engine_create(&s->engine) != ORION_OK ||
        orion_engine_open_raw(s->engine, path) != ORION_OK) {
        std::fprintf(stderr, "shim_open: %s\n",
                     s->engine ? orion_last_error(s->engine) : "no engine");
        if (s->engine) orion_engine_destroy(s->engine);
        delete s;
        return nullptr;
    }
    orion_engine_as_shot(s->engine, &s->adj);
    defaults(s->adj);
    orion_engine_set_adjustments(s->engine, &s->adj);
    return s;
}

void shim_close(Shim* s) {
    if (!s) return;
    settle(s);
    orion_engine_destroy(s->engine);
    delete s;
}

OrionEngine* shim_engine(Shim* s) { return s->engine; }

int shim_field_count(void) { return int(std::size(kFields)); }

const char* shim_field_name(int i, int* length) {
    if (i < 0 || i >= shim_field_count()) return nullptr;
    *length = kFields[i].length;
    return kFields[i].name;
}

// *ok is 0 for an unknown name or an index past the array.
float shim_get(Shim* s, const char* name, int index, int* ok) {
    const Field* f = nullptr;
    void* p = slot(s->adj, name, index, &f);
    *ok = p != nullptr;
    if (!p) return 0;
    return f->isInt ? float(*static_cast<int32_t*>(p)) : *static_cast<float*>(p);
}

int shim_set(Shim* s, const char* name, int index, float value) {
    const Field* f = nullptr;
    void* p = slot(s->adj, name, index, &f);
    if (!p) return 0;
    if (std::strcmp(name, kRevision) == 0 && !restore(s, index, unsigned(std::lround(std::max(value, 0.0f)))))
        return 0;
    put(p, f, value);
    return orion_engine_set_adjustments(s->engine, &s->adj) == ORION_OK;
}

// One tone-curve channel into `xy` (interleaved x, y; room for `capacity`
// points). Returns the point count; a channel the engine reads as identity
// (count 0 or 1) comes back as Engine.swift's ToneCurve.identity, two points.
int shim_curve(Shim* s, int c, float* xy, int capacity) {
    const OrionCurveChannel* ch = channel(s->adj, c);
    if (!ch || capacity < 2) return 0;
    if (ch->count < 2) {
        const float identity[] = {0, 0, 1, 1};
        std::memcpy(xy, identity, sizeof identity);
        return 2;
    }
    const int n = std::min<int>(ch->count, capacity);
    for (int i = 0; i < n; ++i) { xy[2 * i] = ch->x[i]; xy[2 * i + 1] = ch->y[i]; }
    return n;
}

// Writes one channel and pushes it, as Engine.swift's curveChannel: points past
// ORION_CURVE_MAX_POINTS are dropped, and the engine reads anything malformed
// as identity. 0 for a channel outside 0-3.
int shim_set_curve(Shim* s, int c, const float* xy, int count) {
    OrionCurveChannel* ch = channel(s->adj, c);
    if (!ch || count < 0 || (count > 0 && !xy)) return 0;
    *ch = OrionCurveChannel{};
    ch->count = std::min(count, ORION_CURVE_MAX_POINTS);
    for (int i = 0; i < ch->count; ++i) { ch->x[i] = xy[2 * i]; ch->y[i] = xy[2 * i + 1]; }
    return orion_engine_set_adjustments(s->engine, &s->adj) == ORION_OK;
}

// The spots, Engine+Spots.swift's array, in frame coordinates: written and
// pushed. Slots past `count` keep what they held, so a history step that
// writes spot_count back by name (EditHistory's snapshot) gets its spots too.
int shim_set_spots(Shim* s, const OrionSpot* spots, int count) {
    if (count < 0 || count > ORION_MAX_SPOTS || (count > 0 && !spots)) return 0;
    std::copy(spots, spots + count, s->adj.spots);
    s->adj.spot_count = count;
    return orion_engine_set_adjustments(s->engine, &s->adj) == ORION_OK;
}

// Up to `capacity` of the spots the engine holds into `out`; returns how many it holds.
int shim_spots(Shim* s, OrionSpot* out, int capacity) {
    const int n = std::clamp(s->adj.spot_count, 0, ORION_MAX_SPOTS);
    if (out) std::copy(s->adj.spots, s->adj.spots + std::min(n, std::max(capacity, 0)), out);
    return n;
}

// Engine.swift's autoEnhance: the engine measures and writes exposure, whites,
// blacks, fusion (lift) and clarity into the block; the rest stays as sent.
int shim_auto_enhance(Shim* s) {
    settle(s);
    return orion_engine_auto_enhance(s->engine, &s->adj) == ORION_OK &&
           orion_engine_set_adjustments(s->engine, &s->adj) == ORION_OK;
}

// The mask group, Engine.swift's cAdjustments + Engine+Brush.swift's
// pushStrokes, in three calls: shim_mask_set each row's fields by C name
// (no push), shim_mask_stroke each row's dabs, then shim_mask_commit.
int shim_mask_field_count(void) { return int(std::size(kMaskFields)); }

int shim_mask_set(Shim* s, int row, const char* name, float value) {
    const Field* f = find(kMaskFields, name);
    if (!f || row < 0 || row >= ORION_MAX_MASK_COMPONENTS) return 0;
    put(reinterpret_cast<char*>(&s->adj.mask_components[row]) + f->offset, f, value);
    return 1;
}

// `xy` interleaved x, y pairs; `erase` one value per dab, nonzero takes coverage
// away (null: every dab paints, as a sidecar without brushErase means). Sent to
// the engine, and the row given a new revision, only when they differ from
// what the engine already holds: a slider tick on a brush row must not re-lay
// its whole stroke.
int shim_mask_stroke(Shim* s, int row, const float* xy, const float* erase, int dabs) {
    if (row < 0 || row >= ORION_MAX_MASK_COMPONENTS || dabs < 0 || (dabs > 0 && !xy)) return 0;
    Stroke& held = s->held[row];
    if (held.dabs == dabs && holds(held, xy, erase, dabs, dabs)) return 1;
    Stroke st = grown(held, xy, erase, dabs);
    if (!lay(s, row, st)) return 0;
    held = st;
    s->revisions[row] = ++s->last_revision;
    s->kept[s->last_revision] = st;   // an emptied stroke too: undo may write its revision back
    return 1;
}

// One row's stroke as the engine holds it, into `xy` (2 per dab) and `erase`
// (1 per dab, either may be null). Returns the dab count, copying at most `capacity`.
int shim_mask_stroke_get(Shim* s, int row, float* xy, float* erase, int capacity) {
    if (row < 0 || row >= ORION_MAX_MASK_COMPONENTS) return 0;
    const Stroke& st = s->held[row];
    for (int i = 0; i < std::min(st.dabs, capacity); ++i) {
        if (xy) { xy[2 * i] = (*st.xy)[2 * i]; xy[2 * i + 1] = (*st.xy)[2 * i + 1]; }
        if (erase) erase[i] = (*st.erase)[i];
    }
    return st.dabs;
}

// Paint the coverage of mask `layer` over the picture, or stop. Takes effect
// at the next render, and only for it: see shim_render.
void shim_mask_overlay(Shim* s, int on, int layer) {
    s->overlay = on != 0;
    s->overlay_layer = layer;
}


// Rows [0, count) are live; the rest are zeroed, as Engine.swift sends them.
int shim_mask_commit(Shim* s, int count) {
    if (count < 0 || count > ORION_MAX_MASK_COMPONENTS) return 0;
    for (int i = count; i < ORION_MAX_MASK_COMPONENTS; ++i) s->adj.mask_components[i] = OrionMaskComponent{};
    for (int i = 0; i < ORION_MAX_MASK_COMPONENTS; ++i) s->adj.mask_components[i].brush_revision = s->revisions[i];
    s->adj.mask_count = count;
    return orion_engine_set_adjustments(s->engine, &s->adj) == ORION_OK;
}

// preview != 0 renders the quarter-resolution graph a slider drag uses.
// Falls back to the full graph when there is no preview graph.
//
// The mask overlay is switched on for this render alone and off again after
// it, so the adjustment block at rest never carries it: an export, a history
// snapshot or a second reader of this engine cannot bake a red tint into a
// photograph (#57, which Swift guards around export by hand).
// ponytail: costs a develop_linear pass per render while the overlay is up,
// since the toggle back dirties it; a display-only flag in the engine removes that.
int shim_render(Shim* s, int preview, double* gpu_ms) {
    settle(s);
    const bool overlay = s->overlay && s->adj.mask_count > 0;
    if (overlay) {
        s->adj.mask_overlay = 1;
        s->adj.mask_overlay_layer = s->overlay_layer;
        orion_engine_set_adjustments(s->engine, &s->adj);
    }
    int graph = 0;
    if (preview && orion_engine_render_preview(s->engine, gpu_ms) == ORION_OK) graph = 1;
    else graph = orion_engine_render(s->engine, gpu_ms) == ORION_OK ? 2 : 0;
    if (overlay) {
        s->adj.mask_overlay = s->adj.mask_overlay_layer = 0;
        orion_engine_set_adjustments(s->engine, &s->adj);
    }
    return graph;
}

// The last render of `which` (what shim_render returned) and the region of it
// that holds the picture. The texture is square so a straightened frame fits;
// only the top-left region holds the picture (Engine+Render.swift:286):
// image_size for the full graph, preview_size for the preview, each its own
// graph's answer. Null when there is nothing to read. present.mm reads it too.
void* shim_picture(Shim* s, int which, int* w, int* h) {
    void* raw = which == 1 ? orion_engine_preview_texture(s->engine)
                           : orion_engine_output_texture(s->engine);
    if (!raw) return nullptr;
    id<MTLTexture> t = (__bridge id<MTLTexture>)raw;
    unsigned iw = 0, ih = 0;
    if (which == 1) orion_engine_preview_size(s->engine, &iw, &ih);
    else orion_engine_image_size(s->engine, &iw, &ih);
    *w = int(std::min<NSUInteger>(t.width, iw));
    *h = int(std::min<NSUInteger>(t.height, ih));
    return *w > 0 && *h > 0 ? raw : nullptr;
}

// present.mm hands over the command buffer that reads this engine's picture.
void shim_hold(Shim* s, void* commandBuffer) { s->reader = (__bridge id<MTLCommandBuffer>)commandBuffer; }

// Copies the last render into `dst` as RGBA8. Call with dst == NULL to get the
// size. `which` is what shim_render returned.
int shim_read(Shim* s, int which, uint8_t* dst, int capacity, int* w, int* h) {
    void* raw = shim_picture(s, which, w, h);
    if (!raw) return 0;
    if (!dst) return 1;
    if (capacity < *w * *h * 4) return 0;
    id<MTLTexture> t = (__bridge id<MTLTexture>)raw;
    const NSUInteger rw = NSUInteger(*w), rh = NSUInteger(*h);
    const MTLRegion all = MTLRegionMake2D(0, 0, rw, rh);
    switch (t.pixelFormat) {
        case MTLPixelFormatRGBA8Unorm:
            [t getBytes:dst bytesPerRow:rw * 4 fromRegion:all mipmapLevel:0];
            return 1;
        case MTLPixelFormatRGBA16Float: {  // the wide path; not the screen default
            std::vector<__fp16> wide(rw * rh * 4);
            [t getBytes:wide.data() bytesPerRow:rw * 8 fromRegion:all mipmapLevel:0];
            for (size_t i = 0; i < wide.size(); ++i) {
                float v = float(wide[i]);
                dst[i] = uint8_t(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f);
            }
            return 1;
        }
        default:
            std::fprintf(stderr, "shim_read: pixel format %lu\n", (unsigned long)t.pixelFormat);
            return 0;
    }
}

// The camera's embedded JPEG, for the filmstrip.
int shim_thumbnail(const char* path, uint8_t* dst, int capacity, int* turns) {
    uint32_t size = 0;
    if (orion_read_thumbnail(path, dst, uint32_t(capacity), &size, turns) != ORION_OK) return 0;
    return int(size);
}

// An SVG watermark's shape, through NSImage, as WatermarkRaster.swift's Art
// draws it (SVG is native since macOS 10.15). The only SVG rasterizer .NET has
// under a licence Orion may ship is none: Svg.Skia rests on SVG.NET, MS-PL.
// ponytail: macOS only; a Windows or Linux port needs its own rasterizer here.
static NSImage* svg_image(const uint8_t* bytes, int length) {
    if (!bytes || length <= 0) return nil;
    NSImage* image = [[NSImage alloc] initWithData:[NSData dataWithBytes:bytes length:NSUInteger(length)]];
    return image && image.size.width > 0 && image.size.height > 0 ? image : nil;
}

// Its natural size. 0 when it is not an image NSImage can read.
int shim_svg_size(const uint8_t* bytes, int length, double* w, double* h) {
    @autoreleasepool {
        NSImage* image = svg_image(bytes, length);
        if (!image) return 0;
        *w = image.size.width;
        *h = image.size.height;
        return 1;
    }
}

// Drawn `w`×`h`, centred at (cx, cy) of a bw×bh tile (y down), turned `angle`
// radians counter-clockwise as the viewer sees it; its alpha into `alpha`,
// one byte a pixel, rows top first. RGBA, because NSImage will not draw into
// an alpha-only context.
int shim_svg_coverage(const uint8_t* bytes, int length, uint8_t* alpha, int bw, int bh,
                      double cx, double cy, double w, double h, double angle) {
    if (!alpha || bw <= 0 || bh <= 0) return 0;
    @autoreleasepool {
        NSImage* image = svg_image(bytes, length);
        if (!image) return 0;
        std::vector<uint8_t> tile(size_t(bw) * size_t(bh) * 4);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(tile.data(), size_t(bw), size_t(bh), 8, size_t(bw) * 4, space,
                                                 kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(space);
        if (!ctx) return 0;
        // CoreGraphics is y-up and the tile is y-down: flip the centre, then turn.
        CGContextTranslateCTM(ctx, cx, bh - cy);
        CGContextRotateCTM(ctx, angle);
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
        [image drawInRect:NSMakeRect(-w / 2, -h / 2, w, h)];
        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(ctx);
        for (size_t i = 0; i < size_t(bw) * size_t(bh); ++i) alpha[i] = tile[i * 4 + 3];
        return 1;
    }
}

}  // extern "C"

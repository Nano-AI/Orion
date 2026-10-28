// Spike glue between Orion's C facade and C#. Not product code.
//
// Two things the facade does not offer yet, and a second UI needs:
//  - the default look. contrast 1.45 and friends live in Engine.swift, so this
//    file has to repeat them. That duplication is the case for moving shared
//    state below the facade before a real port.
//  - CPU pixels. The facade hands out an id<MTLTexture> for zero-copy display;
//    C# can't draw that on macOS, so we copy it out. On Windows the real path
//    would be a shared D3D texture, not this copy.
#import <Metal/Metal.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>
#include "orion/orion.h"

struct Shim {
    OrionEngine* engine = nullptr;
    OrionAdjustments adj{};
    // What each mask row's brush holds in the engine, and its revision: bumped
    // only when the stroke changes, never reset (Engine+Brush.swift's
    // brushRevisions), so a re-added row cannot repeat a revision the engine saw.
    std::vector<float> strokes[ORION_MAX_MASK_COMPONENTS];
    unsigned revisions[ORION_MAX_MASK_COMPONENTS]{};
};

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

// Every scalar and float[N] member of OrionAdjustments, by its C name.
// fields.inc is generated from orion.h on each build (gen_fields.py).
struct Field { const char* name; int length; bool isInt; size_t offset; };
constexpr bool float_kind = false, int_kind = true;
#define F(n, k) {#n, 1, k##_kind, offsetof(OrionAdjustments, n)},
#define A(n, len) {#n, len, false, offsetof(OrionAdjustments, n)},
static const Field kFields[] = {
#include "fields.inc"
};
#undef F
#undef A
// One row of the mask group (OrionMaskComponent), the same way.
#define F(n, k) {#n, 1, k##_kind, offsetof(OrionMaskComponent, n)},
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
    return reinterpret_cast<char*>(&a) + f->offset + size_t(index) * 4;
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

// Engine.swift's autoEnhance: the engine measures and writes exposure, whites,
// blacks, fusion (lift) and clarity into the block; the rest stays as sent.
int shim_auto_enhance(Shim* s) {
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

// `dabs` interleaved x, y pairs. Sent to the engine, and the row's revision
// bumped, only when they differ from what the engine already holds: a slider
// tick on a brush row must not re-lay its whole stroke. Every dab paints
// (null erase), as a sidecar without brushErase means.
int shim_mask_stroke(Shim* s, int row, const float* xy, int dabs) {
    if (row < 0 || row >= ORION_MAX_MASK_COMPONENTS || dabs < 0 || (dabs > 0 && !xy)) return 0;
    auto& held = s->strokes[row];
    if (held.size() == size_t(dabs) * 2 && std::equal(held.begin(), held.end(), xy)) return 1;
    if (orion_engine_set_brush_stroke(s->engine, row, dabs ? xy : nullptr, nullptr, dabs) != ORION_OK) return 0;
    held.assign(xy, xy + size_t(dabs) * 2);
    ++s->revisions[row];
    return 1;
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
int shim_render(Shim* s, int preview, double* gpu_ms) {
    if (preview && orion_engine_render_preview(s->engine, gpu_ms) == ORION_OK) return 1;
    return orion_engine_render(s->engine, gpu_ms) == ORION_OK ? 2 : 0;
}

// Copies the last render into `dst` as RGBA8. Call with dst == NULL to get the
// size. `which` is what shim_render returned.
int shim_read(Shim* s, int which, uint8_t* dst, int capacity, int* w, int* h) {
    void* raw = which == 1 ? orion_engine_preview_texture(s->engine)
                           : orion_engine_output_texture(s->engine);
    if (!raw) return 0;
    id<MTLTexture> t = (__bridge id<MTLTexture>)raw;
    // The texture is square so a straightened frame fits; only the top-left
    // region holds the picture (Engine+Render.swift:286): image_size for the
    // full graph, preview_size for the preview, each its own graph's answer.
    unsigned iw = 0, ih = 0;
    if (which == 1) orion_engine_preview_size(s->engine, &iw, &ih);
    else orion_engine_image_size(s->engine, &iw, &ih);
    const NSUInteger rw = std::min<NSUInteger>(t.width, iw);
    const NSUInteger rh = std::min<NSUInteger>(t.height, ih);
    if (rw == 0 || rh == 0) return 0;
    *w = int(rw); *h = int(rh);
    if (!dst) return 1;
    if (capacity < *w * *h * 4) return 0;
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

}  // extern "C"

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

static const Field* find(const char* name) {
    for (auto& f : kFields) if (std::strcmp(f.name, name) == 0) return &f;
    return nullptr;
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
    if (f->isInt) *static_cast<int32_t*>(p) = int32_t(std::lround(value));
    else *static_cast<float*>(p) = value;
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
    // image_size region holds the picture (Engine+Render.swift:286). The
    // preview graph holds the same region at its own scale.
    uint32_t iw = 0, ih = 0;
    orion_engine_image_size(s->engine, &iw, &ih);
    const double scale = which == 1
        ? double(t.width) / double(((__bridge id<MTLTexture>)orion_engine_output_texture(s->engine)).width)
        : 1.0;
    const NSUInteger rw = std::min<NSUInteger>(t.width, NSUInteger(iw * scale + 0.5));
    const NSUInteger rh = std::min<NSUInteger>(t.height, NSUInteger(ih * scale + 0.5));
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

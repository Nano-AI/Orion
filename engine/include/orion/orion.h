/*  Orion engine — public C API.
 *
 *  This is the ONLY surface the UI shell sees. Deliberately narrow and
 *  plain-old-data: no templates, no C++ types, no ownership subtleties, and
 *  crucially no exceptions — C# (P/Invoke) cannot catch a C++ exception, and
 *  one that escapes terminates the process. Every entry point below is noexcept in
 *  practice; failures come back as an OrionStatus.
 *
 *  Keeping this boundary stable is what makes the UI replaceable and an
 *  eventual Windows port tractable. Resist widening it.
 */

#ifndef ORION_H
#define ORION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque engine handle. */
typedef struct OrionEngine OrionEngine;

typedef enum OrionStatus {
    ORION_OK            = 0,
    ORION_ERR_NO_GPU    = 1,  /* reserved: never returned. No usable GPU
                                 is INTERNAL, with orion_last_error()   */
    ORION_ERR_BAD_ARG   = 2,  /* null or malformed argument           */
    ORION_ERR_INTERNAL  = 3,  /* see orion_last_error() for detail     */
    ORION_ERR_MEMORY    = 4   /* orion_engine_stack only: needs more memory
                                 than is available; force runs it anyway */
} OrionStatus;

/* Snapshot of the GPU the engine bound to. Fixed-size so it crosses the
 * language boundary without any allocation or lifetime question. */
typedef struct OrionDeviceInfo {
    char     name[128];
    uint64_t recommended_working_set;  /* bytes the GPU wants us to stay under */
    uint64_t max_buffer_length;        /* bytes, largest single allocation      */
    int32_t  has_unified_memory;       /* 1 on Apple silicon                    */
    int32_t  supports_apple7;          /* GPU family floor for our compute path */
} OrionDeviceInfo;

/* Lifecycle. On success *out receives an engine that must be freed with
 * orion_engine_destroy. On failure *out is set to NULL. */
OrionStatus orion_engine_create(OrionEngine** out);
void        orion_engine_destroy(OrionEngine* engine);

/* Adjustments. Plain floats so the block is trivially bridgeable. */
/* One channel of the tone curve.
 *
 * A fixed array rather than a pointer, so the whole adjustment block stays one
 * POD value that can be memcpy'd across the facade. Sixteen points is more than
 * any curve a person draws by hand; the engine interpolates between them with a
 * monotone cubic Hermite, which passes through every point and cannot overshoot.
 */
#define ORION_CURVE_MAX_POINTS 16

typedef struct OrionCurveChannel {
    int32_t count;                       /* 0 or 1 means identity */
    float   x[ORION_CURVE_MAX_POINTS];   /* input,  0..1, ascending */
    float   y[ORION_CURVE_MAX_POINTS];   /* output, 0..1 */
} OrionCurveChannel;

/* How many components one mask group holds. Each live component is one GPU
 * pass and one full-resolution alpha — a memory number, not a concept. Must
 * match kMaxMaskComponents in the engine. Raised 4 -> 8 for the masking UX
 * revamp (decision #209): the components are also the masks, so four was the
 * whole stack, not one mask's shapes. */
#define ORION_MAX_MASK_COMPONENTS 8

/* One component of the mask group: a primitive plus how it folds into the
 * components listed before it. research/masking.md §6.
 *
 * Geometry is in normalized coordinates of the *displayed* picture — the crop
 * and rotation the photographer is looking at. The engine moves it to the
 * frame the shader sees; the caller never does that transform. */
/* Maximum spots one photo can carry. research/spot-removal.md. */
#define ORION_MAX_SPOTS 64

/* One spot: a disc taken from elsewhere in the frame.
 *
 * Both centers are normalized against the frame masks live in, so the same
 * transform that carries a mask's center from the displayed picture carries a
 * spot. `radius` is in normalized x and is converted against the frame's width,
 * so a spot is a disc rather than an ellipse on a non-square frame. */
typedef struct OrionSpot {
    float dest_x, dest_y;
    float src_x, src_y;
    float radius;
    float feather;
    int   heal;             /* 0 clones, nonzero takes the destination's tone */
} OrionSpot;

typedef struct OrionMaskComponent {
    int   kind;             /* 0 off, 1 linear, 2 radial, 3 brush,
                             * 4 matte, 5 luminance range */
    int   compose;          /* 0 add, 1 subtract, 2 intersect — the fold starts
                             * from zero, so the first component should be 0 */
    int   invert;           /* inverts this component, before the fold */
    int   hidden;           /* the eye button: keeps its settings, contributes nothing */
    int   starts_layer;     /* begins a new layer — the fold restarts here */

    float center_x, center_y;
    float angle;            /* radians */
    float length;           /* linear: zero-to-full distance */
    float radius_x, radius_y; /* radial semi-axes */
    float feather;          /* radial, 0..1 */
    float roundness;        /* 2 is an ellipse */

    /* Luminance range, when kind is 5. Stops of log2 Rec.2020 luminance on the
     * reference image — measured before any user adjustment, so editing
     * through the band cannot change what the band selects.
     * research/masking.md §4b. */
    float range_lo, range_hi;
    float range_soft;       /* stops each edge takes to ramp */

    /* Color range, when kind is 6. The picked shade as scene-linear Rec.2020
     * RGB, plus a Euclidean tolerance in Oklab chromaticity (a/L, b/L) and how
     * far its edge ramps. The kernel does the conversion, so the target and the
     * pixel cannot disagree about what Oklab is. research/masking.md §4c. */
    float color_r, color_g, color_b;
    float color_tol;
    float color_soft;

    /* The brush, when kind is 3. One radius for the whole stroke. The dab
     * centers are not here — they are variable-length, and this struct is
     * compared field by field on every slider tick. Set them with
     * orion_engine_set_brush_stroke for this component's index and bump
     * brush_revision, which is what tells the engine the stroke is stale.
     * Change the points without changing the revision and the picture will
     * not follow the hand. */
    float brush_radius;     /* normalized */
    float brush_flow;       /* 0..1 per dab */
    float brush_hardness;   /* 0 soft, 1 hard-edged */
    unsigned brush_revision;
} OrionMaskComponent;

typedef struct OrionAdjustments {
    float temperature_k;  /* white balance, Kelvin        */
    float tint;           /* -1..1, green to magenta      */
    float exposure_ev;
    float highlights;     /* -1..1, negative recovers     */
    float shadows;
    float whites;
    float blacks;
    float vibrance;
    float saturation;     /* -1..1, 0 is untouched        */
    float contrast;       /* display transform slope      */

    int32_t rotate_quarters; /* extra quarter turns clockwise    */
    float   straighten_deg;  /* fine rotation after the turns    */

    /* Perspective correction, each -1..1. research/perspective.md.
     *
     * A homography folded into the geometry node's existing sampling pass, so
     * the picture is still resampled exactly once however many of these are
     * up. Vertical pulls converging verticals apart — the building shot looking
     * up; horizontal does the same for a wall shot from one side; aspect is the
     * squeeze a strong correction leaves behind.
     *
     * The frame is zoomed automatically to stay full, exactly as the lens
     * corrections are. Zero on all three is not a small correction: it is
     * bit-identical to a build with no perspective in it. */
    float   perspective_vertical;
    float   perspective_horizontal;
    float   perspective_aspect;

    float   crop_x, crop_y;  /* normalized, post-rotation        */
    float   crop_w, crop_h;
    int32_t crop_preview;    /* show the whole frame while cropping */

    /* The crop tool's preview canvas, in the same normalized post-rotation
     * coordinates as crop_x/crop_y. It has to cover the frame's rotated
     * bounding box, which depends on the angle and the frame's aspect.
     *
     * The UI computes it because the overlay has to land on the same
     * rectangle; a second derivation in the engine is how the handles and the
     * pixels drifted apart before. Ignored unless crop_preview is set.
     */
    float   preview_x, preview_y;  /* canvas origin  */
    float   preview_size;          /* canvas extent, both axes, >= 1 */

    /* Lens corrections, each -1..1. */
    float lens_distortion;
    float lens_vignette;
    float lens_ca_red;
    float lens_ca_blue;

    /* Nonzero applies the measured profile for this photo's lens in place of
     * lens_distortion and lens_vignette, which the interface then disables.
     * Has no effect when orion_engine_lens_profile reports nothing found. */
    int32_t lens_profile;

    /* Highlight reconstruction, 0..1. Zero by default: the reconstruction is a
     * linear extrapolation, and one asked to reach past its data invents. */
    float highlight_recovery;

    /* Three-way color grading, ASC CDL per tonal zone. Each triple is a
     * wheel's puck position (x, y) in the unit disc, then that zone's
     * luminance slope. research/color-grading.md. */
    float grade_shadow[3];
    float grade_midtone[3];
    float grade_highlight[3];

    /* Balance, -1..+1 -- decision #101. The three zones above are Gaussians
     * fixed at -2.5 / 0 / +2.5 EV; this slides all three centres together, so
     * the photographer decides how much of the picture counts as shadow.
     * Positive is toward the highlights, the same direction as Adobe's slider.
     *
     * Zero is bit-identical to no Balance at all, and with the three wheels
     * centred it does nothing whatever value it holds -- it changes weights
     * that multiply zero, and it never switches the grading node on. */
    float grade_balance;

    /* The CREATIVE vignette: research/vignette.md, decision #103.
     * Not lens_vignette above. That one divides out a falloff a lens measured;
     * this one puts one in on purpose, after the grade, centred on the CROP
     * rather than on the frame. A photograph can carry both and they never read
     * each other.
     * vignette_amount is the exposure change at the corner of the composition
     * in STOPS -- negative darkens, which is the usual direction. Zero is not
     * merely a silent vignette: it skips the falloff and, with the grading
     * wheels centred, disables the node.
     * vignette_field_angle is the half-diagonal field angle in DEGREES of the
     * lens whose natural cos^4 falloff is being imitated -- wide reaches well
     * into the frame, narrow stays in the extreme corners. */
    float vignette_amount;       /* -3..3, stops at the corner */
    float vignette_field_angle;  /* 10..70 degrees             */

    /* Profiled wavelet denoise. Strengths are multiples of the measured noise
     * level, not arbitrary amounts, so one setting behaves the same way on a
     * clean frame and a very noisy one. Zero switches the chain off entirely. */
    float denoise_luma;     /* 0..4                       */
    float denoise_color;   /* 0..4                       */
    /* How much the threshold backs off where the picture has more texture
     * than noise: 0 is the fixed threshold every photograph before it was
     * rendered with, 1 is BayesShrink's shape under the same strength.
     * research/denoise-masked.md section 1. */
    float denoise_detail;  /* 0..1, 0 is bit-identical to before */
    /* Non-local means on luminance before the wavelet chain: k in h = k*sigma,
     * 0.4 being the published value. 0 is off and byte-identical.
     * research/high-iso.md section 4. */
    float denoise_nlm;     /* 0..1 */
    /* Fine-scale texture gain inside the denoise chain, gated by the measured
     * noise so grain under three deviations is never boosted; negative
     * smooths weak texture. Runs the chain even with both strengths at zero.
     * research/high-iso.md section 2. */
    float texture;         /* -1..1, 0 is bit-identical to before */
    /* Chroma denoise at the coarse scales where colour blotches live, 16 and
     * 32 px, at quarter resolution; its strength is denoise_color times this,
     * so it does nothing at Color 0. research/high-iso.md section 1. */
    float denoise_smoothness;  /* 0..1, 0 is bit-identical to before */

    /* How much of the loaded creative LUT to apply, 0..1. The LUT itself is
     * not an adjustment — see orion_engine_load_lut. Zero is byte-identical to
     * having none loaded. */
    float lut_strength;

    /* Film grain, applied after the display transform. research/film-grain.md,
     * decisions #81 and #82.
     *
     * grain_amount is the PEAK standard deviation in display units -- peak
     * because the Boolean model's variance law puts the noise in the midtones
     * and none of it at either end. Zero is not merely silent: it disables the
     * node, so the graph costs exactly what it did before grain existed.
     *
     * grain_size is the grain radius in FRAME pixels, so it is keyed to the
     * negative. A crop enlarges the grain the way enlarging more of a negative
     * does, rather than resampling it with the output. */
    float grain_amount;    /* 0..0.06                    */
    float grain_size;      /* 1.2..8, in frame pixels    */

    /* The mask group: components folded left in listed order into one coverage,
     * and the local adjustment applied once through it — never twice because two
     * components overlap. research/masking.md §6.
     *
     * mask_count says how many entries of mask_components are live; the rest
     * are ignored. Zero means no mask. The alpha scales the *parameter*, so
     * coverage 0.5 with a one-stop local exposure is 2^0.5 — not a blend of two
     * rendered frames. */
    OrionMaskComponent mask_components[ORION_MAX_MASK_COMPONENTS];
    int   mask_count;
    /* One layer's local adjustments, one entry per layer. A layer is a run of
     * mask components with its own coverage, so the subject can be graded one
     * way and the sky another. All pointwise: warmth and tint are a color
     * cast, NOT a white balance — temperature is applied before the demosaic
     * and cannot be local. research/masking.md §2b. */
    float local_exposure_ev[ORION_MAX_MASK_COMPONENTS];
    float local_contrast[ORION_MAX_MASK_COMPONENTS];
    float local_saturation[ORION_MAX_MASK_COMPONENTS];
    float local_warmth[ORION_MAX_MASK_COMPONENTS];
    float local_tint[ORION_MAX_MASK_COMPONENTS];
    /* The four tone bands, per layer, in the globals' units. */
    float local_highlights[ORION_MAX_MASK_COMPONENTS];
    float local_shadows[ORION_MAX_MASK_COMPONENTS];
    float local_whites[ORION_MAX_MASK_COMPONENTS];
    float local_blacks[ORION_MAX_MASK_COMPONENTS];
    /* Denoise through the layer's coverage, in the global sliders' units and
     * on top of the global chain: a second, short wavelet chain in working
     * space whose threshold is scaled per pixel by the coverage.
     * research/denoise-masked.md section 2. Zero everywhere costs nothing. */
    float local_denoise_luma[ORION_MAX_MASK_COMPONENTS];   /* 0..4 */
    float local_denoise_color[ORION_MAX_MASK_COMPONENTS];  /* 0..4 */

    /* Dust and blemishes. research/spot-removal.md. Applied between the lens
     * correction and sharpening, in scene-linear light. */
    OrionSpot spots[ORION_MAX_SPOTS];
    int   spot_count;

    /* Guided feathering of the folded group, 0..1 — research/masking.md §4.
     * Pulls the coverage boundary onto whatever edge in the photograph lies
     * near it, and leaves it alone where there is none. Zero is the identity
     * and disables the whole chain. A property of the group, not of a
     * component: what a photographer wants snapped is the boundary they see. */
    float mask_refine;

    /* Paint the mask's coverage over the picture so it can be placed by eye.
     * A viewing aid only — an export must never set it. */
    int   mask_overlay;

    /* Which layer the overlay paints — the one being edited. Ignored when
     * mask_overlay is 0. */
    int   mask_overlay_layer;

    /* Single-image exposure fusion, 0..1 — shadow lift that keeps local
     * contrast. The value is a power applied to the emitted gain, so zero is
     * bit-exactly the identity. research/exposure-fusion.md. */
    float fusion;

    /* Dehaze, 0..1 — He, Sun & Tang's dark channel prior. The value is the
     * paper's own omega, so zero is exactly the identity and one is the 0.95
     * they fixed for every result they published. research/dehaze.md. */
    float dehaze;

    /* Local Laplacian clarity, -1..1. Negative smooths detail, positive
     * increases its contrast. Zero switches thirty-two nodes off entirely, so
     * it costs nothing when unused. research/local-laplacian.md. */
    float clarity;

    float sharpen_amount;   /* 0..2                       */
    float sharpen_radius;   /* pixels                     */
    float sharpen_masking;  /* 0..1, protects flat areas  */

    /* Color mixer: red, orange, yellow, green, aqua, blue, purple, magenta. */
    float hue_shift[8];
    float sat_shift[8];
    float lum_shift[8];

    /* Black and white: nonzero renders gray, the luminance times a gain each
     * hue band's bw_mix (-1..1, the mixer's bands) sets. research/bw-mix.md. */
    int   black_white;
    float bw_mix[8];

    /* Tone curve, after the display transform. Master applies to all three
     * channels, then each channel's own curve applies on top. */
    OrionCurveChannel curve_master;
    OrionCurveChannel curve_red;
    OrionCurveChannel curve_green;
    OrionCurveChannel curve_blue;

    /* Which generation of tone bands renders highlights, shadows, whites and
     * blacks, global and local. 1 is the four bands as shipped; 2 adds an
     * identity band at middle grey so a midtone face is no longer on the
     * Highlights flank. The photograph's own, from its sidecar: a state
     * finished under one generation keeps opening under it. 0 reads as 1, so
     * a zeroed struct renders what it always rendered. */
    int   process;
} OrionAdjustments;

/* Opens a raw file and builds the develop pipeline for it. */
OrionStatus orion_engine_open_raw(OrionEngine* engine, const char* path);

#include "orion_mask.h"

/* The camera's own white balance, so the UI can open on "as shot". Only the
 * temperature and tint fields are filled; the rest are zeroed. */
OrionStatus orion_engine_as_shot(const OrionEngine* engine, OrionAdjustments* out);

/* Pushes adjustments; cheap enough to call on every slider tick. */
OrionStatus orion_engine_set_adjustments(OrionEngine* engine, const OrionAdjustments* adj);

/* Renders the *preview* graph — a quarter-linear copy of the same pipeline,
 * for showing while a slider is moving. ROADMAP M1, Interaction.
 *
 * ⚠ Its output is for the canvas and nothing else. Export, the histogram and
 * the eyedropper all read the full graph, and must: a preview-resolution export
 * is a mistake only the person receiving the file would find.
 *
 * Returns ORION_ERR_BAD_ARG when there is no preview graph — a machine that
 * could not find room for one still edits, just without the fast path. Not a
 * distinct status code: there are three, and "you asked for something this
 * engine does not have" is what BAD_ARG already means. */
OrionStatus orion_engine_render_preview(OrionEngine* engine, double* out_ms);

/* The preview graph's output dimensions, which are not the full graph's. */
OrionStatus orion_engine_preview_size(const OrionEngine* engine,
                                      unsigned* out_w, unsigned* out_h);

/* Renders only the part of the picture the canvas shows, at the screen's own
 * resolution (issue #22, decision #371): x, y, w, h are normalized in the
 * displayed picture (cropped and turned, 0..1), out_w x out_h the screen
 * pixels that part covers. Cost follows the screen, not the sensor, and the
 * result is never softer than the screen. Canvas only, like the preview.
 *
 * ORION_ERR_BAD_ARG is this call's "render the whole graph instead", as it is
 * the preview's "no preview graph": this edit uses something the region graph
 * cannot render exactly (masks, spots, grain, the creative vignette, dehaze,
 * clarity, fusion, highlight recovery, highlights/shadows, the crop tool's
 * canvas) or the photo is not a mosaic. ORION_REGION_INTERACTING in `flags` (a
 * drag) also refuses a denoised edit zoomed out past 100%, which renders at
 * step 1 and would cost more than the preview: render the preview for the
 * drag, and the region again when it ends. INTERNAL (out of memory, a lost
 * device) is a failure: the whole graph needs more memory than the region, so
 * do not answer it with that. ORION_REGION_NO_WAIT submits the render and
 * returns with *out_ms 0: a read or a display copy of it after this queues
 * behind it, and orion_engine_region_busy says when the GPU is done. */
#define ORION_REGION_INTERACTING 1
#define ORION_REGION_NO_WAIT     2
OrionStatus orion_engine_render_region(OrionEngine* engine, float x, float y,
                                       float w, float h, unsigned out_w,
                                       unsigned out_h, int flags,
                                       double* out_ms);

/* *out_busy 1 while a NO_WAIT region is still on the GPU, 0 once it is done
 * or there is none; INTERNAL when it failed there. Never waits. */
OrionStatus orion_engine_region_busy(OrionEngine* engine, int* out_busy);

/* The region graph's output texture (id<MTLTexture>) and the part of it that
 * holds the last region: top-left out_w x out_h, as asked, capped at the
 * texture. NULL / ORION_ERR_BAD_ARG before the first region render. */
void* orion_engine_region_texture(const OrionEngine* engine);
OrionStatus orion_engine_region_size(const OrionEngine* engine,
                                     unsigned* out_w, unsigned* out_h);

/* Renders dirty nodes. *out_ms receives GPU-side milliseconds (may be NULL). */
OrionStatus orion_engine_render(OrionEngine* engine, double* out_ms);

/* Gives back every graph's device memory, sources too, and keeps the decoded photo,
 * its adjustments, strokes, mattes and LUT on the CPU: a photo a browser keeps but
 * is not showing (#318). Textures are NULL until the next render rebuilds them. */
OrionStatus orion_engine_release_render_memory(OrionEngine* engine);

/* An idle canvas's spare back: pooled textures, the full graph's cache when memory
 * is short (never the picture), and with `full` nonzero all its outputs, as past fit. */
OrionStatus orion_engine_trim(OrionEngine* engine, int full);

/* Bytes of GPU memory the engine's device holds from the driver, every engine
 * in the process together (they share one device). What a process footprint
 * misses on a discrete GPU. 0 where the backend cannot say: Metal, whose
 * textures the footprint already counts. */
OrionStatus orion_engine_gpu_memory(OrionEngine* engine, uint64_t* out_bytes);

/* Dimensions of the open image, as rendered — so the crop and the crop tool's
 * preview canvas both change this. */
OrionStatus orion_engine_image_size(const OrionEngine* engine,
                                    uint32_t* out_width, uint32_t* out_height);

/* Dimensions of the whole frame after rotation, before any crop. This is what
 * the crop rectangle is normalized against, and what the UI needs to work out
 * how far a straightened frame reaches. */
/// Sixteen bits out of the tail of the graph instead of eight.
///
/// The screen path is eight bits because the drawable is `bgra8Unorm` — wider
/// is bytes moved for precision nothing can show, and it costs about 3.5 ms of
/// a 16 ms budget. Export widens the tail on its own. This entry point exists
/// for the measurement harness, which reads the output texture directly and
/// needs the precision to see a change worth four decimal places.
///
/// Reallocates two full-resolution textures. Not for a slider.
/* Loads a creative LUT from a .cube file.
 *
 * Returns ORION_ERR_BAD_ARG when the file cannot be read or is not
 * a .cube Orion understands; the reason is available from
 * orion_last_error. A failed load leaves any previously loaded LUT in
 * place, so a mistyped path does not silently drop the look. */
/* Measures the picture and writes back the sliders auto-enhance may move —
 * exposure, blacks, whites, shadow lift and clarity. Everything else in the
 * block is left as the caller sent it.
 *
 * Renders several times: this is a one-click action, not a slider. */
OrionStatus orion_engine_auto_enhance(OrionEngine* engine, OrionAdjustments* adj);

/* White balance from the picture: writes temperature_k and tint into `adj`
 * so the patch around (u, v) on the displayed picture renders neutral, the
 * eyedropper; `radius` is the patch's half-width as a fraction of the frame's
 * width. A negative radius balances the whole frame to grey on average, Auto
 * (grey world). Renders the preview several times; the rest of `adj` stays as
 * sent and nothing is pushed. ORION_ERR_BAD_ARG, `adj` untouched, when the
 * patch is clipped or black. research/white-balance.md. */
OrionStatus orion_engine_white_balance_at(OrionEngine* engine, float u, float v, float radius,
                                          OrionAdjustments* adj);

/* Lightroom's fixed white balance presets, in its order: Daylight, Cloudy,
 * Shade, Tungsten, Fluorescent, Flash. `name` takes up to `capacity` bytes,
 * NUL-terminated. ORION_ERR_BAD_ARG past the last. */
OrionStatus orion_white_balance_preset(int index, char* name, int capacity,
                                       float* out_temperature_k, float* out_tint);

OrionStatus orion_engine_load_lut(OrionEngine* engine, const char* path);

/* Unloads it. Safe when none is loaded. */
OrionStatus orion_engine_clear_lut(OrionEngine* engine);

/* The loaded LUT's TITLE, or an empty string. */
OrionStatus orion_engine_lut_title(const OrionEngine* engine, char* out, int capacity);

OrionStatus orion_engine_set_wide_output(OrionEngine* engine, int wide);

/* Nonzero for a process that develops and exits - `Orion --agent`, the
 * `--batch-export` driver - rather than the app's canvas. Builds no preview
 * graph and returns every intermediate to the pool after each render, so a
 * one-shot render peaks at one render's worth of textures rather than
 * keeping what each later render rebuilds (decision #312). Call before
 * orion_engine_open; never on the interactive app's engine, whose slider
 * latency depends on both things this turns off. */
OrionStatus orion_engine_set_headless(OrionEngine* engine, int headless);

OrionStatus orion_engine_frame_size(const OrionEngine* engine,
                                    uint32_t* out_width, uint32_t* out_height);

/* The pipeline's output as an id<MTLTexture>, for zero-copy display.
 * Non-owning; valid until the next open_raw. NULL when no image is open.
 * Under ORION_GPU=dawn (decision #375) only an RGBA8 output has one, an
 * IOSurface Dawn renders into; the wide RGBA16F output answers NULL, and a
 * shell reads it back instead. The other texture getters behave the same.
 * Windows and Linux answer NULL always: no zero-copy hand-off yet (#376). */
void* orion_engine_output_texture(const OrionEngine* engine);

/* The preview graph's output texture, or NULL when there is no preview graph.
 * See orion_engine_render_preview — the canvas only. */
void* orion_engine_preview_texture(const OrionEngine* engine);

/* The last render as RGBA8 rows of width * 4 bytes, the picture region only:
 * graph 0 the full graph (orion_engine_image_size), 1 the preview, 2 the
 * region. dst NULL asks only the size; capacity is in bytes. The CPU copy a
 * shell makes where it has no texture it can draw: headless, a renderer with
 * no interop, and Windows and Linux, which have no zero-copy hand-off yet
 * (decision #376). The wide RGBA16F output is converted, clamped to 0..1, on
 * the GPU (#386). Waits for the copy: a canvas takes orion_engine_display_*. */
OrionStatus orion_engine_read_pixels(const OrionEngine* engine, int graph,
                                     uint8_t* dst, uint32_t capacity,
                                     uint32_t* out_width, uint32_t* out_height);

/* The display copy (decision #386): the canvas's CPU copy of a render, at the
 * size it shows, without waiting for the GPU. Begin queues one behind the
 * last render of `graph` (as orion_engine_read_pixels) and returns at once
 * with its size and ticket (1, 2, ... per engine, still counting after
 * release_render_memory, which drops any copy in flight). On the GPU it is `level` 2x2 halvings of the picture,
 * the size present.mm's chain gives: level 0 the picture as it is, level k
 * one halving rounding up then k - 1 rounding down, clamped where a side
 * would reach 1, as RGBA8. Three may be in flight; a fourth waits for the
 * oldest. BAD_ARG: a NULL pointer, a graph outside 0-2 or not rendered,
 * a negative level. */
OrionStatus orion_engine_display_begin(OrionEngine* engine, int graph, int level,
                                       uint32_t* out_width, uint32_t* out_height,
                                       uint64_t* out_ticket);

/* The newest begun copy that has landed and was not taken: its size and
 * ticket, out_ticket 0 when none has. dst NULL only asks, and keeps it;
 * dst copies that same one, rows of row_bytes, and takes it, dropping any
 * older (a canvas shows the newest and never queues). wait != 0 first waits
 * for the last begun copy to land. BAD_ARG: a NULL out pointer, or no room
 * (row_bytes < width * 4, capacity short), in which case it stays kept. */
OrionStatus orion_engine_display_take(OrionEngine* engine, int wait, uint8_t* dst,
                                      uint32_t row_bytes, uint32_t capacity,
                                      uint32_t* out_width, uint32_t* out_height,
                                      uint64_t* out_ticket);

/* Windows only (decision #386): the same copy, not read back but written into
 * a D3D11 texture behind a DXGI NT handle and a keyed mutex, which a
 * compositor on the same adapter opens (Avalonia's ANGLE, D3D11TextureNtHandle)
 * and reads while holding key 0, as Orion writes it holding key 0. Queued
 * behind the render; the call itself waits only while the reader holds the
 * mutex. out_handle is the engine's (never close it), valid until a later
 * call answers a new out_generation: import that one again. INTERNAL, with
 * orion_last_error saying why, where there is no such texture (macOS, Linux,
 * an adapter without the feature): read back instead. BAD_ARG as
 * orion_engine_display_begin. */
OrionStatus orion_engine_display_share(OrionEngine* engine, int graph, int level, void** out_handle,
                                       int* out_generation, uint32_t* out_width, uint32_t* out_height);

/* The engine's id<MTLDevice>, so the view can share it. */
void* orion_engine_metal_device(const OrionEngine* engine);

/* Samples the image at normalized oriented coordinates.
   out_display is the rendered color (what a swatch should show); out_scene is
 * the color before any user adjustment (what a hue band must be derived from).
 * Each takes 3 floats; either may be NULL. */
OrionStatus orion_engine_sample(const OrionEngine* engine, float u, float v,
                                float* out_display, float* out_scene);

/* Per-channel histogram of the rendered image. out_bins takes bins*3 entries,
 * packed red then green then blue. The full render, except while a region
 * (orion_engine_render_region) is newer than it and the preview has rendered
 * the edit: then the preview, the small whole-frame pass (#22). Likewise the
 * displayed color orion_engine_sample reads. */
OrionStatus orion_engine_histogram(const OrionEngine* engine,
                                   uint32_t* out_bins, uint32_t bins);

/* Metadata for a raw file, without decoding it. Strings are NUL-terminated
 * and sized by the caller. */
typedef struct OrionRawInfo {
    uint32_t width, height;
    char     camera[128];
    char     lens[128];
    float    iso;
    float    shutter;
    float    aperture;
    float    focal_length;
    int64_t  timestamp;
} OrionRawInfo;

OrionStatus orion_read_info(const char* path, OrionRawInfo* out);

/* Copies the camera's embedded JPEG preview into `buffer`. Pass a NULL buffer
 * to query the size first. *out_size receives the byte count needed or written.
 * *out_turns, when non-NULL, receives the clockwise quarter turns that display
 * the preview upright. The JPEG stream itself carries no orientation - the tag
 * lives in the raw's TIFF IFD - so a caller that drops this number shows every
 * portrait frame on its side. */
OrionStatus orion_read_thumbnail(const char* path, uint8_t* buffer,
                                 uint32_t capacity, uint32_t* out_size,
                                 int32_t* out_turns);

/* Copies the XMP packet a file embeds (a DNG's XMLPacket tag, as LibRaw reads
 * it) into `buffer`, not NUL-terminated. Pass a NULL buffer to query the size
 * first; *out_size receives the byte count needed or written, 0 when the file
 * carries none (ORION_OK). A buffer too small returns ORION_ERR_BAD_ARG with
 * *out_size still set, so one call with a generous buffer usually suffices. The shell reads it as a sidecar's baseline for a
 * format Adobe embeds XMP into, and never writes it back (#405). */
OrionStatus orion_read_xmp(const char* path, uint8_t* buffer,
                           uint32_t capacity, uint32_t* out_size);

/* Export. Renders at full resolution and writes the file. */
typedef enum OrionImageFormat {
    ORION_FORMAT_PNG  = 0,
    ORION_FORMAT_JPEG = 1,
    ORION_FORMAT_TIFF = 2,
    ORION_FORMAT_JXL  = 3   /* JPEG XL, via libjxl; quality 1.0 is lossless */
} OrionImageFormat;

/* What the file is tagged as, and converted to.
 *
 * The display transform ends in Rec.709 primaries and saturates there, so no
 * pixel Orion produces today lies outside sRGB. A wider space is converted and
 * tagged correctly, which is what a managed workflow needs, but it cannot add
 * saturation the transform never generated. */
typedef enum OrionColorSpace {
    ORION_SPACE_SRGB       = 0,
    ORION_SPACE_DISPLAY_P3 = 1,
    ORION_SPACE_ADOBE_RGB  = 2
} OrionColorSpace;

/* How much of the RAW's own metadata the export carries.
 *
 * The default is NO_LOCATION, not ALL: a photo taken at home carries the home
 * coordinates, and putting it on the web publishes them. Keeping location is a
 * choice the photographer makes on purpose. */
typedef enum OrionMetadata {
    ORION_METADATA_ALL         = 0,
    ORION_METADATA_NO_LOCATION = 1,
    ORION_METADATA_NONE        = 2
} OrionMetadata;

/* Bits per component in the file. PNG, TIFF and JPEG XL carry either; JPEG is
 * an eight-bit container and ignores it.
 *
 * An eight-bit export renders through the narrow graph, which is the path that
 * dithers — so it is byte-for-byte what the screen shows. Sixteen renders wide.
 *
 * The values are the depths themselves, and zero means unspecified, so a caller
 * that zeroes the struct gets the engine's default rather than silently getting
 * eight. Precision lost by omission is the bug this path has already shipped. */
typedef enum OrionBitDepth {
    ORION_DEPTH_DEFAULT = 0,  /* sixteen */
    ORION_DEPTH_8       = 8,
    ORION_DEPTH_16      = 16
} OrionBitDepth;

/* Output sharpening, applied after the resize.
 *
 * Resampling softens, and the correction belongs at the final size — before it,
 * the resample throws the sharpening away. See research/detail.md: the
 * placement is Fraser's multipass model, the amounts are ours and are listed in
 * research/UNSOURCED.md. */
typedef enum OrionSharpen {
    ORION_SHARPEN_NONE   = 0,
    ORION_SHARPEN_SCREEN = 1,
    ORION_SHARPEN_PRINT  = 2
} OrionSharpen;

typedef struct OrionExportOptions {
    int32_t  format;         /* OrionImageFormat; -1 picks from the extension */
    float    quality;        /* JPEG and JPEG XL, 0..1; JXL at 1 is lossless  */
    uint32_t max_dimension;  /* longest edge; 0 keeps full resolution         */
    int32_t  space;          /* OrionColorSpace                               */
    int32_t  rating;         /* 0-5 written as the star rating; -1 writes none */
    int32_t  metadata;       /* OrionMetadata; 0 keeps GPS, and 0 is not the
                              * caller's default — see OrionMetadata          */
    int32_t  bit_depth;      /* OrionBitDepth                                 */
    int32_t  sharpen;        /* OrionSharpen                                  */

    /* A watermark, as a coverage mask over the whole output frame: 8-bit gray,
     * row-major, tightly packed, 0 leaves the pixel and 255 replaces it with
     * watermark_rgb. Opacity is already baked into the values.
     *
     * NULL means no watermark, so a caller that zeroes the struct gets none.
     * The mask need not match the output size exactly: it is stretched over
     * the frame after the resize, so a rounding pixel either way is invisible.
     * watermark_rgb is sRGB, 0..1. See DECISIONS #284. */
    const uint8_t* watermark_mask;
    uint32_t watermark_width;
    uint32_t watermark_height;
    float    watermark_rgb[3];
} OrionExportOptions;

#include "orion_merge.h"

OrionStatus orion_engine_export(OrionEngine* engine, const char* path,
                                const OrionExportOptions* options);

/* Encodes with these options and reports the byte count without writing
 * anything. A real encode rather than an estimate: the number is only worth
 * showing if it can be trusted before committing to the write. */
OrionStatus orion_engine_export_size(OrionEngine* engine,
                                     const OrionExportOptions* options,
                                     uint64_t* out_bytes);

/* orion_engine_export_size in two halves, so the slow half can leave the
 * thread that owns the engine: a 24 MP JPEG XL is seconds of encode.
 *
 * The snapshot renders and reads the pixels back, so call it wherever any
 * other orion_engine_* call is made. The size encodes the snapshot, touches no
 * engine, and is safe from any thread, even after the engine is destroyed.
 * The options (the watermark mask included) are copied. Free every snapshot. */
typedef struct OrionExportSnapshot OrionExportSnapshot;
OrionStatus orion_engine_export_snapshot(OrionEngine* engine,
                                         const OrionExportOptions* options,
                                         OrionExportSnapshot** out);
OrionStatus orion_export_snapshot_size(const OrionExportSnapshot* snapshot,
                                       uint64_t* out_bytes);
void orion_export_snapshot_free(OrionExportSnapshot* snapshot);

/* The lens profile for the open photo.
 *
 * `found` is zero when no correction is known — which includes every manual
 * lens, since those write no lens name at all. `approximate` is nonzero when
 * the match came from a name the lensfun database spells differently, so the
 * interface can say so rather than implying a measurement of this exact copy.
 *
 * `source` says where the correction came from, in order of precedence:
 * ORION_LENS_SOURCE_DNG (the file's own DNG opcodes), ORION_LENS_SOURCE_LENSFUN
 * (the bundled database, or a lens chosen by hand). Zero when nothing found.
 * 2 is unused: it was an Adobe .lcp profile, withdrawn (#372).
 * Appended last so the earlier fields keep their offsets. */
enum {
    ORION_LENS_SOURCE_NONE    = 0,
    ORION_LENS_SOURCE_LENSFUN = 1,
    ORION_LENS_SOURCE_DNG     = 3
};

typedef struct OrionLensProfile {
    int32_t found;
    int32_t approximate;
    char    lens[128];
    char    maker[64];
    int32_t source;
} OrionLensProfile;

OrionStatus orion_engine_lens_profile(const OrionEngine* engine,
                                      OrionLensProfile* out);

/* Camera make and model of the open image, or "" when none. */
const char* orion_engine_camera(const OrionEngine* engine);

/* The lens name the open file records, or "" when it records none.
 *
 * Not the same as OrionLensProfile.lens, which names the database entry that
 * matched and is "" whenever nothing did. Both are needed to explain a miss:
 * a file naming a lens the database does not carry and a file naming no lens
 * at all — which is every manual lens — are different problems. */
const char* orion_engine_lens(const OrionEngine* engine);

/* Everything the open file's metadata records - the same fields
 * orion_read_info returns, without reading the file again. Zeroed and empty
 * when nothing is open. */
OrionStatus orion_engine_info(const OrionEngine* engine, OrionRawInfo* out);

/* The lens database, for a picker.
 *
 * Indices are stable for the life of the process: the database is parsed once
 * at startup and never reloaded. Returns "" for an out-of-range index rather
 * than failing, so a caller iterating a stale count reads empties instead of
 * garbage.
 *
 * ⚠ Two calls rather than one array, because the list is ~1,450 strings and a
 * caller wants to filter it, not own a copy of it. */
int32_t     orion_lens_name_count(void);
const char* orion_lens_name_at(int32_t index);

/* Choose a lens profile by hand, from a name orion_lens_name_at returned.
 * Passing "" restores the match from the file's own EXIF, which may be nothing.
 *
 * Returns ORION_ERR_BAD_ARG when the name is not in the database, leaving the
 * previous profile alone: a sidecar can seat a choice written against a
 * database that has since changed, and silently dropping the correction would
 * be worse than keeping it. */
OrionStatus orion_engine_set_lens_choice(OrionEngine* engine, const char* name);

/* The hand-chosen lens, or "" when the profile came from EXIF. */
const char* orion_engine_lens_choice(const OrionEngine* engine);

/* Fills *out with the bound device's properties. */
OrionStatus orion_engine_device_info(const OrionEngine* engine,
                                     OrionDeviceInfo*   out);

/* Diagnostics. Both return static or engine-owned strings; never free them.
 * orion_last_error returns "" when nothing has failed. With NULL it answers
 * why this thread's last orion_engine_create failed (no usable GPU, say). */
const char* orion_status_string(OrionStatus status);
const char* orion_last_error(const OrionEngine* engine);
const char* orion_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ORION_H */

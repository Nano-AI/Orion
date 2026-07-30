/*  Orion engine — public C API.
 *
 *  This is the ONLY surface the UI shell sees. Deliberately narrow and
 *  plain-old-data: no templates, no C++ types, no ownership subtleties, and
 *  crucially no exceptions — Swift cannot catch a C++ exception, and one that
 *  escapes terminates the process. Every entry point below is noexcept in
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
    ORION_ERR_NO_GPU    = 1,  /* no Metal device available            */
    ORION_ERR_BAD_ARG   = 2,  /* null or malformed argument           */
    ORION_ERR_INTERNAL  = 3   /* see orion_last_error() for detail     */
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
 * match kMaxMaskComponents in the engine. */
#define ORION_MAX_MASK_COMPONENTS 4

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
 * Both centres are normalized against the frame masks live in, so the same
 * transform that carries a mask's centre from the displayed picture carries a
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

    float centre_x, centre_y;
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

    /* Colour range, when kind is 6. The picked shade as scene-linear Rec.2020
     * RGB, plus a Euclidean tolerance in Oklab chromaticity (a/L, b/L) and how
     * far its edge ramps. The kernel does the conversion, so the target and the
     * pixel cannot disagree about what Oklab is. research/masking.md §4c. */
    float colour_r, colour_g, colour_b;
    float colour_tol;
    float colour_soft;

    /* The brush, when kind is 3. One radius for the whole stroke. The dab
     * centres are not here — they are variable-length, and this struct is
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

    /* Three-way colour grading, ASC CDL per tonal zone. Each triple is a
     * wheel's puck position (x, y) in the unit disc, then that zone's
     * luminance slope. research/color-grading.md. */
    float grade_shadow[3];
    float grade_midtone[3];
    float grade_highlight[3];

    /* Profiled wavelet denoise. Strengths are multiples of the measured noise
     * level, not arbitrary amounts, so one setting behaves the same way on a
     * clean frame and a very noisy one. Zero switches the chain off entirely. */
    float denoise_luma;     /* 0..4                       */
    float denoise_color;   /* 0..4                       */

    /* How much of the loaded creative LUT to apply, 0..1. The LUT itself is
     * not an adjustment — see orion_engine_load_lut. Zero is byte-identical to
     * having none loaded. */
    float lut_strength;

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
    float local_exposure_ev;

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

    /* Tone curve, after the display transform. Master applies to all three
     * channels, then each channel's own curve applies on top. */
    OrionCurveChannel curve_master;
    OrionCurveChannel curve_red;
    OrionCurveChannel curve_green;
    OrionCurveChannel curve_blue;
} OrionAdjustments;

/* Opens a raw file and builds the develop pipeline for it. */
OrionStatus orion_engine_open_raw(OrionEngine* engine, const char* path);

/* Replaces one component's brush stroke. `xy` is `count` interleaved x, y
 * pairs in normalized coordinates of the displayed picture — the same space the
 * gradient masks are placed in, so the engine puts them where develop:linear
 * can use them and the caller never sees that transform.
 *
 * `component` indexes mask_components; out of range is ORION_ERR_BAD_ARG rather
 * than a clamp, because paint landing in the wrong component is worse than
 * nothing happening. The points are copied, so the caller's buffer need not
 * outlive the call. Passing NULL or count <= 0 clears the stroke.
 *
 * This does not itself dirty anything: bump that component's brush_revision and
 * push the adjustments, which is what makes the change visible. Two calls are
 * deliberate — a drag sets the stroke once per frame and the revision is what
 * the engine compares, so the stroke never has to be walked to decide whether
 * it moved. */
OrionStatus orion_engine_set_brush_stroke(OrionEngine* engine, int component,
                                          const float* xy, int count);

/* Uploads a raster matte for one component — kind 4, research/masking.md §5.
 *
 * `alpha` is row-major, top-left origin, `width * height` floats in 0..1.
 *
 * ⚠ It must be in FRAME coordinates: the whole uncropped, unturned sensor
 * frame, which is the space masks are applied in. A producer working from the
 * displayed picture has to undo the crop, the straighten and the quarter turns
 * itself. The kernel does no correction, deliberately — that is what lets a
 * matte survive a crop and a rotation the way a gradient does.
 *
 * Larger than orion_engine_max_matte_size on either axis is ORION_ERR_BAD_ARG
 * rather than a silent downscale. Pass NULL to clear.
 *
 * Like the brush stroke, this does not itself dirty anything: set the
 * component's kind to 4 and push the adjustments. */
OrionStatus orion_engine_set_mask_matte(OrionEngine* engine, int component,
                                        const float* alpha, int width, int height);

/* Total clockwise quarter turns currently applied — the camera's own EXIF
 * orientation plus the user's rotation.
 *
 * A matte producer needs this: kind 4 wants frame coordinates, and a render it
 * reads back has been through the orientation node. research/masking.md §5. */
OrionStatus orion_engine_quarter_turns(const OrionEngine* engine, int* out_turns);

/* Carries a point on the displayed picture into the frame the mask and spot
 * kernels work in, using the geometry currently set.
 *
 * The same transform mask centres go through. Exposed because a *spot* has to
 * be converted once when it is placed rather than on every render: dust sits on
 * the sensor, so a spot must follow the subject through a later crop or turn,
 * which is the opposite of what a mask does. research/spot-removal.md §4. */
/// The inverse of `orion_engine_to_frame`: a point in frame coordinates, as a
/// point on the displayed picture. Spots are stored in frame coordinates, so
/// this is what draws one.
OrionStatus orion_engine_from_frame(const OrionEngine* engine,
                                    float x, float y, float* out_x, float* out_y);

OrionStatus orion_engine_to_frame(const OrionEngine* engine,
                                  float x, float y, float* out_x, float* out_y);

/* The largest matte this image will accept, in pixels. */
OrionStatus orion_engine_max_matte_size(const OrionEngine* engine,
                                        unsigned* out_w, unsigned* out_h);

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

/* Renders dirty nodes. *out_ms receives GPU-side milliseconds (may be NULL). */
OrionStatus orion_engine_render(OrionEngine* engine, double* out_ms);

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

OrionStatus orion_engine_load_lut(OrionEngine* engine, const char* path);

/* Unloads it. Safe when none is loaded. */
OrionStatus orion_engine_clear_lut(OrionEngine* engine);

/* The loaded LUT's TITLE, or an empty string. */
OrionStatus orion_engine_lut_title(const OrionEngine* engine, char* out, int capacity);

OrionStatus orion_engine_set_wide_output(OrionEngine* engine, int wide);

OrionStatus orion_engine_frame_size(const OrionEngine* engine,
                                    uint32_t* out_width, uint32_t* out_height);

/* The pipeline's output as an id<MTLTexture>, for zero-copy display.
 * Non-owning; valid until the next open_raw. NULL when no image is open. */
void* orion_engine_output_texture(const OrionEngine* engine);

/* The preview graph's output texture, or NULL when there is no preview graph.
 * See orion_engine_render_preview — the canvas only. */
void* orion_engine_preview_texture(const OrionEngine* engine);

/* The engine's id<MTLDevice>, so the view can share it. */
void* orion_engine_metal_device(const OrionEngine* engine);

/* Samples the image at normalized oriented coordinates.
   out_display is the rendered color (what a swatch should show); out_scene is
 * the color before any user adjustment (what a hue band must be derived from).
 * Each takes 3 floats; either may be NULL. */
OrionStatus orion_engine_sample(const OrionEngine* engine, float u, float v,
                                float* out_display, float* out_scene);

/* Per-channel histogram of the rendered image. out_bins takes bins*3 entries,
 * packed red then green then blue. */
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
 * to query the size first. *out_size receives the byte count needed or written. */
OrionStatus orion_read_thumbnail(const char* path, uint8_t* buffer,
                                 uint32_t capacity, uint32_t* out_size);

/* Export. Renders at full resolution and writes the file. */
typedef enum OrionImageFormat {
    ORION_FORMAT_PNG  = 0,
    ORION_FORMAT_JPEG = 1,
    ORION_FORMAT_TIFF = 2
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

typedef struct OrionExportOptions {
    int32_t  format;         /* OrionImageFormat; -1 picks from the extension */
    float    quality;        /* JPEG only, 0..1                               */
    uint32_t max_dimension;  /* longest edge; 0 keeps full resolution         */
    int32_t  space;          /* OrionColorSpace                               */
    int32_t  rating;         /* 0-5 written as the star rating; -1 writes none */
    int32_t  metadata;       /* OrionMetadata; 0 keeps GPS, and 0 is not the
                              * caller's default — see OrionMetadata          */
} OrionExportOptions;

OrionStatus orion_engine_export(OrionEngine* engine, const char* path,
                                const OrionExportOptions* options);

/* Encodes with these options and reports the byte count without writing
 * anything. A real encode rather than an estimate: the number is only worth
 * showing if it can be trusted before committing to the write. */
OrionStatus orion_engine_export_size(OrionEngine* engine,
                                     const OrionExportOptions* options,
                                     uint64_t* out_bytes);

/* The lens profile for the open photo, from the vendored lensfun database.
 *
 * `found` is zero when the lens is unknown — which includes every manual lens,
 * since those write no lens name at all. `approximate` is nonzero when the
 * match came from a name the database spells differently, so the interface can
 * say so rather than implying a measurement of this exact copy. */
typedef struct OrionLensProfile {
    int32_t found;
    int32_t approximate;
    char    lens[128];
    char    maker[64];
} OrionLensProfile;

OrionStatus orion_engine_lens_profile(const OrionEngine* engine,
                                      OrionLensProfile* out);

/* Camera make and model of the open image, or "" when none. */
const char* orion_engine_camera(const OrionEngine* engine);

/* Fills *out with the bound device's properties. */
OrionStatus orion_engine_device_info(const OrionEngine* engine,
                                     OrionDeviceInfo*   out);

/* Diagnostics. Both return static or engine-owned strings; never free them.
 * orion_last_error returns "" when nothing has failed. */
const char* orion_status_string(OrionStatus status);
const char* orion_last_error(const OrionEngine* engine);
const char* orion_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ORION_H */

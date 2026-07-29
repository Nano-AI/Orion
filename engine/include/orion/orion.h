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

/* The camera's own white balance, so the UI can open on "as shot". Only the
 * temperature and tint fields are filled; the rest are zeroed. */
OrionStatus orion_engine_as_shot(const OrionEngine* engine, OrionAdjustments* out);

/* Pushes adjustments; cheap enough to call on every slider tick. */
OrionStatus orion_engine_set_adjustments(OrionEngine* engine, const OrionAdjustments* adj);

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
OrionStatus orion_engine_set_wide_output(OrionEngine* engine, int wide);

OrionStatus orion_engine_frame_size(const OrionEngine* engine,
                                    uint32_t* out_width, uint32_t* out_height);

/* The pipeline's output as an id<MTLTexture>, for zero-copy display.
 * Non-owning; valid until the next open_raw. NULL when no image is open. */
void* orion_engine_output_texture(const OrionEngine* engine);

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

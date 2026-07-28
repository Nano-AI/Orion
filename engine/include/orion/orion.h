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
typedef struct OrionAdjustments {
    float exposure_ev;
    float black;
    float contrast;
    float saturation;
} OrionAdjustments;

/* Opens a raw file and builds the develop pipeline for it. */
OrionStatus orion_engine_open_raw(OrionEngine* engine, const char* path);

/* Pushes adjustments; cheap enough to call on every slider tick. */
OrionStatus orion_engine_set_adjustments(OrionEngine* engine, const OrionAdjustments* adj);

/* Renders dirty nodes. *out_ms receives GPU-side milliseconds (may be NULL). */
OrionStatus orion_engine_render(OrionEngine* engine, double* out_ms);

/* Dimensions of the open image. */
OrionStatus orion_engine_image_size(const OrionEngine* engine,
                                    uint32_t* out_width, uint32_t* out_height);

/* The pipeline's output as an id<MTLTexture>, for zero-copy display.
 * Non-owning; valid until the next open_raw. NULL when no image is open. */
void* orion_engine_output_texture(const OrionEngine* engine);

/* The engine's id<MTLDevice>, so the view can share it. */
void* orion_engine_metal_device(const OrionEngine* engine);

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

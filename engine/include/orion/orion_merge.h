/*  Orion engine: HDR merge and stacking, part of the public C API.
 *
 *  Included by orion.h inside its extern "C" block, after OrionEngine and
 *  OrionStatus are declared; include orion.h, never this file.
 */

#ifndef ORION_MERGE_H
#define ORION_MERGE_H

/* ── HDR merge ─────────────────────────────────────────────────────────────
 *
 * Merges two or more bracketed raw files into one floating-point linear DNG
 * written to output_path (which must not already exist - the caller picks
 * collision-free names). paths[reference_index] sets the framing. Blocking:
 * call from a worker thread; poll progress and request cancellation from
 * any other thread. Cancellation surfaces as ORION_ERR_INTERNAL with
 * "merge cancelled" in orion_last_error, and never leaves a partial file. */
OrionStatus orion_engine_hdr_merge(OrionEngine* engine,
                                   const char* const* paths, int32_t count,
                                   int32_t reference_index,
                                   const char* output_path);

/* 0..1, monotone within one merge. Safe from any thread. */
OrionStatus orion_engine_hdr_merge_progress(OrionEngine* engine, float* out);

/* Makes the running merge fail at its next stage boundary. Safe from any
 * thread; a no-op when nothing is running. */
OrionStatus orion_engine_hdr_merge_cancel(OrionEngine* engine);

/* ── Stacking ──────────────────────────────────────────────────────────────
 *
 * Stacks two or more frames of one scene into a linear DNG at output_path
 * (must not exist). paths[reference_index] sets the framing.
 * mode: 0 noise (homography alignment, as HDR merge), 1 stars (sky-only,
 *       triangle matching), 2 landscape (sky on stars, ground unaligned,
 *       blended over sky_matte).
 * rejection: -1 auto, 0 none, 1 percentile, 2 sigma, 3 winsorized, 4 linear fit.
 * weight_by_noise: nonzero weights each frame by its inverse noise variance.
 * darks/flats: optional calibration frames (NULL/0 for none), the same size,
 *            camera and kind (mosaic raw or linear DNG) as the frames, else a
 *            refusal. The master dark is their per-sample median, subtracted;
 *            the master flat is the flats' median minus the master dark,
 *            normalized to mean 1 per CFA channel (per RGB channel
 *            for a linear DNG), divided; a clipped sample stays clipped. A raw is
 *            calibrated on its mosaic, before demosaic (research/stacking.md §7).
 * sky_matte: landscape only, matte_width*matte_height floats 0..1, 1 = sky,
 *            UPRIGHT (as displayed); the engine undoes the reference's EXIF
 *            turn. Required for mode 2; ignored in modes 0 and 1, though a
 *            non-NULL matte's dimensions are still validated.
 * Frames of different sizes or cameras are refused before any decode.
 * Without force, so is a stack whose orion_stack_estimate exceeds
 * orion_available_memory: ORION_ERR_MEMORY, with a sentence in
 * orion_last_error. force != 0 runs it anyway.
 * A frame that does not register is left out; fewer than two left is an
 * error. Blocking; progress and cancel from any thread, as
 * orion_engine_hdr_merge ("stack cancelled", never a partial file). */
OrionStatus orion_engine_stack(OrionEngine* engine, const char* const* paths, int32_t count,
                               int32_t reference_index, int32_t mode, int32_t rejection,
                               int32_t weight_by_noise,
                               const char* const* darks, int32_t dark_count,
                               const char* const* flats, int32_t flat_count,
                               const float* sky_matte, int32_t matte_width, int32_t matte_height,
                               const char* output_path, int32_t force);

/* The stack's estimated peak memory in bytes, from the first frame's metadata
 * alone (no decode, no engine): width x height x (max(12 x count, 4 x
 * max(dark_count, flat_count)) + 94), measured (research/UNSOURCED.md §32).
 * ORION_ERR_INTERNAL when the first frame will not read. */
OrionStatus orion_stack_estimate(const char* const* paths, int32_t count, int32_t dark_count, int32_t flat_count,
                                 uint64_t* out_bytes);

/* This machine's memory available now, in bytes (0 when the OS will not
 * say). macOS: free + inactive + purgeable pages; Windows: ullAvailPhys;
 * Linux: MemAvailable. */
OrionStatus orion_available_memory(uint64_t* out_bytes);

/* Test seam: non-zero makes orion_available_memory, and the stack's own
 * check, answer this instead of the OS; 0 restores the OS. */
OrionStatus orion_available_memory_override(uint64_t bytes);

/* 0..1, monotone within one stack. Safe from any thread. */
OrionStatus orion_engine_stack_progress(OrionEngine* engine, float* out);

/* Frames the last stack left out because they did not register. Safe from
 * any thread. */
OrionStatus orion_engine_stack_skipped(OrionEngine* engine, int32_t* out);

/* Makes the running stack fail at its next stage boundary. Safe from any
 * thread; a no-op when nothing is running. */
OrionStatus orion_engine_stack_cancel(OrionEngine* engine);

#endif /* ORION_MERGE_H */

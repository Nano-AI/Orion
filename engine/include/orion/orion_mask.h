/*  Orion engine: masks, spots and the frame they live in, part of the public C API.
 *
 *  Included by orion.h inside its extern "C" block, after OrionEngine,
 *  OrionStatus and OrionAdjustments are declared; include orion.h, never this file.
 */

#ifndef ORION_MASK_H
#define ORION_MASK_H

/* Replaces one component's brush stroke. `xy` is `count` interleaved x, y
 * pairs in normalized coordinates of the displayed picture: the same space the
 * gradient masks are placed in, so the engine puts them where develop:linear
 * can use them and the caller never sees that transform.
 * `component` indexes mask_components; out of range is ORION_ERR_BAD_ARG rather
 * than a clamp, because paint landing in the wrong component is worse than
 * nothing happening. The points are copied, so the caller's buffer need not
 * outlive the call. Passing NULL or count <= 0 clears the stroke. `erase` is
 * one value per dab: nonzero takes coverage away instead of laying it down, so
 * one component can be painted into and rubbed back out; NULL paints throughout.
 * This does not itself dirty anything: bump that component's brush_revision and
 * push the adjustments, which is what makes the change visible. Two calls are
 * deliberate: a drag sets the stroke once per frame and the revision is what
 * the engine compares, so the stroke never has to be walked to decide whether
 * it moved. */
OrionStatus orion_engine_set_brush_stroke(OrionEngine* engine, int component,
                                          const float* xy, const float* erase,
                                          int count);

/* Uploads a raster matte for one component: kind 4, research/masking.md §5.
 * `alpha` is row-major, top-left origin, `width * height` floats in 0..1.
 * ⚠ It must be in FRAME coordinates: the whole uncropped, unturned sensor
 * frame, which is the space masks are applied in. A producer working from the
 * displayed picture has to undo the crop, the straighten and the quarter turns
 * itself. The kernel does no correction, deliberately: that is what lets a
 * matte survive a crop and a rotation the way a gradient does.
 * Larger than orion_engine_max_matte_size on either axis is ORION_ERR_BAD_ARG
 * rather than a silent downscale. Pass NULL to clear.
 * Like the brush stroke, this does not itself dirty anything: set the
 * component's kind to 4 and push the adjustments. */
OrionStatus orion_engine_set_mask_matte(OrionEngine* engine, int component,
                                        const float* alpha, int width, int height);

/* Total clockwise quarter turns currently applied: the camera's own EXIF
 * orientation plus the user's rotation.
 *
 * A matte producer needs this: kind 4 wants frame coordinates, and a render it
 * reads back has been through the orientation node. research/masking.md §5. */
OrionStatus orion_engine_quarter_turns(const OrionEngine* engine, int* out_turns);

/* Carries a point on the displayed picture into the frame the mask and spot
 * kernels work in, using the geometry currently set.
 *
 * The same transform mask centers go through. Exposed because a *spot* has to
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

/* The whole frame <-> display map under the geometry currently set: two
 * projective 3x3 matrices, row-major, in normalized coordinates, one each
 * way. The same map the two point calls above walk, exposed whole so the
 * mask overlay can carry a 128-point outline across without 128 calls. */
OrionStatus orion_engine_display_map(const OrionEngine* engine,
                                     float* out_to_display9,
                                     float* out_to_frame9);

/* The largest matte this image will accept, in pixels. */
OrionStatus orion_engine_max_matte_size(const OrionEngine* engine,
                                        unsigned* out_w, unsigned* out_h);

/* The geometry a legacy sidecar's display-space mask numbers were relative to:
 * the crop, the user's rotation, the straighten and the perspective sliders,
 * exactly as that sidecar persisted them. The EXIF turn is the engine's own
 * and is not in here. */
typedef struct OrionMaskLegacyGeometry {
    float crop_x, crop_y, crop_w, crop_h;
    int   rotate_quarters;
    float straighten_deg;
    float perspective_vertical;
    float perspective_horizontal;
    float perspective_aspect;
} OrionMaskLegacyGeometry;

/* One mask component's spatial numbers. Which fields matter follows the kind:
 * center and angle always, length for a linear gradient, the radii for a
 * radial one, brush_radius for a brush. */
typedef struct OrionMaskShape {
    float center_x, center_y;
    float angle;
    float length;
    float radius_x, radius_y;
    float brush_radius;
} OrionMaskShape;

/* Converts one legacy component's geometry from display space (normalized
 * against the crop it was saved under) into frame coordinates, the space
 * masks are stored and evaluated in now.
 *
 * The migration call: a sidecar written before frame-space masks holds
 * display-space numbers alongside the geometry they were relative to, so the
 * conversion is deterministic. `shape` converts in place. `dab_xy` is
 * `dab_count` interleaved brush-dab centers, converted in place; NULL with a
 * zero count is an empty stroke.
 *
 * The centre and angle are exact; a radial's semi-axes and a linear's length
 * are exact under crops and quarter turns and first order under a straighten
 * or a keystone: the same numbers every render produced before decision
 * #137, so nothing converted renders worse than it always had. */
OrionStatus orion_engine_mask_place_to_frame(
    const OrionEngine* engine, const OrionMaskLegacyGeometry* geometry,
    int kind, OrionMaskShape* shape, float* dab_xy, int dab_count);

#endif /* ORION_MASK_H */

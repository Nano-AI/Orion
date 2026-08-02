/*  orion-tests — the shared harness.
 *
 *  No framework: a counter and three helpers. `report` is the only thing a test
 *  needs, and `CHECK` is sugar over it for a bare condition.
 *
 *  ⚠ This exists because `main.cpp` reached **7,656 lines** — seven times over
 *  the limit `CLAUDE.md` calls a hard constraint, and growing every session. The
 *  tests are split by subject across `tests_*.cpp`; each one includes this and
 *  nothing else of the harness. Every file is under a thousand lines.
 *
 *  The includes are gathered here rather than trimmed per file on purpose. This
 *  is a test binary, not a library: one place to add a header beats twelve
 *  places to hunt for a missing one, and the build cost is a second.
 */

#pragma once

#include "pipe/ToneCurve.h"
#include "pipe/WhiteBalance.h"
#include "raw/NoiseProfile.h"
#include "raw/RawImage.h"
#include "gpu/MetalDevice.h"
#include "gpu/Resources.h"
#include "pipe/ShaderParams.h"
#include "pipe/DevelopPipeline.h"
#include "pipe/HueSatMap.h"
#include "pipe/LensDatabase.h"
#include "pipe/LensGeometry.h"
#include "pipe/AutoEnhance.h"
#include "pipe/CubeLut.h"
#include "pipe/ExposureFusion.h"
#include "pipe/Dehaze.h"
#include "pipe/LocalLaplacian.h"
#include "pipe/HighlightFill.h"
#include <set>

#include "pipe/MaskGeometry.h"
#include "pipe/Perspective.h"
#include "util/ImageWriter.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <vector>

#include <algorithm>
#include <utility>
#include <array>
#include <cmath>
#include <random>
#include <cstdio>
#include <cstring>
#include <string>

// The counters. Defined in harness.cpp, so every translation unit adds to the
// same tally and `main` prints one number.
extern int failures;
extern int checks;

void report(bool ok, const std::string& what, const std::string& detail = "");
void checkNear(double got, double want, double tol, const std::string& what);
void section(const char* name);

#define CHECK(cond) report((cond), #cond)

/// A scratch brush accumulator, for the tests that dispatch `maskComponent`
/// directly. Decision #108 gave that kernel a seventh binding — the persistent
/// coverage it continues a stroke from — and an unbound slot in Metal is nil
/// rather than an error, so every one of these dispatches has to hand it
/// something real even when `accumUse` is zero and the kernel never touches it.
///
/// Cached per size, because several of the call sites are inside loops.
orion::gpu::Texture& scratchAccum(orion::gpu::Device&, std::uint32_t w,
                                  std::uint32_t h);

// Every test, in one list. ⚠ A test defined and not declared here is a test that
// does not link; a test declared and not called is one that never runs, which is
// why `main.cpp` is now short enough to read its call list at a glance.
void testBindingCount();
void testPlanckianLocus();
void testWhiteBalance();
void testToneCurve();
void testCfa();
void testOrientation();
void testLensDatabase();
void testBlackLevels();
void testExportFormats();
void testOrientGpu();
void testDisplayNeutrality();
void testCropGpu();
void testStraightenPivot();
void testPerspectiveMath();
void testPerspectiveShaderMatchesHost();
void testPerspectiveIdentity();
void testPerspectiveConvergingLines();
void testPerspectiveOneResample();
void testPerspectiveAutoScaleFillsTheFrame();
void testPerspectiveMaskGeometry();
void testPerspectiveMaskExtent();
void testDisplayMatrixMatchesFromFrame();
void testRampIsTheExactPullBack();
void testRampDenominatorIsTheMatrix();
void testRadialIsTheExactPullBack();
void testPerspectiveWiring();
void testNoiseEstimator();
void testDenoiseGpu();
void testHighlightRecoveryGpu();
void testLinearizeClipsToWhite();
void testGradeOffsets();
void testLensGpu();
void testHueSatMapGpu();
void testColorGradeGpu();
void testToneBandsWithoutGuide();
void testLocalAdjustments();
void testLensAutoScale();
void testOutputDepth();
void testHighlightHaloGpu();
void testHighlightFillGpu();
void testHighlightFillWiring();
void testLocalLaplacianGpu();
void testDehazeGpu();
void testCreativeLut();
void testExposureFusionMath();
void testExposureFusionGpu();
void testAutoEnhanceStats();
void testMaskGpu();
void testMaskBrushGpu();
void testMaskCompositeGpu();
void testBrushDabsFollowTheFrame();
void testMaskGeometry();
void testMaskGeometryInverse();
void testBayerDecimation();
void testSpotRemovalGpu();
void testDabBlockRejection();
void testLongBrushStroke();
void testBrushErase();
void testBrushPrefixPredicate();
void testBrushPrefixWiring();
void testBrushAccumulator();
void testMaskRangeGpu();
void testMaskColorGpu();
void testMaskMatteGpu();
void testMaskRefineGpu();
void testGrainPlate();
void testGrainGpu();
void testGrainWiring();
void testCompositionCircle();
void testCreativeVignetteGpu();
void testVignetteFollowsTheCrop();

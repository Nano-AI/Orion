/*  orion-tests — the things that broke, and must not break again.
 *
 *  No framework: a handful of helpers and a count. These cover the maths that
 *  has actually produced visible bugs — white balance round-tripping, curve
 *  monotonicity, orientation dimensions, and CFA indexing — so a regression
 *  fails here rather than in a screenshot.
 *
 *  The tests themselves live in `tests_*.cpp`, by subject. This file is the
 *  running order and nothing else.
 */

#include "harness.h"

int main() {
    std::printf("orion-tests\n\n");

    // First, because it is the one that catches a shader and a binary that
    // disagree — a state in which every other GPU test's result is a guess.
    testBindingCount();

    testWhiteBalance();
    testPlanckianLocus();
    testToneCurve();
    testCfa();
    testOrientation();
    testOrientGpu();
    testDisplayNeutrality();
    testCropGpu();
    testStraightenPivot();
    testPerspectiveMath();
    testPerspectiveShaderMatchesHost();
    testPerspectiveIdentity();
    testPerspectiveConvergingLines();
    testPerspectiveOneResample();
    testPerspectiveAutoScaleFillsTheFrame();
    testPerspectiveMaskGeometry();
    testPerspectiveMaskExtent();
    testDisplayMatrixMatchesFromFrame();
    testRampIsTheExactPullBack();
    testRampDenominatorIsTheMatrix();
    testRadialIsTheExactPullBack();
    testTexturePool();
    testPerspectiveWiring();
    testNoiseEstimator();
    testLinearizeClipsToWhite();
    testDenoiseGpu();
    testHighlightRecoveryGpu();
    testGradeOffsets();
    testLensGpu();
    testLensAutoScale();
    testToneBandsWithoutGuide();
    testHueSatMapGpu();
    testColorGradeGpu();
    testLocalLaplacianGpu();
    testDehazeGpu();
    testCreativeLut();
    testExposureFusionMath();
    testExposureFusionGpu();
    testAutoEnhanceStats();
    testMaskGpu();
    testMaskBrushGpu();
    testMaskCompositeGpu();
    testMaskGeometry();
    testMaskRefineGpu();
    testMaskMatteGpu();
    testLongBrushStroke();
    testDabBlockRejection();
    testBrushErase();
    testBrushPrefixPredicate();
    testBrushPrefixWiring();
    testBrushAccumulator();
    testBrushSpacingRipple();
    testDabHardnessAcrossNibSizes();
    testGuideEpsilonInStops();
    testMaskGeometryInverse();
    testLocalAdjustments();
    testMaskRangeGpu();
    testMaskColorGpu();
    testBayerDecimation();
    testSpotRemovalGpu();
    testBrushDabsFollowTheFrame();
    testHighlightHaloGpu();
    testHighlightFillGpu();
    testHighlightFillWiring();
    testOutputDepth();
    testLensDatabase();
    testBlackLevels();
    testExportFormats();
    testGrainPlate();
    testGrainGpu();
    testGrainWiring();
    testCompositionCircle();
    testCreativeVignetteGpu();
    testVignetteFollowsTheCrop();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

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
    // Unbuffered: piped (CI), a crash would otherwise take the last few
    // kilobytes of output with it, and with them where it happened (#376).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("orion-tests\n\n");

    // First, because it is the one that catches a shader and a binary that
    // disagree — a state in which every other GPU test's result is a guess.
    testBindingCount();
    testDisabledCacheReleaseGpu();
    testReleaseEveryRenderGpu();
    testDawnParityGpu();

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
    testPlaceToFrame();
    testTexturePool();
    testPerspectiveWiring();
    testNoiseEstimator();
    testLinearizeClipsToWhite();
    testDenoiseGpu();
    testDenoiseColorOnlyGpu();
    testDenoiseDetailGpu();
    testDenoiseDetailZeroIsShippedGpu();
    testDenoiseMaskedGpu();
    testNlmGpu();
    testTextureGpu();
    testDenoiseSmoothnessGpu();
    testHighlightRecoveryGpu();
    testGradeOffsets();
    testLensGpu();
    testDngLensOpcodes();
    testLensInverseMap();
    testDngLensOpcodesThroughLibRaw();
    testLensSourceOnRealSony();
    testRegionMatchesCropGpu();
    testRegionAtFrameEdgesGpu();
    testLensAutoScale();
    testToneBandsWithoutGuide();
    testHueSatMapGpu();
    testColorGradeGpu();
    testLocalLaplacianGpu();
    testClarityNoiseFloorGpu();
    testDehazeGpu();
    testCreativeLut();
    testExposureFusionMath();
    testExposureFusionGpu();
    testFusionInvalidationGpu();
    testAutoEnhanceStats();
    testMaskGpu();
    testMaskBrushGpu();
    testMaskCompositeGpu();
    testLayerBreakRefreshesTheLayerTable();
    testMaskEightSlotsGpu();
    testMaskRefineLayerEndsGpu();
    testOverlayPaintsTheSelectedLayer();
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
    testPreviewShadowsMatchGpu();
    testSpotRemovalGpu();
    testBrushDabsFollowTheFrame();
    testHighlightHaloGpu();
    testHighlightFillGpu();
    testHighlightFillWiring();
    testOutputDepth();
    testAgxLatitudeIsAnAnchoredRescale();
    testDisplayRollOffIsInjective();
    testLensDatabase();
    testBlackLevels();
    testExportFormats();
    testExportWatermark();
    testExportJxl();
    testFloatToHalf();
    testDngRoundTrip();
    testLinearDngOpens();
    testDngEmbeddedPreview();
    testLinearDngBaselineExposure();
    testLinearDngEngineOpen();
    testFacadeSessionSeams();
    testMergeExposureMaths();
    testMergeReconstruction();
    testMergeDeghostAndCoverage();
    testMergeRenderDemosaic();
    testAlignRecoversHomography();
    testAlignRefusesFeatureless();
    testHdrMergeFacade();
    testHdrMergeCancelIsAtomic();
    testGrainPlate();
    testGrainGpu();
    testGrainWiring();
    testCompositionCircle();
    testCreativeVignetteGpu();
    testVignetteFollowsTheCrop();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    if (skipped > 0) std::printf("%d skipped: no usable GPU (%s)\n", skipped, gpuProblem().c_str());
    return failures == 0 ? 0 : 1;
}

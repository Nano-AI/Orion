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

#include <cstdlib>

int main(int argc, char** argv) {
    // Unbuffered: piped (CI), a crash would otherwise take the last few
    // kilobytes of output with it, and with them where it happened (#376).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("orion-tests\n\n");

    // ORION_STACK_BENCH=1 runs the 24 MP stack bench alone, so the peak RSS it prints is its own.
    if (const char* b = std::getenv("ORION_STACK_BENCH"); b && std::string(b) == "1") {
        testStackBench();
        return failures == 0 ? 0 : 1;
    }

    // --kernels: only "every kernel compiles on Dawn", the GPU check a Windows pull
    // request keeps (WARP renders the rest for an hour; push and nightly run it all, #412).
    // --cpu-only: only the tests below called plainly, never through gpu(): each was
    // read and creates no device (no Device::create, Engine or orion_engine_create).
    // They are colour and tone maths, orientation and geometry, mask and perspective
    // geometry, the noise estimator, DNG and export I/O (JPEG XL, watermark,
    // sharpening), lens maths and the database, HDR merge maths and alignment, and
    // stacking below its facade. ORION_GPU is poisoned so a device any of them
    // creates throws: a GPU test called plainly fails here, it does not slow CI.
    bool cpuOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--kernels") {
            testEveryKernelCompilesOnDawnGpu();
            std::printf("\n%d checks, %d failures\n", checks, failures);
            return failures == 0 ? 0 : 1;
        }
        if (arg != "--cpu-only") {
            std::fprintf(stderr, "orion-tests: unknown argument %s (--cpu-only, --kernels)\n", argv[i]);
            return 1;
        }
        cpuOnly = true;
    }
    if (cpuOnly) {
#ifdef _WIN32
        _putenv_s("ORION_GPU", "none-under-cpu-only");
#else
        setenv("ORION_GPU", "none-under-cpu-only", 1);
#endif
    }
    const auto gpu = [cpuOnly](void (*test)()) { if (!cpuOnly) test(); };

    // First, because it is the one that catches a shader and a binary that
    // disagree — a state in which every other GPU test's result is a guess.
    gpu(testBindingCount);
    gpu(testDisabledCacheReleaseGpu);
    gpu(testReleaseEveryRenderGpu);
    gpu(testGpuBudget42MpGpu);
    gpu(testWarmGraphMatchesFreshGpu);
    gpu(testMemoryRuleGpu);
    gpu(testDawnParityGpu);
    gpu(testEveryKernelCompilesOnDawnGpu);

    testWhiteBalance();
    testPlanckianLocus();
    testToneCurve();
    testCfa();
    testOrientation();
    gpu(testOrientGpu);
    gpu(testDisplayNeutrality);
    gpu(testCropGpu);
    gpu(testStraightenPivot);
    testPerspectiveMath();
    gpu(testPerspectiveShaderMatchesHost);
    gpu(testPerspectiveIdentity);
    gpu(testPerspectiveConvergingLines);
    gpu(testPerspectiveOneResample);
    gpu(testPerspectiveAutoScaleFillsTheFrame);
    testPerspectiveMaskGeometry();
    testPerspectiveMaskExtent();
    testDisplayMatrixMatchesFromFrame();
    testPlaceToFrame();
    gpu(testTexturePool);
    gpu(testPerspectiveWiring);
    testNoiseEstimator();
    gpu(testLinearizeClipsToWhite);
    gpu(testDenoiseGpu);
    gpu(testDenoiseColorOnlyGpu);
    gpu(testDenoiseDetailGpu);
    gpu(testDenoiseDetailZeroIsShippedGpu);
    gpu(testDenoiseMaskedGpu);
    gpu(testDenoiseOrderGpu);
    gpu(testNlmGpu);
    gpu(testTextureGpu);
    gpu(testDenoiseSmoothnessGpu);
    gpu(testHighlightRecoveryGpu);
    testGradeOffsets();
    gpu(testLensGpu);
    gpu(testDngLensOpcodes);
    testLensInverseMap();
    gpu(testDngLensOpcodesThroughLibRaw);
    gpu(testLensSourceOnRealSony);
    gpu(testRegionMatchesCropGpu);
    gpu(testRegionAtFrameEdgesGpu);
    gpu(testRegionWithoutWaitGpu);
    gpu(testRegionDngLensGpu);
    testLensAutoScale();
    gpu(testToneBandsWithoutGuide);
    gpu(testHueSatMapGpu);
    gpu(testColorGradeGpu);
    gpu(testLocalLaplacianGpu);
    gpu(testClarityNoiseFloorGpu);
    gpu(testDehazeGpu);
    gpu(testCreativeLut);
    testExposureFusionMath();
    gpu(testExposureFusionGpu);
    gpu(testFusionInvalidationGpu);
    testAutoEnhanceStats();
    gpu(testMaskGpu);
    gpu(testMaskBrushGpu);
    gpu(testMaskCompositeGpu);
    gpu(testLayerBreakRefreshesTheLayerTable);
    gpu(testMaskEightSlotsGpu);
    gpu(testMaskRefineLayerEndsGpu);
    gpu(testOverlayPaintsTheSelectedLayer);
    testMaskGeometry();
    gpu(testMaskRefineGpu);
    gpu(testMaskMatteGpu);
    gpu(testLongBrushStroke);
    gpu(testDabBlockRejection);
    gpu(testBrushErase);
    testBrushPrefixPredicate();
    gpu(testBrushPrefixWiring);
    gpu(testBrushAccumulator);
    gpu(testBrushSpacingRipple);
    gpu(testDabHardnessAcrossNibSizes);
    gpu(testGuideEpsilonInStops);
    testMaskGeometryInverse();
    gpu(testLocalAdjustments);
    gpu(testMaskRangeGpu);
    gpu(testMaskColorGpu);
    testBayerDecimation();
    gpu(testPreviewShadowsMatchGpu);
    gpu(testSpotRemovalGpu);
    testBrushDabsFollowTheFrame();
    gpu(testHighlightHaloGpu);
    gpu(testHighlightFillGpu);
    gpu(testHighlightFillWiring);
    gpu(testOutputDepth);
    gpu(testAgxLatitudeIsAnAnchoredRescale);
    gpu(testDisplayRollOffIsInjective);
    testLensDatabase();
    testBlackLevels();
    testExportFormats();
    testExportWatermark();
    testExportJxl();
    testExportSharpening();
    testFloatToHalf();
    testDngRoundTrip();
    gpu(testLinearDngOpens);
    testDngEmbeddedPreview();
    testDngEmbeddedXmp();
    gpu(testLinearDngBaselineExposure);
    gpu(testLinearDngEngineOpen);
    gpu(testFacadeSessionSeams);
    gpu(testDisplayCopyGpu);
    testMergeExposureMaths();
    testMergeReconstruction();
    testMergeDeghostAndCoverage();
    gpu(testMergeRenderDemosaic);
    testAlignRecoversHomography();
    testAlignRefusesFeatureless();
    gpu(testHdrMergeFacade);
    gpu(testHdrMergeCancelIsAtomic);
    testStackRejection();
    testStarDetect();
    testStarAlignRecovers();
    testStarAlignRefuses();
    testStarAlignWideField();
    testStarAlignRefusesFolds();
    testStackNoiseFallsSqrtN();
    testStackRejectsStreak();
    testStackRemovesHotPixel();
    testStackNormalizesAndWeights();
    testStackSkyGroundBlend();
    testStackBandsEqualWhole();
    testSkySplitOrientation();
    gpu(testStackFacadeNoise);
    gpu(testStackFacadeStars);
    gpu(testStackFacadeLandscape);
    gpu(testStackFacadeRefuses);
    gpu(testStackCancelIsAtomic);
    gpu(testStackBandsThroughStacker);
    gpu(testStackMemory);
    testStackMasterDark();
    testStackFlatGain();
    testStackBayerRoundTrip();
    gpu(testStackFacadeCalibrated);
    gpu(testStackCalibratedRaws);
    testGrainPlate();
    gpu(testGrainGpu);
    gpu(testGrainWiring);
    testCompositionCircle();
    gpu(testCreativeVignetteGpu);
    gpu(testVignetteFollowsTheCrop);

    std::printf("\n%d checks, %d failures\n", checks, failures);
    if (skipped > 0) std::printf("%d skipped: no usable GPU (%s)\n", skipped, gpuProblem().c_str());
    return failures == 0 ? 0 : 1;
}

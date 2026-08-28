import CoreGraphics
import Foundation

/// Viewport geometry tests.
///
/// This maths has produced two visible bugs — a stretched frame and a cropped
/// one — because it was only ever checked by looking at the screen. The
/// relationships below are the ones that were wrong.
enum ViewportTests {

    nonisolated(unsafe) static var checks = 0
    nonisolated(unsafe) static var failures = 0

    static func report(_ ok: Bool, _ what: String, _ detail: String = "") {
        checks += 1
        guard !ok else { return }
        failures += 1
        print("  FAIL  \(what)\(detail.isEmpty ? "" : " — \(detail)")")
    }

    static func near(_ got: CGFloat, _ want: CGFloat, _ tol: CGFloat, _ what: String) {
        let ok = abs(got - want) <= tol
        report(ok, what, ok ? "" : String(format: "got %.5f, want %.5f", got, want))
    }

    // Landscape 3:2 image; a taller-than-wide view and a wider-than-tall one.
    static let landscape: CGFloat = 6024.0 / 4024.0   // ~1.497
    static let portrait: CGFloat = 4024.0 / 6024.0    // ~0.668

    static func run() -> Int {
        print("Viewport\n")

        testFitShowsEverything()
        testZoomShrinksVisible()
        testQuadLetterboxes()
        testClampKeepsFrameInView()
        testZoomAnchoring()
        testPercent()
        testHueBands()
        testCropLock()
        testFrameRectMatchesRenderer()
        testCropStaysInsideTurnedFrame()
        testConstraintIsIdentityWhenStraight()
        testPreviewCanvasCoversTheTurnedFrame()
        testCanvasIgnoresTheCrop()
        testCornerHandlePositions()
        testCropDragFreeMatchesTheOldArithmetic()
        testCropDragShiftKeepsTheStartRatio()
        testCropDragShiftFollowsTheDominantAxis()
        testCropDragShiftClampsAgainstTheFrame()
        testCropDragShiftRespectsTheMinimum()
        testCropDragShiftSurvivesTheEngineClamp()
        testCropDragMoveIgnoresTheLock()
        testGalleryColumnsFitTheWidth()
        testGalleryMoveClampsAtTheEdges()
        testGalleryMoveHandlesTheRaggedLastRow()
        testGalleryMoveSurvivesDegenerateInput()
        testGallerySliderConstantsHoldTheirOrder()
        testTrashPlanPicksExactlyItsSiblings()
        testTrashPlanDoesNotCrossFilenames()
        testTrashPlanWithNoSiblingsIsEmpty()
        testTrashPlanIgnoresMatteImposters()
        testTrashOutcomeSentences()
        testCurveMatchesTheEngine()
        testCurvePointsStayOrdered()
        testModifiedTracksTheReadout()
        testDrawnRectFollowsTheZoom()
        testDevelopStateRoster()
        testSidecarSurvivesAMissingField()
        testMaskGroupSidecar()
        testMaskSpaceMarkerGatesTheLegacyRead()
        testAmericanKeyMigration()
        testSidecarEscapingDoesNotCompound()
        testEditsSurviveAQuit()
        testSidecarWriteReportsRefusal()
        testAFailedAutosaveIsNotForgotten()

        testMattePngRoundTripsItsMidTones()
        testMatteKeepsItsOrientation()
        testMatteClampsRatherThanWraps()
        testMatteReferenceSurvivesTheSidecar()
        testMatteFileSitsBesideTheSidecar()
        testSweepDistinguishesAbsentFromUnreadable()
        testSweepDoesNotHoardTheDirectory()

        testPictureMapMatchesTheFitRectangle()
        testPictureMapRoundTrips()
        testPictureMapFollowsThePan()
        testBrushCursorIsRound()
        testBatchNeverOverwrites()
        testBatchKeepsGoingAfterAFailure()
        testEveryFieldSurvivesTheSidecar()
        testPresetIsAPatch()
        testSyncKeysMatchTheStructPatch()
        testSyncLeavesUnknownWhiteBalanceAlone()
        testSyncPatchesOnlyItsGroups()
        testPresetNeverCarriesTheFrame()
        testPresetStoreRoundTrip()
        testPresetFileSurvivesOneBadPreset()

        testSnapshotRoundTripsTheWholeState()
        testSnapshotDatesRoundTrip()
        testSnapshotFileSitsBesideTheSidecar()
        testAnUnreadableVersionFileIsNotOverwritten()
        testTheSweepKeepsAMatteAVersionNames()
        testAnUnreadableVersionFileCollectsNothing()
        testAVersionPinsAMatteWithNoSidecar()
        testAVersionSaysWhichMattesAreMissing()
        testRestoringKeepsTheWorkingEdit()
        testRenamingTheAutomaticVersionKeepsIt()
        testVersionsSaveReplaceAndDelete()
        testEveryVersionRefusalGivesAReason()

        testMatteTurnsRoundTrip()
        testMatteTurnAgreesWithTheMaskTransform()
        testMatteAnalysisSize()
        testMaskOutlineLandsOnTheFalloff()
        testMaskIsoLinesAreIsoAlpha()
        testMaskEndpointLandsUnderTheCursor()
        testMaskDragCrossesTheGeometryMap()
        testMaskRotateStaysOnTheCursorRay()
        testMaskBodyDragMovesByTheDrag()
        testMaskAxisDragDoesNotRotate()
        testMaskDragStaysInSliderRange()
        testMaskHitPrefersHandlesOverBody()
        testMaskAnglesAreNormalizedNotScreen()
        testBrushSpacingIsUnderItsBound()
        testBrushDabsAreEvenlySpaced()
        testBrushSpacingSurvivesTheEventRate()

        testSkyFillCannotSqueezeThroughADiagonal()
        testSkyEigenvalueProxyOrdersTheSameWay()
        testSkyFindsAHorizon()
        testSkyNeverAsksWhatSkyLooksLike()
        testSkyRefusesAFrameWithNone()
        testSkyEnergyPicksTheBorder()

        testCatalogueCoversEveryAdjustment()
        testCatalogueAgreesWithTheShaderAboutWhatIsLocal()
        testEveryRefusalGivesAReason()

        testSpotHitPrefersTheSource()
        testSpotHitPrefersTheTopmost()
        testSpotHandleHasAMinimumSize()
        testSpotDragStaysOnThePicture()

        testARowIsFiledUnderTheFolderItWasListedIn()
        testIndexIsColdBeforeItIsWarm()
        testARewrittenRawInvalidatesByMtime()
        testARewrittenRawInvalidatesBySizeAlone()
        testARatingChangeInvalidatesTheMarksAndNothingElse()
        testADeletedSidecarClearsTheRating()
        testADeletedFileLeavesTheIndex()
        testACorruptDatabaseDegradesToARescan()
        testALockedDatabaseIsNotDiscarded()
        testALockAtOpenNeverDeletesTheDatabase()
        testAVanishedFolderIsCollected()
        testAForeignSchemaIsRebuilt()
        testASidecarChangingUnderTheReadIsNotFiled()
        testTheThumbnailCacheEvictsTheLeastRecentlyUsed()
        testAThumbnailKeepsItsShapeAndItsWayUp()
        testAnUntaggedPreviewTurnsByExplicitQuarters()
        testATaggedPreviewIsNotTurnedTwice()
        testAnExplicitZeroTurnsChangesNothing()

        testOnePhotoIsNotASelection()
        testModifiedClicksBuildASelection()
        testShiftClickIsARangeFromTheAnchor()
        testTheOpenPhotoCannotBeDeselected()
        testAFilterChangeCannotHideATarget()
        testTargetsComeBackInStripOrder()

        testJpegNeverAsksForSixteenBits()
        testExportEnumsMatchTheCFacade()
        testExportDefaults()
        testSizeEstimateFollowsTheDepth()

        testMergeEligibility()
        testMergeDefaultReference()
        testMergeOutputNaming()

        print("\n\(checks) checks, \(failures) failures")
        return failures
    }
}

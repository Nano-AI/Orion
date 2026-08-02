// What the export panel's model promises, checked without a GPU.
//
// These are the parts of bit depth and output sharpening that live in Swift:
// which depth actually reaches the engine, and whether the size estimate
// follows the controls. The pixels themselves are checked in orion-tests and in
// repro/export-depth-and-sharpening.txt — this file is the view model.
//
// Added 2026-08-01 with the two controls.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// JPEG cannot hold sixteen bits, and asking for them anyway is not
    /// harmless.
    ///
    /// ⚠ The failure this catches is invisible in the file. Picking 16-bit,
    /// then switching the format to JPEG, leaves the control reading 16 — and
    /// if that number reached the engine it would render the **wide** graph,
    /// which is the one that does not dither, only for ImageIO to round it to
    /// eight afterwards with nothing left to break up the banding. The JPEG
    /// would look no different in a thumbnail and would band in a smooth sky.
    /// `effectiveDepth` is the guard, so it is what every call site uses.
    static func testJpegNeverAsksForSixteenBits() {
        let s = ExportSettings()

        s.format = .tiff
        s.depth = .sixteen
        report(s.effectiveDepth == .sixteen, "TIFF passes sixteen bits through")

        s.format = .png
        report(s.effectiveDepth == .sixteen, "PNG passes sixteen bits through")

        s.format = .jpeg
        report(s.effectiveDepth == .eight,
               "JPEG is eight bits however the control is set",
               "got \(s.effectiveDepth.rawValue)")
        // And the control itself keeps its value, so switching back restores it
        // rather than silently resetting the photographer's choice.
        report(s.depth == .sixteen, "the control remembers sixteen while JPEG is chosen")
        s.format = .tiff
        report(s.effectiveDepth == .sixteen, "and switching back to TIFF returns it")

        report(!ExportSettings.Format.jpeg.carriesDepth, "the panel greys the control out for JPEG")
        report(ExportSettings.Format.png.carriesDepth, "and offers it for PNG")
        report(ExportSettings.Format.tiff.carriesDepth, "and offers it for TIFF")
    }

    /// The raw values are the contract with `orion.h`. A renumbering on either
    /// side would silently export the wrong thing — 16-bit meaning "Print", for
    /// instance — and nothing in the picture would say so.
    static func testExportEnumsMatchTheCFacade() {
        report(ExportSettings.Depth.eight.rawValue == 8, "ORION_DEPTH_8 is 8")
        report(ExportSettings.Depth.sixteen.rawValue == 16, "ORION_DEPTH_16 is 16")

        report(ExportSettings.Sharpening.none.rawValue == 0, "ORION_SHARPEN_NONE is 0")
        report(ExportSettings.Sharpening.screen.rawValue == 1, "ORION_SHARPEN_SCREEN is 1")
        report(ExportSettings.Sharpening.print.rawValue == 2, "ORION_SHARPEN_PRINT is 2")

        // Zero has to stay a safe default on the C side: a zeroed options struct
        // means "unspecified", and unspecified must not mean "throw half the
        // precision away".
        report(ExportSettings.Depth(rawValue: 0) == nil,
               "zero is not a depth — it means unspecified to the engine")

        report(ExportSettings.Metadata.noLocation.rawValue == 1,
               "ORION_METADATA_NO_LOCATION is 1")
        report(ExportSettings().metadata == .noLocation,
               "and the panel opens on it, so location is stripped unless asked")
    }

    /// Defaults. Each of these is a claim the product makes elsewhere.
    static func testExportDefaults() {
        let s = ExportSettings()
        report(s.depth == .eight,
               "a new export is eight bits — what a file handed to someone should be")
        report(s.sharpening == .none,
               "and unsharpened, because an export going back into an editor must not be")
    }

    /// The estimate has to move when the controls do.
    ///
    /// ⚠ It is only on screen for the fifth of a second before the real encode
    /// lands, which is exactly why it can be wrong for months without anyone
    /// noticing. A sixteen-bit PNG is about twice the file; an estimate that
    /// ignored the depth would show the same number for both and make the
    /// control look like it did nothing.
    static func testSizeEstimateFollowsTheDepth() {
        let s = ExportSettings()
        s.format = .png

        s.depth = .eight
        let narrow = s.estimatedBytes(sourceWidth: 6000, sourceHeight: 4000)
        s.depth = .sixteen
        let wide = s.estimatedBytes(sourceWidth: 6000, sourceHeight: 4000)

        report(wide > narrow,
               "a sixteen-bit PNG is estimated larger than an eight-bit one",
               "got \(wide) vs \(narrow)")

        // TIFF is uncompressed, so this one is arithmetic rather than a guess:
        // six bytes of color a pixel against three.
        s.format = .tiff
        s.depth = .eight
        let tiff8 = s.estimatedBytes(sourceWidth: 1000, sourceHeight: 1000)
        s.depth = .sixteen
        let tiff16 = s.estimatedBytes(sourceWidth: 1000, sourceHeight: 1000)
        report(tiff16 == tiff8 * 2, "an uncompressed sixteen-bit TIFF is twice an eight-bit one",
               "got \(tiff16) vs \(tiff8)")

        // JPEG is an eight-bit container: the control cannot change the file, so
        // it must not change the number either.
        s.format = .jpeg
        s.depth = .eight
        let jpeg8 = s.estimatedBytes(sourceWidth: 1000, sourceHeight: 1000)
        s.depth = .sixteen
        let jpeg16 = s.estimatedBytes(sourceWidth: 1000, sourceHeight: 1000)
        report(jpeg8 == jpeg16, "the depth does not move a JPEG's estimate",
               "got \(jpeg16) vs \(jpeg8)")
    }
}

// A raster matte on disk: the PNG round trip and where the file goes.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// A matte survives being written down and read back.
    ///
    /// ⚠ **The ramp is the test.** Every earlier matte fixture in this file is
    /// binary — a disc, a half-plane — and a binary matte survives a wrong
    /// colour space, a wrong bit depth and a wrong byte order, because 0 and 1
    /// land on 0 and 1 however the curve between them is mangled. Only the
    /// mid-values can tell, and mid-values are exactly what a feathered
    /// selection is made of.
    ///
    /// The specific defect this exists to catch: CoreGraphics colour-manages
    /// greyscale. Write 0.5 through a Gamma-2.2 grey space and read it back as
    /// linear and it comes back near 0.22 or 0.73 — every feathered edge on
    /// every reopened photograph shifted, nothing crashing, nothing to see
    /// unless it is measured. That is the purple cast's shape exactly.
    static func testMattePngRoundTripsItsMidTones() {
        let w = 64, h = 40
        var alpha = [Float](repeating: 0, count: w * h)
        for y in 0..<h {
            for x in 0..<w { alpha[y * w + x] = Float(x) / Float(w - 1) }
        }

        guard let png = MatteStore.encode(alpha, width: w, height: h) else {
            report(false, "a matte encodes to PNG at all")
            return
        }
        report(true, "a matte encodes to PNG at all")

        guard let back = MatteStore.decode(png) else {
            report(false, "the PNG decodes back")
            return
        }
        report(back.width == w && back.height == h,
               "the matte comes back the same size",
               "got \(back.width)x\(back.height)")

        // 1/65535 is one step of the stored precision; 2 steps is the honest
        // tolerance for a round trip through a rounding on each side.
        var worst: Float = 0
        for i in 0..<(w * h) { worst = max(worst, abs(back.alpha[i] - alpha[i])) }
        report(worst <= 3.0 / 65535.0,
               "every value comes back within a couple of steps of 16-bit",
               String(format: "worst error %.6f", worst))

        // Named separately, because the aggregate above could be passed by a
        // curve that is right at the ends and wrong in between if the frame
        // were mostly ends. These are the values a gamma error moves furthest.
        for (x, want) in [(0, 0.0), (16, 16.0 / 63.0), (32, 32.0 / 63.0),
                          (48, 48.0 / 63.0), (63, 1.0)] {
            let got = CGFloat(back.alpha[x])
            near(got, CGFloat(want), 1e-4,
                 String(format: "column %d comes back at %.4f", x, want))
        }

        // ⚠ A matte is not square and never has been: a transposed write reads
        // perfectly on a square fixture and scrambles every real photograph.
        // The ramp varies along x only, so a transpose makes every row equal.
        let rowsDiffer = (0..<h).contains { y in
            abs(back.alpha[y * w + 1] - back.alpha[y * w + 0]) > 1e-4
        }
        report(rowsDiffer, "the ramp still runs along x, so the raster is not transposed")
    }

    /// The raster comes back the way up it went in.
    ///
    /// ⚠ The ramp above cannot see this and it is worth saying why: it varies
    /// along x only, so a **vertical flip leaves it identical**. And a flip is a
    /// live risk here rather than a hypothetical — a `CGBitmapContext` has a
    /// bottom-left origin while both `CGImage` rows and the matte's own frame
    /// coordinates run top-down, so the decode draws across that boundary. A
    /// flipped matte is not a broken-looking mask; it is a plausible selection
    /// of the wrong half of the photograph.
    ///
    /// One bright corner is enough, and it pins both axes at once.
    static func testMatteKeepsItsOrientation() {
        let w = 16, h = 10
        var alpha = [Float](repeating: 0, count: w * h)
        for y in 0..<(h / 2) {
            for x in 0..<(w / 2) { alpha[y * w + x] = 1 }
        }

        guard let png = MatteStore.encode(alpha, width: w, height: h),
              let back = MatteStore.decode(png) else {
            report(false, "the corner fixture round-trips")
            return
        }
        near(CGFloat(back.alpha[0]), 1, 1e-4, "the top-left corner is still set")
        near(CGFloat(back.alpha[w - 1]), 0, 1e-4, "the top-right corner is still clear")
        near(CGFloat(back.alpha[(h - 1) * w]), 0, 1e-4,
             "the bottom-left corner is still clear — a vertical flip fails here")
        near(CGFloat(back.alpha[(h - 1) * w + w - 1]), 0, 1e-4,
             "the bottom-right corner is still clear")
    }

    /// Out-of-range input is clamped, not wrapped.
    ///
    /// The mask kernel saturates, so a producer handing over 1.2 means
    /// "covered". Converting that to UInt16 without clamping wraps it to near
    /// zero — a hole punched through the middle of a selection, in exactly the
    /// places the producer was most confident about.
    static func testMatteClampsRatherThanWraps() {
        let alpha: [Float] = [-0.5, 0, 0.5, 1, 1.5, 2]
        guard let png = MatteStore.encode(alpha, width: 6, height: 1),
              let back = MatteStore.decode(png) else {
            report(false, "an out-of-range matte still encodes")
            return
        }
        near(CGFloat(back.alpha[0]), 0, 1e-4, "-0.5 clamps to 0")
        near(CGFloat(back.alpha[2]), 0.5, 1e-4, "0.5 is untouched")
        near(CGFloat(back.alpha[4]), 1, 1e-4, "1.5 clamps to 1, not to near-zero")
        near(CGFloat(back.alpha[5]), 1, 1e-4, "2.0 clamps to 1")
    }

    /// The reference survives the sidecar, and the sweep keeps what is named.
    ///
    /// ⚠ `MaskComponentState` names its coding keys `Key` rather than
    /// `CodingKeys`, so Swift synthesises the *encoder* from the stored
    /// properties while the decoder reads the hand-written list. A field added
    /// to the struct is therefore written immediately and read back never —
    /// which is what happened to `rangeLo`/`rangeHi`/`rangeSoft` for five
    /// sessions. `matteId` failing that way would mean every saved matte was
    /// written to disk, referenced in the sidecar, and orphaned on the next
    /// open — swept away by this feature's own cleanup.
    static func testMatteReferenceSurvivesTheSidecar() {
        var state = DevelopState()
        var c = MaskComponentState()
        c.kind = 4
        c.matteId = "b3c1f0de-0000-4000-8000-000000000001"
        c.matteSource = "Sky"
        state.maskComponents = [c]

        guard let data = try? JSONEncoder().encode(state),
              let back = try? JSONDecoder().decode(DevelopState.self, from: data),
              let got = back.maskComponents.first else {
            report(false, "a state carrying a matte reference round-trips")
            return
        }
        report(got.matteId == c.matteId, "the matte id survives the sidecar",
               "got \(got.matteId ?? "nil")")
        report(got.matteSource == "Sky", "the matte's producer survives the sidecar",
               "got \(got.matteSource ?? "nil")")
        report(MatteStore.referenced(back.maskComponents) == [c.matteId!],
               "the sweep would keep exactly the file that is referenced")

        // A component with no matte contributes nothing to keep, or every
        // photograph would preserve an id-less entry forever.
        report(MatteStore.referenced([MaskComponentState()]).isEmpty,
               "a component with no matte references no file")
    }

    /// The filename is derived from the photograph, and lands beside its
    /// sidecar rather than inside some other photograph's namespace.
    static func testMatteFileSitsBesideTheSidecar() {
        let photo = URL(fileURLWithPath: "/pics/_PIC8095.ARW")
        let matte = MatteStore.url(photo: photo, id: "abc")
        let sidecar = Sidecar.url(for: photo)
        report(matte.deletingLastPathComponent() == sidecar.deletingLastPathComponent(),
               "a matte is written into the same folder as the sidecar")
        report(matte.lastPathComponent == "_PIC8095.orion-matte-abc.png",
               "the matte's name carries the photograph's basename",
               matte.lastPathComponent)
        // ⚠ Two raws of the same basename and different extensions are one
        // photograph's worth of names apart in Finder and must not collide —
        // they are separate photographs with separate edits.
        let other = MatteStore.url(photo: URL(fileURLWithPath: "/pics/_PIC8096.ARW"),
                                   id: "abc")
        report(matte != other, "two photographs do not share a matte file")
    }

    /// The sweep's three cases. ⚠ It had two, and the missing one leaked files
    /// forever.
    ///
    /// The load path swept only inside the successful-parse branch, guarded by a
    /// comment that is right about a sidecar which *exists and did not parse* —
    /// sweeping against that reads as "nothing is referenced" and would delete a
    /// photograph's work. But a photograph that had simply never been saved fell
    /// into the same `else`, and a matte id lives only in a sidecar, so nothing
    /// on disk could ever reference its PNGs. They accumulated: measured at
    /// **26 orphans, 512 KB** beside one sample frame, oldest three days old,
    /// because pressing Subject mints a fresh UUID and writes a new file.
    ///
    /// ⚠ Tested here rather than through a scenario because `Scenario` cannot
    /// reach it — its `reopen` requires a sidecar to reopen *with*, so the
    /// no-sidecar case is unreachable from the runner. Testing the policy where
    /// the policy lives is also what stopped it being written out twice.
    static func testSweepDistinguishesAbsentFromUnreadable() {
        let fm = FileManager.default

        /// A throwaway folder holding a photograph and three matte files.
        func fixture(_ label: String) -> (photo: URL, dir: URL) {
            let dir = URL(fileURLWithPath: NSTemporaryDirectory())
                .appendingPathComponent("orion-sweep-\(label)-\(UUID().uuidString)")
            try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
            let photo = dir.appendingPathComponent("_PIC0001.ARW")
            fm.createFile(atPath: photo.path, contents: Data([0]))
            for id in ["keep", "drop1", "drop2"] {
                fm.createFile(atPath: MatteStore.url(photo: photo, id: id).path,
                              contents: Data([0]))
            }
            return (photo, dir)
        }
        func mattes(_ photo: URL) -> Int {
            let dir = photo.deletingLastPathComponent()
            let prefix = "_PIC0001.orion-matte-"
            let all = (try? fm.contentsOfDirectory(atPath: dir.path)) ?? []
            return all.filter { $0.hasPrefix(prefix) }.count
        }

        // ── 1. Parsed: keep exactly what it references ─────────────────────
        do {
            let (photo, dir) = fixture("parsed")
            defer { try? fm.removeItem(at: dir) }
            var component = MaskComponentState()
            component.kind = 4
            component.matteId = "keep"
            MatteStore.sweepAfterLoad(photo: photo, parsed: [component])
            report(mattes(photo) == 1,
                   "a parsed sidecar keeps only what it references",
                   "\(mattes(photo)) files left")
            report(fm.fileExists(atPath: MatteStore.url(photo: photo, id: "keep").path),
                   "and the one it keeps is the referenced one")
        }

        // ── 2. Absent: collect everything ─────────────────────────────────
        do {
            let (photo, dir) = fixture("absent")
            defer { try? fm.removeItem(at: dir) }
            MatteStore.sweepAfterLoad(photo: photo, parsed: nil)
            report(mattes(photo) == 0,
                   "no sidecar at all collects every matte — nothing can reference one",
                   "\(mattes(photo)) files left")
        }

        // ── 3. Present but unreadable: collect nothing ────────────────────
        //
        // ⚠ The load-bearing half. Without it the fix above turns a recoverable
        // parse failure into permanent loss of a photograph's selections.
        do {
            let (photo, dir) = fixture("unreadable")
            defer { try? fm.removeItem(at: dir) }
            fm.createFile(atPath: Sidecar.url(for: photo).path,
                          contents: Data("this is not xml".utf8))
            MatteStore.sweepAfterLoad(photo: photo, parsed: nil)
            report(mattes(photo) == 3,
                   "an unreadable sidecar collects nothing",
                   "\(mattes(photo)) files left")
        }
    }
}

// What survives being written to a sidecar and read back.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// ⚠ The backstop under decision #110, and the one thing the compiler cannot
    /// do for `DevelopState`.
    ///
    /// No stored property on `DevelopState` carries an inline default any more,
    /// so a field added the ordinary way breaks the build in two places at once
    /// — `DevelopState.init()` and `Engine.state` — and both errors name it. The
    /// hole that leaves is a field added *with* a default anyway: it gets a
    /// default in the memberwise initializer too, and `Engine.state` can go on
    /// omitting it exactly as it silently omitted `grainAmount` and
    /// `gradeBalance`.
    ///
    /// `Mirror` reports stored properties whatever their defaults, so this is
    /// the check that sees that case. Two halves:
    ///
    ///   1. the roster names every field and no others — a new field is red here
    ///      by name, with the four places it has to be listed;
    ///   2. **every field of `busyState()` is off its default** — which is what
    ///      makes `testEveryFieldSurvivesTheSidecar` mean what it says. A field
    ///      the fixture never sets round-trips its own default through the
    ///      encoder and back and passes for free.
    ///
    /// ⚠ Half 2 found four such fields the day it was written: `perspective*`,
    /// `gradeBalance`, `grain*` and `vignette*` were all at their defaults in
    /// the fixture, so the sidecar round trip had never actually carried one.
    static func testDevelopStateRoster() {
        let mirror = Mirror(reflecting: DevelopState())
        let seen = Set(mirror.children.compactMap(\.label))

        report(seen.count == mirror.children.count,
               "every stored property of DevelopState is labelled",
               "\(seen.count) of \(mirror.children.count)")

        let added = seen.subtracting(DevelopState.fieldRoster).sorted()
        report(added.isEmpty,
               "DevelopState has no field the roster does not know about — "
                   + "a new one goes in Engine.state, Engine.assign, "
                   + "DevelopState.init(), the Codable Key enum and busyState()",
               added.joined(separator: ", "))

        let dropped = DevelopState.fieldRoster.subtracting(seen).sorted()
        report(dropped.isEmpty,
               "the roster names no field that has been removed",
               dropped.joined(separator: ", "))

        // ⚠ `String(describing:)` rather than `==`: `Mirror` hands back `Any`,
        // and the fields are Float, Int32, ToneCurve and four different array
        // element types. The description is the one comparison that spans all of
        // them, and it is being used to answer "did the fixture move this at
        // all", which does not need more precision than that.
        let fresh = Mirror(reflecting: DevelopState()).children.map {
            ($0.label ?? "?", String(describing: $0.value))
        }
        let busy = Mirror(reflecting: busyState()).children.map {
            ($0.label ?? "?", String(describing: $0.value))
        }
        var untouched: [String] = []
        for (a, b) in zip(fresh, busy) where a.1 == b.1 { untouched.append(a.0) }
        report(untouched.isEmpty,
               "busyState() moves every field off its default, so the sidecar "
                   + "round trip can actually see each one",
               untouched.joined(separator: ", "))
    }

    /// A sidecar missing keys still restores the ones it has.
    ///
    /// Swift's synthesized decoder throws on a missing key rather than falling
    /// back to the property's default, and `Engine.restore` swallows that with a
    /// `try?`. Adding one field to `DevelopState` would therefore have discarded
    /// *every* adjustment in *every* sidecar already on disk, and the photo
    /// would simply have opened unedited with nothing said.
    static func testSidecarSurvivesAMissingField() {
        func decode(_ json: String) -> DevelopState? {
            guard let data = json.data(using: .utf8) else { return nil }
            return try? JSONDecoder().decode(DevelopState.self, from: data)
        }

        // A sidecar from a build that had only three of the fields.
        guard let sparse = decode(#"{"exposureEv":1.5,"contrast":1.4,"cropW":0.5}"#) else {
            report(false, "a sparse sidecar decodes at all")
            return
        }
        report(true, "a sparse sidecar decodes at all")
        near(CGFloat(sparse.exposureEv), 1.5, 1e-6, "the exposure it carried survives")
        near(CGFloat(sparse.contrast), 1.4, 1e-6, "the contrast it carried survives")
        near(CGFloat(sparse.cropW), 0.5, 1e-6, "the crop it carried survives")
        near(CGFloat(sparse.sharpenRadius), 1.0, 1e-6,
             "a field it never had falls back to its default, not to zero")
        report(sparse.hueShift.count == 8, "the bands are still the right length")

        // Empty and malformed both have to land on the defaults rather than
        // throwing, or one bad sidecar makes a photo look unopenable.
        guard let empty = decode("{}") else {
            report(false, "an empty sidecar decodes")
            return
        }
        report(true, "an empty sidecar decodes")
        report(empty == DevelopState(), "an empty sidecar is exactly the defaults")

        if let junk = decode(#"{"exposureEv":"not a number","tint":0.3}"#) {
            near(CGFloat(junk.tint), 0.3, 1e-6, "a bad field does not poison a good one")
            near(CGFloat(junk.exposureEv), 0, 1e-6, "a bad field falls back to its default")
        } else {
            report(false, "a sidecar with one bad field still decodes")
        }

        // A band of the wrong length would index out of bounds in the panel.
        if let short = decode(#"{"hueShift":[0.1,0.2]}"#) {
            report(short.hueShift.count == 8, "a short band array is refused, not trusted")
        } else {
            report(false, "a sidecar with a short band array still decodes")
        }

        // The pre-rename spelling. A photo finished before the interface moved
        // to American spelling must not lose its noise reduction.
        if let old = decode(#"{"denoiseColour":2.4}"#) {
            near(CGFloat(old.denoiseColor), 2.4, 1e-6,
                 "a sidecar written as denoiseColour still restores")
        } else {
            report(false, "a pre-rename sidecar decodes")
        }

        // And a full round trip has to be exact, or undo and the sidecar would
        // disagree about what was saved.
        var full = DevelopState()
        full.exposureEv = -1.25
        full.denoiseColor = 3.1
        full.curve.master = [CurvePoint(x: 0, y: 0.1), CurvePoint(x: 1, y: 0.9)]
        full.satShift[3] = 0.42
        if let data = try? JSONEncoder().encode(full),
           let back = try? JSONDecoder().decode(DevelopState.self, from: data) {
            report(back == full, "a full state round-trips unchanged")
        } else {
            report(false, "a full state round-trips at all")
        }
    }

    /// The mask group in the sidecar, and the single mask that came before it.
    ///
    /// The migration is the load-bearing half. Every photo finished between the
    /// gradient masks landing and mask groups landing has the flat `maskKind`
    /// keys and no `maskComponents` — and `localExposureEv` kept its own name
    /// through the change, so dropping the mask would not open those photos
    /// unedited. It would open them with the local exposure applied to the
    /// *whole frame*, which looks like a working editor and is worse than a
    /// crash.
    static func testMaskGroupSidecar() {
        func decode(_ json: String) -> DevelopState? {
            guard let data = json.data(using: .utf8) else { return nil }
            return try? JSONDecoder().decode(DevelopState.self, from: data)
        }

        // ── A pre-group sidecar: one linear mask, lifted into one component ──
        let legacy = #"""
        {"exposureEv":2.6,"localExposureEv":-1.6,"maskKind":1,"maskCentreX":0.46,
         "maskCentreY":0.44,"maskAngle":1.05,"maskLength":0.55,"maskInvert":true}
        """#
        if let s = decode(legacy) {
            report(s.maskComponents.count == 1,
                   "a pre-group sidecar's single mask becomes one component",
                   "\(s.maskComponents.count) components")
            if let m = s.maskComponents.first {
                report(m.kind == 1, "and keeps its kind")
                near(CGFloat(m.centerX), 0.46, 1e-6, "and its centre x")
                near(CGFloat(m.centerY), 0.44, 1e-6, "and its centre y")
                near(CGFloat(m.angle), 1.05, 1e-6, "and its angle")
                near(CGFloat(m.length), 0.55, 1e-6, "and its length")
                report(m.invert, "and its invert")
                report(m.compose == 0,
                       "and folds with add, the only op a single mask can have meant")
            }
            near(CGFloat(s.layers.first?.exposureEv ?? 0), -1.6, 1e-6,
                 "and the local exposure it was applying through that mask")
        } else {
            report(false, "a pre-group sidecar decodes at all")
        }

        // ── ⚠ A pre-LAYER sidecar: the single local set becomes layer 1 ──────
        //
        // Every photograph edited before layers existed carries exactly one set
        // of local adjustments, under the scalar keys `localExposureEv`,
        // `localContrast`, `localSaturation`, `localWarmth` and `localTint`.
        // Those keys no longer exist as stored properties, so the synthesised
        // encoder never writes them again — and if the decoder had stopped
        // reading them too, every local grade ever made would have opened at
        // zero, silently, on a photograph that still had its mask.
        //
        // This is the same shape as the two migrations this file has already
        // paid for: `localExposureEv` keeping its name through the group
        // change, and `MaskComponentState` encoding three range fields it never
        // decoded.
        let legacyLocal = #"""
        {"exposureEv":1.0,"maskKind":2,"localExposureEv":-1.6,"localContrast":0.4,
         "localSaturation":-0.7,"localWarmth":0.25,"localTint":-0.15}
        """#
        if let s = decode(legacyLocal) {
            report(s.layers.count == 1,
                   "a pre-layer sidecar produces exactly one layer",
                   "\(s.layers.count)")
            if let l = s.layers.first {
                near(CGFloat(l.exposureEv), -1.6, 1e-6, "carrying its local exposure")
                near(CGFloat(l.contrast), 0.4, 1e-6, "its contrast")
                near(CGFloat(l.saturation), -0.7, 1e-6, "its saturation")
                near(CGFloat(l.warmth), 0.25, 1e-6, "its warmth")
                near(CGFloat(l.tint), -0.15, 1e-6, "and its tint")
            }
        } else {
            report(false, "a pre-layer sidecar decodes")
        }

        // ⚠ And a sidecar carrying **both** — which a newer build writes,
        // because the encoder is synthesised from the stored properties and the
        // legacy keys may still be present from a hand edit — must prefer the
        // layers. Preferring the scalars would silently discard layers 2 and up,
        // which is the exact failure the mask-group migration had.
        let bothForms = #"""
        {"localExposureEv":-1.6,
         "layers":[{"exposureEv":0.5,"contrast":0,"saturation":0,"warmth":0,"tint":0},
                   {"exposureEv":-2.0,"contrast":0,"saturation":0,"warmth":0,"tint":0}]}
        """#
        if let s = decode(bothForms) {
            report(s.layers.count == 2,
                   "a sidecar with both forms keeps every layer",
                   "\(s.layers.count)")
            near(CGFloat(s.layers.first?.exposureEv ?? 0), 0.5, 1e-6,
                 "and takes the layer list rather than the legacy scalar")
        } else {
            report(false, "a sidecar with both forms decodes")
        }

        // A pre-group brush, whose stroke lived beside the mask rather than in it.
        let legacyBrush = #"""
        {"maskKind":3,"brushRadius":0.07,"brushFlow":0.55,"brushHardness":0.45,
         "brushStroke":[0.2,0.66,0.34,0.6]}
        """#
        if let s = decode(legacyBrush), let m = s.maskComponents.first {
            report(m.kind == 3 && m.brushStroke.count == 4,
                   "a pre-group brush's stroke moves inside its component",
                   "\(m.brushStroke.count) values")
            near(CGFloat(m.brushRadius), 0.07, 1e-6, "and the nib comes with it")
        } else {
            report(false, "a pre-group brush sidecar decodes")
        }

        // ── maskKind 0 was "no mask", which is an empty group, not an off row ──
        //
        // A live component that happens to cover nothing is not the same thing:
        // the engine would run a pass for it and `mask_count` would be one.
        if let s = decode(#"{"maskKind":0,"maskCentreX":0.3}"#) {
            report(s.maskComponents.isEmpty,
                   "a pre-group sidecar with no mask decodes to an empty group",
                   "\(s.maskComponents.count) components")
        } else {
            report(false, "a pre-group sidecar with no mask decodes")
        }

        // ── A component list present wins over legacy keys ──────────────────
        //
        // A file holding both was written by a newer build, and its legacy keys
        // are whatever that build's first row happened to be. Preferring them
        // would silently discard rows two and up.
        let both = #"""
        {"maskKind":1,"maskCentreX":0.9,
         "maskComponents":[{"kind":2,"centreX":0.25},{"kind":3,"compose":1}]}
        """#
        if let s = decode(both) {
            report(s.maskComponents.count == 2,
                   "a list plus legacy keys keeps the list, both rows",
                   "\(s.maskComponents.count) components")
            if s.maskComponents.count == 2 {
                report(s.maskComponents[0].kind == 2 && s.maskComponents[1].kind == 3,
                       "in the order it was written")
                report(s.maskComponents[1].compose == 1,
                       "with the second row's subtract intact")
                near(CGFloat(s.maskComponents[0].centerX), 0.25, 1e-6,
                     "and the list's geometry, not the legacy key's")
            }
        } else {
            report(false, "a sidecar with both forms decodes")
        }

        // ── A component missing fields falls back per field ─────────────────
        if let s = decode(#"{"maskComponents":[{"kind":2}]}"#),
           let m = s.maskComponents.first {
            near(CGFloat(m.roundness), 2.0, 1e-6,
                 "a component's absent field is its default, not zero")
            near(CGFloat(m.brushFlow), 0.5, 1e-6, "for the nib too")
        } else {
            report(false, "a sparse component decodes")
        }

        // An off row is dropped on the way in. It cannot render anything, and
        // keeping it would put a row in the panel that does nothing.
        if let s = decode(#"{"maskComponents":[{"kind":1},{"kind":0},{"kind":3}]}"#) {
            report(s.maskComponents.count == 2,
                   "an off component is dropped rather than listed",
                   "\(s.maskComponents.count) components")
            report(s.maskComponents.last?.kind == 3, "and the rows after it survive")
        } else {
            report(false, "a list with an off component decodes")
        }

        // ── Round trip, with a full group ───────────────────────────────────
        var full = DevelopState()
        var a = MaskComponentState()
        a.kind = 1; a.centerX = 0.3; a.angle = 0.8; a.length = 0.7
        var b = MaskComponentState()
        b.kind = 3; b.compose = 1; b.brushStroke = [0.1, 0.2, 0.3, 0.4]
        var c = MaskComponentState()
        c.kind = 2; c.compose = 2; c.invert = true; c.roundness = 4
        full.maskComponents = [a, b, c]
        full.layers = [LocalAdjustState(exposureEv: 1.75)]
        if let data = try? JSONEncoder().encode(full),
           let back = try? JSONDecoder().decode(DevelopState.self, from: data) {
            report(back == full, "a three-component group round-trips unchanged")
            report(back.maskComponents.map(\.compose) == [0, 1, 2],
                   "with every op in place — the fold order is the edit")
        } else {
            report(false, "a group round-trips at all")
        }
    }

    /// Edit, quit, reopen — the adjustments have to still be there.
    ///
    /// They were not. `saveDevelop` ran only when *switching* photos, so the
    /// common case — open one file, work on it, ⌘Q — lost everything. The
    /// sidecar round-trip above was passing the whole time, which is the point:
    /// a correct mechanism nobody triggers reads exactly like a working feature.
    ///
    /// The deferral is handed in, so the coalescing is checked by firing it
    /// rather than by sleeping.
    static func testEditsSurviveAQuit() {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-autosave-\(ProcessInfo.processInfo.processIdentifier)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let a = dir.appendingPathComponent("a.ARW")
        let b = dir.appendingPathComponent("b.ARW")

        /// What is on disk for a photo, decoded back the way `Editor.load` does.
        func onDisk(_ photo: URL) -> DevelopState? {
            guard let data = Sidecar.read(for: photo)?.develop else { return nil }
            return try? JSONDecoder().decode(DevelopState.self, from: data)
        }

        func edited(_ ev: Float) -> DevelopState {
            var s = DevelopState()
            s.exposureEv = ev
            return s
        }

        // --- The invariant, through the real sidecar on a real file. ---
        var fire: (() -> Void)?
        let save = Autosave(deferral: { fire = $0 })

        save.begin(url: a, saved: DevelopState())
        save.note(edited(1.5))
        report(onDisk(a) == nil, "nothing is written before the writes coalesce")
        report(save.isDirty, "the write is owed, not forgotten")

        fire?()
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), 1.5, 1e-6,
             "the edit reaches the sidecar without anybody asking")
        report(!save.isDirty, "and is not owed twice")

        // The ⌘Q that lands inside the window. This is the case that was losing
        // an entire session's work.
        save.note(edited(-2.25))
        report(onDisk(a)?.exposureEv == 1.5, "a later edit has not landed yet")
        save.flush()
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), -2.25, 1e-6,
             "quitting inside the coalescing window still writes")

        // Ratings live in the same file and must survive an edit write.
        Sidecar.merge(into: a) { $0.rating = 4 }
        save.note(edited(0.75))
        save.flush()
        report(Sidecar.read(for: a)?.rating == 4, "an autosave keeps the rating")
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), 0.75, 1e-6, "and carries the edit")

        // --- Nothing may be misfiled. ---
        // A write queued for a and not yet fired, then the photo switches: it
        // belongs to a. Reading the engine at fire time instead would put a's
        // work in b's sidecar, because `current` moves ~50 ms before the decode
        // finishes.
        save.begin(url: a, saved: edited(0.75))
        save.note(edited(3.0))
        save.begin(url: b, saved: DevelopState())
        near(CGFloat(onDisk(a)?.exposureEv ?? 0), 3.0, 1e-6,
             "a pending write follows the photo it was queued for")
        report(onDisk(b) == nil, "and does not land on the photo that arrived")

        fire?()   // the stale timer from a's note must not write b's sidecar
        report(onDisk(b) == nil, "a timer left over from the previous photo writes nothing")

        // --- Restoring is not an edit. ---
        // Every open pushes renders; if those counted, opening a photo would
        // rewrite its sidecar, and opening a folder would rewrite all of them.
        let opened = edited(-1.0)
        save.begin(url: b, saved: opened)
        save.note(opened)
        save.flush()
        report(onDisk(b) == nil, "opening a photo does not write back what it just read")

        // --- Notes while nothing is open are dropped, not queued. ---
        save.stop()
        save.note(edited(9.0))
        save.flush()
        report(onDisk(b) == nil, "an edit with no photo in hand goes nowhere")

        // --- Last note wins. ---
        // `Engine.captureOriginal` applies a neutral state and then the real
        // one, back to back. A queue that appended would persist the neutral.
        save.begin(url: b, saved: opened)
        save.note(DevelopState())        // the neutral compare render
        save.note(edited(2.0))           // the edit, restored immediately after
        save.flush()
        near(CGFloat(onDisk(b)?.exposureEv ?? 0), 2.0, 1e-6,
             "the last state noted is the one written")
    }

    /// Escaping compounds unless reading undoes it.
    ///
    /// `Library.persist` reads, modifies and rewrites the whole sidecar on
    /// every rating change, so a label written as `R&amp;D` and read back as
    /// `R&amp;D` is escaped again on the next save. One save per gained layer,
    /// and the field is read from foreign sidecars — Lightroom writes labels.
    static func testSidecarEscapingDoesNotCompound() {
        let cases = ["R&D", "a < b", "a > b", "say \"hi\"", "&amp;", "plain",
                     "&lt;not a tag&gt;"]
        for original in cases {
            var text = original
            // Three round trips: one save is not enough to show compounding.
            for _ in 0..<3 {
                text = Sidecar.unescape(Sidecar.escapeForTests(text))
            }
            report(text == original,
                   "\(original) survives three save/load cycles unchanged",
                   text == original ? "" : "became \(text)")
        }
    }
}

import Foundation
import Metal
import MetalKit
import AppKit

/// The photograph coming in and going out: open, restore, presets, versions,
/// the creative LUT, and export.
///
/// Everything here moves a *whole* edit at once rather than one control, which
/// is why they share a file — each one suspends, assigns, unsuspends and
/// renders, and getting that sequence wrong is the same bug in all of them.

extension Engine {

    func loadLut(path: String, displayName: String) {
        guard let handle else { return }

        let status = orion_engine_load_lut(handle, path)
        guard status == ORION_OK else {
            lutError = errorText(status)
            return
        }

        lutError = nil
        var buffer = [CChar](repeating: 0, count: 256)
        orion_engine_lut_title(handle, &buffer, Int32(buffer.count))
        let title = String(cString: buffer)
        // A .cube is not obliged to carry a TITLE, and most do not. The file's
        // own name is what the user picked and what they will look for.
        lutName = title.isEmpty ? displayName : title
        pushAndRender()
    }

    func clearLut() {
        guard let handle else { return }
        orion_engine_clear_lut(handle)
        lutName = ""
        lutError = nil
        pushAndRender()
    }

    func open(path: String) throws {
        guard let handle else { return }

        let status = orion_engine_open_raw(handle, path)
        guard status == ORION_OK else {
            throw Failure.open(errorText(status))
        }

        // ⚠ Only assigned when the call answered. Discarding the status wrote
        // the zeroed out-parameters over the size, and every consumer —
        // `frameAspect`, the crop normalization, the export's longest edge —
        // then worked against 0 × 0 on a photograph that had opened fine.
        var w: UInt32 = 0, h: UInt32 = 0
        let sized = orion_engine_image_size(handle, &w, &h)
        guard sized == ORION_OK else { throw Failure.open(errorText(sized)) }
        imageWidth = w
        imageHeight = h
        camera = String(cString: orion_engine_camera(handle))
        photoLensName = String(cString: orion_engine_lens(handle))
        // An answer about the photograph being left. `MatteStore.restore` fills
        // it again for the one arriving.
        missingMattes = []

        // Reset to the camera's own settings before marking loaded, so the
        // didSet observers don't each trigger a render on a half-set model.
        //
        // ⚠ **Started here, not at `assign(defaults)` below.** `lensChoice`'s
        // own `didSet` pushes a render too (it is not a plain adjustment — see
        // its doc comment), and it used to sit above this guard: opening a
        // second photo after ever hand-picking a lens on the first fired that
        // render with the *outgoing* photo's whole `DevelopState` — contrast,
        // exposure, every slider — against the *incoming* photo's pixels,
        // already swapped in by `orion_engine_open_raw` above. Reproduced with
        // `repro/lens-choice-leaks-a-render.txt`: `set contrast 2.80` on photo
        // one, `lens <name>`, `open` photo two — the log shows a render at
        // contrast 2.8 sandwiched between the correct 1.45 default before it
        // and the correct one after. One extra render most of the time, and
        // whatever the previous photo's edit happened to be whenever the
        // canvas is not covered by `showPlaceholder`'s thumbnail long enough to
        // hide it — the shape of "the photo opens looking right, then a moment
        // later the contrast and exposure are wrong."
        suspended = true

        // A choice belongs to the photograph it was made on; the engine clears
        // its own on open, and this keeps the two in step.
        lensChoice = ""
        refreshLensProfile()

        // The held original is the *previous* photo's unedited render. Keeping
        // it means compare shows one picture against another one entirely,
        // which is worse than showing nothing. Compare itself stays on across a
        // switch — that is the point of it while culling — so the render below
        // captures this photo's own original through `refreshOriginal`.
        originalTexture = nil
        originalGeometry = nil
        maskColorSwatch = nil

        defaults = asShotState()
        assign(defaults)
        suspended = false

        isLoaded = true
        history.reset(to: state)
        log.opened(path, state: state)
        pushAndRender()
    }

    /// Restores a state saved to a sidecar. Returns false when the blob would
    /// not decode, and the photograph is then left openable — a sidecar written
    /// by a newer build must not make a file unopenable.
    ///
    /// ⚠ **The `false` is load bearing and used to be a bare `return`.** Two
    /// things downstream read "the sidecar had a develop blob" and took it to
    /// mean "and it is now in the engine":
    ///
    /// - `MatteStore.sweepAfterLoad` was handed `engine.maskComponents`, which
    ///   after a failed decode is the *default* empty list. The sweep then
    ///   deleted every matte PNG beside the photograph. That is #87's lesson
    ///   reached by a second route, and it is not recoverable — the model has
    ///   to be run again, and Vision's answer moves between OS releases.
    /// - `Autosave.begin` was armed with the default state as its baseline, so
    ///   the first slider tick wrote a blank develop blob over the sidecar that
    ///   had only failed to *parse*. An hour's work, gone on a keystroke.
    ///
    /// Both callers now branch on the answer, and both say so out loud.
    @discardableResult
    func restore(encoded: Data) -> Bool {
        guard let decoded = try? JSONDecoder().decode(DevelopState.self, from: encoded) else {
            let why = "the saved edits could not be read"
            lastFailure = why
            FileHandle.standardError.write(Data("orion: restore failed — \(why)\n".utf8))
            return false
        }
        // A sidecar from before frame anchoring converts on the way in, under
        // its own persisted geometry. No rewrite pass: the file adopts frame
        // numbers and the marker on the next edit's save, and converts afresh
        // — deterministically — on every open until then.
        let s = migratedToFrameSpace(decoded)
        suspended = true
        assign(s)
        suspended = false
        history.reset(to: s)
        pushAndRender()
        return true
    }

    /// Applies a preset over the current state, as one history entry.
    ///
    /// One entry, not one per field: a preset is a single act from the
    /// photographer's side, and undo should take it back in a single step the
    /// way it takes back a brush stroke.
    func apply(preset: Preset) {
        guard isLoaded else { return }
        let next = preset.applied(to: state)
        suspended = true
        assign(next)
        suspended = false
        pushAndRender()
        history.record(state, label: preset.name)
        log.record("preset \(preset.name)")
        log.committed(state, label: preset.name)
    }

    /// Restores a saved version of this photograph's edit.
    ///
    /// ⚠ **`history.record`, not `history.reset`.** `restore(encoded:)` resets,
    /// which is right when a photograph is *opening* — there is no earlier
    /// state to walk back to. Resetting here would make a restore the one act
    /// in the program that cannot be undone, and it would do it to the act most
    /// likely to have been a mistake. One entry, like a preset: ⌘Z puts the
    /// working edit back in a single step. `SnapshotStore.restore` covers the
    /// other half — the session that ends before the mistake is noticed.
    ///
    /// ⚠ **And the mattes, which are not in `DevelopState`.** A version can
    /// name a raster mask; assigning the state alone would restore the row and
    /// leave the *previous* state's raster uploaded under the same index, so a
    /// mask would come back covering the wrong thing rather than nothing.
    /// `restoreMattes` records whatever it cannot read in `missingMattes`, and
    /// the panel says so.
    func restore(snapshot: Snapshot, photo: URL?) {
        guard isLoaded else { return }
        // Converted at restore, never at store: `SnapshotStore` re-encoding a
        // legacy version it never restored keeps that version's `maskSpace: 0`
        // and untouched numbers, so the file stays self-describing. Once
        // through here, every history entry this session records is
        // frame-space — undo across the migration boundary cannot resurrect
        // display numbers.
        let restored = migratedToFrameSpace(snapshot.state)
        suspended = true
        assign(restored)
        suspended = false
        pushAndRender()
        if let photo { restoreMattes(photo: photo) }
        history.record(state, label: "Version \(snapshot.name)")
        log.record("snapshot restore \(snapshot.name)")
        log.committed(state, label: "Version \(snapshot.name)")
    }

    /// Returns every adjustment to its default, with white balance back to
    /// what the camera chose. One push, one render.
    func resetEdits() {
        guard isLoaded else { return }
        suspended = true
        assign(defaults)
        suspended = false
        pushAndRender()
        history.record(state, label: "Reset"); log.committed(state, label: "Reset")
    }

    /// `depth` is `OrionBitDepth` — 8 or 16, and 0 keeps the engine's default of
    /// sixteen. ⚠ Pass what the *file* will hold, not what the control shows:
    /// eight renders the narrow, dithered graph and sixteen renders the wide
    /// one, so a JPEG asked for at sixteen would skip the dither and then be
    /// rounded to eight anyway. `ExportSettings.effectiveDepth` is that value.
    func export(to path: String, quality: Float = 0.92,
                maxDimension: UInt32 = 0, space: Int32 = 0,
                rating: Int32 = -1, metadata: Int32 = 1,
                depth: Int32 = 0, sharpen: Int32 = 0) throws {
        // ⚠ Throws rather than returning. A bare `return` here is an export
        // that reports success and writes no file — and the person who finds
        // that out is whoever was sent the photograph. `isLoaded` is checked
        // for the same reason: the engine exists from launch, so exporting with
        // nothing open used to hand the facade a graph with no source and the
        // failure, if any, came back through a path nobody read.
        guard let handle, isLoaded else { throw Failure.export("no photo is open") }

        // The coverage overlay is a viewing aid. Exporting with it on would
        // write a red-tinted photograph and nothing in the file would say why,
        // so it is forced off around the write and restored after — including
        // when the export throws.
        let wasOverlay = maskOverlay
        if wasOverlay { maskOverlay = false }
        defer { if wasOverlay { maskOverlay = true } }

        var options = OrionExportOptions(format: -1, quality: quality,
                                         max_dimension: maxDimension, space: space,
                                         rating: rating, metadata: metadata,
                                         bit_depth: depth, sharpen: sharpen)
        let status = orion_engine_export(handle, path, &options)
        guard status == ORION_OK else { throw Failure.export(errorText(status)) }
    }

    /// Encodes with these options and reports the byte count without writing.
    /// Real work — a 24 MP JPEG is about a sixth of a second — so callers
    /// debounce it.
    func exportedSize(format: Int32, quality: Float, maxDimension: UInt32,
                      space: Int32 = 0, depth: Int32 = 0, sharpen: Int32 = 0) -> Int? {
        guard let handle else { return nil }
        // No rating and no metadata source: the estimate measures the pixels,
        // and a few hundred bytes of EXIF is below its resolution anyway. The
        // depth and the sharpening are not below its resolution and are passed.
        var options = OrionExportOptions(format: format, quality: quality,
                                         max_dimension: maxDimension, space: space,
                                         rating: -1, metadata: 1,
                                         bit_depth: depth, sharpen: sharpen)
        var bytes: UInt64 = 0
        guard orion_engine_export_size(handle, &options, &bytes) == ORION_OK else {
            return nil
        }
        return Int(bytes)
    }
}

extension Engine {

    /// Re-read whatever profile the engine currently holds.
    ///
    /// ⚠ **Read back rather than assumed**, and that is the point of it being
    /// one function called from two places. A hand-chosen name that no longer
    /// resolves leaves the previous profile standing (`Engine::setLensChoice`
    /// reports false and changes nothing), so the interface has to show what is
    /// *applied* rather than what was asked for. Building the display name in
    /// two places is also how the picker's list and the panel's label would
    /// quietly start disagreeing about the same lens.
    func refreshLensProfile() {
        guard let handle else {
            lensProfileName = ""
            lensProfileApproximate = false
            return
        }
        var profile = OrionLensProfile()
        if orion_engine_lens_profile(handle, &profile) == ORION_OK, profile.found != 0 {
            let name = withUnsafeBytes(of: profile.lens) { raw in
                String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
            }
            let maker = withUnsafeBytes(of: profile.maker) { raw in
                String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
            }
            lensProfileName = name.hasPrefix(maker) || maker.isEmpty
                ? name : "\(maker) \(name)"
            lensProfileApproximate = profile.approximate != 0
        } else {
            lensProfileName = ""
            lensProfileApproximate = false
        }
    }

    /// Seat the hand-chosen lens, then read back what the engine settled on.
    func applyLensChoice() {
        guard let handle else { return }
        _ = orion_engine_set_lens_choice(handle, lensChoice)
        refreshLensProfile()
        // ⚠ A refused name leaves the engine's own choice where it was, so the
        // property is corrected to match rather than left claiming a lens that
        // is not applied.
        lensChoice = String(cString: orion_engine_lens_choice(handle))
    }

    /// Every lens the bundled database carries, sorted, for a picker.
    static func lensCatalogue() -> [String] {
        let n = Int(orion_lens_name_count())
        var out: [String] = []
        out.reserveCapacity(n)
        for i in 0..<n { out.append(String(cString: orion_lens_name_at(Int32(i)))) }
        return out
    }
}

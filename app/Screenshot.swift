import AppKit
import Metal
import SwiftUI

/// Renders the real interface to a PNG, offscreen, with no window on screen.
///
/// Built because `screencapture` needs Screen Recording permission that a
/// terminal does not have, and because a screenshot you have to take by hand is
/// a screenshot nobody takes. This one is a command:
///
/// ```
/// Orion.app/Contents/MacOS/Orion --screenshot out.png --photo x.ARW --scene crop
/// ```
///
/// What it does and does not prove: the view hierarchy is the shipping one, so
/// layout, type, spacing and color are real. The canvas is the engine's own
/// developed output, read back through the export path, drawn as a still rather
/// than through `MTKView` — AppKit's `cacheDisplay` does not capture a Metal
/// layer. Canvas-specific geometry stays the viewport suite's job.
///
/// ## ⚠ Three scenes are checks rather than pictures
///
/// Each was written against a named mutation that deleted shipped interface with
/// every check in the repository green (decision #125). Two of the three exit
/// nonzero by themselves; the third is a frame, compared byte for byte against
/// the same scene from the binary before the change, which is how every frame
/// here is read.
///
/// ```
/// --scene detail-tail    --photo x.ARW   the Detail panel scrolled to its end
/// --scene render-failed  --photo x.ARW   the status line's failure warning
/// --scene menu                           the real menu bar, 26 commands
/// ```
///
/// | Scene | Fires when | Exits nonzero |
/// |---|---|---|
/// | `detail-tail` | anything below the fold in Detail is deleted or moved | when nothing overflows the panel column |
/// | `render-failed` | the footer stops drawing `engine.lastFailure` | no — the frame differs |
/// | `menu` | a `PhotoCommands` item is deleted or renamed | when a command is missing |
///
/// ## Where the rest of it lives
///
/// This file is the command line and the driver — what a run *is*, in order.
/// The four extensions are the four things a change to this harness is
/// usually about, so which file is answered by what the change is:
///
/// ```
/// Screenshot+Scenes.swift    a state to photograph  — add a scene here
/// Screenshot+Checks.swift    a scene that asserts   — add a check-scene here
/// Screenshot+Measure.swift   pixels out, as pictures and as numbers
/// Screenshot+Render.swift    a SwiftUI hierarchy to a PNG, offscreen
/// ```
///
/// ⚠ **Swift's `private` is file-scoped, so the ceiling costs encapsulation
/// here as it did in `Engine.swift` (decision #117).** Eleven members that were
/// `private` to one 1,196-line file are now internal to the app target —
/// `fail`, `relaunched`, `checkMenu`, `scrolls`, `minimumHeight`, `tab`,
/// `snapshots`, `apply`, `developed`, `measure` and `render` — because `run`
/// calls across every seam and a driver is what a driver does. Four stayed
/// private, each used only where it lives: `requiredCommands`, `panelClip`,
/// `scrollToEnd` and the `CGImage` overload of `regionStats`. This is recorded
/// so the next reader does not read it as carelessness.
enum Screenshot {

    struct Options {
        var output = "orion.png"
        var photo: String?
        var scene = "light"
        var size = CGSize(width: 1680, height: 1050)
        /// Regions to report statistics for, normalized. Repeat the flag for
        /// several. Repeating beats one-region-per-run because each run pays a
        /// RAW decode and a full render, and a calibration sweep is dozens of
        /// runs.
        var measure: [CGRect] = []
        /// Exposure offset in EV, applied on top of whatever the scene sets.
        /// Exists so a calibration sweep is a loop over one flag rather than a
        /// scene per value.
        var exposure: Float?
        /// Base contrast override, for the same reason.
        var contrast: Float?
        /// Runs the Auto button, through the same facade call the panel uses.
        ///
        /// The bench has an auto-enhance probe, but it drives the policy
        /// directly — so the two disagreeing is invisible to it. This flag is
        /// the app's own path, which is where a reported failure was.
        var auto = false
        /// Repeats Auto, to catch a control that only misbehaves when the
        /// measurement it starts from is already an auto-enhanced frame.
        var autoTimes = 1
        /// Tone endpoints, set directly. The pair is what a reported black frame
        /// pointed at: whites low and blacks high put the white point below the
        /// black point, and nothing in the interface stops them crossing.
        var whites: Float?
        var blacks: Float?
        /// True once `--size` has been given. A scene may ask for a window of
        /// its own (see `minimumHeight`), and it must not overrule a size the
        /// caller stated out loud.
        var sizeExplicit = false
    }

    /// Parses the command line. Returns nil when this is an ordinary launch.
    ///
    /// ⚠ Also nil on the *second* pass. `--scene menu` hands the process back to
    /// `OrionApp` (see `checkMenu`), which runs `App.init` again with the same
    /// `argv` — and without this the harness would call itself forever.
    static func options(_ arguments: [String]) -> Options? {
        guard !relaunched else { return nil }
        guard arguments.contains("--screenshot") else { return nil }

        var o = Options()
        var i = 0
        while i < arguments.count {
            let flag = arguments[i]
            let next: String? = i + 1 < arguments.count ? arguments[i + 1] : nil
            switch flag {
            case "--screenshot": if let next { o.output = next; i += 1 }
            case "--photo":      if let next { o.photo = next; i += 1 }
            case "--scene":      if let next { o.scene = next; i += 1 }
            case "--measure":
                if let next {
                    let n = next.split(separator: ",").compactMap { Double($0) }
                    if n.count == 4 {
                        o.measure.append(CGRect(x: n[0], y: n[1],
                                                width: n[2], height: n[3]))
                    }
                    i += 1
                }
            case "--exposure":
                if let next, let ev = Float(next) { o.exposure = ev; i += 1 }
            case "--contrast":
                if let next, let c = Float(next) { o.contrast = c; i += 1 }
            case "--whites":
                if let next, let v = Float(next) { o.whites = v; i += 1 }
            case "--blacks":
                if let next, let v = Float(next) { o.blacks = v; i += 1 }
            case "--auto":
                o.auto = true
                // An optional count, so `--auto 2` asks what a second press does.
                if let next, let n = Int(next), n > 0 { o.autoTimes = n; i += 1 }
            case "--size":
                if let next {
                    let parts = next.split(separator: "x").compactMap { Double($0) }
                    if parts.count == 2 {
                        o.size = CGSize(width: parts[0], height: parts[1])
                        o.sizeExplicit = true
                    }
                    i += 1
                }
            default: break
            }
            i += 1
        }
        return o
    }

    /// Runs the capture and exits. Never returns.
    static func run(_ options: Options) -> Never {
        var o = options

        // Not a picture: the one scene that has to be the real `Scene`.
        if o.scene == "menu" { checkMenu() }

        if !o.sizeExplicit, let h = minimumHeight(o.scene) {
            o.size.height = max(o.size.height, h)
        }

        // A background accessory app: no Dock icon, no menu bar, no window.
        NSApplication.shared.setActivationPolicy(.accessory)

        guard let engine = try? Engine() else {
            fail("could not start the engine")
        }

        if let photo = o.photo {
            do { try engine.open(path: photo) }
            catch { fail("could not open \(photo) — \(error.localizedDescription)") }
            apply(scene: o.scene, to: engine)
            if let ev = o.exposure { engine.exposureEv = ev }
            if let c = o.contrast { engine.contrast = c }

            if let v = o.whites { engine.whites = v }
            if let v = o.blacks { engine.blacks = v }

            if o.auto {
                for pass in 1...o.autoTimes {
                    engine.autoEnhance()
                    FileHandle.standardError.write(Data(
                        ("orion: auto pass \(pass) — "
                         + String(format: "exposure %+.2f EV, whites %+.2f, "
                                  + "blacks %+.2f, lift %.2f, clarity %.2f\n",
                                  engine.exposureEv, engine.whites, engine.blacks,
                                  engine.fusion, engine.clarity)).utf8))
                }
            }

            // `--measure` needs the wide tail: it resolves differences that
            // eight-bit quantisation would erase, and the changes hunted in
            // this codebase are four decimal places wide. A plain screenshot
            // does not, and must not — the canvas should show what the screen
            // path produces, not a second rendering nobody sees.
            if !o.measure.isEmpty {
                engine.setWideOutput(true)
                for region in o.measure { measure(engine, region: region) }
                engine.setWideOutput(false)
            }

            let image = developed(engine)
            engine.showPlaceholder(image)
        }

        // The export sheet is not reachable from the editor's own hierarchy in
        // a still, so it is rendered on its own.
        if o.scene == "export" {
            let settings = ExportSettings()
            settings.quality = 0.82
            settings.size = .custom
            settings.setCustom(width: 3000, sourceWidth: engine.imageWidth,
                               sourceHeight: engine.imageHeight)
            let measured = engine.exportedSize(
                format: settings.format.code, quality: Float(settings.quality),
                maxDimension: settings.longestEdge(sourceWidth: engine.imageWidth,
                                                   sourceHeight: engine.imageHeight))
            settings.measuredBytes = measured

            // Captured, not read back off the settings the panel is about to
            // clear — otherwise the panel measures the value it just erased.
            let estimate = settings.estimatedBytes(sourceWidth: engine.imageWidth,
                                                   sourceHeight: engine.imageHeight)
            FileHandle.standardError.write(Data(
                "orion: estimate \(estimate) bytes, measured \(measured ?? -1) bytes\n".utf8))

            let panel = ExportPanel(settings: settings,
                                    sourceWidth: engine.imageWidth,
                                    sourceHeight: engine.imageHeight,
                                    measure: { measured },
                                    onExport: {}, onCancel: {})
                .preferredColorScheme(.dark)

            let sheetSize = CGSize(width: 380, height: 520)
            guard let sheet = render(panel, size: sheetSize) else {
                fail("the export panel produced no image")
            }
            do { try sheet.write(to: URL(fileURLWithPath: o.output)) }
            catch { fail("could not write \(o.output)") }
            FileHandle.standardError.write(Data("orion: wrote \(o.output) (export)\n".utf8))
            exit(0)
        }

        // Scan the photo's own folder so the filmstrip has frames in it.
        // Thumbnails stream in on a background queue, so the run loop below
        // has to turn enough times for them to arrive — an empty strip is a
        // screenshot of nothing.
        let library = MainActor.assumeIsolated { () -> Library in
            let lib = Library()
            if let photo = o.photo {
                let folder = URL(fileURLWithPath: photo).deletingLastPathComponent()
                // Fire and forget: the run loop below already waits for the
                // strip to fill, which is the same wait this would be.
                Task { await lib.open(folder: folder) }
            }
            return lib
        }
        // Thumbnails stream in on a background queue, so the run loop has to
        // turn enough times for them to arrive. An empty strip is a screenshot
        // of nothing, which is worse than no screenshot — it looks like a pass.
        for _ in 0..<60 {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.02))
            let ready = MainActor.assumeIsolated {
                !library.loading && !library.photos.isEmpty
                    && library.photos.allSatisfy { $0.thumbnail != nil }
            }
            if ready { break }
        }

        // ⚠ Set last, after every render this run will do, and with the engine
        // suspended — **both halves were needed, and the first capture proved
        // it.** `render()` clears `lastFailure` on success, and laying the
        // interface out renders: the canvas's `onAppear` assigns
        // `engine.cropPreview`, whose `didSet` is `pushAndRender()`. So the
        // first version of this scene planted a failure, SwiftUI wiped it during
        // layout, and the frame came back showing the ordinary hint and
        // `0.0 ms` — a photograph of the bug this line exists to prevent,
        // captioned as a photograph of the fix. Suspending is what a failed
        // engine looks like from the panel's side: no successful frame arrives
        // to take the warning down.
        if o.scene == "render-failed" {
            engine.suspended = true
            engine.lastFailure = failureText
        }

        let versions = snapshots(for: o.scene, photo: o.photo)

        let view = Editor(engine: engine, startTab: tab(for: o.scene),
                          startLibrary: library,
                          startSnapshots: versions)
            .frame(width: o.size.width, height: o.size.height)
            .preferredColorScheme(.dark)

        guard let png = render(view, size: o.size,
                               scrolledToBottom: scrolls(o.scene)) else {
            fail("the view produced no image")
        }

        do { try png.write(to: URL(fileURLWithPath: o.output)) }
        catch { fail("could not write \(o.output) — \(error.localizedDescription)") }

        // Written before it is checked, deliberately: the frame is the evidence
        // when the check goes red, and a check that exits before writing leaves
        // whoever has to look at it with nothing to look at.
        if o.scene == "render-failed" {
            assertFailureLineDrawn(png, view: view, size: o.size, engine: engine)
        }
        if o.scene == "versions" {
            assertVersionsDoNotShowTheClock(versions)
        }

        let note = "orion: wrote \(o.output) "
            + "(\(Int(o.size.width))x\(Int(o.size.height)), scene \(o.scene))\n"
        FileHandle.standardError.write(Data(note.utf8))
        exit(0)
    }

    static func fail(_ message: String) -> Never {
        FileHandle.standardError.write(Data("orion: \(message)\n".utf8))
        exit(1)
    }
}

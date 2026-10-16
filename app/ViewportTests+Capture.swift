// `CaptureInfo`'s spellings: the toolbar line, the popover rows, the browsing
// tooltip and the `capture` block the agent surface reads. All pure; the view
// that draws them is `capture-line`'s job (`Screenshot+Checks.swift`).

import Foundation

extension ViewportTests {

    static func testCaptureShutterSpellings() {
        let table: [(Float, String?)] = [
            (0.004, "1/250"), (1.0 / 8000, "1/8000"), (1.0 / 320, "1/320"),
            (0.25, "1/4"), (1.0 / 3, "1/3"), (0.4, "0.4 s"), (0.5, "0.5 s"),
            (1, "1 s"), (1.3, "1.3 s"), (2, "2 s"), (30, "30 s"),
            (0, nil), (-1, nil), (.nan, nil),
        ]
        for (seconds, want) in table {
            let got = CaptureInfo.shutterText(seconds)
            report(got == want, "shutter \(seconds) s spells \(want ?? "nil")", got ?? "nil")
        }
    }

    static func testCaptureApertureFocalIsoSpellings() {
        let apertures: [(Float, String?)] = [
            (1.8, "f/1.8"), (4, "f/4.0"), (7.1, "f/7.1"), (11, "f/11"),
            (10.99, "f/11"), (22, "f/22"), (0, nil),
        ]
        for (f, want) in apertures {
            let got = CaptureInfo.apertureText(f)
            report(got == want, "aperture \(f) spells \(want ?? "nil")", got ?? "nil")
        }
        let focals: [(Float, String?)] = [(35, "35 mm"), (200, "200 mm"),
                                          (4.3, "4.3 mm"), (24.0, "24 mm"), (0, nil)]
        for (mm, want) in focals {
            let got = CaptureInfo.focalText(mm)
            report(got == want, "focal \(mm) spells \(want ?? "nil")", got ?? "nil")
        }
        let isos: [(Float, String?)] = [(6400, "ISO 6400"), (100, "ISO 100"), (0, nil)]
        for (iso, want) in isos {
            let got = CaptureInfo.isoText(iso)
            report(got == want, "iso \(iso) spells \(want ?? "nil")", got ?? "nil")
        }
    }

    static var captureSample: CaptureInfo {
        CaptureInfo(camera: "SONY ILCE-7RM4", lens: "FE 35mm F1.8",
                    focalLength: 35, aperture: 1.8, shutter: 0.004, iso: 6400,
                    captured: Date(timeIntervalSince1970: 1_700_000_000),
                    width: 6000, height: 4000)
    }

    static func testCaptureLines() {
        let c = captureSample
        report(c.line == "SONY ILCE-7RM4 · 35 mm · f/1.8 · 1/250 · ISO 6400",
               "the line puts the camera first, then focal, aperture, shutter, ISO", c.line)
        report(c.compactLine == "35 mm · f/1.8 · 1/250 · ISO 6400",
               "the compact line is the line without the camera", c.compactLine)
        report(c.exposureLine == "f/1.8 · 1/250 · ISO 6400",
               "the narrowest line drops the focal length too", c.exposureLine)

        var partial = CaptureInfo()
        partial.iso = 200
        partial.aperture = 8
        report(partial.line == "f/8.0 · ISO 200",
               "a missing field leaves no separator behind", partial.line)
    }

    static func testCaptureRows() {
        let rows = captureSample.rows
        let labels = rows.map(\.0)
        report(labels == ["Camera", "Lens", "Focal length", "Aperture", "Shutter",
                          "ISO", "Captured", "Dimensions"],
               "every row, in reading order", "\(labels)")
        let dims = rows.first { $0.0 == "Dimensions" }?.1
        report(dims == "6000 × 4000 · 24.0 MP", "dimensions carry megapixels", dims ?? "nil")
        let iso = rows.first { $0.0 == "ISO" }?.1
        report(iso == "6400", "the ISO row's value is the number under its label", iso ?? "nil")

        var sparse = CaptureInfo()
        sparse.camera = "X"
        sparse.shutter = 2
        report(sparse.rows.map(\.0) == ["Camera", "Shutter"],
               "rows with no value are left out", "\(sparse.rows.map(\.0))")
    }

    static func testCaptureAllZeroIsEmpty() {
        let c = CaptureInfo()
        report(c.line.isEmpty, "an all-zero capture has an empty line", c.line)
        report(c.compactLine.isEmpty, "an all-zero capture has an empty compact line", c.compactLine)
        report(c.rows.isEmpty, "an all-zero capture has no rows", "\(c.rows.map(\.0))")
        report(c.wire == CaptureInfo.Wire(), "an all-zero capture puts nothing on the wire")
        report(c.wireDictionary.isEmpty, "and its dictionary has no keys",
               "\(c.wireDictionary.keys.sorted())")
    }

    static func testCaptureTooltip() {
        let got = CaptureInfo.tooltip(name: "a.ARW", aperture: 4, shutter: 0.01, iso: 100)
        report(got == "a.ARW\nf/4.0 · 1/100 · ISO 100",
               "a tooltip is the name, then the exposure", got)
        let bare = CaptureInfo.tooltip(name: "a.ARW", aperture: 0, shutter: 0, iso: 0)
        report(bare == "a.ARW", "with nothing indexed it is the name alone", bare)
    }

    static func testCaptureWireNumbers() {
        let dict = captureSample.wireDictionary
        report(dict["shutter"] as? Double == 0.004,
               "1/250 travels as 0.004, not a widened float", "\(dict["shutter"] ?? "nil")")
        report(dict["iso"] as? Double == 6400, "iso is a number", "\(dict["iso"] ?? "nil")")
        report(dict["camera"] as? String == "SONY ILCE-7RM4", "camera is carried")
        report(dict["captured"] as? String == "2023-11-14T22:13:20Z",
               "captured is ISO 8601", "\(dict["captured"] ?? "nil")")
    }

    static func testProposalCurrentJSONCarriesCapture() {
        let photo = URL(fileURLWithPath: "/shoot/_PIC8095.ARW")
        let data = Proposal.currentJSON(photo: photo, capture: captureSample)
        guard let doc = try? JSONDecoder().decode(Proposal.CurrentPhoto.self, from: data) else {
            report(false, "currentJSON with a capture should decode back")
            return
        }
        report(doc.capture == captureSample.wire, "capture round-trips through current.json",
               String(data: data, encoding: .utf8) ?? "")

        let none = Proposal.currentJSON(photo: nil, capture: captureSample)
        let closed = try? JSONDecoder().decode(Proposal.CurrentPhoto.self, from: none)
        report(closed != nil && closed?.capture == nil,
               "no photo carries no capture, whatever the engine still holds")

        let old = Data(#"{"photo":"/a.ARW","folder":"/","updated":"2026-01-01T00:00:00Z"}"#.utf8)
        report((try? JSONDecoder().decode(Proposal.CurrentPhoto.self, from: old)) != nil,
               "a current.json written before capture existed still decodes")
    }
}

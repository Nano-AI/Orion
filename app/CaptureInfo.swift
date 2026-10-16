import Foundation

/// What the camera recorded when the shutter fired - camera, lens, exposure,
/// focal length, time and size - and the spellings a photographer reads them
/// in.
///
/// Foundation only - the `OrionRawInfo` initialiser lives beside the engine
/// call in `Engine+Document.swift` - so the formatting is a table the viewport suite can check
/// and the toolbar, the popover, the HDR merge sheet, the browsing tooltips
/// and the agent surface all print one spelling. Zero means "the file does
/// not say", and every formatter answers nil for it rather than "ISO 0".
struct CaptureInfo: Equatable {
    var camera = ""
    var lens = ""
    /// Millimetres.
    var focalLength: Float = 0
    /// The f-number.
    var aperture: Float = 0
    /// Seconds: 0.004 is 1/250.
    var shutter: Float = 0
    var iso: Float = 0
    var captured: Date?
    var width: UInt32 = 0
    var height: UInt32 = 0

    init(camera: String = "", lens: String = "", focalLength: Float = 0,
         aperture: Float = 0, shutter: Float = 0, iso: Float = 0,
         captured: Date? = nil, width: UInt32 = 0, height: UInt32 = 0) {
        self.camera = camera; self.lens = lens; self.focalLength = focalLength
        self.aperture = aperture; self.shutter = shutter; self.iso = iso
        self.captured = captured; self.width = width; self.height = height
    }

    // MARK: Single values

    /// The conventional camera spelling: a fraction up to 1/3 s, decimal
    /// seconds from 0.4 s, whole seconds once there is nothing after the
    /// point. 1/250, 1/3, 0.5 s, 1.3 s, 2 s, 30 s.
    static func shutterText(_ seconds: Float) -> String? {
        guard seconds > 0, seconds.isFinite else { return nil }
        // A third of a second is the last stop a camera writes as a fraction;
        // 0.4 is the next one up, and every camera prints it as a decimal.
        if seconds < 0.35 {
            return "1/\(Int((1 / seconds).rounded()))"
        }
        let tenths = (seconds * 10).rounded() / 10
        return tenths == tenths.rounded()
            ? "\(Int(tenths)) s"
            : String(format: "%.1f s", tenths)
    }

    /// f/1.8, f/4.0, f/11: one decimal below ten, as lenses are marked, and
    /// whole numbers above, where the decimal is never anything but noise.
    static func apertureText(_ f: Float) -> String? {
        guard f > 0, f.isFinite else { return nil }
        return f < 9.95 ? String(format: "f/%.1f", f) : String(format: "f/%.0f", f)
    }

    /// 35 mm. A compact's 4.3 mm keeps its decimal.
    static func focalText(_ mm: Float) -> String? {
        guard mm > 0, mm.isFinite else { return nil }
        let tenths = (mm * 10).rounded() / 10
        return mm < 10 && tenths != tenths.rounded()
            ? String(format: "%.1f mm", tenths)
            : String(format: "%.0f mm", mm)
    }

    static func isoText(_ iso: Float) -> String? {
        guard iso > 0, iso.isFinite else { return nil }
        return String(format: "ISO %.0f", iso)
    }

    var shutterText: String? { Self.shutterText(shutter) }
    var apertureText: String? { Self.apertureText(aperture) }
    var focalText: String? { Self.focalText(focalLength) }
    var isoText: String? { Self.isoText(iso) }

    // MARK: Lines

    static let separator = " · "

    /// Camera first, then the exposure in the order a viewfinder reads it.
    var line: String {
        ([camera.isEmpty ? nil : camera] + exposureParts(focal: true))
            .compactMap { $0 }.joined(separator: Self.separator)
    }

    /// `line` without the camera, for where the camera is already obvious or
    /// the toolbar is too narrow for it.
    var compactLine: String {
        exposureParts(focal: true).compactMap { $0 }.joined(separator: Self.separator)
    }

    /// The narrowest spelling: the three numbers that set the exposure.
    var exposureLine: String {
        exposureParts(focal: false).compactMap { $0 }.joined(separator: Self.separator)
    }

    private func exposureParts(focal: Bool) -> [String?] {
        [focal ? focalText : nil, apertureText, shutterText, isoText]
    }

    /// A browsing tooltip: the file name, and under it the exposure the
    /// folder index carries. The index has no lens or focal length (adding
    /// them would bump its schema, and a bump drops the thumbnail table), so
    /// this is the exposure alone - `Library.Photo.tooltip` feeds it.
    static func tooltip(name: String, aperture: Float, shutter: Float, iso: Float) -> String {
        let exposure = CaptureInfo(aperture: aperture, shutter: shutter, iso: iso).compactLine
        return exposure.isEmpty ? name : name + "\n" + exposure
    }

    // MARK: The popover

    /// Label and value, in reading order. A row the file does not record is
    /// left out rather than shown as a dash.
    var rows: [(String, String)] {
        var out: [(String, String)] = []
        func add(_ label: String, _ value: String?) {
            if let value, !value.isEmpty { out.append((label, value)) }
        }
        add("Camera", camera)
        add("Lens", lens)
        add("Focal length", focalText)
        add("Aperture", apertureText)
        add("Shutter", shutterText)
        add("ISO", iso > 0 ? String(format: "%.0f", iso) : nil)
        add("Captured", captured.map {
            DateFormatter.localizedString(from: $0, dateStyle: .medium, timeStyle: .short)
        })
        add("Dimensions", dimensionsText)
        return out
    }

    /// 6000 × 4000 · 24.0 MP.
    var dimensionsText: String? {
        guard width > 0, height > 0 else { return nil }
        let mp = Double(width) * Double(height) / 1_000_000
        return "\(width) × \(height)" + Self.separator + String(format: "%.1f MP", mp)
    }

    // MARK: The agent surface

    /// The `capture` block `get_stats` and `current.json` carry. A field the
    /// file does not record is absent rather than zero, so a caller testing
    /// `capture.iso` never reads "ISO 0" as a fact.
    struct Wire: Codable, Equatable {
        var camera: String?
        var lens: String?
        var focalLength: Double?
        var aperture: Double?
        var shutter: Double?
        var iso: Double?
        /// ISO 8601.
        var captured: String?
    }

    var wire: Wire {
        // Through the shortest decimal spelling, so 1/250 travels as 0.004
        // rather than the 0.004000000189989805 a widening conversion prints.
        func number(_ f: Float) -> Double? { f > 0 && f.isFinite ? Double("\(f)") : nil }
        return Wire(camera: camera.isEmpty ? nil : camera,
                    lens: lens.isEmpty ? nil : lens,
                    focalLength: number(focalLength), aperture: number(aperture),
                    shutter: number(shutter), iso: number(iso),
                    captured: captured.map { ISO8601DateFormatter().string(from: $0) })
    }

    /// `wire` as the dictionary `JSONSerialization` writes, absent keys and all.
    var wireDictionary: [String: Any] {
        guard let data = try? JSONEncoder().encode(wire),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return [:] }
        return object
    }
}

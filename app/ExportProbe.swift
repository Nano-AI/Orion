// Reading an exported file back, so a scenario can assert on what was written
// rather than on what the code meant to write.
//
// Every property here exists because the corresponding control fails silently.
// A file that is eight bits when it was asked for sixteen looks identical in a
// thumbnail. A file that still carries GPS after "Strip location" looks
// identical to one that does not — the coordinates are only visible to whoever
// receives it. Neither is catchable by reading the source.
//
// Added 2026-08-01 with the bit depth and output sharpening controls.

import CoreGraphics
import Foundation
import ImageIO

enum ExportProbe {

    enum Property: String, CaseIterable {
        /// Bits per component in the container, as written.
        case depth
        /// 1 when a GPS block survived, 0 when it did not.
        case gps
        /// 1 when the camera's own EXIF survived — exposure, lens, date.
        /// "Strip everything" has to remove this and "Keep all" has to keep it,
        /// and without both sides a policy wired to nothing looks correct.
        case exif
        /// 1 when IPTC still names a place — city, sub-location, province,
        /// country. GPS is not the only way a file says where you were.
        case iptcLocation = "iptclocation"
        /// Mean absolute Laplacian of luma: how hard the edges are.
        ///
        /// A single number for "sharpness". It is not calibrated against
        /// anything and is not meant to be — it exists to compare two exports of
        /// the same frame, where more of it means more acutance.
        case acutance
    }

    /// The property as a number, or nil when the file cannot be read.
    static func measure(_ path: String, _ property: Property) -> Double? {
        let url = URL(fileURLWithPath: path)
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }

        switch property {
        case .depth:
            guard let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil)
                    as? [CFString: Any],
                  let depth = props[kCGImagePropertyDepth] as? Int else { return nil }
            return Double(depth)

        case .gps:
            guard let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil)
                    as? [CFString: Any] else { return nil }
            return props[kCGImagePropertyGPSDictionary] == nil ? 0 : 1

        case .exif:
            guard let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil)
                    as? [CFString: Any] else { return nil }
            // ImageIO synthesises an EXIF dictionary for almost anything, so
            // presence alone proves nothing — the camera's own fields are what
            // the policy is about.
            guard let exif = props[kCGImagePropertyExifDictionary] as? [CFString: Any] else {
                return 0
            }
            let cameraKeys: [CFString] = [
                kCGImagePropertyExifExposureTime,
                kCGImagePropertyExifFNumber,
                kCGImagePropertyExifISOSpeedRatings,
                kCGImagePropertyExifFocalLength,
                kCGImagePropertyExifDateTimeOriginal,
            ]
            return cameraKeys.contains { exif[$0] != nil } ? 1 : 0

        case .iptcLocation:
            guard let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil)
                    as? [CFString: Any] else { return nil }
            guard let iptc = props[kCGImagePropertyIPTCDictionary] as? [CFString: Any] else {
                return 0
            }
            let placeKeys: [CFString] = [
                kCGImagePropertyIPTCSubLocation,
                kCGImagePropertyIPTCCity,
                kCGImagePropertyIPTCProvinceState,
                kCGImagePropertyIPTCCountryPrimaryLocationCode,
                kCGImagePropertyIPTCCountryPrimaryLocationName,
                kCGImagePropertyIPTCContentLocationCode,
                kCGImagePropertyIPTCContentLocationName,
            ]
            return placeKeys.contains { iptc[$0] != nil } ? 1 : 0

        case .acutance:
            guard let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
            return acutance(of: image)
        }
    }

    /// Mean |∇²luma| over the decoded picture, in 0…1 units.
    ///
    /// Decoded to eight-bit sRGB deliberately: the comparison has to be between
    /// two files as a viewer sees them, and normalising the decode means a
    /// sixteen-bit file and an eight-bit one are measured the same way.
    private static func acutance(of image: CGImage) -> Double? {
        let w = image.width, h = image.height
        guard w > 2, h > 2 else { return nil }

        var pixels = [UInt8](repeating: 0, count: w * h * 4)
        guard let space = CGColorSpace(name: CGColorSpace.sRGB) else { return nil }
        guard let ctx = pixels.withUnsafeMutableBytes({ raw -> CGContext? in
            CGContext(data: raw.baseAddress, width: w, height: h, bitsPerComponent: 8,
                      bytesPerRow: w * 4, space: space,
                      bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
        }) else { return nil }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))

        // Rec.709, the primaries the pipeline ends in.
        var luma = [Double](repeating: 0, count: w * h)
        for i in 0..<(w * h) {
            luma[i] = (0.2126 * Double(pixels[i * 4])
                     + 0.7152 * Double(pixels[i * 4 + 1])
                     + 0.0722 * Double(pixels[i * 4 + 2])) / 255.0
        }

        var total = 0.0
        for y in 1..<(h - 1) {
            for x in 1..<(w - 1) {
                let i = y * w + x
                let lap = 4 * luma[i] - luma[i - 1] - luma[i + 1]
                        - luma[i - w] - luma[i + w]
                total += abs(lap)
            }
        }
        return total / Double((w - 2) * (h - 2))
    }
}

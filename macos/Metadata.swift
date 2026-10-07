import Foundation
import ImageIO
import zlib

/// Bounded, read-only metadata inspection. ImageIO retains native property names.
enum Metadata {
    static let maximumTextBytes = 1_048_576
    static let maximumReportCharacters = 262_144
    static let maximumPNGBytes = 67_108_864

    static func report(url: URL, image: CGImage?, frames: Int, duration: Double,
                       includeColors: Bool = false, shouldCancel: @escaping () -> Bool = { false },
                       translate: (String) -> String = { $0 }) -> String {
        var lines: [String] = []
        var characters = 0
        var truncated = false
        func append(_ text: String) {
            guard !shouldCancel() else { return }
            guard characters < maximumReportCharacters else { truncated = true; return }
            let remaining = maximumReportCharacters - characters
            let safe = String(text.prefix(remaining))
            lines.append(safe); characters += safe.count + 1
            if safe.count < text.count { truncated = true }
        }
        func field(_ name: String, _ value: String) { append("\(translate(name)): \(value)") }
        append("[\(translate("File"))]")
        field("Name", url.lastPathComponent)
        field("Folder", url.deletingLastPathComponent().path)
        if let attributes = try? FileManager.default.attributesOfItem(atPath: url.path) {
            if let size = attributes[.size] as? NSNumber { field("Size", "\(size) bytes") }
            if let date = attributes[.modificationDate] as? Date {
                let formatter = DateFormatter(); formatter.dateStyle = .medium; formatter.timeStyle = .medium
                field("Modified", formatter.string(from: date))
            }
        }
        append("\n[\(translate("Image"))]")
        if let image = image {
            field("Dimensions", "\(image.width) × \(image.height)")
            field("Bit depth", "\(image.bitsPerComponent) bits/component; \(image.bitsPerPixel) bits/pixel")
            field("Color space", image.colorSpace?.name as String? ?? translate("Unknown"))
            if includeColors { field("Colors", colorCount(image: image, shouldCancel: shouldCancel).report(translate: translate)) }
            let alpha = image.alphaInfo
            field("Transparency", translate([CGImageAlphaInfo.first, .last, .premultipliedFirst, .premultipliedLast, .alphaOnly].contains(alpha) ? "Yes" : "No"))
        }
        field("Frames", String(frames))
        if frames > 1 { field("Duration", String(format: "%.3f s", duration)) }
        let depthLimit = translate("Metadata depth limit")
        let arrayLimit = translate("Metadata array limit")
        func walk(_ value: Any, _ path: String, _ depth: Int) {
            guard !shouldCancel() else { return }
            guard characters < maximumReportCharacters else { truncated = true; return }
            guard depth < 16 else { append("\(path): [\(depthLimit)]"); return }
            if let dictionary = value as? [String: Any] {
                for key in dictionary.keys.sorted() {
                    if shouldCancel() { break }
                    walk(dictionary[key]!, path.isEmpty ? key : "\(path).\(key)", depth + 1)
                }
            } else if let array = value as? [Any] {
                for (index, item) in array.prefix(1024).enumerated() {
                    if shouldCancel() { break }
                    walk(item, "\(path)[\(index)]", depth + 1)
                }
                if array.count > 1024 { append("\(path): [\(arrayLimit)]") }
            } else if let data = value as? Data {
                append("\(path): [\(data.count) bytes] " + data.prefix(64).map { String(format: "%02x", $0) }.joined())
            } else { append("\(path): \(String(describing: value).prefix(8192))") }
        }
        if url.pathExtension.lowercased() == "svg" { field("Format", "SVG") }
        if !shouldCancel(), let source = CGImageSourceCreateWithURL(url as CFURL, nil), CGImageSourceGetCount(source) > 0 {
            append("\n[\(translate("Properties"))]")
            if let type = CGImageSourceGetType(source) { field("Format", type as String) }
            if let props = CGImageSourceCopyProperties(source, nil) as? [String: Any] { walk(props, "Container", 0) }
            let count = CGImageSourceGetCount(source)
            for index in 0..<min(count, 64) {
                if shouldCancel() { break }
                if let props = CGImageSourceCopyPropertiesAtIndex(source, index, nil) as? [String: Any] { walk(props, "Frame[\(index)]", 0) }
                // Metadata tags include XMP namespaces that are not always present in properties.
                if let metadata = CGImageSourceCopyMetadataAtIndex(source, index, nil) {
                    CGImageMetadataEnumerateTagsUsingBlock(metadata, nil, nil) { path, tag in
                        let namespace = CGImageMetadataTagCopyNamespace(tag) as String? ?? ""
                        if let value = CGImageMetadataTagCopyValue(tag) { walk(value, "Frame[\(index)].\(namespace).\(path)", 0) }
                        return !shouldCancel() && characters < maximumReportCharacters
                    }
                }
            }
            if count > 64 { append(translate("Metadata limited to the first 64 frames")) }
        }
        if url.pathExtension.lowercased() == "png" || url.pathExtension.lowercased() == "apng" {
            let texts = pngText(url: url, shouldCancel: shouldCancel)
            if !texts.isEmpty { append("\n[\(translate("Embedded text"))]") }
            for (key, value) in texts { if shouldCancel() { break }; append("\(key): \(value)") }
        }

        if truncated { lines.append(translate("Metadata report truncated at 256 KiB")) }
        return lines.joined(separator: "\n")
    }

    enum ColorCountResult: Equatable {
        case count(Int), cancelled, limitExceeded, error
        func report(translate: (String) -> String = { $0 }) -> String {
            switch self {
            case .count(let count): return String(count)
            case .cancelled: return translate("Color count cancelled")
            case .limitExceeded: return translate("Color count skipped: transparency exceeds memory limit")
            case .error: return translate("Error")
            }
        }
    }

    /// Exact normalized premultiplied sRGB RGBA counting with <= 5 MiB owned buffers.
    /// Opaque RGB uses a 2 MiB bitset; transparent RGBA uses a fixed 2 MiB hash table.
    /// ImageIO/CoreGraphics decoder backing storage is outside this scratch bound.
    static func colorCount(image: CGImage, shouldCancel: () -> Bool = { false }) -> ColorCountResult {
        if shouldCancel() { return .cancelled }
        guard image.width > 0, image.height > 0, image.width <= 262_144,
              let space = CGColorSpace(name: CGColorSpace.sRGB) else { return .error }
        let rows = max(1, min(image.height, 1_048_576 / (image.width * 4)))
        guard let context = CGContext(data: nil, width: image.width, height: rows,
                                      bitsPerComponent: 8, bytesPerRow: image.width * 4, space: space,
                                      bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue),
              let data = context.data else { return .error }
        var opaque = [UInt64](repeating: 0, count: 262_144)
        var transparent = [UInt64](repeating: 0, count: 262_144)
        var count = 0, transparentCount = 0
        context.setBlendMode(.copy)
        context.interpolationQuality = .none
        for y in stride(from: 0, to: image.height, by: rows) {
            if shouldCancel() { return .cancelled }
            let height = min(rows, image.height - y)
            guard let stripe = image.cropping(to: CGRect(x: 0, y: y, width: image.width, height: height)) else { return .error }
            context.clear(CGRect(x: 0, y: 0, width: image.width, height: rows))
            context.draw(stripe, in: CGRect(x: 0, y: 0, width: image.width, height: height))
            let bytes = data.bindMemory(to: UInt8.self, capacity: image.width * rows * 4)
            for i in 0..<(image.width * height) {
                if i & 4095 == 0 && shouldCancel() { return .cancelled }
                let offset = ((rows - height) * image.width + i) * 4
                let rgb = Int(bytes[offset]) << 16 | Int(bytes[offset + 1]) << 8 | Int(bytes[offset + 2])
                if bytes[offset + 3] == 255 {
                    let index = rgb >> 6, bit = UInt64(1) << (rgb & 63)
                    if opaque[index] & bit == 0 { opaque[index] |= bit; count += 1 }
                } else {
                    let key = UInt64((UInt32(rgb) << 8) | UInt32(bytes[offset + 3])) + 1
                    var slot = Int((key &* 11_400_714_819_323_198_485) >> 46)
                    while transparent[slot] != 0 && transparent[slot] != key { slot = (slot + 1) & 262_143 }
                    if transparent[slot] == 0 {
                        guard transparentCount < 131_072 else { return .limitExceeded }
                        transparent[slot] = key; transparentCount += 1; count += 1
                    }
                }
            }
        }
        return .count(count)
    }

    private static func inflate(_ data: Data) -> Data? {
        guard !data.isEmpty else { return nil }
        var result = Data(count: maximumTextBytes)
        var length = uLongf(maximumTextBytes)
        let status = result.withUnsafeMutableBytes { output in
            data.withUnsafeBytes { input in
                uncompress(output.bindMemory(to: Bytef.self).baseAddress!, &length,
                           input.bindMemory(to: Bytef.self).baseAddress!, uLong(data.count))
            }
        }
        guard status == Z_OK else { return nil }
        result.count = Int(length); return result
    }

    static func pngText(_ data: Data, shouldCancel: () -> Bool = { false }) -> [(String, String)] {
        guard data.count <= maximumPNGBytes, data.starts(with: [137,80,78,71,13,10,26,10]) else { return [] }
        var offset = 8, total = 0
        var result: [(String, String)] = []
        while offset <= data.count - 12 && result.count < 128 && !shouldCancel() {
            let length = data[offset..<offset+4].reduce(0) { ($0 << 8) | Int($1) }
            guard length <= data.count - offset - 12 else { break }
            let kind = String(data: data.subdata(in: offset+4..<offset+8), encoding: .ascii) ?? ""
            let start = offset + 8
            offset += length + 12
            guard ["tEXt", "zTXt", "iTXt"].contains(kind), length <= maximumTextBytes else { continue }
            let chunk = data.subdata(in: start..<start+length)
            guard
                  let zero = chunk.firstIndex(of: 0), zero > 0, zero <= 79 else { continue }
            let key = String(data: chunk.prefix(zero), encoding: .isoLatin1) ?? ""
            var payload = Data(chunk.dropFirst(zero + 1))
            var utf8 = false
            if kind == "zTXt" {
                guard payload.first == 0, let expanded = inflate(Data(payload.dropFirst())) else { continue }
                payload = expanded
            } else if kind == "iTXt" {
                guard payload.count >= 2, payload[1] == 0, payload[0] <= 1 else { continue }
                let compressed = payload[0] == 1
                payload = Data(payload.dropFirst(2))
                guard let languageEnd = payload.firstIndex(of: 0) else { continue }
                payload = Data(payload.dropFirst(languageEnd + 1))
                guard let translatedEnd = payload.firstIndex(of: 0) else { continue }
                payload = Data(payload.dropFirst(translatedEnd + 1))
                if compressed { guard let expanded = inflate(payload) else { continue }; payload = expanded }
                utf8 = true
            }
            guard total + payload.count <= maximumTextBytes else { break }
            if let text = String(data: payload, encoding: .utf8) ?? (utf8 ? nil : String(data: payload, encoding: .isoLatin1)) {
                result.append((key, text)); total += payload.count
            }
        }
        return result
    }

    /// Seek past image payloads; only bounded text chunks are materialized.
    static func pngText(url: URL, shouldCancel: () -> Bool = { false }) -> [(String, String)] {
        guard !shouldCancel(), let handle = try? FileHandle(forReadingFrom: url) else { return [] }
        defer { try? handle.close() }
        let signature = Data([137,80,78,71,13,10,26,10])
        guard (try? handle.read(upToCount: 8)) == signature,
              let size = try? handle.seekToEnd(), (try? handle.seek(toOffset: 8)) != nil else { return [] }
        var result: [(String, String)] = [], total = 0, chunks = 0
        while !shouldCancel() && result.count < 128 && chunks < 100_000 {
            guard let offset = try? handle.offset(), offset <= size, size - offset >= 12,
                  let header = try? handle.read(upToCount: 8), header.count == 8 else { break }
            chunks += 1
            let length = header.prefix(4).reduce(UInt64(0)) { ($0 << 8) | UInt64($1) }
            guard length <= size - offset - 12 else { break }
            let kind = String(data: header.suffix(4), encoding: .ascii) ?? ""
            if ["tEXt", "zTXt", "iTXt"].contains(kind), length <= maximumTextBytes,
               let payload = try? handle.read(upToCount: Int(length)), payload.count == Int(length) {
                let texts = pngText(signature + header + payload + Data(repeating: 0, count: 4), shouldCancel: shouldCancel)
                for pair in texts {
                    let bytes = pair.1.utf8.count
                    guard total + bytes <= maximumTextBytes else { return result }
                    result.append(pair); total += bytes
                }
            }
            guard (try? handle.seek(toOffset: offset + length + 12)) != nil else { break }
            if kind == "IEND" { break }
        }
        return result
    }

    static func selfTest() throws {
        enum Failure: Error { case embeddedText, imageFixture, imageMetadata }
        func chunk(_ kind: String, _ bytes: [UInt8]) -> Data {
            let count = UInt32(bytes.count)
            return Data([UInt8(count >> 24), UInt8((count >> 16) & 255), UInt8((count >> 8) & 255), UInt8(count & 255)]) + Data(kind.utf8) + Data(bytes) + Data(repeating: 0, count: 4)
        }
        let header = Data([137,80,78,71,13,10,26,10])
        let plain = chunk("tEXt", Array("prompt\0test".utf8))
        let international = chunk("iTXt", Array("Comment\0".utf8) + [0,0,0,0] + Array("日本語".utf8))
        let compressed = chunk("zTXt", Array("prompt\0".utf8) + [0,120,156,43,73,45,46,1,0,4,93,1,193])
        let compressedInternational = chunk("iTXt", Array("Comment\0".utf8) + [1,0,0,0,120,156,43,73,45,46,1,0,4,93,1,193])
        let text = pngText(header + plain + international + compressed + compressedInternational)
        guard text.count == 4, text[0].1 == "test", text[1].1 == "日本語", text[2].1 == "test", text[3].1 == "test",
              pngText(Data([0,1,2])).isEmpty,
              pngText(header + Data([255,255,255,255,116,69,88,116])).isEmpty else { throw Failure.embeddedText }
        guard pngText(header + plain, shouldCancel: { true }).isEmpty else { throw Failure.embeddedText }
        let streamingURL = FileManager.default.temporaryDirectory.appendingPathComponent("tliv-png-stream-\(UUID().uuidString).png")
        defer { try? FileManager.default.removeItem(at: streamingURL) }
        guard FileManager.default.createFile(atPath: streamingURL.path, contents: header),
              let stream = try? FileHandle(forWritingTo: streamingURL) else { throw Failure.embeddedText }
        let imageBytes: UInt64 = 70 * 1024 * 1024
        try stream.seekToEnd()
        try stream.write(contentsOf: Data([4,96,0,0]) + Data("IDAT".utf8))
        try stream.seek(toOffset: 8 + 8 + imageBytes)
        try stream.write(contentsOf: Data(repeating: 0, count: 4) + plain)
        try stream.close()
        guard pngText(url: streamingURL).first?.1 == "test",
              pngText(url: streamingURL, shouldCancel: { true }).isEmpty else { throw Failure.embeddedText }
        let fixtureURL = FileManager.default.temporaryDirectory.appendingPathComponent("tliv-metadata-\(UUID().uuidString).jpg")
        defer { try? FileManager.default.removeItem(at: fixtureURL) }
        guard let context = CGContext(data: nil, width: 2, height: 2, bitsPerComponent: 8,
                                      bytesPerRow: 8, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let image = context.makeImage(),
              let destination = CGImageDestinationCreateWithURL(fixtureURL as CFURL, "public.jpeg" as CFString, 1, nil) else { throw Failure.imageFixture }
        guard colorCount(image: image, shouldCancel: { true }) == .cancelled else { throw Failure.imageFixture }
        context.setFillColor(CGColor(red: 1, green: 0, blue: 0, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: 1, height: 1))
        guard let mixed = context.makeImage(), colorCount(image: mixed) == .count(2) else { throw Failure.imageFixture }
        context.setFillColor(CGColor(red: 0, green: 1, blue: 0, alpha: 0.5))
        context.fill(CGRect(x: 1, y: 1, width: 1, height: 1))
        guard let alphaImage = context.makeImage(), colorCount(image: alphaImage) == .count(3) else { throw Failure.imageFixture }
        guard let stripeContext = CGContext(data: nil, width: 1024, height: 257, bitsPerComponent: 8,
                                            bytesPerRow: 4096, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                            bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue) else { throw Failure.imageFixture }
        stripeContext.setFillColor(CGColor(red: 1, green: 0, blue: 0, alpha: 1))
        stripeContext.fill(CGRect(x: 0, y: 0, width: 1024, height: 257))
        guard let stripeImage = stripeContext.makeImage(), colorCount(image: stripeImage) == .count(1) else { throw Failure.imageFixture }
        stripeContext.setFillColor(CGColor(red: 0, green: 1, blue: 0, alpha: 1))
        stripeContext.fill(CGRect(x: 0, y: 0, width: 1024, height: 1))
        guard let lastRowImage = stripeContext.makeImage(), colorCount(image: lastRowImage) == .count(2) else { throw Failure.imageFixture }
        var checks = 0
        guard colorCount(image: lastRowImage, shouldCancel: { checks += 1; return checks > 5 }) == .cancelled else { throw Failure.imageFixture }
        let properties: [String: Any] = [
            kCGImagePropertyExifDictionary as String: [kCGImagePropertyExifUserComment as String: "TLIV EXIF fixture", kCGImagePropertyExifExposureTime as String: 0.125],
            kCGImagePropertyTIFFDictionary as String: [kCGImagePropertyTIFFMake as String: "TLIV Camera", kCGImagePropertyTIFFModel as String: "Test Model"]
        ]
        CGImageDestinationAddImage(destination, image, properties as CFDictionary)
        guard CGImageDestinationFinalize(destination), colorCount(image: image) == .count(1) else { throw Failure.imageFixture }
        let localized = report(url: fixtureURL, image: image, frames: 1, duration: 0, includeColors: true, translate: { "L:" + $0 })
        guard localized.contains("[L:File]"), localized.contains("[L:Image]"),
              localized.contains("L:Name:"), localized.contains("L:Colors: 1"),
              localized.contains("TLIV EXIF fixture"), localized.contains("TLIV Camera"),
              localized.contains("Test Model"), localized.contains("ExposureTime") else { throw Failure.imageMetadata }
    }
}

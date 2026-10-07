import AppKit
import Foundation

/// Uses AppKit's SVG rasterizer through public NSImage APIs. External resources are
/// rejected before decoding, so opening an image never loads files or network URLs.
enum SVGDecoder {
    enum DecodeError: LocalizedError {
        case invalid(String)
        var errorDescription: String? {
            switch self { case .invalid(let reason): return "SVG: \(reason)" }
        }
    }

    static func decode(url: URL) throws -> CGImage {
        let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
        guard size <= 16 * 1024 * 1024 else { throw DecodeError.invalid("file exceeds 16 MB") }
        return try decode(data: Data(contentsOf: url, options: .mappedIfSafe))
    }

    private static func decode(data: Data) throws -> CGImage {
        guard data.count <= 16 * 1024 * 1024,
              let text = String(data: data, encoding: .utf8) else {
            throw DecodeError.invalid("requires UTF-8 XML up to 16 MB")
        }
        guard !text.localizedCaseInsensitiveContains("<!DOCTYPE"),
              !text.localizedCaseInsensitiveContains("<!ENTITY") else {
            throw DecodeError.invalid("DTD and entities are unsupported")
        }
        let validator = Validator()
        let parser = XMLParser(data: data)
        parser.shouldResolveExternalEntities = false
        parser.delegate = validator
        guard parser.parse(), validator.problem == nil, validator.sawSVG else {
            throw DecodeError.invalid(validator.problem ?? parser.parserError?.localizedDescription ?? "invalid XML")
        }
        guard let image = NSImage(data: data) else { throw DecodeError.invalid("AppKit cannot decode this SVG") }
        let size = image.size
        guard size.width.isFinite, size.height.isFinite,
              size.width > 0, size.height > 0,
              size.width <= 8192, size.height <= 8192,
              size.width * size.height <= 32_000_000 else {
            throw DecodeError.invalid("invalid or oversized canvas")
        }
        // Explicit bitmap dimensions avoid dependence on the attached display scale.
        let width = Int(ceil(size.width)), height = Int(ceil(size.height))
        guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width,
                                            pixelsHigh: height, bitsPerSample: 8, samplesPerPixel: 4,
                                            hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                                            bytesPerRow: 0, bitsPerPixel: 0),
              let context = NSGraphicsContext(bitmapImageRep: bitmap) else {
            throw DecodeError.invalid("cannot allocate canvas")
        }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = context
        image.draw(in: NSRect(x: 0, y: 0, width: width, height: height),
                   from: .zero, operation: .copy, fraction: 1)
        NSGraphicsContext.restoreGraphicsState()
        guard let result = bitmap.cgImage else { throw DecodeError.invalid("cannot rasterize SVG") }
        return result
    }

    private final class Validator: NSObject, XMLParserDelegate {
        var problem: String?
        var sawSVG = false
        var depth = 0
        var styleText: String?
        let elements: Set<String> = ["svg", "g", "defs", "title", "desc", "metadata", "path",
            "rect", "circle", "ellipse", "line", "polyline", "polygon", "text", "tspan",
            "linearGradient", "radialGradient", "stop", "clipPath", "mask", "use", "symbol", "style"]
        func fail(_ parser: XMLParser, _ reason: String) {
            problem = reason; parser.abortParsing()
        }
        func safeReferences(_ value: String) -> Bool {
            // CSS escapes and at-rules could disguise external URL references.
            guard !value.contains("\\"), !value.contains("@") else { return false }
            let regex = try! NSRegularExpression(pattern: "url\\s*\\(([^)]*)\\)", options: .caseInsensitive)
            let ns = value as NSString
            for match in regex.matches(in: value, range: NSRange(location: 0, length: ns.length)) {
                let reference = ns.substring(with: match.range(at: 1))
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                    .trimmingCharacters(in: CharacterSet(charactersIn: "\"'"))
                if !reference.hasPrefix("#") { return false }
            }
            return true
        }
        func parser(_ parser: XMLParser, didStartElement elementName: String,
                    namespaceURI: String?, qualifiedName qName: String?, attributes: [String: String]) {
            guard elements.contains(elementName), depth < 128 else {
                fail(parser, "unsupported element: \(elementName)"); return
            }
            if depth == 0 {
                guard elementName == "svg" else { fail(parser, "root must be svg"); return }
                sawSVG = true
            }
            depth += 1
            if elementName == "style" { styleText = "" }
            for (key, value) in attributes {
                if key.lowercased().hasPrefix("on") || key == "xml:base" {
                    fail(parser, "active content is unsupported"); return
                }
                if (key == "href" || key.hasSuffix(":href")) && !value.hasPrefix("#") {
                    fail(parser, "external references are unsupported"); return
                }
                if !safeReferences(value) {
                    fail(parser, "external CSS resources are unsupported"); return
                }
            }
        }
        func parser(_ parser: XMLParser, didEndElement elementName: String,
                    namespaceURI: String?, qualifiedName qName: String?) {
            if elementName == "style", let content = styleText {
                if !safeReferences(content) { fail(parser, "external CSS resources are unsupported") }
                styleText = nil
            }
            depth -= 1
        }
        func parser(_ parser: XMLParser, foundCharacters string: String) {
            if styleText != nil { styleText! += string }
        }
        func parser(_ parser: XMLParser, foundCDATA CDATABlock: Data) {
            if styleText != nil {
                guard let content = String(data: CDATABlock, encoding: .utf8) else {
                    fail(parser, "invalid CSS encoding"); return
                }
                styleText! += content
            }
        }
        func parser(_ parser: XMLParser, foundProcessingInstructionWithTarget target: String, data: String?) {
            fail(parser, "processing instructions are unsupported")
        }
    }

    static func selfTest() throws {
        let svg = """
        <svg xmlns="http://www.w3.org/2000/svg" width="24" height="16" viewBox="0 0 12 8">
          <rect width="12" height="8" fill="#ff0000"/>
          <g transform="translate(6 0)"><path d="M0 0 H6 V8 H0 Z" fill="#0000ff"/></g>
        </svg>
        """
        let image = try decode(data: Data(svg.utf8))
        guard image.width == 24, image.height == 16 else { throw DecodeError.invalid("dimension self-test failed") }
        let bitmap = NSBitmapImageRep(cgImage: image)
        guard let red = bitmap.colorAt(x: 3, y: 8)?.usingColorSpace(.deviceRGB),
              let blue = bitmap.colorAt(x: 20, y: 8)?.usingColorSpace(.deviceRGB),
              red.redComponent > 0.9, red.blueComponent < 0.1,
              blue.blueComponent > 0.9, blue.redComponent < 0.1 else {
            throw DecodeError.invalid("path, transform, fill or viewBox self-test failed")
        }
        for forbidden in [
            "<svg><image href='https://example.com/a.png'/></svg>",
            "<svg><use href='file:///etc/passwd'/></svg>",
            "<svg><style>rect {fill:url(https://example.com/a)}</style></svg>",
            "<!DOCTYPE svg [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><svg>&x;</svg>",
            "<svg><script>1</script></svg>"
        ] {
            do { _ = try decode(data: Data(forbidden.utf8)) }
            catch { continue }
            throw DecodeError.invalid("external-resource rejection self-test failed")
        }
    }
}

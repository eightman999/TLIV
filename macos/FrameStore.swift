import Foundation
import ImageIO
import CoreGraphics

/// Retains at most three decoded frames and 32 MiB of pixels. A larger single
/// frame is permitted, alone, so the effective budget is max(32 MiB, one frame).
/// ImageIO owns the compressed source; its private working memory is not included.
final class FrameStore {
    static let cacheByteLimit = 32 * 1024 * 1024
    private static let frameByteLimit = 512 * 1024 * 1024
    let frameCount: Int
    let delays: [Double]
    private let source: CGImageSource
    private let orientation: Int
    private var cache: [Int: CGImage] = [:]
    private var recency: [Int] = []
    private(set) var retainedPixelBytes = 0
    var cachedFrameCount: Int { cache.count }

    private static let decodeOptions: CFDictionary = [
        kCGImageSourceShouldCache: false,
        kCGImageSourceShouldCacheImmediately: false
    ] as CFDictionary
    // Materialize retained CGImages once: disabling their pixel cache causes
    // ImageIO to decode the full source repeatedly for drawing and pixel probes.
    private static let pixelOptions: CFDictionary = [
        kCGImageSourceShouldCache: true,
        kCGImageSourceShouldCacheImmediately: true
    ] as CFDictionary

    private init(source: CGImageSource, count: Int, delays: [Double], orientation: Int) {
        self.source = source; frameCount = count; self.delays = delays
        self.orientation = orientation
    }

    static func open(url: URL) throws -> FrameStore {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, decodeOptions) else {
            throw failure("Cannot read image")
        }
        let type = CGImageSourceGetType(source) as String? ?? ""
        let animated = ["com.compuserve.gif", "public.png", "org.webmproject.webp"].contains(type)
        let count = animated ? CGImageSourceGetCount(source) : min(1, CGImageSourceGetCount(source))
        guard count > 0 else { throw failure("Image has no frames") }
        guard count <= 100_000 else { throw failure("Image exceeds the 100000 frame metadata limit") }
        var delays: [Double] = []; delays.reserveCapacity(count)
        var orientation = 1
        for index in 0..<count {
            guard let props = CGImageSourceCopyPropertiesAtIndex(source, index, decodeOptions) as? [String: Any],
                  let width = props[kCGImagePropertyPixelWidth as String] as? Int,
                  let height = props[kCGImagePropertyPixelHeight as String] as? Int,
                  width > 0, height > 0, width <= 32768, height <= 32768,
                  Double(width) * Double(height) * 4 <= Double(frameByteLimit) else {
                throw failure("Frame dimensions exceed the image limit")
            }
            if index == 0 && !animated { orientation = props[kCGImagePropertyOrientation as String] as? Int ?? 1 }
            let animation = (props[kCGImagePropertyGIFDictionary as String] ?? props[kCGImagePropertyPNGDictionary as String] ?? props["{WebP}"]) as? [String: Any] ?? [:]
            let delay = animation["UnclampedDelayTime"] as? Double ?? animation["DelayTime"] as? Double ?? 0.1
            delays.append(delay.isFinite && delay > 0 ? delay : 0.1)
        }
        let store = FrameStore(source: source, count: count, delays: delays, orientation: orientation)
        guard store.image(at: 0) != nil else { throw failure("Cannot decode first frame") }
        return store
    }

    func image(at index: Int) -> CGImage? {
        guard index >= 0, index < frameCount else { return nil }
        if let image = cache[index] {
            recency.removeAll { $0 == index }; recency.append(index)
            return image
        }
        // Free old pixels before asking ImageIO to decode another large frame.
        // Small frames use the bounded LRU; no anticipatory decoding is performed.
        if retainedPixelBytes > Self.cacheByteLimit { removeAll() }
        let sourceImage: CGImage?
        if orientation > 1 {
            let props = CGImageSourceCopyPropertiesAtIndex(source, index, Self.decodeOptions) as? [String: Any] ?? [:]
            let size = max(props[kCGImagePropertyPixelWidth as String] as? Int ?? 0,
                           props[kCGImagePropertyPixelHeight as String] as? Int ?? 0)
            let options: CFDictionary = [
                kCGImageSourceShouldCache: true,
                kCGImageSourceShouldCacheImmediately: true,
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true,
                kCGImageSourceThumbnailMaxPixelSize: size
            ] as CFDictionary
            sourceImage = CGImageSourceCreateThumbnailAtIndex(source, index, options)
        } else {
            sourceImage = CGImageSourceCreateImageAtIndex(source, index, Self.pixelOptions)
        }
        guard let decoded = sourceImage, decoded.height <= Self.frameByteLimit / max(1, decoded.bytesPerRow) else {
            CGImageSourceRemoveCacheAtIndex(source, index)
            return nil
        }
        let image: CGImage
        if frameCount == 1 {
            // Static provider crops can repeatedly decode a compressed image.
            guard let materialized = Self.materialize(decoded) else {
                CGImageSourceRemoveCacheAtIndex(source, index); return nil
            }
            image = materialized
            CGImageSourceRemoveCacheAtIndex(source, index)
        } else {
            // Avoid a second bitmap allocation for every animation frame. ImageIO
            // decoded frame caching lasts only until this frame's LRU eviction.
            image = decoded
        }
        let bytes = image.bytesPerRow * image.height
        while !recency.isEmpty && (cache.count >= 3 || retainedPixelBytes + bytes > Self.cacheByteLimit) {
            let oldest = recency.removeFirst()
            if let removed = cache.removeValue(forKey: oldest) { retainedPixelBytes -= removed.bytesPerRow * removed.height }
            CGImageSourceRemoveCacheAtIndex(source, oldest)
        }
        cache[index] = image
        recency.append(index); retainedPixelBytes += bytes
        return image
    }

    private static func materialize(_ image: CGImage) -> CGImage? {
        // CopyData resolves ImageIO's lazy provider once. Rebuilding the CGImage
        // from its retained CFData avoids a color conversion and CGContext draw,
        // while preserving native row padding, precision, ICC space and alpha.
        if !image.isMask, let space = image.colorSpace,
           let pixels = image.dataProvider?.data,
           CFDataGetLength(pixels) >= image.bytesPerRow * image.height,
           let provider = CGDataProvider(data: pixels),
           let bitmap = CGImage(width: image.width, height: image.height,
                                bitsPerComponent: image.bitsPerComponent,
                                bitsPerPixel: image.bitsPerPixel, bytesPerRow: image.bytesPerRow,
                                space: space, bitmapInfo: image.bitmapInfo,
                                provider: provider, decode: image.decode,
                                shouldInterpolate: image.shouldInterpolate,
                                intent: image.renderingIntent) {
            return bitmap
        }
        let originalSpace = image.colorSpace ?? CGColorSpaceCreateDeviceRGB()
        // Preserve RGB/gray ICC space, precision and alpha whenever the bitmap
        // context supports the decoder's exact layout. Indexed/CMYK images need
        // color-managed conversion because CGContext cannot render into those layouts.
        let exact = (originalSpace.model == .rgb || originalSpace.model == .monochrome)
            ? CGContext(data: nil, width: image.width, height: image.height,
                        bitsPerComponent: image.bitsPerComponent, bytesPerRow: 0,
                        space: originalSpace, bitmapInfo: image.bitmapInfo.rawValue) : nil
        let context: CGContext?
        if let exact { context = exact }
        else {
            let space: CGColorSpace
            if originalSpace.model == .rgb { space = originalSpace }
            else if originalSpace.model == .indexed, let base = originalSpace.baseColorSpace, base.model == .rgb { space = base }
            else { space = CGColorSpace(name: CGColorSpace.sRGB)! }
            context = CGContext(data: nil, width: image.width, height: image.height,
                                bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
        }
        guard let context, context.height <= frameByteLimit / max(1, context.bytesPerRow) else { return nil }
        context.interpolationQuality = .none
        context.setBlendMode(.copy)
        context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        return context.makeImage()
    }

    private func removeAll() {
        for index in recency { CGImageSourceRemoveCacheAtIndex(source, index) }
        cache.removeAll(); recency.removeAll(); retainedPixelBytes = 0
    }

    static func selfTest() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("TLIV-FrameStore-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        func pixels(_ image: CGImage) -> [UInt8] {
            var result = [UInt8](repeating: 0, count: image.width * image.height * 4)
            result.withUnsafeMutableBytes { bytes in
                let context = CGContext(data: bytes.baseAddress, width: image.width, height: image.height,
                                        bitsPerComponent: 8, bytesPerRow: image.width * 4,
                                        space: CGColorSpaceCreateDeviceRGB(),
                                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
                context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
            }
            return result
        }
        for transparent in [false, true] {
            let url = directory.appendingPathComponent(transparent ? "disposal.gif" : "frames.gif")
            guard let destination = CGImageDestinationCreateWithURL(url as CFURL, "com.compuserve.gif" as CFString, 4, nil) else {
                throw failure("Self-test GIF encoder unavailable")
            }
            for index in 0..<4 {
                let context = CGContext(data: nil, width: 4, height: 3, bitsPerComponent: 8, bytesPerRow: 16,
                                        space: CGColorSpaceCreateDeviceRGB(),
                                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
                context.setFillColor(CGColor(red: CGFloat(index) / 3, green: 0, blue: 0, alpha: 1))
                context.fill(transparent ? CGRect(x: index % 3, y: 0, width: 2, height: 2) : CGRect(x: 0, y: 0, width: 4, height: 3))
                CGImageDestinationAddImage(destination, context.makeImage()!, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: 0.1]] as CFDictionary)
            }
            guard CGImageDestinationFinalize(destination) else { throw failure("Self-test GIF encoding failed") }
            if transparent {
                // ImageIO has no public GIF disposal encoding property. Set the
                // standard Graphic Control Extension disposal bits in this tiny fixture.
                var bytes = [UInt8](try Data(contentsOf: url))
                var extensions = 0
                for offset in 0..<(bytes.count - 7) where bytes[offset] == 0x21 && bytes[offset + 1] == 0xf9 && bytes[offset + 2] == 4 && bytes[offset + 7] == 0 {
                    bytes[offset + 3] = (bytes[offset + 3] & 0xe3) | UInt8((extensions % 2 == 0 ? 2 : 3) << 2)
                    extensions += 1
                }
                precondition(extensions == 4)
                try Data(bytes).write(to: url)
            }
            let store = try open(url: url)
            guard let reference = CGImageSourceCreateWithURL(url as CFURL, nil) else { throw failure("Self-test reference decoder failed") }
            precondition(store.frameCount == 4 && store.cachedFrameCount == 1 && store.delays.count == 4)
            let retainedFirst = store.image(at: 0)!
            let retainedPixels = pixels(retainedFirst)
            for index in [0, 1, 2, 3, 0, 3, 2, 1, 0] {
                guard let image = store.image(at: index), let expected = CGImageSourceCreateImageAtIndex(reference, index, nil) else {
                    throw failure("Self-test frame decode failed")
                }
                precondition(image.width == 4 && image.height == 3)
                precondition(pixels(image) == pixels(expected))
                precondition(store.image(at: index) === image)
                precondition(store.cachedFrameCount <= 3 && store.retainedPixelBytes <= cacheByteLimit)
            }
            precondition(store.image(at: -1) == nil && store.image(at: 4) == nil)
            // A caller's current frame remains usable after its source cache entry
            // is evicted by subsequent navigation.
            precondition(pixels(retainedFirst) == retainedPixels)
        }
        let colorURL = directory.appendingPathComponent("wide-gamut.png")
        let colorContext = CGContext(data: nil, width: 4, height: 3, bitsPerComponent: 8, bytesPerRow: 16,
                                     space: CGColorSpace(name: CGColorSpace.displayP3)!,
                                     bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        colorContext.setFillColor(CGColor(colorSpace: colorContext.colorSpace!, components: [0.8, 0.2, 0.1, 0.5])!)
        colorContext.fill(CGRect(x: 0, y: 0, width: 4, height: 3))
        guard let destination = CGImageDestinationCreateWithURL(colorURL as CFURL, "public.png" as CFString, 1, nil) else {
            throw failure("Self-test PNG encoder unavailable")
        }
        CGImageDestinationAddImage(destination, colorContext.makeImage()!, nil)
        precondition(CGImageDestinationFinalize(destination))
        let colorStore = try open(url: colorURL)
        let referenceSource = CGImageSourceCreateWithURL(colorURL as CFURL, nil)!
        let referenceImage = CGImageSourceCreateImageAtIndex(referenceSource, 0, nil)!
        let retainedImage = colorStore.image(at: 0)!
        precondition(retainedImage.colorSpace == referenceImage.colorSpace)
        precondition(retainedImage.bitsPerComponent == referenceImage.bitsPerComponent)
        precondition(pixels(retainedImage) == pixels(referenceImage))
        // The raw-copy path must preserve >8-bit precision and row padding too.
        let highPrecisionBytes = Data(repeating: 0x80, count: 4 * 3 * 8)
        let highPrecision = CGImage(width: 4, height: 3, bitsPerComponent: 16, bitsPerPixel: 64,
                                    bytesPerRow: 32, space: CGColorSpace(name: CGColorSpace.displayP3)!,
                                    bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue).union(.byteOrder16Big),
                                    provider: CGDataProvider(data: highPrecisionBytes as CFData)!, decode: nil,
                                    shouldInterpolate: false, intent: .relativeColorimetric)!
        let precisionCopy = materialize(highPrecision)!
        precondition(precisionCopy.bitsPerComponent == 16 && precisionCopy.bitsPerPixel == 64)
        precondition(precisionCopy.bytesPerRow == highPrecision.bytesPerRow)
        precondition(precisionCopy.bitmapInfo == highPrecision.bitmapInfo)
        precondition(precisionCopy.colorSpace == highPrecision.colorSpace)
        precondition(precisionCopy.renderingIntent == highPrecision.renderingIntent)
        precondition((precisionCopy.dataProvider!.data! as Data) == highPrecisionBytes)
        let palette: [UInt8] = [255, 0, 0, 0, 255, 0]
        let indexedSpace = palette.withUnsafeBufferPointer {
            CGColorSpace(indexedBaseSpace: CGColorSpace(name: CGColorSpace.sRGB)!, last: 1, colorTable: $0.baseAddress!)!
        }
        let indexedPixels = Data([0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1])
        let indexed = CGImage(width: 4, height: 3, bitsPerComponent: 8, bitsPerPixel: 8,
                              bytesPerRow: 4, space: indexedSpace, bitmapInfo: [],
                              provider: CGDataProvider(data: indexedPixels as CFData)!, decode: nil,
                              shouldInterpolate: false, intent: .defaultIntent)!
        let indexedCopy = materialize(indexed)!
        precondition(indexedCopy.colorSpace == indexedSpace && indexedCopy.bitsPerPixel == 8)
        precondition(pixels(indexedCopy) == pixels(indexed))

    }

    private static func failure(_ message: String) -> NSError {
        NSError(domain: "TLIV.FrameStore", code: 1, userInfo: [NSLocalizedDescriptionKey: message])
    }
}

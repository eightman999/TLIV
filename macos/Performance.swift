import AppKit
import ImageIO
import Darwin

/// Reproducible, opt-in workloads. Each scenario should run in a fresh process.
enum Performance {
    static func fixtures(at directory: URL) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let space = CGColorSpace(name: CGColorSpace.sRGB)!
        let context = CGContext(data: nil, width: 4096, height: 3072, bitsPerComponent: 8, bytesPerRow: 4096 * 4,
                                space: space, bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue)!
        let bytes = context.data!.assumingMemoryBound(to: UInt8.self)
        for y in 0..<3072 { for x in 0..<4096 {
            let index = (y * 4096 + x) * 4
            bytes[index] = UInt8(x & 255); bytes[index + 1] = UInt8(y & 255)
            bytes[index + 2] = UInt8((x / 256 + y / 256) & 255); bytes[index + 3] = 255
        } }
        let raster = directory.appendingPathComponent("large.tiff")
        let target = CGImageDestinationCreateWithURL(raster as CFURL, "public.tiff" as CFString, 1, nil)!
        CGImageDestinationAddImage(target, context.makeImage()!, nil)
        precondition(CGImageDestinationFinalize(target))
        let gif = CGImageDestinationCreateWithURL(directory.appendingPathComponent("animation.gif") as CFURL, "com.compuserve.gif" as CFString, 128, nil)!
        let animation = CGContext(data: nil, width: 512, height: 512, bitsPerComponent: 8, bytesPerRow: 512 * 4,
                                  space: space, bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue)!
        for index in 0..<128 {
            animation.setFillColor(CGColor(srgbRed: CGFloat(index) / 127, green: CGFloat(127-index) / 127, blue: 0.2, alpha: 1))
            animation.fill(CGRect(x: 0, y: 0, width: 512, height: 512))
            CGImageDestinationAddImage(gif, animation.makeImage()!, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: 0.04]] as CFDictionary)
        }
        precondition(CGImageDestinationFinalize(gif))
        print("Created deterministic 4096×3072 TIFF and 128-frame 512×512 GIF")
    }
    static func run(scenario: String, directory: URL) throws {
        let view = Viewer(frame: CGRect(x: 0, y: 0, width: 1600, height: 1000))
        view.playing = false
        var result: [String: Any] = ["scenario": scenario]
        func timed(_ key: String, _ block: () -> Void) {
            let start = ProcessInfo.processInfo.systemUptime
            block(); result[key] = (ProcessInfo.processInfo.systemUptime - start) * 1000
        }
        let url = directory.appendingPathComponent(scenario == "animation" ? "animation.gif" : "large.tiff")
        timed("load_ms") { view.load(url) }
        guard let image = view.current else { throw NSError(domain: "TLIV.Performance", code: 1) }
        let render = CGContext(data: nil, width: 1600, height: 1000, bitsPerComponent: 8, bytesPerRow: 1600 * 4,
                               space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        timed("first_display_ms") { render.draw(image, in: CGRect(x: 0, y: 0, width: 1600, height: 1000)); render.flush() }
        switch scenario {
        case "cursor":
            timed("first_sample_ms") { view.updateCursor(CGPoint(x: view.imageRect.midX, y: view.imageRect.midY)) }
            timed("2000_samples_ms") {
                for index in 0..<2000 {
                    view.updateCursor(CGPoint(x: view.imageRect.minX + CGFloat(index % image.width) * view.scale + view.scale / 2,
                                              y: view.imageRect.minY + CGFloat((index * 7) % image.height) * view.scale + view.scale / 2))
                }
            }
            result["color"] = view.cursorColor
        case "metadata":
            timed("metadata_ms") {
                let report = Metadata.report(url: url, image: image, frames: 1, duration: 0)
                result["report_characters"] = report.count
            }
        case "colors":
            timed("color_count_ms") {
                // Baseline report counted automatically; optimized build calls the bounded counter.
                #if TLIV_BASELINE
                result["report_characters"] = Metadata.report(url: url, image: image, frames: 1, duration: 0).count
                #else
                result["color_result"] = Metadata.colorCount(image: image).report()
                #endif
            }
        case "animation":
            timed("128_frame_visits_ms") {
                for _ in 0..<128 { autoreleasepool {
                    view.stepFrame(1)
                    if let frame = view.current { render.draw(frame, in: CGRect(x: 0, y: 0, width: 512, height: 512)) }
                    render.flush()
                } }
            }
            #if !TLIV_BASELINE
            result["retained_pixel_bytes"] = view.retainedPixelBytes
            result["cached_frames"] = view.cachedFrameCount
            #endif
        case "checker":
            view.background = 0
            let target = CGContext(data: nil, width: 1600, height: 1000, bitsPerComponent: 8, bytesPerRow: 1600 * 4,
                                   space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            let graphics = NSGraphicsContext(cgContext: target, flipped: true)
            timed("60_draws_ms") {
                NSGraphicsContext.saveGraphicsState(); NSGraphicsContext.current = graphics
                defer { NSGraphicsContext.restoreGraphicsState() }
                for _ in 0..<60 { autoreleasepool { view.draw(view.bounds) } }
                target.flush()
            }
        default: throw NSError(domain: "TLIV.Performance", code: 2, userInfo: [NSLocalizedDescriptionKey: "Unknown scenario"])
        }
        var usage = rusage(); getrusage(RUSAGE_SELF, &usage)
        result["peak_rss_bytes"] = usage.ru_maxrss
        result["cpu_user_s"] = Double(usage.ru_utime.tv_sec) + Double(usage.ru_utime.tv_usec) / 1e6
        let data = try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys])
        print(String(data: data, encoding: .utf8)!)
    }
}

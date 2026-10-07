import AppKit
import ImageIO
import UniformTypeIdentifiers

final class Viewer: NSView {
    var frameStore: FrameStore?
    var displayedImage: CGImage?
    var frameCount: Int { frameStore?.frameCount ?? (displayedImage == nil ? 0 : 1) }
    var firstImage: CGImage? { frameStore?.image(at: 0) ?? displayedImage }
    var retainedPixelBytes: Int { frameStore?.retainedPixelBytes ?? displayedImage.map { $0.bytesPerRow * $0.height } ?? 0 }
    var cachedFrameCount: Int { frameStore?.cachedFrameCount ?? (displayedImage == nil ? 0 : 1) }
    var delays: [Double] = []
    var frameIndex = 0
    var scale: CGFloat = 1
    var offset = CGPoint.zero
    var anchor = CGPoint.zero
    var dragged = false
    var selection: CGRect?
    var selecting = false
    var playing = true
    var timer: Timer?
    var file: URL?
    var files: [URL] = []
    var pixel = UserDefaults.standard.object(forKey: "pixel") as? Bool ?? true
    var background = min(3, max(0, UserDefaults.standard.integer(forKey: "background")))
    var customBackground: NSColor = {
        let values = UserDefaults.standard.array(forKey: "customBackground") as? [CGFloat] ?? [0.35, 0.48, 0.54]
        return NSColor(srgbRed: values.count == 3 ? values[0] : 0.35, green: values.count == 3 ? values[1] : 0.48, blue: values.count == 3 ? values[2] : 0.54, alpha: 1)
    }()
    var noConfirm = false
    var stateChanged: (() -> Void)?
    var infoAction: (() -> Void)?
    var settingsAction: (() -> Void)?
    var flashAction: ((String) -> Void)?
    var cursorPosition = ""
    var cursorColor = ""
    var cursorSwatch: NSColor?
    private var lastSample: (frame: Int, x: Int, y: Int)?
    var cursorChanged: (() -> Void)?
    var statusChanged: (() -> Void)?
    private var sampleTile: (frame: Int, x: Int, y: Int)?
    private let sampleContext = CGContext(data: nil, width: 64, height: 64, bitsPerComponent: 8, bytesPerRow: 256,
                                          space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                          bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue)
    static let checkerTile: CGImage = {
        let context = CGContext(data: nil, width: 16, height: 16, bitsPerComponent: 8, bytesPerRow: 64,
                                space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
        context.setFillColor(CGColor(gray: 0.8, alpha: 1)); context.fill(CGRect(x: 0, y: 0, width: 16, height: 16))
        context.setFillColor(CGColor(gray: 0.65, alpha: 1))
        context.fill(CGRect(x: 0, y: 8, width: 8, height: 8)); context.fill(CGRect(x: 8, y: 0, width: 8, height: 8))
        return context.makeImage()!
    }()
    func changed(controls: Bool = true) { needsDisplay = true; if controls { stateChanged?() } else { statusChanged?() } }
    func savePreferences() {
        UserDefaults.standard.set(pixel, forKey: "pixel")
        UserDefaults.standard.set(background, forKey: "background")
        let color = customBackground.usingColorSpace(.sRGB) ?? .black
        UserDefaults.standard.set([color.redComponent, color.greenComponent, color.blueComponent], forKey: "customBackground")
    }
    func togglePixel() { pixel.toggle(); if !pixel { clearCursor() }; savePreferences(); changed() }
    func cycleBackground() { background = (background + 1) % 4; savePreferences(); changed() }
    func toggleSelection() { selecting.toggle(); changed() }
    func togglePlay() { guard frameCount > 1 else { return }; playing.toggle(); schedule(); changed() }
    func stepFrame(_ direction: Int) {
        guard frameCount > 1 else { return }
        playing = false; timer?.invalidate()
        setFrame((frameIndex + direction + frameCount) % frameCount); changed()
    }
    func setFrame(_ index: Int) {
        guard let store = frameStore, let image = store.image(at: index) else { return }
        frameIndex = index; displayedImage = image; lastSample = nil; sampleTile = nil
    }
    func clearCursor() {
        lastSample = nil; sampleTile = nil
        guard !cursorPosition.isEmpty || !cursorColor.isEmpty else { return }
        cursorPosition = ""; cursorColor = ""; cursorSwatch = nil; cursorChanged?()
    }
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas { removeTrackingArea(area) }
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect], owner: self))
    }
    override func mouseMoved(with event: NSEvent) { updateCursor(convert(event.locationInWindow, from: nil)) }
    override func mouseExited(with event: NSEvent) { clearCursor() }
    func updateCursor(_ point: CGPoint) {
        guard pixel, let image = current else { clearCursor(); return }
        let p = imagePoint(point)
        guard p.x >= 0, p.y >= 0, p.x < CGFloat(image.width), p.y < CGFloat(image.height) else {
            clearCursor(); return
        }
        let x = Int(p.x), y = Int(p.y)
        if let lastSample, lastSample.frame == frameIndex && lastSample.x == x && lastSample.y == y { return }
        guard let context = sampleContext else { return }
        let tileX = (x / 64) * 64, tileY = (y / 64) * 64
        if sampleTile?.frame != frameIndex || sampleTile?.x != tileX || sampleTile?.y != tileY {
            let width = min(64, image.width - tileX), height = min(64, image.height - tileY)
            guard let cropped = image.cropping(to: CGRect(x: tileX, y: tileY, width: width, height: height)) else { return }
            context.setBlendMode(.copy); context.interpolationQuality = .none
            context.draw(cropped, in: CGRect(x: 0, y: 64 - height, width: width, height: height))
            sampleTile = (frameIndex, tileX, tileY)
        }
        guard let data = context.data else { return }
        let bytes = data.assumingMemoryBound(to: UInt8.self).advanced(by: (y - tileY) * 256 + (x - tileX) * 4)
        let alpha = Int(bytes[3])
        func channel(_ component: Int) -> Int { alpha == 0 ? 0 : min(255, (Int(bytes[component]) * 255 + alpha / 2) / alpha) }
        let red = channel(0), green = channel(1), blue = channel(2)
        lastSample = (frameIndex, x, y); cursorPosition = "\(x), \(y)"
        cursorSwatch = NSColor(srgbRed: CGFloat(red) / 255, green: CGFloat(green) / 255, blue: CGFloat(blue) / 255, alpha: 1)
        cursorColor = alpha == 0 ? tr("ST_TRANSPARENT") : String(format: "#%02X%02X%02X", red, green, blue) + (alpha < 255 ? " a\(alpha)" : "")
        cursorChanged?()
    }
    override var acceptsFirstResponder: Bool { true }
    override var isFlipped: Bool { true }
    override var wantsDefaultClipping: Bool { true }
    var current: CGImage? { displayedImage }
    var imageRect: CGRect {
        guard let image = current else { return .zero }
        let x = bounds.midX + offset.x - CGFloat(image.width) * scale / 2
        let y = bounds.midY + offset.y - CGFloat(image.height) * scale / 2
        return CGRect(x: pixel ? floor(x) : x,
                      y: pixel ? floor(y) : y,
                      width: CGFloat(image.width) * scale, height: CGFloat(image.height) * scale)
    }
    func showError(_ message: String) {
        let alert = NSAlert(); alert.messageText = message; alert.runModal()
    }
    func load(_ input: URL) {
        let url = input.resolvingSymlinksInPath()
        let store: FrameStore?, image: CGImage
        do {
            if url.pathExtension.lowercased() == "svg" {
                store = nil; image = try SVGDecoder.decode(url: url)
            } else {
                let opened = try FrameStore.open(url: url)
                guard let first = opened.image(at: 0) else { throw NSError(domain: "TLIV", code: 1) }
                store = opened; image = first
            }
        } catch { showError(tr("CANNOT_OPEN") + "\n" + error.localizedDescription); return }
        timer?.invalidate(); frameStore = store; displayedImage = image
        delays = store?.delays ?? [0.1]; frameIndex = 0; file = url; selection = nil
        clearCursor()
        let supported = Set(["png", "jpg", "jpeg", "bmp", "gif", "webp", "apng", "ico", "tif", "tiff", "heic", "svg"])
        files = ((try? FileManager.default.contentsOfDirectory(at: url.deletingLastPathComponent(), includingPropertiesForKeys: [.isRegularFileKey])) ?? [])
            .filter { supported.contains($0.pathExtension.lowercased()) && (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true }
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
        window?.title = "\(url.lastPathComponent) — TLIV"; reset(); stateChanged?(); schedule()
    }
    func schedule() {
        timer?.invalidate()
        guard playing, frameCount > 1 else { return }
        timer = Timer.scheduledTimer(withTimeInterval: delays[frameIndex], repeats: false) { [weak self] _ in
            guard let self = self else { return }
            self.setFrame((self.frameIndex + 1) % self.frameCount); self.changed(controls: false); self.schedule()
        }
    }
    func reset() {
        guard let image = current else { return }
        let fit = min(bounds.width / CGFloat(image.width), bounds.height / CGFloat(image.height), 1)
        scale = max(0.01, fit); offset = .zero; changed(controls: false)
    }
    override func draw(_ dirtyRect: NSRect) {
        let colors: [NSColor] = [classicShadow, .black, .white, customBackground]
        colors[min(3, max(0, background))].setFill(); bounds.fill()
        guard let image = current, let context = NSGraphicsContext.current?.cgContext else {
            let text = tr("EMPTY") as NSString
            text.draw(at: CGPoint(x: 30, y: 40), withAttributes: [.foregroundColor: NSColor.white]); return
        }
        let rect = imageRect
        if background == 0 {
            context.saveGState(); context.clip(to: rect)
            context.interpolationQuality = .none
            context.draw(Self.checkerTile, in: CGRect(x: 0, y: 0, width: 16, height: 16), byTiling: true)
            context.restoreGState()
        }
        context.saveGState(); context.interpolationQuality = pixel || scale >= 1 ? .none : .high
        context.translateBy(x: rect.minX, y: rect.maxY); context.scaleBy(x: 1, y: -1)
        context.draw(image, in: CGRect(origin: .zero, size: rect.size)); context.restoreGState()
        if let selection = selection, !selection.isNull, !selection.isEmpty {
            NSColor.black.setStroke()
            let outline = NSBezierPath(rect: CGRect(x: rect.minX + selection.minX * scale, y: rect.minY + selection.minY * scale,
                                     width: selection.width * scale, height: selection.height * scale))
            outline.stroke(); NSColor.white.setStroke(); outline.setLineDash([4, 4], count: 2, phase: 0); outline.stroke()
        }
    }
    func imagePoint(_ point: CGPoint) -> CGPoint {
        let rect = imageRect
        return CGPoint(x: (point.x - rect.minX) / scale, y: (point.y - rect.minY) / scale)
    }
    override func mouseDown(with event: NSEvent) {
        dragged = false; window?.makeFirstResponder(self); anchor = convert(event.locationInWindow, from: nil)
        if selecting { selection = nil }
    }
    override func mouseDragged(with event: NSEvent) {
        dragged = true
        let point = convert(event.locationInWindow, from: nil)
        if selecting, let image = current {
            let start = imagePoint(anchor), end = imagePoint(point)
            selection = CGRect(x: floor(min(start.x, end.x)), y: floor(min(start.y, end.y)),
                               width: ceil(abs(end.x - start.x)), height: ceil(abs(end.y - start.y)))
                .intersection(CGRect(x: 0, y: 0, width: image.width, height: image.height))
        } else { offset.x += point.x - anchor.x; offset.y += point.y - anchor.y; anchor = point }
        changed(controls: false)
    }
    override func mouseUp(with event: NSEvent) {
        if !selecting, !dragged, event.clickCount == 1, convert(event.locationInWindow, from: nil) == anchor {
            togglePlay()
        }
    }
    override func scrollWheel(with event: NSEvent) {
        if event.modifierFlags.contains(.shift), event.scrollingDeltaY != 0 { navigate(event.scrollingDeltaY > 0 ? -1 : 1); return }
        guard event.scrollingDeltaY != 0 else { return }
        zoom(event.scrollingDeltaY > 0 ? 1 : -1, at: convert(event.locationInWindow, from: nil))
    }
    func zoom(_ direction: Int, at point: CGPoint) {
        guard current != nil else { return }
        let old = scale
        if pixel {
            scale = direction > 0 ? (scale >= 1 ? floor(scale) + 1 : min(1, scale * 1.25)) : (scale > 1 ? ceil(scale) - 1 : scale / 1.25)
        } else { scale *= direction > 0 ? 1.1 : 1 / 1.1 }
        scale = min(128, max(0.01, scale))
        offset.x = point.x - bounds.midX - (point.x - bounds.midX - offset.x) * scale / old
        offset.y = point.y - bounds.midY - (point.y - bounds.midY - offset.y) * scale / old
        changed(controls: false)
    }
    func navigate(_ direction: Int) {
        guard let file = file, let index = files.firstIndex(where: { $0.standardizedFileURL.path == file.standardizedFileURL.path }), !files.isEmpty else { return }
        load(files[(index + direction + files.count) % files.count])
    }
    @objc func openImage(_ sender: Any?) {
        let panel = NSOpenPanel(); panel.allowedContentTypes = [.image, .svg]; panel.canChooseDirectories = false
        panel.title = Language.shared.label("P_OPEN"); panel.prompt = Language.shared.label("P_OPEN").replacingOccurrences(of: "...", with: "")
        if panel.runModal() == .OK, let url = panel.url { load(url) }
    }
    @objc func copy(_ sender: Any?) {
        guard let image = current else { return }
        let cropped = selection.flatMap { !$0.isNull && !$0.isEmpty ? image.cropping(to: $0) : nil } ?? image
        let bitmap = NSBitmapImageRep(cgImage: cropped)
        guard let png = bitmap.representation(using: .png, properties: [:]) else { return }
        NSPasteboard.general.clearContents(); NSPasteboard.general.setData(png, forType: .png)
        flashAction?(tr("COPIED").replacingOccurrences(of: "%dx%d", with: "\(cropped.width)x\(cropped.height)"))
    }
    @objc override func selectAll(_ sender: Any?) {
        guard let image = current else { return }
        selection = CGRect(x: 0, y: 0, width: image.width, height: image.height); changed()
    }
    func trash() {
        guard let file = file else { return }
        if !noConfirm {
            let alert = NSAlert(); alert.messageText = tr("CONFIRM_BIN").replacingOccurrences(of: "%s", with: file.lastPathComponent)
            alert.addButton(withTitle: Language.shared.label("BTN_YES")); alert.addButton(withTitle: tr("BTN_CANCEL"))
            guard alert.runModal() == .alertFirstButtonReturn else { return }
        }
        let next = files.first { $0.standardizedFileURL.path != file.standardizedFileURL.path }
        do {
            try FileManager.default.trashItem(at: file, resultingItemURL: nil)
            if let next = next { load(next) } else { timer?.invalidate(); frameStore = nil; displayedImage = nil; self.file = nil; selection = nil; clearCursor(); window?.title = "TLIV"; changed() }
        } catch { showError(tr("CANT_DELETE").replacingOccurrences(of: "%s", with: file.lastPathComponent) + "\n" + error.localizedDescription) }
    }
    func info() { infoAction?() }
    override func keyDown(with event: NSEvent) {
        if event.modifierFlags.contains(.command) || event.modifierFlags.contains(.control) {
            switch event.charactersIgnoringModifiers?.lowercased() {
            case "o": openImage(nil)
            case "a": selectAll(nil)
            case "c": copy(nil)
            case ",": settingsAction?()
            default: super.keyDown(with: event)
            }
            return
        }
        switch event.keyCode {
        case 123: navigate(-1)
        case 124: navigate(1)
        case 51, 117: trash()
        case 53: selection = nil; changed()
        default:
            switch event.charactersIgnoringModifiers?.lowercased() {
            case "r": reset()
            case "p": togglePixel()
            case "b": cycleBackground()
            case "s": toggleSelection()
            case " ": togglePlay()
            case "q": stepFrame(-1)
            case "e": stepFrame(1)
            case "i": info()
            default: super.keyDown(with: event)
            }
        }
    }
    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { .copy }
    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        guard let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self]) as? [URL], let url = urls.first else { return false }
        load(url); return true
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    let viewer = Viewer(frame: CGRect(x: 0, y: 0, width: 900, height: 650))
    lazy var ui = ClassicUI(viewer: viewer)
    var window: NSWindow!
    func applicationDidFinishLaunching(_ notification: Notification) {
        if let iconURL = Bundle.main.url(forResource: "TLIV", withExtension: "icns"), let icon = NSImage(contentsOf: iconURL) {
            NSApplication.shared.applicationIconImage = icon
        }
        window = NSWindow(contentRect: ui.bounds, styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = viewer.file.map { "\($0.lastPathComponent) — TLIV" } ?? "TLIV"
        window.contentView = ui; window.contentMinSize = NSSize(width: 640, height: 300)
        if !window.setFrameAutosaveName("TLIV") { window.center() }
        window.acceptsMouseMovedEvents = true; window.makeKeyAndOrderFront(nil); window.makeFirstResponder(viewer)
        viewer.registerForDraggedTypes([.fileURL])
        ui.languageDidChange = { [weak self] in self?.installMenus() }
        installMenus(); ui.needsLayout = true; ui.layoutSubtreeIfNeeded()
        if ui.infoOn {
            let width = ui.preferredInfoWidth
            ui.splitter.setPosition(max(200, ui.splitter.bounds.width - max(120, width == 0 ? 240 : width)), ofDividerAt: 0)
        }
        ui.restoringLayout = false
        NSApplication.shared.activate(ignoringOtherApps: true)
        if CommandLine.arguments.count > 1 { viewer.load(URL(fileURLWithPath: CommandLine.arguments[1])) }
        else if viewer.current != nil { viewer.reset(); ui.refresh() }
    }
    func installMenus() {
        let menu = NSMenu(), appMenu = NSMenu()
        let appItem = NSMenuItem(); appItem.submenu = appMenu; menu.addItem(appItem)
        let settings = NSMenuItem(title: Language.shared.label("P_SETTINGS"), action: #selector(ClassicUI.menuAction(_:)), keyEquivalent: ",")
        settings.target = ui; settings.representedObject = "settings"; appMenu.addItem(settings)
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: Language.shared.label("P_EXIT") + " TLIV", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        for (index, key) in ["M_FILE", "M_EDIT", "M_VIEW", "M_HELP"].enumerated() {
            let item = NSMenuItem(title: Language.shared.label(key), action: nil, keyEquivalent: "")
            item.submenu = ui.menus[index]; menu.addItem(item)
        }
        NSApplication.shared.mainMenu = menu
    }
    func application(_ sender: NSApplication, openFiles filenames: [String]) {
        if let name = filenames.first { viewer.load(URL(fileURLWithPath: name)) }
        sender.reply(toOpenOrPrint: .success)
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
if CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--make-performance-fixtures" {
    try Performance.fixtures(at: URL(fileURLWithPath: CommandLine.arguments[2]))
} else if CommandLine.arguments.count == 4 && CommandLine.arguments[1] == "--benchmark" {
    try Performance.run(scenario: CommandLine.arguments[2], directory: URL(fileURLWithPath: CommandLine.arguments[3]))
} else if CommandLine.arguments.contains("--self-test-ui") {
    _ = NSApplication.shared
    let viewer = Viewer(frame: .zero)
    let ui = ClassicUI(viewer: viewer)
    viewer.background = 1; ui.infoOn = true; ui.infoWrap.isHidden = false
    let testWindow = NSWindow(contentRect: ui.bounds, styleMask: [.titled, .resizable], backing: .buffered, defer: false)
    testWindow.contentView = ui
    ui.needsLayout = true; ui.layoutSubtreeIfNeeded()
    precondition(ui.splitter.frame.height > 100 && ui.imageWrap.frame.height > 100 && viewer.frame.height > 100)
    precondition(ui.buttons.count == 14 && ui.buttons.values.allSatisfy { $0.icon != nil })
    precondition([ui, ui.splitter, ui.imageWrap, ui.infoWrap, viewer, ui.status].allSatisfy { $0.clipsToBounds })
    let iconURL = Bundle.main.url(forResource: "TLIV", withExtension: "icns")!
    let iconSource = CGImageSourceCreateWithURL(iconURL as CFURL, nil)!
    var iconSizes = Set<Int>()
    for index in 0..<CGImageSourceGetCount(iconSource) {
        let props = CGImageSourceCopyPropertiesAtIndex(iconSource, index, nil) as! [String: Any]
        iconSizes.insert(props[kCGImagePropertyPixelWidth as String] as! Int)
        precondition(CGImageSourceCreateImageAtIndex(iconSource, index, nil) != nil)
    }
    precondition(iconSizes == Set([16, 32, 64, 128, 256, 512, 1024]))
    let rendered = ui.bitmapImageRepForCachingDisplay(in: ui.bounds)!
    ui.cacheDisplay(in: ui.bounds, to: rendered)
    let left = rendered.colorAt(x: rendered.pixelsWide * 2 / 9, y: rendered.pixelsHigh / 2)!.usingColorSpace(.sRGB)!
    let right = rendered.colorAt(x: rendered.pixelsWide * 7 / 9, y: rendered.pixelsHigh / 2)!.usingColorSpace(.sRGB)!
    precondition(left.redComponent < 0.01 && right.redComponent > 0.99, "Pane paint must stay within its own bounds")
    let button = ui.buttons["pixel"]!
    button.needsDisplay = false; button.state = button.state == .on ? .off : .on
    precondition(button.needsDisplay)
    button.needsDisplay = false; button.isEnabled = !button.isEnabled
    precondition(button.needsDisplay)
    print("PASS: pane clipping/rendering, initial split layout, 14 toolbar icons, state/disabled redraw and seven app icon sizes")
} else if CommandLine.arguments.contains("--self-test") {
    let view = Viewer(frame: CGRect(x: 0, y: 0, width: 900, height: 650))
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    var bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 16, pixelsHigh: 8, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    bitmap = bitmap.retagging(with: .sRGB)!
    bitmap.bitmapData!.initialize(repeating: 0, count: bitmap.bytesPerRow * bitmap.pixelsHigh)
    bitmap.bitmapData![0] = 255; bitmap.bitmapData![3] = 255
    let png = directory.appendingPathComponent("01.png")
    try bitmap.representation(using: .png, properties: [:])!.write(to: png)
    let gif = directory.appendingPathComponent("02.gif")
    let destination = CGImageDestinationCreateWithURL(gif as CFURL, UTType.gif.identifier as CFString, 2, nil)!
    for index in 0..<2 {
        bitmap.bitmapData![0] = index == 0 ? 255 : 0
        bitmap.bitmapData![1] = index == 0 ? 0 : 255
        CGImageDestinationAddImage(destination, bitmap.cgImage!, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: 0.2]] as CFDictionary)
    }
    precondition(CGImageDestinationFinalize(destination))
    view.load(png)
    precondition(view.current?.width == 16 && view.current?.height == 8)
    precondition(view.scale == 1 && view.files.count == 2)
    var fullUpdates = 0, cursorUpdates = 0
    view.stateChanged = { fullUpdates += 1 }; view.cursorChanged = { cursorUpdates += 1 }
    view.updateCursor(CGPoint(x: view.imageRect.minX + 0.5, y: view.imageRect.minY + 0.5))
    precondition(view.cursorPosition == "0, 0" && view.cursorColor == "#FF0000", "cursor: \(view.cursorPosition) \(view.cursorColor)")
    for _ in 0..<100 { view.updateCursor(CGPoint(x: view.imageRect.minX + 0.5, y: view.imageRect.minY + 0.5)) }
    precondition(fullUpdates == 0 && cursorUpdates == 1)
    view.updateCursor(CGPoint(x: view.imageRect.maxX - 0.5, y: view.imageRect.maxY - 0.5))
    precondition(view.cursorPosition == "15, 7" && view.cursorColor == tr("ST_TRANSPARENT"))
    let oldPixel = view.pixel; view.pixel = false
    let zoomPoint = CGPoint(x: view.imageRect.minX + 3, y: view.imageRect.minY + 2)
    let beforeZoom = view.imagePoint(zoomPoint)
    view.zoom(1, at: zoomPoint)
    let afterZoom = view.imagePoint(zoomPoint)
    precondition(abs(beforeZoom.x - afterZoom.x) < 0.001 && abs(beforeZoom.y - afterZoom.y) < 0.001)
    view.pixel = oldPixel; view.reset()
    view.selectAll(nil)
    precondition(view.selection == CGRect(x: 0, y: 0, width: 16, height: 8))
    precondition(view.current!.cropping(to: CGRect(x: 0, y: 0, width: 4, height: 3))!.width == 4)
    view.navigate(1)
    precondition(view.file?.path == gif.resolvingSymlinksInPath().path && view.frameCount == 2)
    precondition(abs(view.delays[0] - 0.2) < 0.01)
    view.stepFrame(1); precondition(view.frameIndex == 1 && !view.playing)
    view.stepFrame(-1); precondition(view.frameIndex == 0)
    view.navigate(1)
    precondition(view.file?.path == png.resolvingSymlinksInPath().path)
    view.timer?.invalidate()
    let svg = directory.appendingPathComponent("03.svg")
    try "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='8'><rect width='16' height='8' fill='red'/></svg>".write(to: svg, atomically: true, encoding: .utf8)
    view.load(svg)
    precondition(view.current?.width == 16 && view.frameCount == 1 && view.files.count == 3)
    precondition(Metadata.report(url: svg, image: view.current, frames: 1, duration: 0).contains("Format: SVG"))
    let corners = directory.appendingPathComponent("04.svg")
    try "<svg xmlns='http://www.w3.org/2000/svg' width='70' height='65'><rect width='70' height='65' fill='red'/><rect x='64' width='6' height='65' fill='blue'/><rect y='64' width='64' height='1' fill='green'/></svg>".write(to: corners, atomically: true, encoding: .utf8)
    view.load(corners); view.pixel = true
    for (x,y,channel) in [(0,0,0),(69,0,2),(0,64,1),(69,64,2)] {
        view.updateCursor(CGPoint(x: view.imageRect.minX + CGFloat(x) + 0.5, y: view.imageRect.minY + CGFloat(y) + 0.5))
        let color = view.cursorSwatch!.usingColorSpace(.sRGB)!
        let values = [color.redComponent, color.greenComponent, color.blueComponent]
        precondition(values[channel] > 0.4 && values[(channel+1)%3] < 0.1)
    }
    try SVGDecoder.selfTest()
    try Metadata.selfTest()
    try FrameStore.selfTest()
    precondition(localizedStrings.values.allSatisfy { $0.count == 4 && $0.allSatisfy { !$0.isEmpty } })
    precondition(localizedStrings["M_FILE"] == ["File", "ファイル(F)", "文件(F)", "檔案(F)"])
    print("PASS: four-language strings, SVG shape/color/resource checks, EXIF/TIFF metadata and PNG text")
    print("PASS: PNG decode, GIF frames/timing, fit, selection, crop, cursor HEX, zoom anchor, frame stepping, navigation, SVG loading")
} else {
let app = NSApplication.shared
let delegate = AppDelegate()
app.setActivationPolicy(.regular)
app.delegate = delegate
app.run()
}

import AppKit

final class Language {
    static let shared = Language()
    static let codes = ["en", "ja", "zh-CN", "zh-TW"]
    static let names = ["English", "日本語", "简体中文", "繁體中文"]
    var index: Int {
        didSet { index = min(3, max(0, index)); UserDefaults.standard.set(Self.codes[index], forKey: "language") }
    }
    init() { index = Self.codes.firstIndex(of: UserDefaults.standard.string(forKey: "language") ?? "en") ?? 0 }
    func text(_ key: String) -> String {
        let value = localizedStrings[key]?[index] ?? key
        return value.replacingOccurrences(of: "Ctrl+", with: "⌘")
    }
    func label(_ key: String) -> String {
        text(key).replacingOccurrences(of: #"\(&?[A-Z]\)"#, with: "", options: .regularExpression).replacingOccurrences(of: "&", with: "")
    }
}
func tr(_ key: String) -> String { Language.shared.text(key) }
let classicFace = NSColor(white: 192 / 255, alpha: 1)
let classicShadow = NSColor(white: 128 / 255, alpha: 1)
let classicLight = NSColor(white: 223 / 255, alpha: 1)
let classicFont = NSFont(name: "Tahoma", size: 11) ?? NSFont.systemFont(ofSize: 12)
func fill(_ rect: CGRect, _ color: NSColor) { color.setFill(); rect.fill() }
func bevel(_ rect: CGRect, inset: Bool = true) {
    fill(CGRect(x: rect.minX, y: rect.minY, width: rect.width, height: 1), inset ? classicShadow : .white)
    fill(CGRect(x: rect.minX, y: rect.minY, width: 1, height: rect.height), inset ? classicShadow : .white)
    fill(CGRect(x: rect.minX, y: rect.maxY - 1, width: rect.width, height: 1), inset ? .white : classicShadow)
    fill(CGRect(x: rect.maxX - 1, y: rect.minY, width: 1, height: rect.height), inset ? .white : classicShadow)
}
func classicText(_ text: String, rect: CGRect, color: NSColor = .black) {
    let style = NSMutableParagraphStyle(); style.lineBreakMode = .byTruncatingTail
    (text as NSString).draw(in: rect, withAttributes: [.font: classicFont, .foregroundColor: color, .paragraphStyle: style])
}

final class ClassicButton: NSButton {
    var icon: NSImage? { didSet { needsDisplay = true } }
    override var state: NSControl.StateValue { didSet { if oldValue != state { needsDisplay = true } } }
    override var isEnabled: Bool { didSet { if oldValue != isEnabled { needsDisplay = true } } }
    var isMenu = false
    var hover = false
    override var isFlipped: Bool { true }
    override var wantsDefaultClipping: Bool { true }
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas { removeTrackingArea(area) }
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect], owner: self))
    }
    override func mouseEntered(with event: NSEvent) { hover = true; needsDisplay = true }
    override func mouseExited(with event: NSEvent) { hover = false; needsDisplay = true }
    override func draw(_ dirtyRect: NSRect) {
        fill(bounds, state == .on ? classicLight : classicFace)
        if state == .on || isHighlighted || hover { bevel(bounds, inset: state == .on || isHighlighted) }
        if let icon = icon {
            NSGraphicsContext.current?.imageInterpolation = .none
            icon.draw(in: CGRect(x: (bounds.width - 16) / 2, y: (bounds.height - 16) / 2, width: 16, height: 16),
                      from: .zero, operation: .sourceOver, fraction: isEnabled ? 1 : 0.3, respectFlipped: true, hints: nil)
        } else { classicText(title, rect: bounds.insetBy(dx: 6, dy: 2), color: isEnabled ? .black : classicShadow) }
    }
}

final class StatusBar: NSView {
    weak var viewer: Viewer?
    var flash = ""
    override var isFlipped: Bool { true }
    override var wantsDefaultClipping: Bool { true }
    private var panes: [(String, CGFloat)] = []
    func refresh(cursorOnly: Bool = false) {
        guard let viewer else { return }
        if cursorOnly {
            guard viewer.pixel, panes.count >= 2 else { return }
            let count = panes.count
            guard panes[count-2].0 != viewer.cursorPosition || panes[count-1].0 != viewer.cursorColor else { return }
            panes[count-2].0 = viewer.cursorPosition; panes[count-1].0 = viewer.cursorColor
            setNeedsDisplay(CGRect(x: max(0, bounds.width - 195), y: 0, width: 195, height: bounds.height))
            setAccessibilityValue(panes.map { $0.0 }.joined(separator: " | "))
            return
        }
        var panes: [(String, CGFloat)] = []
        if let image = viewer.current {
            panes += [("\(image.width) × \(image.height)", 82)]
            if viewer.frameCount > 1 { panes += [("\(viewer.frameIndex + 1)/\(viewer.frameCount)", 48)] }
            let mode = tr(viewer.pixel ? "ST_PIXEL" : "ST_NORMAL")
            let modeWidth = max(52, (mode as NSString).size(withAttributes: [.font: classicFont]).width + 10)
            panes += [(String(format: "%.1f%%", viewer.scale * 100), 65), (mode, modeWidth)]
            if viewer.pixel { panes += [(viewer.cursorPosition, 76), (viewer.cursorColor, 113)] }
        }
        let index = viewer.file.flatMap { file in viewer.files.firstIndex { $0.path == file.path } }.map { "\($0 + 1)/\(viewer.files.count)" } ?? ""
        panes.insert((index, max(34, CGFloat(index.count * 7 + 10))), at: 0)
        let used = panes.reduce(CGFloat(0)) { $0 + $1.1 + 2 }
        panes.insert((flash.isEmpty ? viewer.file?.lastPathComponent ?? "" : flash, max(0, bounds.width - used - 2)), at: 0)
        self.panes = panes; needsDisplay = true
        setAccessibilityElement(true); setAccessibilityRole(.staticText)
        setAccessibilityValue(panes.map { $0.0 }.joined(separator: " | "))
    }
    override func setFrameSize(_ newSize: NSSize) { super.setFrameSize(newSize); refresh() }
    override func draw(_ dirtyRect: NSRect) {
        fill(dirtyRect, classicFace)
        guard let viewer else { return }
        var x: CGFloat = 0
        for (text, width) in panes {
            let rect = CGRect(x: x, y: 0, width: width, height: bounds.height)
            defer { x += width + 2 }
            guard rect.intersects(dirtyRect) else { continue }
            bevel(rect)
            var textRect = rect.insetBy(dx: 5, dy: 3)
            if viewer.pixel, text == viewer.cursorColor, !text.isEmpty, let color = viewer.cursorSwatch {
                fill(CGRect(x: rect.minX + 5, y: 5, width: 10, height: 10), .black)
                fill(CGRect(x: rect.minX + 6, y: 6, width: 8, height: 8), color)
                textRect.origin.x += 13; textRect.size.width -= 13
            }
            classicText(text, rect: textRect, color: text == tr("ST_PIXEL") ? NSColor(red: 0, green: 0, blue: 0.5, alpha: 1) : .black)
        }
    }
}

final class FramedView: NSView {
    override var isFlipped: Bool { true }
    override var wantsDefaultClipping: Bool { true }
    override func draw(_ dirtyRect: NSRect) { fill(dirtyRect, classicFace); bevel(bounds); bevel(bounds.insetBy(dx: 1, dy: 1)); }
}

final class WorkCancellation {
    private let lock = NSLock()
    private var cancelled = false
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
}

final class ClassicUI: NSView, NSSplitViewDelegate {
    let viewer: Viewer
    let splitter = NSSplitView()
    let imageWrap = FramedView()
    let infoWrap = FramedView()
    let infoScroll = NSScrollView()
    let infoText = NSTextView()
    let countButton = ClassicButton()
    let status = StatusBar()
    var buttons: [String: ClassicButton] = [:]
    var menuButtons: [ClassicButton] = []
    var menus: [NSMenu] = []
    var infoOn = UserDefaults.standard.bool(forKey: "infoOn")
    let preferredInfoWidth = UserDefaults.standard.double(forKey: "infoWidth")
    var restoringLayout = true
    var metadataSerial = 0
    let metadataQueue = DispatchQueue(label: "TLIV.metadata", qos: .userInitiated)
    var metadataWork: DispatchWorkItem?
    var metadataCancellation = WorkCancellation()
    var baseReport = ""
    var colorResult: Metadata.ColorCountResult?
    var colorRequested = false
    var infoURL: URL?
    var languageDidChange: (() -> Void)?
    let tools: [(String, String)?] = [
        ("prev", "TIP_PREV"), ("next", "TIP_NEXT"), nil,
        ("fback", "TIP_FPREV"), ("play", "TIP_PLAY"), ("fnext", "TIP_FNEXT"), nil,
        ("zoomin", "TIP_ZOOMIN"), ("zoomout", "TIP_ZOOMOUT"), ("reset", "TIP_RESET"), nil,
        ("pixel", "TIP_PIXEL"), ("bg", "TIP_BG"), nil,
        ("select", "TIP_SELECT"), ("copy", "TIP_COPY"), nil, ("info", "TIP_INFO"), nil, ("delete", "TIP_DELETE")]
    override var isFlipped: Bool { true }
    override var wantsDefaultClipping: Bool { true }
    init(viewer: Viewer) {
        self.viewer = viewer
        super.init(frame: CGRect(x: 0, y: 0, width: 900, height: 650))
        for view in [self, viewer, imageWrap, infoWrap, status] { view.clipsToBounds = true }
        splitter.isVertical = true; splitter.dividerStyle = .thin; splitter.delegate = self
        splitter.clipsToBounds = true
        addSubview(splitter); splitter.addArrangedSubview(imageWrap); splitter.addArrangedSubview(infoWrap)
        imageWrap.addSubview(viewer); infoWrap.addSubview(infoScroll); infoWrap.addSubview(countButton)
        countButton.clipsToBounds = true
        countButton.title = tr("COUNT_COLORS"); countButton.target = self; countButton.action = #selector(countColors)
        countButton.isBordered = false; countButton.isEnabled = false
        viewer.autoresizingMask = [.width, .height]
        infoScroll.hasVerticalScroller = true; infoScroll.hasHorizontalScroller = false
        infoScroll.autoresizingMask = [.width, .height]
        infoScroll.documentView = infoText
        infoText.isEditable = false; infoText.isSelectable = true; infoText.isRichText = false
        infoText.font = classicFont
        infoText.backgroundColor = .white; infoText.textColor = .black
        infoText.isVerticallyResizable = true; infoText.isHorizontallyResizable = false
        infoText.autoresizingMask = [.width]
        infoText.textContainer?.widthTracksTextView = true
        infoText.textContainer?.containerSize = NSSize(width: 240, height: CGFloat.greatestFiniteMagnitude)
        infoText.minSize = .zero; infoText.maxSize = NSSize(width: 10000, height: CGFloat.greatestFiniteMagnitude)
        infoText.setAccessibilityLabel(tr("P_INFO"))
        infoWrap.isHidden = !infoOn
        status.viewer = viewer; addSubview(status)
        for (index, key) in ["M_FILE", "M_EDIT", "M_VIEW", "M_HELP"].enumerated() {
            let button = ClassicButton(); button.clipsToBounds = true; button.isMenu = true; button.tag = index
            button.target = self; button.action = #selector(openMenu(_:)); button.identifier = NSUserInterfaceItemIdentifier(key)
            menuButtons.append(button); addSubview(button)
        }
        for tool in tools.compactMap({ $0 }) {
            let button = ClassicButton(); button.clipsToBounds = true; button.identifier = NSUserInterfaceItemIdentifier(tool.0)
            let url = Bundle.main.resourceURL?.appendingPathComponent("icon_\(tool.0).png")
            button.icon = url.flatMap { NSImage(contentsOf: $0) }
            button.target = self; button.action = #selector(toolAction(_:)); button.isBordered = false
            buttons[tool.0] = button; addSubview(button)
        }
        viewer.stateChanged = { [weak self] in self?.refresh() }
        viewer.cursorChanged = { [weak self] in self?.status.refresh(cursorOnly: true) }
        viewer.statusChanged = { [weak self] in self?.status.refresh() }
        viewer.infoAction = { [weak self] in self?.toggleInfo() }
        viewer.settingsAction = { [weak self] in self?.settings() }
        viewer.flashAction = { [weak self] text in
            self?.status.flash = text; self?.status.refresh()
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.status.flash = ""; self?.status.refresh() }
        }
        localize()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func draw(_ dirtyRect: NSRect) {
        fill(dirtyRect, classicFace)
        fill(CGRect(x: 2, y: 20, width: bounds.width - 4, height: 1), classicShadow)
        fill(CGRect(x: 2, y: 21, width: bounds.width - 4, height: 1), .white)
        var x: CGFloat = 4
        for tool in tools {
            if tool == nil { fill(CGRect(x: x + 3, y: 26, width: 1, height: 18), classicShadow); fill(CGRect(x: x + 4, y: 26, width: 1, height: 18), .white); x += 8 }
            else { x += 24 }
        }
    }
    override func layout() {
        super.layout()
        var x: CGFloat = 2
        for button in menuButtons {
            let width = (button.title as NSString).size(withAttributes: [.font: classicFont]).width + 12
            button.frame = CGRect(x: x, y: 2, width: width, height: 18); x += width
        }
        x = 4
        for tool in tools {
            guard let tool = tool else { x += 8; continue }
            buttons[tool.0]?.frame = CGRect(x: x, y: 22, width: 24, height: 22); x += 24
        }
        splitter.frame = CGRect(x: 2, y: 46, width: bounds.width - 4, height: max(100, bounds.height - 70))
        // Initial arranged views start with zero height; explicitly lay out the split.
        splitter.adjustSubviews()
        viewer.frame = imageWrap.bounds.insetBy(dx: 4, dy: 4)
        countButton.frame = CGRect(x: 2, y: 2, width: max(1, infoWrap.bounds.width - 4), height: 22)
        infoScroll.frame = CGRect(x: 2, y: 26, width: max(1, infoWrap.bounds.width - 4), height: max(1, infoWrap.bounds.height - 28))
        infoText.setFrameSize(NSSize(width: max(1, infoScroll.contentSize.width), height: max(1, infoText.frame.height)))
        status.frame = CGRect(x: 2, y: bounds.height - 22, width: bounds.width - 4, height: 20)
    }
    func splitView(_ splitView: NSSplitView, constrainMinCoordinate proposedMinimumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat { 200 }
    func splitView(_ splitView: NSSplitView, constrainMaxCoordinate proposedMaximumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat { max(200, splitView.bounds.width - 120) }
    func splitViewDidResizeSubviews(_ notification: Notification) {
        viewer.frame = imageWrap.bounds.insetBy(dx: 4, dy: 4); countButton.frame = CGRect(x: 2, y: 2, width: max(1, infoWrap.bounds.width - 4), height: 22)
        infoScroll.frame = CGRect(x: 2, y: 26, width: max(1, infoWrap.bounds.width - 4), height: max(1, infoWrap.bounds.height - 28))
        infoText.setFrameSize(NSSize(width: max(1, infoScroll.contentSize.width), height: max(1, infoText.frame.height)))
        if infoOn && !restoringLayout { UserDefaults.standard.set(infoWrap.frame.width, forKey: "infoWidth") }
    }
    func localize() {
        for button in menuButtons { button.title = Language.shared.text(button.identifier!.rawValue); button.setAccessibilityLabel(button.title) }
        for tool in tools.compactMap({ $0 }) {
            buttons[tool.0]?.toolTip = tr(tool.1); buttons[tool.0]?.setAccessibilityLabel(tr(tool.1))
        }
        infoText.setAccessibilityLabel(Language.shared.label("P_INFO"))
        countButton.title = tr("COUNT_COLORS"); countButton.setAccessibilityLabel(tr("COUNT_COLORS"))
        menus = makeMenus(); needsLayout = true; needsDisplay = true; viewer.needsDisplay = true
        languageDidChange?(); requestMetadata(force: true); refresh()
    }
    func refresh() {
        status.refresh()
        for name in ["fback", "play", "fnext"] { buttons[name]?.isEnabled = viewer.frameCount > 1 }
        buttons["pixel"]?.state = viewer.pixel ? .on : .off
        buttons["select"]?.state = viewer.selecting ? .on : .off
        buttons["play"]?.state = viewer.playing && viewer.frameCount > 1 ? .on : .off
        buttons["info"]?.state = infoOn ? .on : .off

        for menu in menus {
            for item in menu.items {
                switch item.representedObject as? String {
                case "pixel": item.state = viewer.pixel ? .on : .off
                case "select": item.state = viewer.selecting ? .on : .off
                case "info": item.state = infoOn ? .on : .off
                case "play": item.state = viewer.playing && viewer.frameCount > 1 ? .on : .off; item.isEnabled = viewer.frameCount > 1
                case "fback", "fnext": item.isEnabled = viewer.frameCount > 1
                default: break
                }
            }
        }
        requestMetadata()
    }
    func toggleInfo() {
        let savedWidth = UserDefaults.standard.double(forKey: "infoWidth")
        restoringLayout = true
        infoOn.toggle(); infoWrap.isHidden = !infoOn; UserDefaults.standard.set(infoOn, forKey: "infoOn")
        splitter.adjustSubviews()
        if infoOn {
            let width = savedWidth
            splitter.setPosition(max(200, splitter.bounds.width - max(120, width == 0 ? 240 : width)), ofDividerAt: 0)
        }
        restoringLayout = false
        if !infoOn { metadataCancellation.cancel(); metadataWork?.cancel(); metadataSerial += 1; infoURL = nil }
        needsLayout = true; refresh()
    }
    func requestMetadata(force: Bool = false) {
        guard infoOn else { return }
        guard force || viewer.file != infoURL else { return }
        if viewer.file != infoURL { colorResult = nil }
        infoURL = viewer.file; metadataSerial += 1; let serial = metadataSerial
        metadataCancellation.cancel(); metadataWork?.cancel()
        let cancellation = WorkCancellation(); metadataCancellation = cancellation
        colorRequested = false
        guard let url = viewer.file else { baseReport = ""; infoText.string = ""; countButton.isEnabled = false; return }
        let image = viewer.firstImage, count = viewer.frameCount, duration = viewer.delays.reduce(0, +)
        let language = Language.shared.index
        countButton.isEnabled = true; countButton.title = tr("COUNT_COLORS")
        infoText.string = tr("Counting…")
        let work = DispatchWorkItem { [weak self] in
            guard !cancellation.isCancelled else { return }
            let report = Metadata.report(url: url, image: image, frames: count, duration: duration,
                                         shouldCancel: { cancellation.isCancelled }, translate: { localizedStrings[$0]?[language] ?? $0 })
            DispatchQueue.main.async { [weak self] in
                guard let self = self, serial == self.metadataSerial, !cancellation.isCancelled else { return }
                self.baseReport = report; self.updateInfoText()
                self.infoText.scrollRangeToVisible(NSRange(location: 0, length: 0))
            }
        }
        metadataWork = work; metadataQueue.async(execute: work)
    }
    func updateInfoText() {
        infoText.string = baseReport + (colorResult.map { "\n\n" + tr("Colors") + ": " + $0.report(translate: { tr($0) }) } ?? "")
    }
    @objc func countColors() {
        guard !colorRequested, let image = viewer.firstImage, viewer.file != nil else { return }
        colorRequested = true; countButton.isEnabled = false; countButton.title = tr("Counting…")
        let serial = metadataSerial, cancellation = metadataCancellation
        metadataQueue.async { [weak self] in
            let result = Metadata.colorCount(image: image, shouldCancel: { cancellation.isCancelled })
            DispatchQueue.main.async { [weak self] in
                guard let self = self, serial == self.metadataSerial, !cancellation.isCancelled else { return }
                self.colorResult = result; self.updateInfoText(); self.countButton.title = tr("COUNT_COLORS")
            }
        }
    }
    deinit { metadataCancellation.cancel(); metadataWork?.cancel() }
    func makeMenus() -> [NSMenu] {
        let groups: [[(String, String, String)]] = [
            [("P_OPEN", "open", "o"), ("FINDER", "finder", ""), ("-", "", ""), ("P_SETTINGS", "settings", ","), ("P_EXIT", "quit", "")],
            [("P_COPY", "copy", "c"), ("P_SELMODE", "select", ""), ("P_SELALL", "all", "a"), ("P_DESEL", "deselect", ""), ("P_DELETE", "delete", "")],
            [("P_ZOOMIN", "zoomin", ""), ("P_ZOOMOUT", "zoomout", ""), ("P_RESET", "reset", ""), ("P_INFO", "info", ""), ("-", "", ""), ("SET_PIXEL", "pixel", ""), ("SET_BG", "bg", ""), ("-", "", ""), ("P_PLAY", "play", ""), ("P_FPREV", "fback", ""), ("P_FNEXT", "fnext", "")],
            [("P_KEYS", "keys", ""), ("P_ABOUT", "about", "")]]
        return groups.map { group in
            let menu = NSMenu(); menu.autoenablesItems = false
            for (label, action, key) in group {
                if label == "-" { menu.addItem(.separator()); continue }
                let item = NSMenuItem(title: Language.shared.label(label), action: #selector(menuAction(_:)), keyEquivalent: key)
                item.target = self; item.representedObject = action; menu.addItem(item)
            }
            return menu
        }
    }
    @objc func openMenu(_ sender: ClassicButton) {
        refresh(); menus[sender.tag].popUp(positioning: nil, at: CGPoint(x: sender.frame.minX, y: sender.frame.maxY), in: self)
    }
    @objc func menuAction(_ sender: NSMenuItem) { perform(sender.representedObject as? String ?? "") }
    @objc func toolAction(_ sender: ClassicButton) { perform(sender.identifier?.rawValue ?? "") }
    func perform(_ action: String) {
        switch action {
        case "open": viewer.openImage(nil)
        case "finder": if let url = viewer.file { NSWorkspace.shared.activateFileViewerSelecting([url]) }
        case "settings": settings()
        case "quit": NSApplication.shared.terminate(nil)
        case "prev": viewer.navigate(-1)
        case "next": viewer.navigate(1)
        case "fback": viewer.stepFrame(-1)
        case "fnext": viewer.stepFrame(1)
        case "play": viewer.togglePlay()
        case "zoomin": viewer.zoom(1, at: CGPoint(x: viewer.bounds.midX, y: viewer.bounds.midY))
        case "zoomout": viewer.zoom(-1, at: CGPoint(x: viewer.bounds.midX, y: viewer.bounds.midY))
        case "reset": viewer.reset()
        case "pixel": viewer.togglePixel()
        case "bg": viewer.cycleBackground()
        case "select": viewer.toggleSelection()
        case "copy": viewer.copy(nil)
        case "all": viewer.selectAll(nil)
        case "deselect": viewer.selection = nil; viewer.changed()
        case "info": toggleInfo()
        case "delete": viewer.trash()
        case "keys": notice(title: tr("KEYS_TITLE"), body: tr("KEYS_BODY"))
        case "about": notice(title: tr("ABOUT_TITLE"), body: "TLIV \(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.0.0")\nTenoMicromochi\nMIT License")
        default: break
        }
        refresh(); window?.makeFirstResponder(viewer)
    }
    func notice(title: String, body: String) {
        let alert = NSAlert(); alert.messageText = title; alert.informativeText = body; alert.addButton(withTitle: tr("BTN_OK")); alert.runModal()
    }
    func settings() {
        let alert = NSAlert(); alert.messageText = tr("SET_TITLE")
        alert.addButton(withTitle: tr("BTN_OK")); alert.addButton(withTitle: tr("BTN_CANCEL"))
        let contents = NSView(frame: CGRect(x: 0, y: 0, width: 360, height: 190))
        let pixel = NSButton(checkboxWithTitle: tr("SET_PIXEL"), target: nil, action: nil)
        pixel.state = viewer.pixel ? .on : .off; pixel.frame = CGRect(x: 0, y: 158, width: 350, height: 24); contents.addSubview(pixel)
        let bgLabel = NSTextField(labelWithString: tr("SET_BG")); bgLabel.frame = CGRect(x: 0, y: 125, width: 110, height: 24); contents.addSubview(bgLabel)
        let bg = NSPopUpButton(frame: CGRect(x: 110, y: 122, width: 140, height: 28))
        bg.addItems(withTitles: ["SET_CHECKER", "SET_BLACK", "SET_WHITE", "SET_CUSTOM"].map { tr($0) }); bg.selectItem(at: viewer.background); contents.addSubview(bg)
        let color = NSColorWell(frame: CGRect(x: 264, y: 122, width: 60, height: 28)); color.color = viewer.customBackground; contents.addSubview(color)
        let languageLabel = NSTextField(labelWithString: tr("SET_LANG")); languageLabel.frame = CGRect(x: 0, y: 85, width: 110, height: 24); contents.addSubview(languageLabel)
        let language = NSPopUpButton(frame: CGRect(x: 110, y: 82, width: 214, height: 28))
        language.addItems(withTitles: Language.names); language.selectItem(at: Language.shared.index); contents.addSubview(language)
        let noConfirm = NSButton(checkboxWithTitle: tr("SET_NOCONF"), target: nil, action: nil)
        noConfirm.state = viewer.noConfirm ? .on : .off; noConfirm.frame = CGRect(x: 0, y: 45, width: 350, height: 24); contents.addSubview(noConfirm)
        let note = NSTextField(wrappingLabelWithString: tr("SET_RESETS")); note.frame = CGRect(x: 0, y: 0, width: 350, height: 40); contents.addSubview(note)
        alert.accessoryView = contents
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        viewer.pixel = pixel.state == .on; viewer.background = bg.indexOfSelectedItem
        viewer.customBackground = color.color; viewer.noConfirm = noConfirm.state == .on
        viewer.savePreferences(); Language.shared.index = language.indexOfSelectedItem
        localize(); viewer.changed()
    }
}

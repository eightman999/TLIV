#!/bin/sh
set -eu
cd "$(dirname "$0")"
APP=build/TLIV.app
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" build/module-cache
# Build natively on Apple Silicon or Intel; no third-party dependencies.
xcrun swiftc -module-cache-path build/module-cache -O -swift-version 5 -target "$(uname -m)-apple-macosx11.0" \
    -framework AppKit -framework ImageIO macos/*.swift -o "$APP/Contents/MacOS/TLIV"
cp res/icon_*.png "$APP/Contents/Resources/"
python3 -B tools/make_macos_icon.py "$APP/Contents/Resources/TLIV.icns"
VERSION=$(sed -n 's/^#define TLIV_VER_STR "\(.*\)"/\1/p' src/version.h)
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>TLIV</string>
<key>CFBundleIdentifier</key><string>com.tenomicromochi.tliv</string>
<key>CFBundleName</key><string>TLIV</string>
<key>CFBundleIconFile</key><string>TLIV.icns</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
<key>CFBundleVersion</key><string>$VERSION</string>
<key>LSMinimumSystemVersion</key><string>11.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>CFBundleDocumentTypes</key><array><dict>
<key>CFBundleTypeName</key><string>Image</string>
<key>CFBundleTypeRole</key><string>Viewer</string>
<key>LSHandlerRank</key><string>Alternate</string>
<key>LSItemContentTypes</key><array><string>public.image</string><string>public.svg-image</string></array>
</dict></array>
</dict></plist>
PLIST
codesign --force --sign - "$APP"
printf 'Built %s\n' "$APP"

#!/bin/sh
set -eu
cd "$(dirname "$0")"
case "${1:-}" in
    "") ./build-macos.sh ;;
    --skip-build) ;;
    *) printf 'Usage: %s [--skip-build]\n' "$0" >&2; exit 2 ;;
esac
APP=build/TLIV.app
test -x "$APP/Contents/MacOS/TLIV"
codesign --verify --deep --strict "$APP"
VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")
mkdir -p build
STAGING=$(mktemp -d "$PWD/build/TLIV-dmg.XXXXXX")
trap 'rm -rf "$STAGING"' EXIT HUP INT TERM
ditto "$APP" "$STAGING/TLIV.app"
cp LICENSE "$STAGING/LICENSE.txt"
ln -s /Applications "$STAGING/Applications"
cat > "$STAGING/Install.txt" <<'TEXT'
TLIV for macOS

Drag TLIV.app to Applications, then eject this disk image.
Requires macOS 11 or later. This build supports the architecture shown below.
The application is ad-hoc signed and has not been notarized.

TLIV.app を Applications にドラッグしてから、このディスクを取り出してください。
macOS 11以降が必要です。対応CPUは下記を確認してください。
このビルドはアドホック署名済みで、公証はされていません。
TEXT
lipo -archs "$APP/Contents/MacOS/TLIV" >> "$STAGING/Install.txt"
hdiutil create -ov -format UDZO -fs HFS+ -volname "TLIV $VERSION" \
    -srcfolder "$STAGING" build/TLIVSetup.dmg
hdiutil verify build/TLIVSetup.dmg
printf 'Built build/TLIVSetup.dmg\n'

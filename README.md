# TLIV

**A very light image viewer for Windows and macOS, made for pixel art.** It has a pixel mode (no smoothing, whole-number zoom) and a normal mode.

![User's Guide 1/3: parts and what they do](docs/guide_1_parts.png)
![User's Guide 2/3: controls and features](docs/guide_2_controls.png)
![User's Guide 3/3: specifications and notes](docs/guide_3_spec.png)

## Download

Get either one from GitHub Releases:

- **Installer** (`TLIVSetup-<version>.exe`): per-user, no administrator rights needed.
- **Single exe** (`TLIV.exe`): just run it. Settings are saved in `TLIV.ini` next to it.

TLIV is not code-signed, so Windows SmartScreen may warn you. Click "More info" > "Run anyway".

## Keys and mouse

| Input | Action |
|---|---|
| Wheel / Ctrl+Wheel | Zoom at cursor |
| Shift+Wheel / Left, Right | Previous / next image |
| Left drag | Pan (in Select mode: select area) |
| S | Select mode on / off (off at startup) |
| Space / Click | Play / pause animation (GIF, APNG, WebP) |
| Q / E | Previous / next frame |
| Ctrl+A | Select all |
| Ctrl+C | Copy selection (or whole image), keeps transparency |
| Esc | Deselect |
| Delete | Move to Recycle Bin (asks first) |
| R | Reset zoom and position |
| B | Background: checker / black / white / custom |
| P | Pixel mode / normal mode |
| I | Info panel (file, image and embedded text; read-only) |
| Ctrl+O | Open... |
| Ctrl+, | Settings... |

## Modes (P)

- **Pixel mode:** no smoothing, whole-number zoom steps, pixel-snapped. Shows x, y and HEX colour.
- **Normal mode:** continuous zoom, no pixel-snapping. Enlarging is never smoothed; only shrinking is smoothed (high quality).

## Language

Choose the display language in Settings: English / 日本語 / 简体中文 / 繁體中文.

## Formats / Requirements

PNG, JPEG, BMP, GIF (animated), WebP, APNG, ICO, SVG (simple). Windows 10 / 11 (x64).

## Build

- Run `build.bat` with VS 2022 Build Tools installed (`build.bat clean` for a clean build). Output: `build\TLIV.exe`.
- Run `release.bat` for the distributables in `dist\` (needs Inno Setup 6).
- If you redraw the icons in `images\`, run `build.bat icons` (needs Python 3) to regenerate `res\`.

## macOS

The native AppKit / ImageIO port requires macOS 11 or later and Xcode Command
Line Tools. Build for the current machine (Apple Silicon or Intel):

```sh
./build-macos.sh
open build/TLIV.app
open build/TLIV.app --args /absolute/path/to/image.svg
# Generates disposable fixtures; does not alter user files, preferences or clipboard
build/TLIV.app/Contents/MacOS/TLIV --self-test
```

Create a drag-and-drop installer disk image with `./package-macos.sh`, or run
`./package-macos.sh --skip-build` to package the existing verified app. The result
is `build/TLIVSetup.dmg`: drag TLIV.app to the Applications shortcut, then eject
the disk image. The app supports the CPU architecture on which it was built
(Apple Silicon or Intel); packaging does not make a universal binary.

The bundle is ad-hoc signed for local use, not notarized. The Windows build is
unchanged. Builds and packaging are local; no GitHub Actions workflow is included.
To create a ZIP instead of a disk image:

```sh
ditto -c -k --sequesterRsrc --keepParent build/TLIV.app build/TLIV-macos.zip
```

### UI and language

The macOS window recreates the Windows menu row, grey beveled borders, toolbar
using the same 14 icon assets and grouping, resizable right information panel,
and status fields (name, index, dimensions, animation frame, zoom, mode,
coordinates, HEX/alpha and colour swatch). macOS retains its native title bar,
application menu and system dialogs; installed fonts can differ from Windows.

Settings (Command+,) offers English / 日本語 / 简体中文 / 繁體中文, pixel mode,
checker / black / white / custom backgrounds, and deletion confirmation.
Language changes immediately update menus, tooltips, status, and metadata
headings, and persist on restart. Deletion without confirmation resets to off
on every launch, as in Windows. Windows translations are generated directly
from `src/lang_strings.h`; regenerate after changing that table:

```sh
python3 tools/make_macos_strings.py
python3 tools/make_macos_strings.py --check
```

### Controls and information

The toolbar implements the same actions as Windows. Keyboard controls above
apply, with Command replacing Ctrl (Ctrl shortcuts are also accepted while the
viewer has focus). I toggles the information panel, S toggles selection, and
B cycles all four backgrounds. Finder Open With and file drag-and-drop work.

The read-only, selectable information panel includes file size and modification
time, dimensions, bit depth, colour space, alpha, animation,
recursive ImageIO EXIF / TIFF / IPTC / GPS properties, XMP tags, and PNG
`tEXt` / `zTXt` / `iTXt` text, including compressed UTF-8 text. Parsing runs on a
serial background queue; stale results cannot overwrite the current image.
Technical metadata property names remain in their original form.

Unique colour counting is explicit (the information panel's Count colours button),
so opening metadata does not scan every pixel. It uses at most 5 MiB of owned
scratch buffers and can be cancelled on image changes. Counting uses normalized
premultiplied sRGB RGBA; fully transparent RGB is collapsed. Over 131,072 distinct
non-opaque colours produces an explicit limit instead of unbounded allocation.

PNG text inspection streams chunks and skips image data. Text chunks,
decompression and collected text are capped at 1 MiB; the report is capped at
256 Ki characters and metadata at 64 frames. Frame metadata is limited to
100,000 entries. Image pixels load on demand into a cache of at most three
frames and normally 32 MiB. One larger frame may occupy the cache alone, up to
512 MiB. These limits cover retained pixels and owned working buffers, not
ImageIO's internal allocations or total process RSS.

Static images resolve the native decoder provider once into retained bitmap storage. Cursor sampling uses a reusable 16 KiB colour-managed tile. Cursor motion refreshes only its status panes. The checkerboard reuses one
small tile. See [performance measurements](macos/PERFORMANCE.md) for comparisons
and reproduction commands.

The macOS application icon reuses the original `images/icon.png` artwork.
`tools/make_macos_icon.py` packages 16–1024px representations without third-party
dependencies, and the build installs `TLIV.icns` into the app bundle.

### SVG and validation

SVG uses the system AppKit rasterizer. Paths, shapes, groups, transforms,
viewBox, fill/stroke, gradients, internal references and clipping are supported.
The SVG is rasterized at its declared dimensions; zoom uses the same pixel/normal
modes as other images. External files/URLs, scripts, DTD/entities, filters and
animated SVG are unsupported and produce an error. Limits are 16 MiB input,
8,192 pixels per side and 32 megapixels. XML must be UTF-8.

Self-tests cover SVG dimensions/colours/transforms and rejected resources,
PNG/GIF decoding and timing, selection/crop/navigation, four-language strings,
PNG embedded text (including compression), JPEG EXIF/TIFF, metadata localisation
and colour counting. GUI checks are separate from these self-tests.

Other raster formats (JPEG, BMP, TIFF, HEIC, ICO, WebP, APNG) use ImageIO;
all OS-specific animation/disposal variants have not been verified. TIFF/ICO
show the first image. Animation currently loops continuously. Remaining
Windows parity work includes directory watching and exact decoder/animation
behaviour. Intel hardware and macOS 11 runtime remain unverified.

## License

MIT. The icon artwork in `images\` is under the same license.

`src\inflate.cpp` is based on puff.c by Mark Adler (zlib license).

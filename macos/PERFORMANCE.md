# macOS performance measurements

Measured 2026-10-07 on arm64, macOS 26.6.2 (25G83), optimized Swift builds.
Each cell is the median of three sequential fresh-process runs with the same
fixtures and commands. Filesystem caches were warm; this is a synthetic local
comparison, not a cold-launch, GUI frame-rate, or Intel runtime measurement.
Peak RSS is for the whole scenario process, including loading and first render.

The before build is the macOS implementation immediately before this optimization
(saved source under ignored `build/performance/baseline/`). The after build uses
bounded frame caching, streamed metadata, explicit bounded colour counting,
small cursor tiles, partial status updates, and tiled checkerboard drawing.
No additional runtime dependency was added.

Fixtures: a deterministic 4096 × 3072 RGBA TIFF (1,769,472 colours) and a
128-frame 512 × 512 GIF. Both versions render the first image before timing the
scenario. GIF visits decode/render all 128 frames, including wrapping.

| Operation | Before | After | Before peak RSS | After peak RSS |
| --- | ---: | ---: | ---: | ---: |
| Metadata report | 181.06 ms | 2.24 ms | 236.0 MiB | 157.1 MiB |
| Explicit colour count | 180.87 ms | 47.72 ms | 235.9 MiB | 160.1 MiB |
| First cursor colour | 13.03 ms | 0.40 ms | 170.9 MiB | 157.2 MiB |
| 2,000 subsequent cursor probes | 5.55 ms | 17.49 ms | 170.9 MiB | 157.2 MiB |
| 60 checkerboard image draws | 1343.84 ms | 526.98 ms | 180.4 MiB | 157.1 MiB |
| 128 GIF frame visits | 147.98 ms | 150.81 ms | 56.7 MiB | 57.8 MiB |

Metadata formerly included an automatic full colour scan; the new metadata timing
excludes that scan. The colour-count baseline includes report generation; its
~2 ms overhead is small beside the measured scan. Explicit colour results match.
The new GIF retains 3 frames / 3 MiB of pixel storage. This does **not** bound
ImageIO's internal decoder storage or total RSS.

## Tradeoffs and remaining work

- Static image materialization avoids repeated source decoding and eliminates
  the extra full-image cursor buffer. It adds an up-front copy: warm TIFF load
  plus first render increased from approximately 42 ms to 56 ms in this run.
- The first cursor colour no longer copies the entire image. Sustained probes
  use bounded, colour-managed tiles and are slower than the old full-image
  buffer. First probe plus 2,000 probes is approximately 18 ms in both builds.
- This small GIF has similar speed and slightly higher process RSS. The cache
  bounds retained frames; it is not a demonstrated GIF speed improvement.
- One frame larger than 32 MiB may occupy the cache alone, up to 512 MiB.
  Decode/materialization may temporarily overlap two buffers. Metadata workers
  can retain a frame until cancellation is observed.
- Self-tests verify GIF disposal/wrapping/LRU, Display P3/alpha preservation,
  tile boundaries, PNG text streaming, colour count stripes and cancellation.
  Native GUI verified the metadata panel and explicit colour-count result.
  Apple Silicon and Intel (x86_64) native builds and runtimes passed;
  macOS 11 runtime and broad decoder behaviour remain unverified.

## Reproduce

```sh
./build-macos.sh
build/TLIV.app/Contents/MacOS/TLIV --self-test
build/TLIV.app/Contents/MacOS/TLIV --make-performance-fixtures "$PWD/build/performance/fixtures"
build/TLIV.app/Contents/MacOS/TLIV --benchmark metadata "$PWD/build/performance/fixtures"
build/TLIV.app/Contents/MacOS/TLIV --benchmark colors "$PWD/build/performance/fixtures"
build/TLIV.app/Contents/MacOS/TLIV --benchmark cursor "$PWD/build/performance/fixtures"
build/TLIV.app/Contents/MacOS/TLIV --benchmark checker "$PWD/build/performance/fixtures"
build/TLIV.app/Contents/MacOS/TLIV --benchmark animation "$PWD/build/performance/fixtures"
```

Each benchmark prints JSON with elapsed milliseconds, peak RSS bytes and CPU user
time. Local raw runs are in ignored `build/performance/results/`. Do not run
comparisons concurrently; contention changes the result.

## Initial display refinement and icon/toolbar follow-up

A further same-session comparison changed static image materialization from
CGContext draw/makeImage to native provider data / CGImage reconstruction.
The pixel format, row padding, ICC profile, precision, alpha and rendering intent
are retained; unsupported provider layouts use the prior rendering fallback.
This avoids a colour conversion and full bitmap redraw during opening.

Median load + first render (sum of the reported per-stage medians) changed from
55.61 ms to 52.09 ms, about 6% faster, for the same 4096 × 3072 TIFF and 1600 ×
1000 target. An additional width-450 fit harness measured 57.53 → 51.11 ms.
Peak RSS is unchanged at approximately 157 MiB; checkerboard, colour count and
GIF timings stayed within a few percent. This is a refinement of the optimized
build above and does not establish faster cold launch or a win over the original
pre-optimization first display. Raw comparisons: ignored
`build/performance/results/first-display-refinement.json`.

The app icon now derives from original `images/icon.png` using deterministic
nearest-neighbour scaling into seven PNG-backed ICNS representations, 16–1024px.
Custom views explicitly clip to their bounds, restoring toolbar separators and
preventing partial paints from covering other panes on macOS 14+. Toolbar buttons
explicitly invalidate when enabled/state/icon changes. Initial
split layout is forced before use; GUI rendering is verified separately.
`--self-test-ui` checks rendered pane colours, layout, all seven ICNS sizes,
the 14 bundled toolbar icons and redraw on state
changes; it needs an AppKit window-server session.

## Intel (x86_64) verification

Measured 2026-10-07 on Intel Core i9-9980HK, macOS 26.6, native optimized Swift build (`-target x86_64-apple-macosx11.0`).
Each cell is the median of three sequential fresh-process runs with warm filesystem cache:

| Operation | Time | Peak RSS |
| --- | ---: | ---: |
| Metadata report | 2.76 ms | 149.2 MiB |
| Explicit colour count | 124.92 ms | 150.9 MiB |
| First cursor colour | 0.59 ms | 149.2 MiB |
| 2,000 subsequent cursor probes | 43.63 ms | 149.2 MiB |
| 60 checkerboard image draws | 593.67 ms | 149.2 MiB |
| 128 GIF frame visits | 304.84 ms | 47.0 MiB |

All automated self-tests (`--self-test`, `--self-test-ui`) and disk image packaging (`./package-macos.sh`) pass on native Intel hardware.


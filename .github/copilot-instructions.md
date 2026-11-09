# Microreader2 — Project Summary

E-ink ebook reader targeting **ESP32-C3 + SSD1677 e-paper** (800×480 physical, rotated 90° → **480×800 portrait**) with a **desktop SDL2 emulator** for rapid development.

## Architecture

```
lib/microreader/          ← shared core (platform-agnostic C++17)
  content/                ← EPUB parsing & layout pipeline
    mrb/                  ← MRB binary format (MrbFormat, MrbReader, MrbWriter, MrbConverter)
  display/                ← Canvas, DisplayQueue, Font, Display interface
  screens/                ← IScreen implementations (UI screens)
  stats/                  ← reading statistics (ReadingStats: recorder, month files, calendar math)
platforms/desktop/        ← SDL2 desktop emulator
platforms/esp32/          ← real hardware (ESP-IDF + PlatformIO)
test/                     ← Google Test suite (unit + integration)
tools/                    ← Python scripts (LUT editor, upload, etc.)
```

**Core** defines abstract interfaces; each platform provides concrete implementations:

| Interface      | Desktop (SDL2)              | ESP32 (real hardware)     |
|----------------|-----------------------------|---------------------------|
| `IDisplay`     | `DesktopEmulatorDisplay`    | `EInkDisplay`             |
| `IRuntime`     | `DesktopRuntime`            | `Esp32Runtime`            |
| `IInputSource` | `DesktopInputSource`        | `Esp32InputSource` (ADC)  |

## Key distinction: Desktop vs ESP32

- **`platforms/desktop/`** = **emulator/simulator**. `display.h` has a per-pixel float `sim_` buffer that simulates e-ink particle physics (exponential approach). This is where display simulation tweaks go.
- **`platforms/esp32/`** = **real device**. `epd.h` drives the actual SSD1677 panel over SPI. No simulation needed.
- **`lib/microreader/`** = **shared application logic**. `Application`, `Canvas`, `DisplayQueue`, `Loop`, screens — runs identically on both platforms. Do NOT modify core files for desktop-only display concerns.

## EPUB content pipeline

Located in `lib/microreader/content/`. This is the chain that turns an `.epub` file into rendered pages:

```
EPUB file (ZIP on SD card)
  → ZipReader        (parse central directory, lazy extraction)
  → Epub::open()     (streaming OPF/NCX parse via XmlReader, builds spine + TOC)
  → Epub::parse_chapter()  (extract XHTML → parse into Chapter)
  → layout_page()    (Chapter + Font → PageContent with positioned words)
  → ReaderScreen     (PageContent → paint lambda → DisplayQueue)
```

### Key content files

| File | Purpose |
|------|---------|
| `ZipReader.h/.cpp` | ZIP central directory reader. `ZipEntry` stores `std::string_view name` + offsets (no data); all names live in a single contiguous `name_blob_` vector inside `ZipReader` (two-pass: count total name bytes, then bulk-allocate). `ZipEntryInput` streams decompression with ~33KB constant memory (32KB LZ dict + 1KB input buffer). `extract()` decompresses fully into `std::vector<uint8_t>`. |
| `MrbReader.h/.cpp` | Reads `.mrb` (pre-processed binary) files. Loads chapter table + image refs into RAM on `open()`, but the paragraph index is **not loaded into RAM** — each entry (8 bytes) is seeked on demand in `load_paragraph()` to avoid ~136KB allocation for large books. `MrbChapterSource` wraps `MrbReader` as an `IParagraphSource` with lazy paragraph caching per chapter. |
| `XmlReader.h/.cpp` | Streaming XML SAX parser with configurable buffer (typically 4–8KB). Emits `StartElement`, `EndElement`, `Text`, `CData` events. Pluggable `IXmlInput` for data source. |
| `EpubParser.h/.cpp` | `Epub` class: `open()` streams OPF via `ZipEntryInput` → `XmlReader` (never loads full OPF into RAM). Builds compact manifest (FNV-1a hashes, ~6 bytes/entry). `parse_chapter()` extracts full XHTML into memory then parses to `Chapter`. Also resolves image dimensions by extracting image files. |
| `ContentModel.h` | Core data structures: `Run` (styled text span), `TextParagraph` (runs + alignment/indent), `Paragraph` (Text/Image/Hr/PageBreak), `Chapter` (title + paragraphs), `SpineItem`, `TocEntry`, `EpubMetadata`. |
| `TextLayout.h/.cpp` | `IFont` interface for font metrics; `FixedFont` for testing. `layout_paragraph()` → word-wrap runs into `LayoutLine`s. `layout_page()` → fill one page from chapter position → `PageContent` with positioned `PageTextItem`/`PageImageItem`/`PageHrItem`. |
| `CssParser.h/.cpp` | Minimal CSS parser for EPUB stylesheets. `CssStylesheet` cascades by specificity. |
| `ImageDecoder.h/.cpp` | JPEG/PNG detection and dimension reading. `ImageSizeStream` is a streaming header parser (~44 bytes state) used for lazy image size resolution. `images_enabled` runtime flag (default `true`). |
| `Book.h/.cpp` | High-level wrapper: owns `StdioZipFile` + `Epub`. Methods: `open(path)`, `load_chapter(index, Chapter&)`, `decode_image()`. |
| `MrbFormat.h` | MRB binary format constants and on-disk structs: `MrbHeader` (32 bytes), `MrbChapterEntry` (16 bytes), `MrbImageRef` (8 bytes: `local_header_offset(u32)` + `width(u16)` + `height(u16)`), paragraph record layout. Version 3. Shared between `MrbReader` and `MrbWriter`. |
| `MrbWriter.h/.cpp` | Writes `.mrb` binary files. `BufferedFileWriter` batches into 4KB buffer. Paragraphs are doubly-linked (prev/next offsets). Uses **deferred paragraph writing**: each paragraph is serialized into `pending_para_` buffer, then flushed when the *next* paragraph arrives (so `next_offset` can be filled in without seeking). This eliminates all backward seeks in `end_chapter()`. |
| `MrbConverter.h/.cpp` | Orchestrates EPUB→MRB conversion: iterates spine chapters, streams paragraphs via `ParagraphSink` callback from `EpubParser` → `MrbWriter`. Exposes `convert_epub_to_mrb()` and `convert_epub_to_mrb_streaming()`. |
| `HtmlExporter.h/.cpp` | Exports a `Book` to a self-contained HTML file using the `TextLayout` engine to paginate content exactly as the reader would. Used by `HtmlExportTest`. Options control page size, padding, chapter limit, and debug output. |

### Memory constraints (critical for ESP32-C3)

The ESP32-C3 has **~320KB total RAM**, of which **~155KB is free after SD card init**. The EPUB pipeline must fit within this budget:

| Component | Typical Cost | Notes |
|-----------|-------------|-------|
| `Book::open()` metadata | 30–45KB | ZIP entries × ~60 bytes each (struct + string_view into contiguous name blob — 2 allocations total) + spine + TOC + stylesheet. War and Peace (377 entries) verified on device: 126KB free after open. |
| `MrbReader::open()` | 3–5KB | Chapter table (368 × 8 = ~3KB) + image refs + metadata/TOC. Paragraph index is NOT loaded into RAM — seeked on demand. |
| `parse_chapter()` XHTML extract | up to 218KB | Full chapter XHTML decompressed into `std::vector<uint8_t>` + 33KB work buffer |
| `parse_chapter()` image dim resolve | up to 200KB | Extracts each image file to read JPEG/PNG headers |
| `Chapter` in memory | up to 250KB+ | 268 paragraphs × 684 runs × `std::string` text (177KB text alone for largest chapter) |
| `layout_page()` | ~5–10KB | One page at a time, returns `PageContent` with word pointers into `Chapter` |

**Known crash**: ohler.epub ("Der totale Rausch") has 487 spine items mapping to only ~10 XHTML files, the largest being 218KB. The current `parse_chapter()` approach of extracting the full XHTML + resolving image dimensions exceeds available heap. **This needs to be fixed** — likely by pre-processing EPUBs into a compact on-device format or by streaming the XHTML parsing.

**`convert_epub_to_mrb_streaming()`** in `MrbConverter.h` uses ~37KB of working memory per chapter (vs. full XHTML extract) and is safe for ESP32's limited RAM. Optional `work_buf`/`xml_buf` parameters let callers pass pre-allocated split buffers to avoid heap fragmentation from a single large allocation.

### ⚠️ CRITICAL: Image sizes are NOT resolved during MRB conversion

The MRB conversion step does **NOT** load, decode, or read the dimensions of any images. It records the **local file header offset** (`local_header_offset`) for each image reference, allowing direct access to the image data in the EPUB ZIP at render time without parsing the central directory. Image dimensions stored in `MrbImageRef.width/height` come **only** from explicit HTML `width`/`height` attributes in the EPUB's XHTML — most EPUBs do **not** have these, so `width=0, height=0` is the common case.

**Do NOT try to add image size resolution to the conversion step.** Image dimensions are resolved lazily at display time by `ReaderScreen::resolve_image_size_()`, which decodes images directly from the EPUB using `ZipReader::read_local_entry()` + `decode_image_from_entry()`. This reads ~30 bytes from the local file header (no central directory needed), saving ~35KB+ vs the old `open_zip_only()` approach that parsed the full central directory and caused OOM on books like ohler.epub (487 entries).

## Screens (IScreen implementations)

All inherit from `IScreen` (`lib/microreader/screens/IScreen.h`): `name()`, `start()`, `stop()`, `update()`.

| Screen | File | Description |
|--------|------|-------------|
| `MainMenu` | `MainMenu.h/.cpp` | Main navigation menu. Owns all child screens. |
| `ReaderScreen` | `ReaderScreen.h/.cpp` | EPUB page viewer. 2×-scaled 8×8 bitmap font (16×16 glyphs). Next/prev page, chapter transitions. |
| `BouncingBallDemo` | `BouncingBallDemo.h/.cpp` | Bouncing ball + random shapes. |
| `StatsCalendarScreen` | `StatsCalendarScreen.h/.cpp` | Reading-stats month calendar (Settings → Reading Stats). Portrait and landscape layouts. |
| `DateTimeScreen` | `DateTimeScreen.h/.cpp` | Sets the wall clock (day/month/year/hour/minute). |

Screens that don't derive from `ListMenuScreen` get the UI fonts via `ListMenuScreen::shared_font()` / `shared_menu_font()` and the standard battery + hint row via `ListMenuScreen::draw_menu_bottom_bar()`. **Don't include `display/ui_font_*.h` in new .cpp files** — the tables are `static constexpr` arrays, so every TU that uses them gets its own copy in flash (the header font alone is ~110 KB).

## Reading statistics (`lib/microreader/stats/ReadingStats.h/.cpp`)

- **Recorder**: `Application` owns a `ReadingStats` (`app_->stats()`), clocked by the platform `IRuntime`. `ReaderScreen` calls `book_opened()` (start), `page_turned(forward)` (after each render), `reading_paused()/reading_resumed()` (pause/resume around child screens) and `book_closed()` (stop). `Application::do_sleep_()` calls `flush()`.
- **Accounting**: time a page is on screen is credited when it's left, capped at `kMaxPageMs` (3 min); a forward turn after ≥ `kMinPageMs` (2 s) counts as a page. Pending data is flushed on book close, sleep, day change, and every `kFlushEveryMs` (10 min).
- **Storage**: append-only `<data_dir>/stats/YYYY-MM.txt`, lines `day seconds pages title` (`#` = comment). `MonthStats::load()` sums lines and skips malformed/partial ones. Not under `cache/`, so Clear Cache keeps it.
- **Clock**: `IRuntime::monotonic_ms()`, `local_time()`, `set_local_time()` (defaults: no clock → nothing recorded). The device clock holds **local wall time, no time zone**. ESP32: `time()/settimeofday()` — survives deep sleep, resets to 1970 on cold power-on; `Application::restore_clock_()` then restores `max(saved "clock=" from settings, firmware build time)`. Desktop: host local time plus an offset (`set_local_time` never touches the host clock).
- **Sync**: serial `CMND 'E'` + int64 LE local seconds. The browser manager sends it on every connect; `serial_cmd.py --sync-time` / interactive `time`.


### Screen navigation flow

```
Application
  └─ ScreenManager (push/pop stack, max depth 8)
       └─ MainMenu (always at bottom)
            ├─ "Select Book" → push BookSelectScreen
            │     └─ user picks → push ReaderScreen (via Application chain)
            └─ "Bouncing Ball" → push BouncingBallDemo

Button0 = back (screen returns false → pop)
Button1 = select
Button2 = down / next page
Button3 = up / prev page
```

## Core systems

- **DisplayQueue**: dual-buffer (ground_truth + target) phase-based animation. Commands progress over N phases before committing.
- **Canvas**: z-ordered scene graph with damage-rect redraw. Elements: `CanvasRect`, `CanvasCircle`, `CanvasText`.
- **Proportional font system** (`BitmapFont.h`, `BitmapFontFormat.h`, `DrawBuffer.h`):
  - **MBF format**: Custom binary font format. One file per pixel size, each containing up to 4 styles (Regular/Bold/Italic/BoldItalic). Generated from TTF via `tools/generate_font.py`.
  - **BitmapFont**: Reads MBF data via `init(data, size)`. Zero-heap: all pointers reference the input buffer (works with mmap). Implements `IFont`.
  - **FontManager**: Manages the loaded fonts, parsing the `FNTS` bundle inside the file system and holding up to `kMaxFontSizes` (8) scale variations dynamically.
  - **DrawBuffer::draw_text_proportional()**: Renders text using `IFont*`.
  - **Font sizes**: The typical bundle uses exactly 4 sizes: 20px, 24px, 28px, 32px (configurable via `--bundle-sizes` in `generate_font.py`), which are mapped sequentially into `ReaderSettings::font_size_idx`.
  - **FNTS bundle**: Multi-font bundle format for ESP32 flash partition. Format: `[FNTS:4][num:1][version:1(=1)][reserved:2][font_name:32][num×size:4][data...]`. Font name is null-terminated, zero-padded to 32 bytes. Uploaded as a single blob via `serial_cmd.py --upload-font`.
  - **ESP32 storage**: Fonts live in the SPIFFS-style font partition (3.375 MB), mmapped via `font_partition.h`. The font bundles (zlib-compressed FNTS) are embedded directly in the firmware image as DROM data (see asset blob below). On first book open (or after a firmware update with a new font CRC), `FontPartition::provision_embedded()` stream-decompresses the requested bundle into the partition (~15s one-time cost). The partition header stores the manifest CRC32 to skip re-provisioning on subsequent boots. After regenerating fonts with `generate_font.py`, just rebuild — `tools/generate_assets.py` is invoked as a PIO pre-build script and picks up the new `.bin` files automatically.
  - **Asset blob (DROM-embedded in firmware)**: `tools/build_assets.py` packages `bookerly.bin`, `alegreya.bin`, and the three sleep `.mgr` images into `assets.bin` (manifest = `[ASTS:4][version:4=1][count:4][total:4]` + N×44-byte entries `{name[32], offset, length, crc32}`). The PIO pre-build script `tools/generate_assets.py` generates both `platforms/esp32/assets.bin` and `platforms/esp32/assets_embedded.S` (an assembly file with `.incbin` that embeds the binary as `_binary_assets_bin_start`/`_binary_assets_bin_end` symbols in the `.rodata` section). `assets_embedded.S` is listed in `SRCS` in `CMakeLists.txt` so it compiles into the firmware as DROM data. `asset_blob.cpp` reads directly from those symbols — no partition reads, no mmap. Runtime access: `asset_blob::g_assets.init()` in `app_main`, then `map(name, &size, &handle)` / `unmap(handle)` / `crc(name)` return direct DROM pointers. `FontManager::ensure_ready()` and `DrawBuffer::show_sleep_image_embedded()` map their asset on demand and unmap immediately after use. **Tradeoff**: assets (~2 MB) consume DROM MMU pages, but firmware.bin is a valid self-contained IDF binary — web flashers (ESP Web Tools etc.) work correctly. Flash usage ~52%, RAM usage ~36%.
  - **Partition header**: 12 bytes: `[FONT:4][decompressed_size:4][embedded_crc32:4]` followed by raw FNTS data.
  - **Desktop**: Loads individual `.mbf` files from `resources/fonts/` (`font-0.mbf` up to `font-3.mbf`).
  - **Generation**: `python tools/generate_font.py "resources/fonts/Bookerly.ttf" -o resources/fonts/font-normal.mbf --with-styles --bold "resources/fonts/Bookerly Bold.ttf" --italic "resources/fonts/Bookerly Italic.ttf" --bold-italic "resources/fonts/Bookerly Bold Italic.ttf" --bundle --bundle-sizes 20 24 28 32 --font-name Bookerly` generates 4 sizes + bundle.
- **Input**: `ButtonState` carries `current` + `pressed_latch`. Auto-repeat at hardware layer (5ms sample on ESP32). Screens use `is_pressed()`.
- **Loop**: `run_loop()` polls input → app.update() → queue.tick() → wait_next_frame().
- **Logging** (`HeapLog.h`): `MR_LOGI(tag, fmt, ...)` maps to `ESP_LOGI` on device and `printf("[tag] fmt\n")` on desktop. `HEAP_LOG(tag)` logs free heap + largest block (ESP32-only, no-op on desktop). No `ILogger` abstraction — use these macros directly.

## Build commands

### Desktop
```bash
cd microreader2
cmake -S platforms/desktop -B build/desktop-debug -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5"
cmake --build build/desktop-debug --config Debug
```

### ESP32 (PlatformIO)
```bash
cd microreader2
# Build only:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run
# Build + flash:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -t upload
# Monitor serial:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe device monitor --baud 115200
```
- Board: ESP32-C3-DevKitM-1, 16MB flash, COM4
- Upload baud: 921600, monitor baud: 115200
- Main task stack: 16KB
- ESP32 sources are **explicitly listed** in `platforms/esp32/CMakeLists.txt` (NOT auto-discovered by PIO LDF). When adding new `.cpp` files, you must add them to the source list.

### Tests

Three levels of testing, from fastest to most thorough:

#### 1. Unit tests (fastest — run after every change)

```bash
cd microreader2/test
cmake -B build2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
cmake --build build2 --config Debug
.\build2\Debug\unit_tests.exe
```

~375 tests, runs in <1 second. Covers ZipReader, XmlReader, CssParser, EpubParser, ImageDecoder, TextLayout with synthetic fixtures only. **Always run this first.**

VS Code task shortcut: **Run Unit Tests** (builds + runs automatically).

#### 2. Integration tests (includes real EPUB books)

```bash
.\build2\Debug\microreader_tests.exe
```

Includes everything from unit tests plus RealBookTest, BulkBookTest, DebugOhlerTest, HtmlExportTest. Requires real `.epub` files in `test/books/`. Run a subset with `--gtest_filter`:

```bash
.\build2\Debug\microreader_tests.exe --gtest_filter="HtmlExport.*"
.\build2\Debug\microreader_tests.exe --gtest_filter="DebugOhler.*"
```

VS Code task shortcuts: **Run All Tests**, **Run HTML Export Tests**.

#### 3. On-device tests (real hardware or QEMU)

Tests book opening, page navigation, and heap stability on actual ESP32 firmware.

**Real device (COM4):**
```bash
# Build + flash firmware
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload

# Run book tests (opens each book, navigates pages, checks for crashes)
python tools/test_books.py --port COM4 --pages 20 --heap
```

**QEMU (no hardware needed):**
```bash
# Terminal 1: start QEMU with books pre-loaded
python tools/run_qemu.py --with-books

# Terminal 2: run tests against QEMU
python tools/test_books.py --port socket://localhost:4444 --pages 20 --delay 0.1
```

`test_books.py` options:
- `--pages N` — pages to navigate per book (default: 20)
- `--heap` — print heap stats per book (before/after open/close)
- `--filter STR` — only test books matching STR
- `--clean` — delete all `.mrb` files first (forces fresh conversion)
- `-v` — verbose per-page output

#### Recommended workflow

1. **Edit code** → run **unit tests** (task or `unit_tests.exe`)
2. If content/layout changes → run **integration tests** (`microreader_tests.exe`)
3. If touching memory, images, or ESP32-specific code → **flash + test_books.py** on device
4. For CI-like coverage without hardware → **QEMU + test_books.py**

Two test binaries:
- `unit_tests`: ZipReader, XmlReader, CssParser, EpubParser, ImageDecoder, TextLayout tests
- `microreader_tests`: above + RealBookTest, BulkBookTest, DebugOhlerTest, HtmlExportTest

Test fixtures in `test/fixtures/` (synthetic EPUBs). Real books in `test/books/`.

## ESP32 hardware

- **MCU**: ESP32-C3 (RISC-V single core, 160MHz)
- **RAM**: ~320KB total, ~155KB free after SD init
- **Flash**: 16MB, dual OTA partitions (6.4MB each)
- **Display**: SSD1677 e-ink, 800×480 physical, rotated → 480×800 portrait, 1-bit packed (100-byte row stride)
- **SD card**: FAT32 via SPI (shares bus with display)
- **Buttons**: ADC-based with hardware auto-repeat

## Known issues & gotchas

- **ESP32 CMake source list**: New `.cpp` files MUST be added to `platforms/esp32/CMakeLists.txt` explicitly. PIO's LDF auto-discovery does not work for this project structure.
- **CMake exit code 1**: PIO/CMake may return exit code 1 from miniz deprecation warnings on stderr. This is not an actual error — build succeeds.
- **Desktop CMake policy warning**: Use `-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5` to silence.
- **COM4 port busy**: Kill any running monitor terminal before uploading firmware.
- **ohler.epub OOM**: 487 spine items, XHTML files up to 218KB, causes heap exhaustion on ESP32 during `parse_chapter()`. See "Memory constraints" section.
- **Heap fragmentation on ESP32**: After `Book::open()`, the largest contiguous block can be much smaller than total free heap (e.g. 59KB largest with 133KB total free). Large single allocations (>50KB) should be split into smaller ones.
- **SD SPI max 20 MHz**: The SD card on the prototype board fails at 40 MHz (`ESP_ERR_INVALID_RESPONSE` during `sdmmc_enable_hs_mode_and_check`). Keep `max_freq_khz = 20000`.
- **`uint32_t` is `unsigned long` on ESP-IDF 5** and IDF builds with `-Werror=all`: cast to `unsigned`/`unsigned long` before passing to `%u`/`%lu` (fine on x86, a build error on device).
- **`Application::set_data_dir()` keeps the raw `const char*`** (firmware passes static strings). Tests must keep the string alive for the Application's lifetime.
- **Linux/GCC test build**: needs `#include <cstring>` wherever `std::strlen` is used (MSVC pulls it in implicitly).
- **Do NOT add `setvbuf` to `StdioZipFile`**: Adding a read buffer to the EPUB input `FILE*` causes wasteful 4 KB read-ahead after every seek, making SEEK 3× slower and DECOMP ~12% slower. The `ZipEntryInput` already manages its own efficient input buffering.

## Device testing

To verify changes on real hardware:
1. Upload test books via `python tools/serial_cmd.py --port COM4 --upload <path>.epub`
2. Build + flash: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload`
3. Monitor serial: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --baud 115200`
4. Use `python tools/serial_cmd.py --port COM4` for interactive control (see below).

### Serial command protocol (`tools/serial_cmd.py`)

Serial communication is handled by `serial_communication.h`. It runs a FreeRTOS `serial_receiver_task` that multiplexes three magic-prefixed protocols: LUT frames (`0xDEADBEEF`), EPUB uploads (`EPUB`), and control commands (`CMND`).

Device commands (sent via `serial_cmd.py` — see interactive commands below):

| Command | Description |
|---------|-------------|
| Button press | Inject button press by index |
| Open book | Push ReaderScreen for a book path |
| Status | Query heap info (`STATUS:free=N,largest=N`) |
| Set clock | `CMND 'E'` + 8-byte LE local seconds (`OK`) — `serial_cmd.py --sync-time` |
| List books | List `.epub`/`.mrb` files on SD card |
| Clear `.mrb` | Delete all `.mrb` files (`CLEARED:N`) |
| Bench | Run EPUB conversion benchmark for a book |
| Image bench | Run image size-read benchmark for a book |

Interactive commands in `serial_cmd.py`:
```
> status        # heap info
> books         # list SD card books
> btn 1         # press button 1 (select)
> select        # alias for btn 1
> back          # alias for btn 0
> down          # alias for btn 2
> up            # alias for btn 3
> open alice.epub  # open book (auto-prepends /sdcard/books/)
> upload alice.epub  # upload EPUB file to the device
> clear         # delete all .mrb files on the device
> bench alice.epub   # run EPUB conversion benchmark
> imgsize alice.epub # run image size-read benchmark
> test [filter] [--clean] [-v]  # open each book and watch for BOOK_OK/BOOK_FAIL
```

Non-interactive CLI flags:
```bash
python tools/serial_cmd.py --upload <path>.epub   # upload then exit
python tools/serial_cmd.py --bench ohler.epub [--capture bench.log] [--timeout 300]
python tools/serial_cmd.py --bench-all [--timeout 180]  # bench every .epub on device
python tools/serial_cmd.py --imgbench-all [--timeout 120]
```

Capture mode (non-interactive, saves all output to file):
```bash
python tools/serial_cmd.py --capture bench.log --reset --timeout 300
# --reset toggles DTR/RTS to reboot the device before capturing
# stops early when "=== DONE:" appears (override with --done-marker)
```

Useful heap logging pattern:
```cpp
#include "esp_heap_caps.h"
ESP_LOGI("test", "Free heap: %lu largest=%lu",
         (unsigned long)esp_get_free_heap_size(),
         (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
```

## Agent rules

- **Keep this file up to date after every change.** When you add, rename, or restructure files, interfaces, systems, or tools — update the relevant section immediately. This file is the single source of truth for new chat sessions.
- **Record useful discoveries.** When you learn something non-obvious (hardware quirks, tricky build steps, constraints, or gotchas), add it to the relevant section so future sessions benefit.
- **Remove stale information.** When a known issue is fixed or a design is superseded, remove or update the entry rather than leaving outdated history.

# Sami

Sami is a fork of [Microreader](https://github.com/CidVonHighwind/microreader) that adds reading statistics, a daily reading goal, a Finished shelf, a chapter progress bar, more sleep screen options and Wi-Fi file transfer. Everything below about Microreader applies to Sami too.

**Installing Sami:** download `sami-firmware.bin` from this repository's [Releases](../../releases/latest) page and flash it with the CrossPoint web flasher's **Custom .bin** option (see [Installation](#installation)). Every change to the code builds a new release automatically.

---

# Microreader

An EPUB reader for the [Xteink X4](https://xteink.com), written from scratch.

Built around one idea: do EPUB rendering really well and nothing else. No Wi-Fi, no Bluetooth, no sync. Just put EPUBs on the SD card and read them.

The first time you open a book there's a single conversion pass to a compact binary format. After that — changing chapters, adjusting font size, tweaking settings — everything is instant.

**Features**

- EPUB rendering with proportional fonts, bold/italic, and inline images
- Hyphenation via the Liang/TeX algorithm (EN, DE, FR, ES, IT, NL, PT, PL, RU)
- Fully configurable reader: font, size, line spacing, margins, justification
- Multiple built-in and SD card fonts — swap at runtime without reflashing
- Table of contents navigation with chapter progress display
- Book position saved and restored automatically per book
- Customizable sleep screen with looping support
- Single-pass EPUB → `.mrb` conversion — fast cold open, instant everything after
- Licensed under GPL v2

**Demo Video**

[![Microreader demo](https://img.youtube.com/vi/KPsLV7BLEz0/0.jpg)](https://www.youtube.com/shorts/KPsLV7BLEz0)

https://www.youtube.com/shorts/KPsLV7BLEz0

## Installation

> [!WARNING]
> **Requires an unlocked Xteink X4.** Do not flash this on a locked device — you may be permanently stuck on that firmware.

Download the latest `.bin` from the **[Releases](https://github.com/CidVonHighwind/microreader/releases)** page.

Flash using the [Crosspoint flash tool](https://crosspointreader.com/#flash-tools) (browser-based, no install needed), or with [esptool](https://docs.espressif.com/projects/esptool/en/latest/) directly:

```powershell
python -m esptool --chip esp32c3 --port COM5 --baud 921600 write_flash 0x0 microreader.bin
```

Replace `COM5` with your device's port (`/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-*` on macOS). Hold BOOT while connecting if the device doesn't enter flash mode automatically.

## Managing Content

Books (`.epub`) go anywhere on the SD card — the device scans recursively from the root. Fonts (`.mfb`) go in the `fonts/` folder. Sleep images go in the `.sleep/` folder.

You can copy files directly to the SD card, or transfer them over USB while the device is connected.

### Browser Manager

Open [microreader-manager](https://cidvonhighwind.github.io/microreader/) in Chrome, Edge, or Firefox (Web Serial API). Provides a file browser, EPUB/font/sleep-image upload, and auto-reconnects on page refresh.

![Microreader Manager](images/Microreader%20Manager.png)

Also includes a **Font Generator** for building `.mfb` fonts in the browser with live device preview, size presets, and script/range presets.

![Font Generator](images/Font%20Generator.png)

### Wi-Fi Transfer (Sami)

Manage the SD card from any phone or computer browser, no cable or app needed. On the book list, select **Wi-Fi Transfer**, then either:

- **Join a Wi-Fi network** — pick your network (type its password once with the on-screen keyboard; Sami remembers it), then open **http://sami.local** — or the address shown on the reader, like `http://192.168.1.42` — on a phone or computer on the same Wi-Fi.
- **Create a hotspot** — the reader makes its own network called **Sami Reader** and shows its password. Join it from your phone, then open **http://sami.local** or **http://192.168.4.1** (many phones open the page by themselves).

The **Sami Reader** page lists the card's folders and files: upload (button or drag and drop, several at once), download, rename, delete and create folders. Hidden files (starting with `.`) appear with **Show hidden**. Books you upload, rename or delete update the book list when you leave the Wi-Fi screen. Press ◀ to stop Wi-Fi; it also turns itself off after 10 minutes without activity, and while it's on the reader won't fall asleep.

Notes: the reader's Wi-Fi is 2.4 GHz only. The page uses plain HTTP and has no password, so anyone on the same network can open it while Wi-Fi Transfer is on — use it at home or on the hotspot. Remembered networks are kept in `.microreader/wifi.txt` (delete a line to forget one).

### Calibre Plugin

Install the plugin to send books directly from [Calibre](https://calibre-ebook.com):

1. In Calibre: **Preferences → Plugins → Load plugin from file** → select `tools/calibre-plugin/microreader.zip`
2. Restart Calibre.

The device is detected automatically. Books on the device show checkmarks in the library; you can send and delete books from the Device menu.

> Requires Calibre 5+ and the device connected over USB.

## Sleep Screen

The device shows a sleep screen when it enters deep sleep. Choose it in **Settings → Sleep Screen** (press ▶ to step through the options):

| Option | What it shows |
|--------|---------------|
| **Random image** (default) | A random image from your sleep folder each time, never the same one twice in a row |
| **Book cover** | The cover of the book you're reading, with its title and how far through you are. Books without a cover image fall back to a random image. |
| **Reading stats** | The Reading Stats calendar with today selected |

Several images are built into the firmware; add your own by putting them in the `sleep/` or `.sleep/` folder on the SD card, or upload them with the browser manager (which uses `sleep/`). When you have your own images, the built-in ones are skipped.

**Supported formats:** JPEG (`.jpg`, `.jpeg`), PNG, and BMP (1 bpp monochrome, 4/8 bpp indexed, 16 bpp RGB565/BGR555, 24 bpp BGR, 32 bpp BGRA). Images are scaled to fit; landscape images are turned sideways to fill the screen. Use 480×800 (or 800×480) for best quality. Very large photos can take a while to convert or fail for lack of memory — resize them first if they don't show.

The first time an image or cover is shown it is converted and cached; later sleeps load the cache directly. Clear the cache via **Settings → Clear Cache**.

## Reading Stats

Open **Settings → Reading Stats** for a month calendar of your reading. Each day shows the time spent reading and a bar that fills at one hour; the selected day lists the books you read, and the panel below shows the month's total time, days read, pages, daily average and current/longest streaks.

| Button | Action |
|--------|--------|
| ▲ / ▼ (front, follows *Menu Nav*) | Previous / next day |
| Side up / down | Previous / next month |
| ▶ | Set date & time |
| ◀ | Back |

**How it's counted:** the time a page stays on screen is credited when you turn it, capped at 3 minutes so leaving the device on doesn't count. A forward page turn after at least 2 seconds counts as a page read, so skimming or holding the button doesn't inflate the numbers. Time spent in menus over the book is not counted.

**The clock:** stats are dated with the device clock, which keeps running through sleep. Set it in **Settings → Date & Time** (or press ▶ in the calendar); connecting the browser manager, or running `python tools/serial_cmd.py --sync-time`, sets it from your computer automatically. If the battery is ever fully drained the clock resumes from the last time it was saved — check the date shown on the *Today* line and correct it if needed.

**Daily goal:** turn on **Settings → Daily Goal** and pick a target under it (10 minutes to 2 hours a day). Each day's bar then fills at your goal instead of at one hour, days that reach it get a small flag in the corner, the day summary says whether you met it, "Days read" becomes "Goals met", and streaks only count days that reached the goal.

**Storage:** one small text file per month in `.microreader/stats/` on the SD card (`YYYY-MM.txt`, one `day seconds pages title` line per reading session). **Clear Cache** does not touch it; delete the folder to reset your stats.

## Finished Books

While reading, press ▶ to open the book menu and select **Finished: No** to mark the book as finished. It moves off the main book list onto its own shelf: **Finished Books (N)**, the first row of the book list, which lists finished books with the date you finished them, newest first. Select a book there to open it again; select **Finished: Yes** in its menu to put it back on the main list. The shelf is stored in `.microreader/finished.txt` and recognises a book by its title and author, so renaming or moving the file keeps it on the shelf.

## Book List

The book list (titled **Sami**) starts with **Finished Books (N)**, then your books. **Settings → Book List** chooses how books are shown: *Title*, *Title & Percent Read* (e.g. "The Hobbit · 42%" — the percentage appears once you've opened a book with Sami), *Filename* or *Title & Author*.

## Chapter Progress Bar

In the book menu (▶ while reading), **Chapter Bar: On** adds an outlined bar along the very bottom of the page that fills as you read through the current chapter. It works alongside the regular **Progress** indicator, which moves up above it — for example *Progress: Percent* with *Scope: Book* shows both the whole-book percentage and the chapter bar.

## Building

### Prerequisites

| Tool | Purpose |
|------|---------|
| CMake 3.5+ | Build system |
| SDL2 | Desktop emulator |
| Python 3 | Tools and scripts |
| [PlatformIO](https://platformio.org/) | ESP32 firmware build and flash |

### Desktop (emulator)

The desktop build runs the full reader UI in an SDL2 window on Windows, Linux, or macOS — no hardware needed. It uses an `sd/` folder in the working directory as the virtual SD card: drop `.epub` files there to read them, `.mfb` fonts in `sd/fonts/`, and sleep images in `sd/.sleep/`.

```powershell
cmake -S platforms/desktop -B build/desktop-debug -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5"
cmake --build build/desktop-debug --config Debug
.\build\desktop-debug\Debug\microreader_desktop.exe
```

| | |
|---|---|
| ![Book list](images/main%20menu.png) | ![Reader](images/reader.png) |

![Reading options](images/reading%20options.png)

![Settings](images/settings.png)

### ESP32 (PlatformIO)

```powershell
# Build + flash
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -t upload

# Serial monitor
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe device monitor --baud 115200
```

Upload baud: 921600.

### Tests

```powershell
cd test
cmake -B build2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
cmake --build build2 --config Debug

.\build2\Debug\unit_tests.exe          # fast (~375 tests, <1s)
.\build2\Debug\microreader_tests.exe   # includes real EPUB integration tests
```

### QEMU (no hardware needed)

```powershell
# Terminal 1
python tools/run_qemu.py --with-books

# Terminal 2
python tools/test_books.py --port socket://localhost:4444 --pages 20 --delay 0.1
```

## Project Structure

```
lib/microreader/          shared core (platform-agnostic C++20)
  content/                EPUB parsing, layout, MRB binary format
  display/                Canvas, DisplayQueue, Font interfaces
  screens/                UI screen implementations
platforms/desktop/        SDL2 emulator
platforms/esp32/          ESP-IDF + PlatformIO firmware
test/                     Google Test suite
tools/                    Python scripts and dev tools
  calibre-plugin/         Calibre device plugin (build.py → microreader.zip)
docs/                     GitHub Pages — browser-based file manager (Web Serial API)
resources/                Fonts, sleep images
```

## Serial Command Line

`tools/serial_cmd.py` talks to the device over USB serial using the same CMND protocol as the browser manager. It has two modes:

**Non-interactive** — pass flags and exit. Useful for scripting and CI:

```powershell
python tools/serial_cmd.py --port COM5 --upload "book.epub"          # upload an EPUB
python tools/serial_cmd.py --port COM5 --upload-dir "path/to/dir"    # upload all EPUBs in a folder (skips duplicates)
python tools/serial_cmd.py --port COM5 --upload-sleep "image.bmp"    # upload a sleep image
python tools/serial_cmd.py --port COM5 --upload-sd-font "font.mfb"   # upload a font to SD card
python tools/serial_cmd.py --port COM5 --list                        # list books on device
python tools/serial_cmd.py --port COM5 --sync-time                   # set the device clock from this PC
```

**Interactive** — a REPL for manual control:

```powershell
python tools/serial_cmd.py --port COM5
```

Interactive commands include `open`, `rm`, `upload`, `btn` (simulate button presses), `test` (open every book and watch for pass/fail), `bench` (conversion benchmark), and more. Type `help` once connected for the full list.

## Font Generation

UI fonts (the bitmap fonts used for the device interface) are built from source using `tools/generate_font.py` and embedded as headers:

```powershell
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 14 --header lib/microreader/display/ui_font_small.h --bw-only --ranges ui
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 18 --header lib/microreader/display/ui_font_medium.h --bw-only --ranges ui
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 24 --header lib/microreader/display/ui_font_large.h --bw-only --ranges ui
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 32 --header lib/microreader/display/ui_font_header.h --bw-only --ranges ui
```

Reader fonts (`.mfb`) are generated via the browser **Font Generator** in the [microreader-manager](https://cidvonhighwind.github.io/microreader/). SD card fonts must fit within 3.375 MB (`0x360000` bytes including the 4 KB header).

## Hyphenation

Uses the [Liang/TeX algorithm](https://tug.org/docs/liang/) with pattern files compiled into binary tries by [Typst hypher](https://github.com/typst/hypher). Supported languages: EN, DE, FR, ES, IT, NL, PT, PL, RU. Language is detected automatically from the EPUB's `xml:lang` attribute.

Trie data is embedded as `constexpr` byte arrays in `lib/microreader/content/hyphenation/Liang/` — no heap allocation, no flash reads at runtime (placed in DROM on ESP32).

<details>
<summary>Adding a new language</summary>

1. Download the `.bin` from [typst/hypher/tries](https://github.com/typst/hypher/tree/main/tries) into `tools/hyphenation/`
2. Generate the header: `python tools/generate_trie_header.py tools/hyphenation/<lang>.bin lib/microreader/content/hyphenation/Liang/hyph-<lang>.trie.h <lang>`
3. Add the new enum value to `HyphenationLang` in `Hyphenation.h`
4. Add a `#include` + `case` in `Hyphenation.cpp` (`hyphenate_word`) and an `ieq` check in `detect_language`

</details>

## Firmware Backup & Restore

The default X4 firmware uses two app partitions — `app0` (at `0x10000`) and `app1` (at `0x650000`). A small OTA data sector at `0xE000` records which one to boot. When you flash new firmware directly with esptool, the OTA data may still point to the other partition, causing the device to boot the old firmware — use `switch_partition.py` to fix that.

Replace `COM5` with your port (`/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-*` on macOS).

### Backup

```powershell
# Full 16 MB flash
python -m esptool --chip esp32c3 --port COM5 read_flash 0x0 0x1000000 firmware_backup.bin

# app0 only (faster)
python -m esptool --chip esp32c3 --port COM5 read_flash 0x10000 0x640000 app0_backup.bin

# app1 only
python -m esptool --chip esp32c3 --port COM5 read_flash 0x650000 0x640000 app1_backup.bin
```

### Restore

```powershell
# Full flash
python -m esptool --chip esp32c3 --port COM5 write_flash 0x0 firmware_backup.bin

# app0 only
python -m esptool --chip esp32c3 --port COM5 write_flash 0x10000 app0_backup.bin

# app1 only
python -m esptool --chip esp32c3 --port COM5 write_flash 0x650000 app1_backup.bin
```

### Switch boot partition

```powershell
python tools/switch_partition.py app0 --port COM5 --flash
python tools/switch_partition.py app1 --port COM5 --flash
```

## Calibre Plugin Development

The plugin source lives in `tools/calibre-plugin/`. It bundles `pyserial` because Calibre's embedded Python doesn't include it.

### Build

```bash
cd tools/calibre-plugin
python build.py             # packages __init__.py + serial/ into microreader.zip
python build.py --install   # ...and also copies it into Calibre's plugins folder
```

### Debug

```powershell
.\launch-debug.ps1   # equivalent to: calibre-debug -g
```

All `print()` calls in `__init__.py` appear in that terminal, prefixed with `[Microreader]`.

### Protocol smoke test (no Calibre needed)

```powershell
& "C:\Program Files\Calibre2\calibre-debug.exe" -e test.py
```

Opens the serial port directly and verifies device detection and the CMND protocol without starting the full Calibre GUI.

---

## License

GPL v2 — see [LICENSE](LICENSE).

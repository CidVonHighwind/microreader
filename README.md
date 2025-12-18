# MicroReader

A minimal EPUB/TXT reader for ESP32-C3 e-ink devices.

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
  <img src="resources/images/20251218_181610.jpg" alt="Sample 1" style="width: 49%;">
  <img src="resources/images/20251218_181719.jpg" alt="Sample 2" style="width: 49%;">
  <img src="resources/images/20251218_181444.jpg" alt="Sample 3" style="width: 49%;">
  <img src="resources/images/20251218_181453.jpg" alt="Sample 4" style="width: 49%;">
</div>

## Features

- [x] Antialiased font rendering
- [x] TXT reader
- [x] EPUB reader
- [x] File browser for SD card navigation
- [x] Bold/Italic font support
- [x] Simple English/German hyphenation

---

## Quick Start

### I want to contribute code (no hardware needed)

```bash
# Clone
git clone https://github.com/CidVonHighwind/microreader.git
cd microreader

# Build tests
test/scripts/build_tests.sh      # macOS/Linux
test\scripts\build_tests.ps1     # Windows

# Run tests
test/scripts/run_tests.sh        # macOS/Linux
test\scripts\run_tests.ps1       # Windows
```

### I have the hardware

```bash
# Build + upload + monitor
platformio run -t upload && platformio device monitor
```

---

## Project Structure

```
src/
├── content/          # EPUB/TXT parsing, word providers
│   ├── epub/         # EPUB reader
│   ├── providers/    # StringWordProvider, FileWordProvider, EpubWordProvider
│   └── xml/          # XML parser
├── text/             # Text layout engine
│   ├── layout/       # GreedyLayout, KnuthPlassLayout
│   └── hyphenation/  # Language-specific hyphenation
├── rendering/        # Font rendering (TextRenderer, SimpleFont)
├── ui/               # Screens (FileBrowser, TextViewer, ImageViewer)
├── core/             # Hardware drivers (EInkDisplay, SDCard, Buttons)
└── resources/        # Embedded fonts and images

test/
├── unit/             # Test executables
├── mocks/            # Arduino mocks for desktop testing
├── common/           # Test utilities (TestRunner)
└── scripts/          # Build/run scripts
```

---

## Running Tests

Tests run on your machine - no hardware required. Uses mocked Arduino APIs.

```powershell
# Write back the entire flash
python -m esptool --chip esp32c3 --baud 921600 --port COM5 write_flash 0x0 firmware_backup.bin
```

**Windows (PowerShell):**
```powershell
# Write back only app0 (faster)
python -m esptool --chip esp32c3 --baud 921600 --port COM5 write_flash 0x10000 app0_backup.bin
```

### Run a specific test
```bash
./test/build/bin/TextLayoutPageRenderTest
./test/build/bin/WordProviderTest
./test/build/bin/GermanHyphenationEvaluationTest
```

### Available tests

| Test | What it validates |
|------|-------------------|
| `WordProviderTest` | Text tokenization, forward/backward navigation |
| `TextLayoutPageRenderTest` | Page layout, pagination |
| `GreedyLayoutBidirectionalParagraphTest` | Line breaking algorithm |
| `GermanHyphenationEvaluationTest` | Hyphenation rules |
| `SimpleXmlParserTest` | XML parsing |
| `EpubReaderTest` | EPUB file reading |

---

## Building for Hardware

### Requirements
- [PlatformIO](https://platformio.org/install)

### Commands

```bash
# Build only
platformio run

# Build + upload
platformio run -t upload

# Serial monitor (115200 baud)
platformio device monitor
```

---

## Hardware

| Component | Spec |
|-----------|------|
| Board | ESP32-C3 (RISC-V @ 160MHz) |
| RAM | 400KB SRAM |
| Flash | 16MB |
| Display | 4.26" E-Ink 800×480 (GDEQ0426T82, SSD1677) |
| Storage | SD Card |

<details>
<summary>Display pin configuration</summary>

| Signal | GPIO | Description |
|--------|------|-------------|
| SCLK | 8 | SPI Clock |
| MOSI | 10 | SPI Data (Master Out) |
| CS | 21 | E-Ink Chip Select |
| DC | 4 | Data/Command |
| RST | 5 | Reset |
| BUSY | 6 | Busy status |

Supports B&W and 4-level grayscale modes.

</details>

---

## Utility Scripts

### Font Generation
```bash
# Install dependencies
pip install -r scripts/generate_simplefont/requirements.txt

# Generate font header from TTF
python -m scripts.generate_simplefont.cli \
  --name Font14 --size 16 \
  --chars-file resources/chars_input.txt \
  --ttf path/to/YourFont.ttf \
  --out src/resources/fonts/Font14.h

# Preview glyphs (GUI)
python scripts/generate_simplefont/gui.py
```

### Other tools
- `scripts/simple_convert_image.py` - Convert images to C++ byte arrays
- `scripts/lut_editor.py` - E-ink waveform LUT editor
- `scripts/extract_chars.py` - Extract unique chars from text files

---

## Advanced: Firmware Backup & Restore

<details>
<summary>Backup original firmware</summary>

```bash
# Full 16MB flash
python -m esptool --chip esp32c3 --port COM5 read_flash 0x0 0x1000000 firmware_backup.bin

# App partition only (faster)
python -m esptool --chip esp32c3 --port COM5 read_flash 0x10000 0x640000 app0_backup.bin
```

</details>

<details>
<summary>Restore firmware</summary>

```bash
# Full flash
python -m esptool --chip esp32c3 --port COM5 write_flash 0x0 firmware_backup.bin

# App partition only
python -m esptool --chip esp32c3 --port COM5 write_flash 0x10000 app0_backup.bin
```

</details>

<details>
<summary>Switch boot partitions (app0/app1)</summary>

```bash
# Backup OTA data first
python -m esptool --port COM4 read_flash 0xE000 0x2000 otadata_backup.bin

# Boot app0
python -m esptool --port COM4 write_flash 0xE000 otadata_boot_app0.bin

# Boot app1
python -m esptool --port COM4 write_flash 0xE000 otadata_boot_app1.bin
```

**Note:** Replace `COM5`/`COM4` with your actual port (`/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-*` on macOS).
</details>

## Settings consolidation

Settings are now consolidated into a single file stored at `/microreader/settings.cfg` on the SD card. The file uses a simple key=value format and is intentionally easy to extend.

Keys of note:
- `ui.screen` - integer last-visible screen id
- `textviewer.lastPath` - last opened file path
- `textviewer.layout` - layout CSV matching previous format

Per-file positions are stored in `.pos` files next to each document (e.g. `/books/foo.txt.pos`) and continue to be used as before; they are not part of `settings.cfg`.

The Settings manager is implemented in `src/core/Settings.{h,cpp}`.

### LUT Editor
`scripts/lut_editor.py` - Visual editor for e-ink display waveform lookup tables
- Edit voltage patterns and timing groups
- Configure display refresh settings

# MicroReader

A minimal EPUB/TXT reader for ESP32-C3 e-ink devices.

## Hardware

- **Board**: ESP32-C3 (QFN32) revision v0.4
- **Chip**: ESP32-C3 (RISC-V @ 160MHz)
- **MAC Address**: 9c:13:9e:64:a6:c4
- **RAM**: 400KB SRAM
- **Flash**: 4MB (actual, not 16MB as originally thought)
- **Display**: 4.26" E-Ink GDEQ0426T82 (800×480px, SSD1677 controller)
  - Custom SPI pins: SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6
  - Supports B&W and 4-level grayscale modes
- **Storage**: SD Card
- **Features**: WiFi, BLE

### Buttons

The device has 6 buttons connected via two resistor ladders:

**GPIO1 (4 buttons)**:
- **Button 0**: Back (ADC: ~3470)
- **Button 1**: Confirm (ADC: ~2655)
- **Button 2**: Left (ADC: ~1470)
- **Button 3**: Right (ADC: ~3)

**GPIO2 (2 buttons)**:
- **Button 4**: Volume Up (ADC: ~2205)
- **Button 5**: Volume Down (ADC: ~3)

**Note**: Each resistor ladder can only detect one button at a time. Lower resistance buttons (higher priority) will override others when multiple buttons are pressed simultaneously on the same ladder. However, buttons on different ladders (GPIO1 vs GPIO2) can be detected independently.

## Firmware Backup

Original firmware has been backed up to `firmware_backup.bin` (16MB).

**To restore original firmware:**
```bash
esptool.py --chip esp32c3 --port COM4 write_flash 0x0 firmware_backup.bin
```

**Backup created:** November 18, 2025

## Features

- [x] Button input via ADC resistor ladder
- [x] E-ink display driver (GxEPD2 with custom modifications)\
  - [x] Non-blocking display updates via FreeRTOS dual-core
- [x] Text-based menu system
- [ ] SD card file browser
- [ ] TXT file reader
- [ ] EPUB reader
- [ ] Power management

## Building

This project uses PlatformIO in a virtual environment:

```powershell
# Build
C:/Users/Patrick/Desktop/microreader/.venv/Scripts/platformio.exe run

# Upload to device (COM4)
C:/Users/Patrick/Desktop/microreader/.venv/Scripts/platformio.exe run -t upload

# Monitor serial output
C:/Users/Patrick/Desktop/microreader/.venv/Scripts/platformio.exe device monitor
```

**Device Info:**
- Port: COM4
- Chip: ESP32-C3 (QFN32) revision v0.4
- MAC: 9c:13:9e:64:a6:c4

## Development Notes

### Completed
- ✅ Firmware backup created
- ✅ PlatformIO environment configured
- ✅ Flash size corrected to 4MB
- ✅ USB CDC Serial working
- ✅ Button input via ADC working
- ✅ E-ink display driver integrated and customized
- ✅ FreeRTOS dual-core architecture (core 0: display, core 1: main loop)
- ✅ Both B&W and 4-level grayscale modes functional
- ✅ Text-based menu with "responsive" cursor navigation

### Architecture
- **Display Task**: Runs on core 0, handles non-blocking e-ink updates
- **Main Loop**: Runs on core 1, processes button input and menu logic
- **Display Drivers**:
  - `EInk426_BW`: B&W mode driver for 4.26" display
  - `EInk426_Gray`: 4-level grayscale mode driver for 4.26" display
  - `EInk_BW_Display<>`: Template wrapper adding Adafruit GFX support for B&W
  - `EInk_Gray_Display<>`: Template wrapper for grayscale displays
- **Menu System**: Modular design with swappable `MenuDisplay` and `MenuDisplayGray` classes

### Todoy
- Create custom LUT

### Next Steps
- Fast find LUT
- Font rendering
- Implement SD card reading
- Build file browser UI
- Add text file viewer

// GDEQ0426T82 4.26" e-ink display driver
// Controller: SSD1677

#include "EInk426_BW.h"

// =============================================================================
// Constructor
// =============================================================================

EInk426_BW::EInk426_BW(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
    : EInk_Base(cs, dc, rst, busy, HIGH, 10000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate,
                hasFastPartialUpdate) {}

// =============================================================================
// Display Initialization & Power Control
// =============================================================================

void EInk426_BW::_InitDisplay() {
  if (_hibernating)
    _reset();
  delay(10);

  _writeCommand(CMD_SOFT_RESET);
  delay(10);

  // Temperature sensor control (internal)
  _writeCommand(CMD_TEMP_SENSOR_CONTROL);
  _writeData(TEMP_SENSOR_INTERNAL);

  // Booster soft-start control (GDEQ0426T82 specific values)
  _writeCommand(CMD_BOOSTER_SOFT_START);
  _writeData(0xAE);
  _writeData(0xC7);
  _writeData(0xC3);
  _writeData(0xC0);
  _writeData(0x80);

  // Driver output control: set display height and scan direction
  _writeCommand(CMD_DRIVER_OUTPUT_CONTROL);
  _writeData((HEIGHT - 1) % 256);  // gates A0..A7 (low byte)
  _writeData((HEIGHT - 1) / 256);  // gates A8..A9 (high byte)
  _writeData(0x02);                // SM=1 (interlaced), TB=0

  // Border waveform control
  _writeCommand(CMD_BORDER_WAVEFORM);
  _writeData(0x01);

  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _init_display_done = true;
}

void EInk426_BW::_PowerOn() {
  if (!_power_is_on) {
    _writeCommand(CMD_DISPLAY_UPDATE_CTRL2);
    _writeData(UPDATE_POWER_ON);
    _writeCommand(CMD_MASTER_ACTIVATION);
    _waitWhileBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

void EInk426_BW::_PowerOff() {
  if (_power_is_on) {
    _writeCommand(CMD_DISPLAY_UPDATE_CTRL2);
    _writeData(UPDATE_POWER_OFF);
    _writeCommand(CMD_MASTER_ACTIVATION);
    _waitWhileBusy("_PowerOff", power_off_time);
  }
  _power_is_on = false;
  _using_partial_mode = false;
}

void EInk426_BW::powerOff() {
  _PowerOff();
}

void EInk426_BW::hibernate() {
  _PowerOff();
  if (_rst >= 0) {
    _writeCommand(CMD_DEEP_SLEEP);
    _writeData(0x01);  // enter deep sleep
    _hibernating = true;
    _init_display_done = false;
  }
}

// =============================================================================
// RAM Area Configuration
// =============================================================================

void EInk426_BW::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  // Gates are reversed on this display, but controller has no gates reverse scan
  // Reverse data entry on y to compensate
  y = HEIGHT - y - h;

  // Set RAM data entry mode: X increment, Y decrement (Y reversed)
  _writeCommand(CMD_DATA_ENTRY_MODE);
  _writeData(DATA_ENTRY_X_INC_Y_DEC);

  // Set RAM X address range (start, end)
  _writeCommand(CMD_SET_RAM_X_RANGE);
  _writeData(x % 256);
  _writeData(x / 256);
  _writeData((x + w - 1) % 256);
  _writeData((x + w - 1) / 256);

  // Set RAM Y address range (start, end)
  _writeCommand(CMD_SET_RAM_Y_RANGE);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
  _writeData(y % 256);
  _writeData(y / 256);

  // Set RAM X address counter
  _writeCommand(CMD_SET_RAM_X_COUNTER);
  _writeData(x % 256);
  _writeData(x / 256);

  // Set RAM Y address counter
  _writeCommand(CMD_SET_RAM_Y_COUNTER);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
}

// =============================================================================
// Screen Buffer Operations
// =============================================================================

void EInk426_BW::clearScreen(uint8_t value) {
  // Full refresh needed for all cases (previous != screen)
  // Reset to default display settings (in case custom LUT was loaded)
  _InitDisplay();
  _writeScreenBuffer(CMD_WRITE_RAM_RED, value);  // set previous buffer (RED RAM used as previous)
  _writeScreenBuffer(CMD_WRITE_RAM_BW, value);   // set current buffer (BW RAM)
  refresh(false);                                // full refresh
  _initial_write = false;
}

void EInk426_BW::writeScreenBuffer(uint8_t value) {
  if (_initial_write)
    return clearScreen(value);
  _writeScreenBuffer(CMD_WRITE_RAM_BW, value);  // set current buffer
}

void EInk426_BW::writeScreenBufferAgain(uint8_t value) {
  _writeScreenBuffer(CMD_WRITE_RAM_BW, value);   // set current buffer
  _writeScreenBuffer(CMD_WRITE_RAM_RED, value);  // set previous buffer
}

void EInk426_BW::_writeScreenBuffer(uint8_t command, uint8_t value) {
  if (!_init_display_done)
    _InitDisplay();
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(command);
  _startTransfer();
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++) {
    _transfer(value);
  }
  _endTransfer();
}

// =============================================================================
// Image Writing (to RAM, no refresh)
// =============================================================================

void EInk426_BW::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                            bool mirror_y, bool pgm) {
  _writeImage(CMD_WRITE_RAM_BW, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void EInk426_BW::writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                          bool invert, bool mirror_y, bool pgm) {
  _writeImage(CMD_WRITE_RAM_RED, bitmap, x, y, w, h, invert, mirror_y, pgm);  // set previous buffer
  _writeImage(CMD_WRITE_RAM_BW, bitmap, x, y, w, h, invert, mirror_y, pgm);   // set current buffer
}

void EInk426_BW::writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                                 bool mirror_y, bool pgm) {
  _writeImage(CMD_WRITE_RAM_RED, bitmap, x, y, w, h, invert, mirror_y, pgm);  // set previous buffer
  _writeImage(CMD_WRITE_RAM_BW, bitmap, x, y, w, h, invert, mirror_y, pgm);   // set current buffer
}

void EInk426_BW::_writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                             bool invert, bool mirror_y, bool pgm) {
  delay(1);                                                        // yield() to avoid WDT on ESP8266 and ESP32
  int16_t wb = (w + 7) / 8;                                        // width bytes, bitmaps are padded
  x -= x % 8;                                                      // byte boundary
  w = wb * 8;                                                      // byte boundary
  int16_t x1 = x < 0 ? 0 : x;                                      // limit
  int16_t y1 = y < 0 ? 0 : y;                                      // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;    // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;  // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0))
    return;
  if (!_init_display_done)
    _InitDisplay();
  if (_initial_write)
    writeScreenBuffer();  // initial full screen buffer clean
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++) {
    for (int16_t j = 0; j < w1 / 8; j++) {
      uint8_t data;
      // use wb, h of bitmap for index!
      int32_t idx = mirror_y ? j + dx / 8 + ((h - 1 - int32_t(i + dy))) * wb : j + dx / 8 + int32_t(i + dy) * wb;
      if (pgm) {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      } else {
        data = bitmap[idx];
      }
      if (invert)
        data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1);  // yield() to avoid WDT on ESP8266 and ESP32
}

void EInk426_BW::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
                                int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                                bool mirror_y, bool pgm) {
  _writeImagePart(CMD_WRITE_RAM_BW, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void EInk426_BW::writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
                                     int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                                     bool mirror_y, bool pgm) {
  _writeImagePart(CMD_WRITE_RAM_BW, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImagePart(CMD_WRITE_RAM_RED, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void EInk426_BW::_writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                 int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool invert, bool mirror_y, bool pgm) {
  delay(1);  // yield() to avoid WDT on ESP8266 and ESP32
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0))
    return;
  if ((x_part < 0) || (x_part >= w_bitmap))
    return;
  if ((y_part < 0) || (y_part >= h_bitmap))
    return;
  int16_t wb_bitmap = (w_bitmap + 7) / 8;                          // width bytes, bitmaps are padded
  x_part -= x_part % 8;                                            // byte boundary
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w;               // limit
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h;               // limit
  x -= x % 8;                                                      // byte boundary
  w = 8 * ((w + 7) / 8);                                           // byte boundary, bitmaps are padded
  int16_t x1 = x < 0 ? 0 : x;                                      // limit
  int16_t y1 = y < 0 ? 0 : y;                                      // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;    // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;  // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0))
    return;
  if (!_init_display_done)
    _InitDisplay();
  if (_initial_write)
    writeScreenBuffer();  // initial full screen buffer clean
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++) {
    for (int16_t j = 0; j < w1 / 8; j++) {
      uint8_t data;
      // use wb_bitmap, h_bitmap of bitmap for index!
      int32_t idx = mirror_y ? x_part / 8 + j + dx / 8 + int32_t((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap
                             : x_part / 8 + j + dx / 8 + int32_t(y_part + i + dy) * wb_bitmap;
      if (pgm) {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      } else {
        data = bitmap[idx];
      }
      if (invert)
        data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1);  // yield() to avoid WDT on ESP8266 and ESP32
}

void EInk426_BW::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h,
                            bool invert, bool mirror_y, bool pgm) {
  if (black) {
    writeImage(black, x, y, w, h, invert, mirror_y, pgm);
  }
}

void EInk426_BW::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part,
                                int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
                                bool invert, bool mirror_y, bool pgm) {
  if (black) {
    writeImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
}

void EInk426_BW::writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h,
                             bool invert, bool mirror_y, bool pgm) {
  if (data1) {
    writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
  }
}

// =============================================================================
// Image Drawing (write + refresh)
// =============================================================================

void EInk426_BW::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                           bool mirror_y, bool pgm) {
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImageAgain(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void EInk426_BW::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
                               int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
                               bool pgm) {
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImagePartAgain(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void EInk426_BW::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h,
                           bool invert, bool mirror_y, bool pgm) {
  if (black) {
    drawImage(black, x, y, w, h, invert, mirror_y, pgm);
  }
}

void EInk426_BW::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part,
                               int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
                               bool invert, bool mirror_y, bool pgm) {
  if (black) {
    drawImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
}

void EInk426_BW::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h,
                            bool invert, bool mirror_y, bool pgm) {
  if (data1) {
    drawImage(data1, x, y, w, h, invert, mirror_y, pgm);
  }
}

// =============================================================================
// Display Refresh
// =============================================================================

void EInk426_BW::refresh(bool partial_update_mode) {
  if (partial_update_mode)
    refresh(0, 0, WIDTH, HEIGHT);
  else {
    _Update_Full();
    _initial_refresh = false;  // initial full update done
  }
}

void EInk426_BW::refresh(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (_initial_refresh)
    return refresh(false);  // initial update needs be full update
  // intersection with screen
  int16_t w1 = x < 0 ? w + x : w;                              // reduce
  int16_t h1 = y < 0 ? h + y : h;                              // reduce
  int16_t x1 = x < 0 ? 0 : x;                                  // limit
  int16_t y1 = y < 0 ? 0 : y;                                  // limit
  w1 = x1 + w1 < int16_t(WIDTH) ? w1 : int16_t(WIDTH) - x1;    // limit
  h1 = y1 + h1 < int16_t(HEIGHT) ? h1 : int16_t(HEIGHT) - y1;  // limit
  if ((w1 <= 0) || (h1 <= 0))
    return;
  // make x1, w1 multiple of 8
  w1 += x1 % 8;
  if (w1 % 8 > 0)
    w1 += 8 - w1 % 8;
  x1 -= x1 % 8;
  _setPartialRamArea(x1, y1, w1, h1);
  _Update_Part();
}

// =============================================================================
// Update Routines (LUT & Waveform Control)
// =============================================================================

// clang-format off
// Custom test LUT with voltage values (110 bytes total)
// Format: 50 bytes VS + 50 bytes TP/RP + 5 bytes frame rate + 5 bytes voltages
const unsigned char EInk426_BW::lut_custom_test[] PROGMEM = {
    // VS blocks (5 x 10 bytes = 50 bytes)
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT0 - Black to Black
    0x88, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT1 - Black to White
    0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT2 - White to Black
    0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT3 - White to White
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT4 - VCOM

    // TP/RP blocks (10 x 5 bytes = 50 bytes)
    0x01, 0x01, 0x01, 0x01, 0x00,  // Group 0
    0x01, 0x01, 0x01, 0x01, 0x00,  // Group 1
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 2
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 3
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 4
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 5
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 6
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 7
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 8
    0x00, 0x00, 0x00, 0x00, 0x00,  // Group 9

    // Frame rate bytes (5 bytes)
    0x44, 0x44, 0x44, 0x44, 0x44,

    // Voltage values (5 bytes): VGH, VSH1, VSH2, VSL, VCOM
    0x17, 0x41, 0xA8, 0x32, 0x30,
};
// clang-format on

void EInk426_BW::_Update_Full() {
  // Display Update Control 1
  _writeCommand(CMD_DISPLAY_UPDATE_CTRL1);
  _writeData(UPDATE_CTRL1_BYPASS_RED);  // bypass RED RAM as 0
  _writeData(0x00);                     // single chip application

  if (useFastFullUpdate) {
    // Fast full update mode (reduced temperature range)
    _writeCommand(0x1A);  // Write temperature register
    _writeData(0x5A);     // temperature value for fast mode
    _writeCommand(CMD_DISPLAY_UPDATE_CTRL2);
    _writeData(UPDATE_MODE_FULL_FAST);
  } else {
    // Normal full update mode
    _writeCommand(CMD_DISPLAY_UPDATE_CTRL2);
    _writeData(UPDATE_MODE_FULL_REFRESH);
  }

  _writeCommand(CMD_MASTER_ACTIVATION);
  _waitWhileBusy("_Update_Full", full_refresh_time);
  _power_is_on = false;
}

void EInk426_BW::_Update_Part() {
  // Display Update Control 1
  _writeCommand(CMD_DISPLAY_UPDATE_CTRL1);
  _writeData(UPDATE_CTRL1_NORMAL);  // RED RAM normal (not bypassed)
  _writeData(0x00);                 // single chip application

  // Display Update Control 2: use custom LUT mode if active, otherwise normal partial
  _writeCommand(CMD_DISPLAY_UPDATE_CTRL2);
  if (_custom_lut_active) {
    _writeData(UPDATE_MODE_PARTIAL_FAST);  // Use fast mode with custom LUT
  } else {
    _writeData(UPDATE_MODE_PARTIAL_NORMAL);  // Normal partial update
  }

  _writeCommand(CMD_MASTER_ACTIVATION);

  // Capture actual elapsed time from BUSY pin
  unsigned long elapsed_us = _waitWhileBusy("_Update_Part", 0u);
  Serial.printf("Parial update time: %d ms\n", elapsed_us / 1000);

  _power_is_on = true;
}

// =============================================================================
// Testing / Debug Methods
// =============================================================================

uint16_t EInk426_BW::calculateLUTRefreshTime(const uint8_t* lut) {
  // Calculate refresh time from LUT timing parameters
  // LUT structure: 50 bytes VS + 50 bytes TP/RP + 5 bytes frame rate + 5 bytes voltages
  // TP/RP section: 10 groups of 5 bytes each (TP0, TP1, TP2, TP3, RP)
  // Each TP value represents number of frames, RP is repeat count (0 = repeat once)

  uint32_t total_frames = 0;

  // Parse the 10 TP/RP groups (bytes 50-99)
  for (uint8_t group = 0; group < 10; group++) {
    uint16_t group_offset = 50 + (group * 5);
    uint8_t tp0 = pgm_read_byte(&lut[group_offset + 0]);
    uint8_t tp1 = pgm_read_byte(&lut[group_offset + 1]);
    uint8_t tp2 = pgm_read_byte(&lut[group_offset + 2]);
    uint8_t tp3 = pgm_read_byte(&lut[group_offset + 3]);
    uint8_t rp = pgm_read_byte(&lut[group_offset + 4]);

    // RP value: 0 means repeat once, 1 means repeat twice, etc.
    uint8_t repeat_count = rp + 1;

    // Sum all timing phases for this group
    uint16_t group_frames = (tp0 + tp1 + tp2 + tp3) * repeat_count;
    total_frames += group_frames;
  }

  // Read frame rate bytes (100-104) - these control the base timing per frame
  // Average the 5 frame rate bytes to get timing multiplier
  uint32_t frame_rate_sum = 0;
  for (uint8_t i = 0; i < 5; i++) {
    frame_rate_sum += pgm_read_byte(&lut[100 + i]);
  }
  uint8_t avg_frame_rate = frame_rate_sum / 5;

  // Convert frame rate value to milliseconds per frame
  // Formula: FrameTime = BaseClock / FrameRateByte
  // BaseClock is typically 2000-3000 (assumed 2500 for this calculation)
  // Lower FrameRateByte = longer frame time (slower refresh)
  // 0x22 (34) → 2500/34 ≈ 73ms, 0x44 (68) → 2500/68 ≈ 36ms
  uint16_t ms_per_frame = 2500 / avg_frame_rate;
  if (ms_per_frame < 10)
    ms_per_frame = 10;  // Minimum 10ms per frame

  uint16_t refresh_time = (total_frames * ms_per_frame);

  // Add safety margin (10%) to account for controller overhead
  refresh_time = refresh_time + (refresh_time / 10);

  return refresh_time;
}

void EInk426_BW::setCustomLUT(bool enabled) {
  if (enabled) {
    // Load custom LUT with voltage initialization
    // Does NOT trigger a refresh - just prepares the LUT for subsequent refresh operations

    // Initialize display if needed
    if (!_init_display_done)
      _InitDisplay();

    // Load custom test LUT with voltage initialization
    // First 105 bytes are LUT data (VS + TP/RP + frame rate)
    _writeCommand(CMD_WRITE_LUT);
    for (uint16_t i = 0; i < 105; i++) {
      _writeData(pgm_read_byte(&lut_custom_test[i]));
    }

    // Set voltage values from bytes 105-109
    _writeCommand(CMD_GATE_VOLTAGE);  // 0x03: VGH
    _writeData(pgm_read_byte(&lut_custom_test[105]));

    _writeCommand(CMD_SOURCE_VOLTAGE);                 // 0x04: VSH1, VSH2, VSL
    _writeData(pgm_read_byte(&lut_custom_test[106]));  // VSH1
    _writeData(pgm_read_byte(&lut_custom_test[107]));  // VSH2
    _writeData(pgm_read_byte(&lut_custom_test[108]));  // VSL

    _writeCommand(CMD_WRITE_VCOM);  // 0x2C: VCOM
    _writeData(pgm_read_byte(&lut_custom_test[109]));

    // Calculate and store the refresh time for this LUT
    _custom_lut_refresh_time = calculateLUTRefreshTime(lut_custom_test);
    _custom_lut_active = true;

    // LUT is now loaded and ready to use for subsequent refresh operations
  } else {
    // Disable custom LUT and reset to default settings
    resetDisplay();
  }
}

void EInk426_BW::resetDisplay() {
  // Reset display to default settings after custom LUT testing
  _custom_lut_active = false;
  _custom_lut_refresh_time = 0;
  _InitDisplay();
}

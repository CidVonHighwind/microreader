#include "EInkDisplay.h"

#include <cstring>
#include <fstream>
#include <vector>

// SSD1677 command definitions
// Initialization and reset
#define CMD_SOFT_RESET 0x12             // Soft reset
#define CMD_BOOSTER_SOFT_START 0x0C     // Booster soft-start control
#define CMD_DRIVER_OUTPUT_CONTROL 0x01  // Driver output control
#define CMD_BORDER_WAVEFORM 0x3C        // Border waveform control
#define CMD_TEMP_SENSOR_CONTROL 0x18    // Temperature sensor control

// RAM and buffer management
#define CMD_DATA_ENTRY_MODE 0x11     // Data entry mode
#define CMD_SET_RAM_X_RANGE 0x44     // Set RAM X address range
#define CMD_SET_RAM_Y_RANGE 0x45     // Set RAM Y address range
#define CMD_SET_RAM_X_COUNTER 0x4E   // Set RAM X address counter
#define CMD_SET_RAM_Y_COUNTER 0x4F   // Set RAM Y address counter
#define CMD_WRITE_RAM_BW 0x24        // Write to BW RAM (current frame)
#define CMD_WRITE_RAM_RED 0x26       // Write to RED RAM (used for fast refresh)
#define CMD_AUTO_WRITE_BW_RAM 0x46   // Auto write BW RAM
#define CMD_AUTO_WRITE_RED_RAM 0x47  // Auto write RED RAM

// Display update and refresh
#define CMD_DISPLAY_UPDATE_CTRL1 0x21  // Display update control 1
#define CMD_DISPLAY_UPDATE_CTRL2 0x22  // Display update control 2
#define CMD_MASTER_ACTIVATION 0x20     // Master activation
#define CTRL1_NORMAL 0x00              // Normal mode - compare RED vs BW for partial
#define CTRL1_BYPASS_RED 0x40          // Bypass RED RAM (treat as 0) - for full refresh

// LUT and voltage settings
#define CMD_WRITE_LUT 0x32       // Write LUT
#define CMD_GATE_VOLTAGE 0x03    // Gate voltage
#define CMD_SOURCE_VOLTAGE 0x04  // Source voltage
#define CMD_WRITE_VCOM 0x2C      // Write VCOM
#define CMD_WRITE_TEMP 0x1A      // Write temperature

// Power management
#define CMD_DEEP_SLEEP 0x10  // Deep sleep

// Custom LUT for fast refresh
const unsigned char lut_grayscale[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x00, 0x00, 0x00, 0x00, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

const unsigned char lut_grayscale_revert[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0x54, 0x54, 0x54, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0xA8, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xFC, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

EInkDisplay::EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : _sclk(sclk), _mosi(mosi), _cs(cs), _dc(dc), _rst(rst), _busy(busy), customLutActive(false) {
  Serial.printf("[%lu] EInkDisplay: Constructor called\n", millis());
  Serial.printf("[%lu]   SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BUSY=%d\n", millis(), sclk, mosi, cs, dc, rst, busy);

  memset(frameBuffer, 0xFF, BUFFER_SIZE);  // Initialize to white
  Serial.printf("[%lu]   Frame buffer initialized (%lu bytes)\n", millis(), BUFFER_SIZE);
}

EInkDisplay::~EInkDisplay() {
  Serial.printf("[%lu] EInkDisplay: Destructor called\n", millis());
}

void EInkDisplay::begin() {
  Serial.printf("[%lu] EInkDisplay: begin() called\n", millis());
  Serial.printf("[%lu]   Initializing e-ink display driver...\n", millis());

  // Initialize SPI with custom pins
  SPI.begin(_sclk, -1, _mosi, _cs);
  spiSettings = SPISettings(40000000, MSBFIRST, SPI_MODE0);  // MODE0 is standard for SSD1677
  Serial.printf("[%lu]   SPI initialized at 40 MHz, Mode 0\n", millis());

  // Setup GPIO pins
  pinMode(_cs, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT);

  digitalWrite(_cs, HIGH);
  digitalWrite(_dc, HIGH);

  Serial.printf("[%lu]   GPIO pins configured\n", millis());

  // Reset display
  resetDisplay();

  // Initialize display controller
  initDisplayController();

  Serial.printf("[%lu]   E-ink display driver initialized\n", millis());
}

// ============================================================================
// Low-level display control methods
// ============================================================================

void EInkDisplay::resetDisplay() {
  Serial.printf("[%lu]   Resetting display...\n", millis());
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
  Serial.printf("[%lu]   Display reset complete\n", millis());
}

void EInkDisplay::sendCommand(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, LOW);  // Command mode
  digitalWrite(_cs, LOW);  // Select chip
  SPI.transfer(command);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(uint8_t data) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  // Data mode
  digitalWrite(_cs, LOW);   // Select chip
  SPI.transfer(data);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(const uint8_t* data, uint16_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);       // Data mode
  digitalWrite(_cs, LOW);        // Select chip
  SPI.writeBytes(data, length);  // Transfer all bytes
  digitalWrite(_cs, HIGH);       // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::waitWhileBusy(const char* comment) {
  unsigned long start = millis();
  while (digitalRead(_busy) == HIGH) {
    delay(1);
    if (millis() - start > 10000) {
      Serial.printf("[%lu]   Timeout waiting for busy%s\n", millis(), comment ? comment : "");
      break;
    }
  }
  if (comment) {
    Serial.printf("[%lu]   Wait complete: %s (%lu ms)\n", millis(), comment, millis() - start);
  }
}

void EInkDisplay::initDisplayController() {
  Serial.printf("[%lu]   Initializing SSD1677 controller...\n", millis());

  const uint8_t TEMP_SENSOR_INTERNAL = 0x80;

  // Soft reset
  sendCommand(CMD_SOFT_RESET);
  waitWhileBusy(" CMD_SOFT_RESET");

  // Temperature sensor control (internal)
  sendCommand(CMD_TEMP_SENSOR_CONTROL);
  sendData(TEMP_SENSOR_INTERNAL);

  // Booster soft-start control (GDEQ0426T82 specific values)
  sendCommand(CMD_BOOSTER_SOFT_START);
  sendData(0xAE);
  sendData(0xC7);
  sendData(0xC3);
  sendData(0xC0);
  sendData(0x40);

  // Driver output control: set display height (480) and scan direction
  const uint16_t HEIGHT = 480;
  sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
  sendData((HEIGHT - 1) % 256);  // gates A0..A7 (low byte)
  sendData((HEIGHT - 1) / 256);  // gates A8..A9 (high byte)
  sendData(0x02);                // SM=1 (interlaced), TB=0

  // Border waveform control
  sendCommand(CMD_BORDER_WAVEFORM);
  sendData(0x01);

  // Set up full screen RAM area
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  Serial.printf("[%lu]   Clearing RAM buffers...\n", millis());
  sendCommand(CMD_AUTO_WRITE_BW_RAM);  // Auto write BW RAM
  sendData(0xF7);
  waitWhileBusy(" CMD_AUTO_WRITE_BW_RAM");

  sendCommand(CMD_AUTO_WRITE_RED_RAM);  // Auto write RED RAM
  sendData(0xF7);                       // Fill with white pattern
  waitWhileBusy(" CMD_AUTO_WRITE_RED_RAM");

  Serial.printf("[%lu]   SSD1677 controller initialized\n", millis());
}

void EInkDisplay::setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  const uint16_t WIDTH = 800;
  const uint16_t HEIGHT = 480;
  const uint8_t DATA_ENTRY_X_INC_Y_DEC = 0x01;

  // Reverse Y coordinate (gates are reversed on this display)
  y = HEIGHT - y - h;

  // Set data entry mode (X increment, Y decrement for reversed gates)
  sendCommand(CMD_DATA_ENTRY_MODE);
  sendData(DATA_ENTRY_X_INC_Y_DEC);

  // Set RAM X address range (start, end) - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_RANGE);
  sendData(x % 256);            // start low byte
  sendData(x / 256);            // start high byte
  sendData((x + w - 1) % 256);  // end low byte
  sendData((x + w - 1) / 256);  // end high byte

  // Set RAM Y address range (start, end) - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_RANGE);
  sendData((y + h - 1) % 256);  // start low byte
  sendData((y + h - 1) / 256);  // start high byte
  sendData(y % 256);            // end low byte
  sendData(y / 256);            // end high byte

  // Set RAM X address counter - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_COUNTER);
  sendData(x % 256);  // low byte
  sendData(x / 256);  // high byte

  // Set RAM Y address counter - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_COUNTER);
  sendData((y + h - 1) % 256);  // low byte
  sendData((y + h - 1) / 256);  // high byte
}

void EInkDisplay::clearScreen(uint8_t color) {
  Serial.printf("[%lu]   Clearing frame buffer to 0x%02X...\n", millis(), color);
  memset(frameBuffer, color, BUFFER_SIZE);
  memset(frameBuffer_lsb, 0x00, BUFFER_SIZE);
  memset(frameBuffer_msb, 0x00, BUFFER_SIZE);
}

void EInkDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem) {
  Serial.printf("[%lu]   Drawing image to frame buffer at (%d,%d) size %dx%d...\n", millis(), x, y, w, h);

  // Calculate bytes per line for the image
  uint16_t imageWidthBytes = w / 8;

  // Copy image data to frame buffer
  for (uint16_t row = 0; row < h; row++) {
    uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT)
      break;

    uint16_t destOffset = destY * DISPLAY_WIDTH_BYTES + (x / 8);
    uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= DISPLAY_WIDTH_BYTES)
        break;

      if (fromProgmem) {
        frameBuffer[destOffset + col] = pgm_read_byte(&imageData[srcOffset + col]);
      } else {
        frameBuffer[destOffset + col] = imageData[srcOffset + col];
      }
    }
  }

  Serial.printf("[%lu]   Image drawn to frame buffer\n", millis());
}

void EInkDisplay::writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size) {
  const char* bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  unsigned long startTime = millis();
  Serial.printf("[%lu]   Writing frame buffer to %s RAM (%lu bytes)...\n", startTime, bufferName, size);

  sendCommand(ramBuffer);
  sendData(data, size);

  unsigned long duration = millis() - startTime;
  Serial.printf("[%lu]   %s RAM write complete (%lu ms)\n", millis(), bufferName, duration);
}

void EInkDisplay::setGrayscaleBuffers(const uint8_t* bwBuffer, const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  if (bwBuffer != nullptr) {
    memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
  }
  if (lsbBuffer != nullptr) {
    memcpy(frameBuffer_lsb, lsbBuffer, BUFFER_SIZE);
  }
  if (msbBuffer != nullptr) {
    memcpy(frameBuffer_msb, msbBuffer, BUFFER_SIZE);
  }

  drawGrayscale = true;
}

void EInkDisplay::enableGrayscaleDrawing(bool enable) {
  drawGrayscale = enable;
}

void EInkDisplay::grayscaleRevert() {
  inGrayscaleMode = false;

  // Load the revert LUT
  setCustomLUT(true, lut_grayscale_revert);
  refreshDisplay(FAST_REFRESH);
  setCustomLUT(false);
}

void EInkDisplay::displayBuffer(RefreshMode mode) {
  if (!isScreenOn) {
    // Force half refresh if screen is off
    mode = HALF_REFRESH;
  }

  // If currently in grayscale mode, revert first to black/white
  if (inGrayscaleMode) {
    inGrayscaleMode = false;
    grayscaleRevert();
  }

  // Set up full screen RAM area
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (mode != FAST_REFRESH) {
    // For full refresh, write to both buffers before refresh
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, BUFFER_SIZE);
  } else {
    // For fast refresh, write to BW buffer only
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, BUFFER_SIZE);
  }

  // switch active buffer for next time
  uint8_t* temp = frameBuffer;
  frameBuffer = frameBufferActive;
  frameBufferActive = temp;

  // Refresh the display
  refreshDisplay(mode);

  if (drawGrayscale) {
    drawGrayscale = false;
    inGrayscaleMode = true;

    // grayscale pass
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer_lsb, BUFFER_SIZE);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer_msb, BUFFER_SIZE);

    // activate the custom LUT for grayscale rendering and refresh
    setCustomLUT(true, lut_grayscale);
    refreshDisplay(FAST_REFRESH);
    setCustomLUT(false);
  }
}

void EInkDisplay::refreshDisplay(RefreshMode mode) {
  // Configure Display Update Control 1
  sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
  sendData((mode == FAST_REFRESH) ? CTRL1_NORMAL : CTRL1_BYPASS_RED);  // Configure buffer comparison mode

  // best guess at display mode bits:
  // bit | hex | name                    | effect
  // ----+-----+--------------------------+-------------------------------------------
  // 7   | 80  | CLOCK_ON                | Start internal oscillator
  // 6   | 40  | ANALOG_ON               | Enable analog power rails (VGH/VGL drivers)
  // 5   | 20  | TEMP_LOAD               | Load temperature (internal or I2C)
  // 4   | 10  | LUT_LOAD                | Load waveform LUT
  // 3   | 08  | MODE_SELECT             | Mode 1/2
  // 2   | 04  | DISPLAY_START           | Run display
  // 1   | 02  | ANALOG_OFF_PHASE        | Shutdown step 1 (undocumented)
  // 0   | 01  | CLOCK_OFF               | Disable internal oscillator

  // Select appropriate display mode based on refresh type
  uint8_t displayMode;

  // enable counter and analog is not already on
  if (!isScreenOn) {
    isScreenOn = true;
    displayMode |= 0xC0;  // Set CLOCK_ON and ANALOG_ON bits
  }

  if (mode == FULL_REFRESH) {
    displayMode |= 0x34;  // Full refresh
  } else if (mode == HALF_REFRESH) {
    // write high temp to the register for a faster refresh
    // this is not a real mode
    sendCommand(CMD_WRITE_TEMP);
    sendData(0x5A);
    displayMode |= 0xD4;
  } else {                                         // FAST_REFRESH
    displayMode |= customLutActive ? 0x0C : 0x1C;  // turn off stuff
  }

  // Power on and refresh display
  const char* refreshType = (mode == FULL_REFRESH) ? "full" : (mode == HALF_REFRESH) ? "half" : "fast";
  Serial.printf("[%lu]   Powering on display 0x%02X (%s refresh)...\n", millis(), displayMode, refreshType);
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(displayMode);

  sendCommand(CMD_MASTER_ACTIVATION);

  // Wait for display to finish updating
  Serial.printf("[%lu]   Waiting for display refresh...\n", millis());
  waitWhileBusy(refreshType);
}

void EInkDisplay::setCustomLUT(bool enabled, const unsigned char* lutData) {
  if (enabled) {
    Serial.printf("[%lu]   Loading custom LUT...\n", millis());

    // Load custom LUT (first 105 bytes: VS + TP/RP + frame rate)
    sendCommand(CMD_WRITE_LUT);
    for (uint16_t i = 0; i < 105; i++) {
      sendData(pgm_read_byte(&lutData[i]));
    }

    // Set voltage values from bytes 105-109
    sendCommand(CMD_GATE_VOLTAGE);  // VGH
    sendData(pgm_read_byte(&lutData[105]));

    sendCommand(CMD_SOURCE_VOLTAGE);         // VSH1, VSH2, VSL
    sendData(pgm_read_byte(&lutData[106]));  // VSH1
    sendData(pgm_read_byte(&lutData[107]));  // VSH2
    sendData(pgm_read_byte(&lutData[108]));  // VSL

    sendCommand(CMD_WRITE_VCOM);  // VCOM
    sendData(pgm_read_byte(&lutData[109]));

    customLutActive = true;
    Serial.printf("[%lu]   Custom LUT loaded\n", millis());
  } else {
    customLutActive = false;
    // Reinitialize display to restore default LUT
    // initDisplayController();
    Serial.printf("[%lu]   Custom LUT disabled\n", millis());
  }
}

void EInkDisplay::deepSleep() {
  // Enter deep sleep mode
  Serial.printf("[%lu]   Entering deep sleep mode...\n", millis());
  sendCommand(CMD_DEEP_SLEEP);
  sendData(0x01);  // Enter deep sleep
  waitWhileBusy(" after deep sleep mode");
}

void EInkDisplay::saveFrameBufferAsPBM(const char* filename) {
#ifndef ARDUINO
  const uint8_t* buffer = getFrameBuffer();

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    Serial.printf("Failed to open %s for writing\n", filename);
    return;
  }

  // Rotate the image 90 degrees counterclockwise when saving
  // Original buffer: 800x480 (landscape)
  // Output image: 480x800 (portrait)
  const int DISPLAY_WIDTH_LOCAL = DISPLAY_WIDTH;    // 800
  const int DISPLAY_HEIGHT_LOCAL = DISPLAY_HEIGHT;  // 480
  const int DISPLAY_WIDTH_BYTES_LOCAL = DISPLAY_WIDTH_LOCAL / 8;

  file << "P4\n";  // Binary PBM
  file << DISPLAY_HEIGHT_LOCAL << " " << DISPLAY_WIDTH_LOCAL << "\n";

  // Create rotated buffer
  std::vector<uint8_t> rotatedBuffer((DISPLAY_HEIGHT_LOCAL / 8) * DISPLAY_WIDTH_LOCAL, 0);

  for (int outY = 0; outY < DISPLAY_WIDTH_LOCAL; outY++) {
    for (int outX = 0; outX < DISPLAY_HEIGHT_LOCAL; outX++) {
      int inX = outY;
      int inY = DISPLAY_HEIGHT_LOCAL - 1 - outX;

      int inByteIndex = inY * DISPLAY_WIDTH_BYTES_LOCAL + (inX / 8);
      int inBitPosition = 7 - (inX % 8);
      bool isWhite = (buffer[inByteIndex] >> inBitPosition) & 1;

      int outByteIndex = outY * (DISPLAY_HEIGHT_LOCAL / 8) + (outX / 8);
      int outBitPosition = 7 - (outX % 8);
      if (!isWhite) {  // Invert: e-ink white=1 -> PBM black=1
        rotatedBuffer[outByteIndex] |= (1 << outBitPosition);
      }
    }
  }

  file.write(reinterpret_cast<const char*>(rotatedBuffer.data()), rotatedBuffer.size());
  file.close();
  Serial.printf("Saved framebuffer to %s\n", filename);
#else
  (void)filename;
  Serial.println("saveFrameBufferAsPBM is not supported on Arduino builds.");
#endif
}

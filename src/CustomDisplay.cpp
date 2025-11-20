#include "CustomDisplay.h"

#include "bebop_image.h"

// SSD1677 RAM buffer commands
#define CMD_WRITE_RAM_BW 0x24   // Write to BW RAM (current frame)
#define CMD_WRITE_RAM_RED 0x26  // Write to RED RAM (previous frame for partial refresh)

// Custom LUT for fast partial refresh
const unsigned char lut_custom[] PROGMEM = {
    // VS L0-L3 (voltage patterns per transition)
    // Black → Black: [VSH1→VSS→VSS→VSH1→VSS→VSS→VSH1→VSS]
    0x41, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Black → White: [VSL→VSL→VSS→VSL→VSL→VSS→VSL→VSL]
    0xA2, 0x8A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // White → Black: [VSH2→VSH2→VSS→VSH2→VSH2→VSS→VSH1→VSH1]
    0xF3, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // White → White: [VSL→VSS→VSS→VSL→VSS→VSS→VSL→VSS]
    0x82, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x00, 0x00, 0x00, 0x00, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0
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

CustomDisplay::CustomDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : _sclk(sclk),
      _mosi(mosi),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
      bebopImageVisible(false),
      customLutActive(false) {
  Serial.printf("[%lu] CustomDisplay: Constructor called\n", millis());
  Serial.printf("[%lu]   SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BUSY=%d\n", millis(), sclk, mosi, cs, dc, rst, busy);

  // Allocate frame buffer
  frameBuffer = new uint8_t[BUFFER_SIZE];
  memset(frameBuffer, 0xFF, BUFFER_SIZE);  // Initialize to white
  Serial.printf("[%lu]   Frame buffer allocated (%lu bytes)\n", millis(), BUFFER_SIZE);
}

CustomDisplay::~CustomDisplay() {
  Serial.printf("[%lu] CustomDisplay: Destructor called\n", millis());
  if (frameBuffer) {
    delete[] frameBuffer;
    frameBuffer = nullptr;
  }
}

void CustomDisplay::begin() {
  Serial.printf("[%lu] CustomDisplay: begin() called\n", millis());
  Serial.printf("[%lu]   Initializing custom display driver...\n", millis());

  // Initialize SPI with custom pins
  SPI.begin(_sclk, -1, _mosi, _cs);
  spiSettings = SPISettings(40000000, MSBFIRST, SPI_MODE0);  // 40 MHz (2x spec, max stable)
  Serial.printf("[%lu]   SPI initialized at 40 MHz (actual)\n", millis());

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

  // Display bebop image on startup
  // clearScreen(0xFF);  // Clear buffer first
  drawImage(bebop_image, 0, 0, BEBOP_WIDTH, BEBOP_HEIGHT, true);
  displayBuffer(true);  // Full refresh

  Serial.printf("[%lu]   Custom display driver initialized\n", millis());
}

void CustomDisplay::handleButton(Button button) {
  switch (button) {
    case VOLUME_UP:
      Serial.printf("[%lu] CustomDisplay: VOLUME_UP pressed\n", millis());
      Serial.printf("[%lu]   Clearing screen to BLACK\n", millis());
      clearScreen(0x00);
      displayBuffer(false);  // Partial refresh
      break;

    case VOLUME_DOWN:
      Serial.printf("[%lu] CustomDisplay: VOLUME_DOWN pressed\n", millis());
      Serial.printf("[%lu]   Clearing screen to WHITE\n", millis());
      clearScreen(0xFF);
      displayBuffer(false);  // Partial refresh
      break;

    case CONFIRM:
      Serial.printf("[%lu] CustomDisplay: CONFIRM pressed\n", millis());
      Serial.printf("[%lu]   Displaying bebop image...\n", millis());
      drawImage(bebop_image, 0, 0, BEBOP_WIDTH, BEBOP_HEIGHT, true);
      displayBuffer(false);  // Partial refresh
      Serial.printf("[%lu]   Bebop image displayed\n", millis());
      break;

    case BACK:
      Serial.printf("[%lu] CustomDisplay: BACK pressed\n", millis());
      break;

    case LEFT:
      Serial.printf("[%lu] CustomDisplay: LEFT pressed\n", millis());
      break;

    case RIGHT:
      Serial.printf("[%lu] CustomDisplay: RIGHT pressed\n", millis());
      break;
  }
}

// ============================================================================
// Low-level display control methods
// ============================================================================

void CustomDisplay::resetDisplay() {
  Serial.printf("[%lu]   Resetting display...\n", millis());
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
  Serial.printf("[%lu]   Display reset complete\n", millis());
}

void CustomDisplay::sendCommand(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, LOW);  // Command mode
  digitalWrite(_cs, LOW);  // Select chip
  SPI.transfer(command);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void CustomDisplay::sendData(uint8_t data) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  // Data mode
  digitalWrite(_cs, LOW);   // Select chip
  SPI.transfer(data);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void CustomDisplay::sendData(const uint8_t* data, uint16_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  // Data mode
  digitalWrite(_cs, LOW);   // Select chip
  for (uint16_t i = 0; i < length; i++) {
    SPI.transfer(data[i]);
  }
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void CustomDisplay::waitWhileBusy(const char* comment) {
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

void CustomDisplay::initDisplayController() {
  Serial.printf("[%lu]   Initializing SSD1677 controller...\n", millis());

  // SSD1677 command definitions
  const uint8_t CMD_SOFT_RESET = 0x12;
  const uint8_t CMD_TEMP_SENSOR_CONTROL = 0x18;
  const uint8_t CMD_BOOSTER_SOFT_START = 0x0C;
  const uint8_t CMD_DRIVER_OUTPUT_CONTROL = 0x01;
  const uint8_t CMD_BORDER_WAVEFORM = 0x3C;
  const uint8_t TEMP_SENSOR_INTERNAL = 0x80;

  // Soft reset
  sendCommand(CMD_SOFT_RESET);
  delay(10);

  // Temperature sensor control (internal)
  sendCommand(CMD_TEMP_SENSOR_CONTROL);
  sendData(TEMP_SENSOR_INTERNAL);

  // Booster soft-start control (GDEQ0426T82 specific values)
  sendCommand(CMD_BOOSTER_SOFT_START);
  sendData(0xAE);
  sendData(0xC7);
  sendData(0xC3);
  sendData(0xC0);
  sendData(0x80);

  // Driver output control: set display height (480) and scan direction
  const uint16_t HEIGHT = 480;
  sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
  sendData((HEIGHT - 1) % 256);  // gates A0..A7 (low byte)
  sendData((HEIGHT - 1) / 256);  // gates A8..A9 (high byte)
  sendData(0x02);                // SM=1 (interlaced), TB=0

  // Border waveform control
  sendCommand(CMD_BORDER_WAVEFORM);
  sendData(0x01);

  Serial.printf("[%lu]   SSD1677 controller initialized\n", millis());
}

void CustomDisplay::setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  const uint16_t WIDTH = 800;
  const uint16_t HEIGHT = 480;

  // SSD1677 commands
  const uint8_t CMD_DATA_ENTRY_MODE = 0x11;
  const uint8_t CMD_SET_RAM_X_RANGE = 0x44;
  const uint8_t CMD_SET_RAM_Y_RANGE = 0x45;
  const uint8_t CMD_SET_RAM_X_COUNTER = 0x4E;
  const uint8_t CMD_SET_RAM_Y_COUNTER = 0x4F;
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

void CustomDisplay::clearScreen(uint8_t color) {
  Serial.printf("[%lu]   Clearing frame buffer to 0x%02X...\n", millis(), color);
  memset(frameBuffer, color, BUFFER_SIZE);
}

void CustomDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
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

void CustomDisplay::writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size) {
  const char* bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  unsigned long startTime = millis();
  Serial.printf("[%lu]   Writing frame buffer to %s RAM (%lu bytes)...\n", startTime, bufferName, size);

  sendCommand(ramBuffer);
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  // Data mode
  digitalWrite(_cs, LOW);   // Select chip

  // Use writeBytes for efficient bulk transfer
  SPI.writeBytes(data, size);

  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
  unsigned long duration = millis() - startTime;
  Serial.printf("[%lu]   %s RAM write complete (%lu ms)\n", millis(), bufferName, duration);
}

void CustomDisplay::displayBuffer(bool fullRefresh) {
  // Set up full screen RAM area
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (fullRefresh) {
    // For full refresh, write to both buffers before refresh
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, BUFFER_SIZE);  // Previous frame
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);   // Current frame
  } else {
    // For partial refresh, write to BW buffer only
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);  // Current frame
  }

  // Refresh the display
  refreshDisplay(fullRefresh);

  // After partial refresh, sync the buffers
  if (!fullRefresh) {
    setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, BUFFER_SIZE);  // Copy current to previous for next partial update
  }
}

void CustomDisplay::refreshDisplay(bool fullRefresh) {
  const uint8_t CMD_DISPLAY_UPDATE_CTRL1 = 0x21;
  const uint8_t CMD_DISPLAY_UPDATE_CTRL2 = 0x22;
  const uint8_t CMD_MASTER_ACTIVATION = 0x20;

  // Power off sequence
  const uint8_t MODE_POWER_OFF = 0x83;

  // Display Update Control 1 settings
  const uint8_t CTRL1_NORMAL = 0x00;      // Normal mode - compare RED vs BW for partial
  const uint8_t CTRL1_BYPASS_RED = 0x40;  // Bypass RED RAM (treat as 0) - for full refresh

  sendCommand(0x1A);  // Write temperature register
  sendData(0x5A);     // temperature value for fast mode
  sendData(0x00);     // temperature value for fast mode

  uint8_t ctrl1Mode = fullRefresh ? CTRL1_BYPASS_RED : CTRL1_NORMAL;
  const char* refreshType = fullRefresh ? "full" : "partial";

  // Configure Display Update Control 1
  sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
  sendData(ctrl1Mode);  // Configure buffer comparison mode
  sendData(0x00);       // Single chip application

  // enable counter and analog
  if (!fullRefresh) {
    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(0xC0);
    sendCommand(CMD_MASTER_ACTIVATION);
    waitWhileBusy(" enabling count and analog");
  }

  uint8_t lutFlag = customLutActive ? 0x30 : 0x00;

  // Select appropriate display mode
  // FC is faster
  uint8_t displayMode = (fullRefresh ? 0xD7 : 0x1C) | lutFlag;
  // Power on and refresh display
  Serial.printf("[%lu]   Powering on display 0x%02X (%s refresh)...\n", millis(), displayMode, refreshType);
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(displayMode);
  sendCommand(CMD_MASTER_ACTIVATION);

  // Wait for display to finish updating
  Serial.printf("[%lu]   Waiting for display refresh...\n", millis());
  waitWhileBusy(fullRefresh ? " after full refresh" : " after partial refresh");
}

void CustomDisplay::setCustomLUT(bool enabled) {
  if (enabled) {
    Serial.printf("[%lu]   Loading custom LUT...\n", millis());

    // SSD1677 LUT command definitions
    const uint8_t CMD_WRITE_LUT = 0x32;
    const uint8_t CMD_GATE_VOLTAGE = 0x03;
    const uint8_t CMD_SOURCE_VOLTAGE = 0x04;
    const uint8_t CMD_WRITE_VCOM = 0x2C;

    // Load custom LUT (first 105 bytes: VS + TP/RP + frame rate)
    sendCommand(CMD_WRITE_LUT);
    for (uint16_t i = 0; i < 105; i++) {
      sendData(pgm_read_byte(&lut_custom[i]));
    }

    // Set voltage values from bytes 105-109
    sendCommand(CMD_GATE_VOLTAGE);  // VGH
    sendData(pgm_read_byte(&lut_custom[105]));

    sendCommand(CMD_SOURCE_VOLTAGE);            // VSH1, VSH2, VSL
    sendData(pgm_read_byte(&lut_custom[106]));  // VSH1
    sendData(pgm_read_byte(&lut_custom[107]));  // VSH2
    sendData(pgm_read_byte(&lut_custom[108]));  // VSL

    sendCommand(CMD_WRITE_VCOM);  // VCOM
    sendData(pgm_read_byte(&lut_custom[109]));

    customLutActive = true;
    Serial.printf("[%lu]   Custom LUT loaded\n", millis());
  } else {
    customLutActive = false;
    // Reinitialize display to restore default LUT
    initDisplayController();
    Serial.printf("[%lu]   Custom LUT disabled\n", millis());
  }
}

void CustomDisplay::powerOff() {
  const uint8_t CMD_DISPLAY_UPDATE_CTRL2 = 0x22;
  const uint8_t CMD_MASTER_ACTIVATION = 0x20;
  const uint8_t MODE_POWER_OFF = 0x83;

  Serial.printf("[%lu]   Powering off display...\n", millis());
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(MODE_POWER_OFF);  // Power off sequence
  sendCommand(CMD_MASTER_ACTIVATION);
  waitWhileBusy(" after power off");
}

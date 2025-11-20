#ifndef CUSTOM_DISPLAY_H
#define CUSTOM_DISPLAY_H

#include <Arduino.h>
#include <SPI.h>

class CustomDisplay {
 public:
  enum Button { VOLUME_UP, VOLUME_DOWN, CONFIRM, BACK, LEFT, RIGHT };

  // Constructor with pin configuration
  CustomDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  // Destructor
  ~CustomDisplay();

  // Initialize the display hardware and driver
  void begin();

  // Button handling
  void handleButton(Button button);

 private:
  // Pin configuration
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Display dimensions
  static const uint16_t DISPLAY_WIDTH = 800;
  static const uint16_t DISPLAY_HEIGHT = 480;
  static const uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static const uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer
  uint8_t* frameBuffer;

  // SPI settings
  SPISettings spiSettings;

  // State
  bool bebopImageVisible;
  bool customLutActive;

  // Low-level display control
  void resetDisplay();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendData(const uint8_t* data, uint16_t length);
  void waitWhileBusy(const char* comment = nullptr);
  void initDisplayController();

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF);
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false);
  void displayBuffer(bool fullRefresh = false);

  // Low-level display operations
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size);
  void refreshDisplay(bool fullRefresh = false);
  void powerOff();
  void setCustomLUT(bool enabled);
};

#endif

#ifndef EINK_DISPLAY_H
#define EINK_DISPLAY_H

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#else
#include "../test/platform_stubs.h"
#endif

class EInkDisplay {
 public:
  // Constructor with pin configuration
  EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  // Destructor
  ~EInkDisplay();

  // Refresh modes (guarded to avoid redefinition in test builds)
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Initialize the display hardware and driver
  void begin();

  // Display dimensions
  static const uint16_t DISPLAY_WIDTH = 800;
  static const uint16_t DISPLAY_HEIGHT = 480;
  static const uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static const uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF);
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false);

  void setGrayscaleBuffers(const uint8_t* bwBuffer, const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void grayscaleRevert();

  void displayBuffer(RefreshMode mode = FAST_REFRESH);
  void refreshDisplay(RefreshMode mode = FAST_REFRESH);

  // LUT control
  void setCustomLUT(bool enabled, const unsigned char* lutData = nullptr);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() {
    return frameBuffer;
  }

  // Save the current framebuffer to a PBM file (desktop/test builds only)
  void saveFrameBufferAsPBM(const char* filename);

 private:
  // Pin configuration
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Frame buffer
  uint8_t frameBuffer0[BUFFER_SIZE];
  uint8_t frameBuffer1[BUFFER_SIZE];

  // grayscale buffers
  uint8_t frameBuffer_lsb[BUFFER_SIZE];
  uint8_t frameBuffer_msb[BUFFER_SIZE];

  uint8_t* frameBuffer = frameBuffer0;
  uint8_t* frameBufferActive = frameBuffer1;

  // SPI settings
  SPISettings spiSettings;

  // State
  bool isScreenOn;
  bool customLutActive;
  bool inGrayscaleMode;
  bool drawGrayscale;

  // Low-level display control
  void resetDisplay();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendData(const uint8_t* data, uint16_t length);
  void waitWhileBusy(const char* comment = nullptr);
  void initDisplayController();

  // Low-level display operations
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size);
};

#endif

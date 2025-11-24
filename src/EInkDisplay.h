#ifndef EINK_DISPLAY_H
#define EINK_DISPLAY_H

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#else
#include <cstdint>

// Compatibility shims for non-Arduino desktop builds (test harness)
#include <chrono>
#include <cstring>
#include <thread>

// PROGMEM / pgm_read helpers
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#endif

// Minimal SPISettings stub (used by the driver code)
#ifndef SPISettings
struct SPISettings {
  SPISettings() {}
  SPISettings(uint32_t, int, int) {}
};
#endif

// Minimal SPI mock
struct MockSPI {
  void begin(int sclk = -1, int miso = -1, int mosi = -1, int ssel = -1) {
    (void)sclk;
    (void)miso;
    (void)mosi;
    (void)ssel;
  }
  void beginTransaction(const SPISettings&) {}
  void endTransaction() {}
  void transfer(uint8_t) {}
};

static MockSPI SPI;

// SPI mode / bit order constants
#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif

// Arduino GPIO and timing stubs
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) {
  return 0;
}
inline void delay(unsigned long) {}

// Arduino constants
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif

#ifdef TEST_BUILD
#include "Arduino.h"
#else
// Provide minimal declarations so code can reference Serial/millis when
// building outside the Arduino framework but without the TEST_BUILD header.
extern struct MockSerial {
  void printf(const char*, ...);
  void println(const char*);
  void print(const char*);
} Serial;

unsigned long millis();
#endif
#endif

// Refresh modes (guarded to avoid redefinition in test builds)
#ifndef REFRESH_MODE_DEFINED
enum RefreshMode {
  FULL_REFRESH,  // Full refresh with complete waveform
  HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
  FAST_REFRESH   // Fast refresh using custom LUT
};
#define REFRESH_MODE_DEFINED
#endif

class EInkDisplay {
 public:
  // Constructor with pin configuration
  EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  // Destructor
  ~EInkDisplay();

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
  void displayBuffer(RefreshMode mode = FAST_REFRESH);

  // LUT control
  void setCustomLUT(bool enabled);

  // Power management
  void powerOn();
  void powerOff();

  // Access to frame buffer
  uint8_t* getFrameBuffer() {
    return frameBuffer;
  }

  // Save the current framebuffer to a PBM file (desktop/test builds only)
  void saveFrameBufferAsPBM(const char* filename);

  // Debug
  void debugPrintFramebuffer();

 private:
  // Pin configuration
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Frame buffer
  uint8_t* frameBuffer;

  // SPI settings
  SPISettings spiSettings;

  // State
  bool customLutActive;

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
  void refreshDisplay(RefreshMode mode = FAST_REFRESH);
};

#endif

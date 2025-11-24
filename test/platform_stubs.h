#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

// PROGMEM / pgm_read helpers for host builds
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#endif

// Minimal SPISettings stub (used by the driver code)
struct SPISettings {
  SPISettings() {}
  SPISettings(uint32_t, int, int) {}
};

// Minimal SPI mock
struct MockSPI {
  void begin(int sclk = -1, int miso = -1, int mosi = -1, int ssel = -1) { (void)sclk; (void)miso; (void)mosi; (void)ssel; }
  void beginTransaction(const SPISettings&){ }
  void endTransaction(){ }
  void transfer(uint8_t) { }
};

extern MockSPI SPI;

// SPI mode / bit order constants
#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif

// Arduino GPIO and timing stubs (inline so header-only callers work)
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

// Minimal Serial mock declaration
struct MockSerial {
  void printf(const char*, ...);
  void println(const char*);
  void print(const char*);
};

extern MockSerial Serial;

// Host millis() declaration (defined in platform_stubs.cpp)
unsigned long millis();

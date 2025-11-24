// Mock Arduino.h for Windows testing
#ifndef Arduino_h
#define Arduino_h

#include <cstdarg>
#include <iostream>

// This file provides Arduino compatibility for Windows compilation

// Define PROGMEM for non-Arduino builds
#ifndef PROGMEM
#define PROGMEM
#endif

// Define pgm_read_byte for non-Arduino builds
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#endif

// Provide the Arduino-like String class for test builds
#include "WString.h"

// Mock Serial for debug output

struct MockSerial {
  void begin(unsigned long baudrate) {
    (void)baudrate;  // Do nothing
  }
  size_t write(unsigned int c) {
    std::cout << (char)c;
    return 1;
  }
  void flush() {
    std::cout.flush();
  }
  void end() {
    // Do nothing
  }
  void print(const char* str) {
    std::cout << str;
  }
  void print(int val) {
    std::cout << val;
  }
  void print(unsigned long val) {
    std::cout << val;
  }
  void print(const String& str) {
    std::cout << str.c_str();
  }
  void println(const char* str) {
    std::cout << str << std::endl;
  }
  void println(const String& str) {
    std::cout << str.c_str() << std::endl;
  }
  void println(int val) {
    std::cout << val << std::endl;
  }
  void println(unsigned long val) {
    std::cout << val << std::endl;
  }

  void printf(const char* format, ...) {
#ifdef DEBUG_LAYOUT
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
#else
    (void)format;
#endif
  }
};

extern MockSerial Serial;

// Provide declaration for millis() so non-Arduino translation units
// (compiled in TEST_BUILD) can call it without needing Arduino framework.
unsigned long millis();

#endif  // Arduino_h

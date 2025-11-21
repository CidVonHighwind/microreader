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

// Mock Serial for debug output
struct MockSerial {
  void print(const char* str) {
    std::cout << str;
  }
  void print(int val) {
    std::cout << val;
  }
  void print(unsigned long val) {
    std::cout << val;
  }
  void println(const char* str) {
    std::cout << str << std::endl;
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

#endif  // Arduino_h

#include "platform_stubs.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

// Definitions for the declarations in platform_stubs.h

MockSerial Serial;

// Implement MockSerial methods
void MockSerial::printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void MockSerial::println(const char* s) {
  if (s) printf("%s\n", s);
  else printf("\n");
}

void MockSerial::print(const char* s) {
  if (s) printf("%s", s);
}

// Provide a concrete SPI object for host tests
MockSPI SPI;

// Provide millis() implementation for host tests
unsigned long millis() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

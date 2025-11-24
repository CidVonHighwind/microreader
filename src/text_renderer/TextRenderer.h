#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#ifdef TEST_BUILD
#include "Arduino.h"
#else
#include "WString.h"
#endif

#include <cstddef>
#include <cstdint>

#include "SimpleFont.h"

class EInkDisplay;  // Forward declaration

class TextRenderer {
 public:
  // Constructor
  TextRenderer(EInkDisplay& display);

  // Low-level pixel draw used by font blitting
  void drawPixel(int16_t x, int16_t y, uint16_t color);

  // Minimal API used by the rest of the project
  void setFont(const SimpleGFXfont* f = nullptr);
  void setTextColor(uint16_t c);
  void setCursor(int16_t x, int16_t y);
  size_t print(const char* s);
  size_t print(const String& s);

  // Measure text bounds for layout
  void getTextBounds(const char* str, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h);

  // Color constants (0 = black, 1 = white for 1-bit display)
  static const uint16_t COLOR_BLACK = 0;
  static const uint16_t COLOR_WHITE = 1;

 private:
  EInkDisplay& display;
  const SimpleGFXfont* currentFont = nullptr;
  int16_t cursorX = 0;
  int16_t cursorY = 0;
  uint16_t textColor = COLOR_BLACK;

  void drawChar(char c);
};

#endif

// Mock Adafruit_GFX.h for Windows testing
#ifndef _ADAFRUIT_GFX_H
#define _ADAFRUIT_GFX_H

#ifndef ARDUINO

#include <cstdint>

#include "gfxfont.h"

// Mock Adafruit_GFX class for Windows testing
class Adafruit_GFX {
 protected:
  int16_t WIDTH, HEIGHT;
  int16_t cursor_x, cursor_y;
  uint8_t textsize_x, textsize_y;
  uint16_t textcolor, textbgcolor;
  GFXfont* gfxFont;

 public:
  Adafruit_GFX(int16_t w, int16_t h)
      : WIDTH(w),
        HEIGHT(h),
        cursor_x(0),
        cursor_y(0),
        textsize_x(1),
        textsize_y(1),
        textcolor(0xFFFF),
        textbgcolor(0xFFFF),
        gfxFont(nullptr) {}

  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

  void setCursor(int16_t x, int16_t y) {
    cursor_x = x;
    cursor_y = y;
  }

  void setTextSize(uint8_t s) {
    textsize_x = textsize_y = s;
  }

  void setTextSize(uint8_t sx, uint8_t sy) {
    textsize_x = sx;
    textsize_y = sy;
  }

  void setFont(const GFXfont* f = nullptr) {
    gfxFont = (GFXfont*)f;
  }

  void setTextColor(uint16_t c) {
    textcolor = textbgcolor = c;
  }

  void setTextColor(uint16_t c, uint16_t bg) {
    textcolor = c;
    textbgcolor = bg;
  }

  void print(const char* str);
  void print(const class String& str);  // Forward declaration

  void getTextBounds(const char* str, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h);
};

#endif  // ARDUINO

#endif  // _ADAFRUIT_GFX_H

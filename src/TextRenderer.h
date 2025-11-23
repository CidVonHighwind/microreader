#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#ifdef ARDUINO
#include <Adafruit_GFX.h>
#else
// For non-Arduino builds, include the mock
#include <Adafruit_GFX.h>
#endif

#include "EInkDisplay.h"

class TextRenderer : public Adafruit_GFX {
 public:
  // Constructor
  TextRenderer(EInkDisplay& display);

  // Required Adafruit_GFX overrides
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;

  // Helper methods
  void clearText();
  void refresh(EInkDisplay::RefreshMode mode = EInkDisplay::FAST_REFRESH);

  // Color constants (0 = black, 1 = white for 1-bit display)
  static const uint16_t COLOR_BLACK = 0;
  static const uint16_t COLOR_WHITE = 1;

 private:
  EInkDisplay& display;
};

#endif

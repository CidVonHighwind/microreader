#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#ifdef ARDUINO
#include <Adafruit_GFX.h>
#else
// For non-Arduino builds, include the mock
#include "Adafruit_GFX.h"
#endif

#ifdef TEST_BUILD
#include "Arduino.h"
#endif

class EInkDisplay;  // Forward declaration

// Define RefreshMode for all builds
#ifndef REFRESH_MODE_DEFINED
enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
#define REFRESH_MODE_DEFINED
#endif

#ifndef TEST_BUILD
#include "EInkDisplay.h"
#endif

class TextRenderer : public Adafruit_GFX {
 public:
  // Constructor
  TextRenderer(EInkDisplay& display);

  // Required Adafruit_GFX overrides
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;

  // Helper methods
  void clearText();
  void refresh(RefreshMode mode = FAST_REFRESH);

  // Color constants (0 = black, 1 = white for 1-bit display)
  static const uint16_t COLOR_BLACK = 0;
  static const uint16_t COLOR_WHITE = 1;

 private:
  EInkDisplay& display;
};

#endif

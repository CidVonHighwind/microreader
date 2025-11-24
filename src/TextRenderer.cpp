#include "TextRenderer.h"

#include "EInkDisplay.h"

TextRenderer::TextRenderer(EInkDisplay& display)
    : Adafruit_GFX(EInkDisplay::DISPLAY_HEIGHT, EInkDisplay::DISPLAY_WIDTH), display(display) {
  Serial.printf("[%lu] TextRenderer: Constructor called (portrait 480x800)\n", millis());
}

void TextRenderer::drawPixel(int16_t x, int16_t y, uint16_t color) {
  // Bounds checking (portrait: 480x800)
  if (x < 0 || x >= EInkDisplay::DISPLAY_HEIGHT || y < 0 || y >= EInkDisplay::DISPLAY_WIDTH) {
    return;
  }

  // Rotate coordinates: portrait (480x800) -> landscape (800x480)
  // Rotation: 90 degrees clockwise
  int16_t rotatedX = y;
  int16_t rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;

  // Get frame buffer
  uint8_t* frameBuffer = display.getFrameBuffer();

  // Calculate byte position and bit position
  uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  // Set or clear the bit (0 = black, 1 = white in e-ink)
  if (color == COLOR_BLACK) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit (black)
  } else {
    frameBuffer[byteIndex] |= (1 << bitPosition);  // Set bit (white)
  }
}

void TextRenderer::clearText() {
  display.clearScreen(0xFF);
}

void TextRenderer::refresh(RefreshMode mode) {
  display.displayBuffer(mode);
}

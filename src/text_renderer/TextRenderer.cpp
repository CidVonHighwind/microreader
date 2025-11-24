#include "TextRenderer.h"

#include <cstring>

#include "EInkDisplay.h"
#include "SimpleFont.h"

TextRenderer::TextRenderer(EInkDisplay& display) : display(display) {
  Serial.printf("[%lu] TextRenderer: Constructor called\n", millis());
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

void TextRenderer::setFont(const SimpleGFXfont* f) {
  currentFont = f;
}

void TextRenderer::setTextColor(uint16_t c) {
  textColor = c;
}

void TextRenderer::setCursor(int16_t x, int16_t y) {
  cursorX = x;
  cursorY = y;
}

size_t TextRenderer::print(const char* s) {
  if (!s)
    return 0;
  size_t count = 0;
  for (const char* p = s; *p; ++p) {
    char c = *p;
    drawChar(c);
    count++;
  }
  return count;
}

size_t TextRenderer::print(const String& s) {
  return print(s.c_str());
}

void TextRenderer::getTextBounds(const char* str, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w,
                                 uint16_t* h) {
  if (!str) {
    if (x1)
      *x1 = x;
    if (y1)
      *y1 = y;
    if (w)
      *w = 0;
    if (h)
      *h = 0;
    return;
  }

  int16_t minx = x;
  int16_t miny = y;
  uint16_t width = 0;
  uint16_t height = 0;

  if (currentFont) {
    // Measure using SimpleGFXfont xAdvance sums
    const SimpleGFXfont* f = currentFont;
    uint16_t tot = 0;
    for (const char* p = str; *p; ++p) {
      unsigned char c = (unsigned char)*p;
      // Find glyph by scanning per-glyph codepoint field
      int idx = -1;
      for (uint16_t i = 0; i < f->glyphCount; ++i) {
        if (f->glyph[i].codepoint == (uint16_t)c) {
          idx = (int)i;
          break;
        }
      }
      if (idx >= 0) {
        const SimpleGFXglyph* glyph = &f->glyph[idx];
        tot += glyph->xAdvance;
      } else {
        tot += 6;  // fallback
      }
    }
    width = tot;
    height = (f->yAdvance > 0) ? f->yAdvance : 10;
  } else {
    // Simple fixed width font (fallback)
    size_t len = strlen(str);
    width = (uint16_t)(len * 6);  // 6px per char approx
    height = 10;
  }

  if (x1)
    *x1 = minx;
  if (y1)
    *y1 = miny;
  if (w)
    *w = width;
  if (h)
    *h = height;
}

void TextRenderer::drawChar(char c) {
  if (!currentFont)
    return;
  const SimpleGFXfont* f = currentFont;
  unsigned int code = (unsigned char)c;
  int glyphIndex = -1;
  for (uint16_t i = 0; i < f->glyphCount; ++i) {
    if (f->glyph[i].codepoint == (uint16_t)code) {
      glyphIndex = (int)i;
      break;
    }
  }
  if (glyphIndex < 0) {
    cursorX += 6;  // unsupported
    return;
  }

  const SimpleGFXglyph* glyph = &f->glyph[glyphIndex];
  const uint8_t* bitmap = f->bitmap;
  uint16_t bo = glyph->bitmapOffset;
  uint8_t w = glyph->width;
  uint8_t h = glyph->height;
  int8_t xo = glyph->xOffset;
  int8_t yo = glyph->yOffset;

  // For each pixel in glyph
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      uint32_t bit = 1 << (7 - (xx & 7));
      uint8_t data = bitmap[bo + (yy * ((w + 7) / 8)) + (xx / 8)];
      // Extract bit value
      bool on = (data & (1 << (7 - (xx % 8)))) != 0;
      if (on) {
        int16_t px = cursorX + xo + xx;
        int16_t py = cursorY + yo + yy;
        drawPixel(px, py, textColor);
      }
    }
  }

  // Advance cursor by xAdvance
  cursorX += glyph->xAdvance + 2;
}

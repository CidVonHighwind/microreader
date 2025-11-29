#include "TextRenderer.h"

#include <cstring>

#include "EInkDisplay.h"
#include "SimpleFont.h"

static constexpr int GLYPH_PADDING = 0;

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

void TextRenderer::drawPixelGray(int16_t x, int16_t y, bool bw, bool lsb, bool msb) {
  // Bounds checking (portrait: 480x800)
  if (x < 0 || x >= EInkDisplay::DISPLAY_HEIGHT || y < 0 || y >= EInkDisplay::DISPLAY_WIDTH) {
    return;
  }

  // Rotate coordinates: portrait (480x800) -> landscape (800x480)
  // Rotation: 90 degrees clockwise
  int16_t rotatedX = y;
  int16_t rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;

  // Get buffers
  uint8_t* bwBuffer = display.getFrameBuffer();
  uint8_t* lsbBuffer = display.getFrameBufferLSB();
  uint8_t* msbBuffer = display.getFrameBufferMSB();

  // Calculate byte position and bit position
  uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  // Set BW buffer
  if (bw) {
    bwBuffer[byteIndex] &= ~(1 << bitPosition);
  } else {
    bwBuffer[byteIndex] |= (1 << bitPosition);
  }

  // Set LSB buffer
  if (lsb) {
    lsbBuffer[byteIndex] &= ~(1 << bitPosition);
  } else {
    lsbBuffer[byteIndex] |= (1 << bitPosition);
  }

  // Set MSB buffer
  if (msb) {
    msbBuffer[byteIndex] &= ~(1 << bitPosition);
  } else {
    msbBuffer[byteIndex] |= (1 << bitPosition);
  }
}

void TextRenderer::setFont(const SimpleGFXfont* f) {
  currentFont = f;
}

void TextRenderer::setTextColor(uint16_t c) {
  textColor = c;
}

void TextRenderer::setGrayscaleMode(bool enable) {
  grayscaleMode = enable;
}

void TextRenderer::setCursor(int16_t x, int16_t y) {
  cursorX = x;
  cursorY = y;
}

size_t TextRenderer::print(const char* s) {
  if (!s)
    return 0;
  size_t written = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  while (*p) {
    uint32_t cp = 0;
    unsigned char c = *p;

    if (c < 0x80) {
      // 1-byte ASCII
      cp = c;
      p += 1;
    } else if ((c & 0xE0) == 0xC0) {
      // 2-byte sequence
      unsigned char c1 = p[1];
      if ((c1 & 0xC0) == 0x80) {
        cp = ((c & 0x1F) << 6) | (c1 & 0x3F);
        p += 2;
      } else {
        // malformed sequence -> replacement char
        cp = 0xFFFD;
        p += 1;
      }
    } else if ((c & 0xF0) == 0xE0) {
      // 3-byte sequence
      unsigned char c1 = p[1];
      unsigned char c2 = p[2];
      if (((c1 & 0xC0) == 0x80) && ((c2 & 0xC0) == 0x80)) {
        cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        p += 3;
      } else {
        cp = 0xFFFD;
        p += 1;
      }
    } else if ((c & 0xF8) == 0xF0) {
      // 4-byte sequence
      unsigned char c1 = p[1];
      unsigned char c2 = p[2];
      unsigned char c3 = p[3];
      if (((c1 & 0xC0) == 0x80) && ((c2 & 0xC0) == 0x80) && ((c3 & 0xC0) == 0x80)) {
        cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        p += 4;
      } else {
        cp = 0xFFFD;
        p += 1;
      }
    } else {
      // invalid leading byte
      cp = 0xFFFD;
      p += 1;
    }

    drawChar(cp);
    ++written;
  }

  return written;
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
    // Measure using SimpleGFXfont xAdvance sums; decode UTF-8 like print()
    const SimpleGFXfont* f = currentFont;
    uint16_t tot = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(str);
    while (*p) {
      uint32_t cp = 0;
      unsigned char c = *p;
      if (c < 0x80) {
        cp = c;
        p += 1;
      } else if ((c & 0xE0) == 0xC0) {
        unsigned char c1 = p[1];
        if ((c1 & 0xC0) == 0x80) {
          cp = ((c & 0x1F) << 6) | (c1 & 0x3F);
          p += 2;
        } else {
          cp = 0xFFFD;
          p += 1;
        }
      } else if ((c & 0xF0) == 0xE0) {
        unsigned char c1 = p[1];
        unsigned char c2 = p[2];
        if (((c1 & 0xC0) == 0x80) && ((c2 & 0xC0) == 0x80)) {
          cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
          p += 3;
        } else {
          cp = 0xFFFD;
          p += 1;
        }
      } else if ((c & 0xF8) == 0xF0) {
        unsigned char c1 = p[1];
        unsigned char c2 = p[2];
        unsigned char c3 = p[3];
        if (((c1 & 0xC0) == 0x80) && ((c2 & 0xC0) == 0x80) && ((c3 & 0xC0) == 0x80)) {
          cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
          p += 4;
        } else {
          cp = 0xFFFD;
          p += 1;
        }
      } else {
        cp = 0xFFFD;
        p += 1;
      }

      // Find glyph by scanning per-glyph codepoint field
      int idx = -1;
      for (uint16_t i = 0; i < f->glyphCount; ++i) {
        if (f->glyph[i].codepoint == cp) {
          idx = (int)i;
          break;
        }
      }
      if (idx >= 0) {
        const SimpleGFXglyph* glyph = &f->glyph[idx];
        tot += glyph->xAdvance + GLYPH_PADDING;
      } else {
        tot += 6;  // fallback
      }
    }
    width = tot;
    height = (f->yAdvance > 0) ? f->yAdvance : 10;
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

void TextRenderer::drawChar(uint32_t codepoint) {
  if (!currentFont)
    return;
  const SimpleGFXfont* f = currentFont;
  int glyphIndex = -1;
  for (uint16_t i = 0; i < f->glyphCount; ++i) {
    if (f->glyph[i].codepoint == codepoint) {
      glyphIndex = (int)i;
      break;
    }
  }
  if (glyphIndex < 0) {
    // unsupported codepoint; advance by a small fallback amount
    cursorX += 6;
    return;
  }

  const SimpleGFXglyph* glyph = &f->glyph[glyphIndex];
  const uint8_t* bitmap = f->bitmap;
  const uint8_t* bitmap_gray_lsb = f->bitmap_gray_lsb;
  const uint8_t* bitmap_gray_msb = f->bitmap_gray_msb;
  uint16_t bo = glyph->bitmapOffset;
  uint8_t w = glyph->width;
  uint8_t h = glyph->height;
  int8_t xo = glyph->xOffset;
  int8_t yo = glyph->yOffset;

  // For each pixel in glyph
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      int16_t px = cursorX + xo + xx;
      int16_t py = cursorY + yo + yy;
      if (grayscaleMode && bitmap_gray_lsb && bitmap_gray_msb) {
        // Grayscale mode
        uint8_t bw_data = bitmap[bo + (yy * ((w + 7) / 8)) + (xx / 8)];
        bool bw_on = (bw_data & (1 << (7 - (xx % 8)))) != 0;

        uint8_t lsb_data = bitmap_gray_lsb[bo + (yy * ((w + 7) / 8)) + (xx / 8)];
        bool lsb_bit = (lsb_data & (1 << (7 - (xx % 8)))) != 0;

        uint8_t msb_data = bitmap_gray_msb[bo + (yy * ((w + 7) / 8)) + (xx / 8)];
        bool msb_bit = (msb_data & (1 << (7 - (xx % 8)))) != 0;

        drawPixelGray(px, py, !bw_on, !lsb_bit, !msb_bit);
      } else {
        // BW mode
        uint8_t data = bitmap[bo + (yy * ((w + 7) / 8)) + (xx / 8)];
        bool on = (data & (1 << (7 - (xx % 8)))) != 0;
        if (on) {
          drawPixel(px, py, textColor);
        }
      }
    }
  }

  if (grayscaleMode && bitmap_gray_lsb && bitmap_gray_msb) {
    // In grayscale mode, ensure we inform the display to use grayscale drawing
    display.enableGrayscaleDrawing(true);
  }

  // Advance cursor by xAdvance
  cursorX += glyph->xAdvance + GLYPH_PADDING;
}

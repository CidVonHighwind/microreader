#include "TextRenderer.h"

#include <cstring>

#include "../core/EInkDisplay.h"
#include "SimpleFont.h"

static constexpr int GLYPH_PADDING = 0;
static constexpr uint32_t UTF8_REPLACEMENT_CHAR = 0xFFFD;
static constexpr uint16_t FALLBACK_GLYPH_WIDTH = 6;

// Helper function to decode a single UTF-8 codepoint from a byte sequence
// Returns the decoded codepoint and advances the pointer
static uint32_t decodeUtf8Codepoint(const unsigned char*& p) {
  if (!p || !*p) {
    return 0;
  }

  unsigned char c = *p;

  // 1-byte ASCII
  if (c < 0x80) {
    p += 1;
    return c;
  }

  // 2-byte sequence
  if ((c & 0xE0) == 0xC0) {
    if (p[1] && (p[1] & 0xC0) == 0x80) {
      uint32_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
      return cp;
    }
    p += 1;
    return UTF8_REPLACEMENT_CHAR;
  }

  // 3-byte sequence
  if ((c & 0xF0) == 0xE0) {
    if (p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
      return cp;
    }
    p += 1;
    return UTF8_REPLACEMENT_CHAR;
  }

  // 4-byte sequence
  if ((c & 0xF8) == 0xF0) {
    if (p[1] && p[2] && p[3] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
      uint32_t cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
      p += 4;
      return cp;
    }
    p += 1;
    return UTF8_REPLACEMENT_CHAR;
  }

  // Invalid leading byte
  p += 1;
  return UTF8_REPLACEMENT_CHAR;
}

TextRenderer::TextRenderer(EInkDisplay& display) : display(display) {
  Serial.printf("[%lu] TextRenderer: Constructor called\n", millis());
}

void TextRenderer::drawPixel(int16_t x, int16_t y, bool state) {
  // Early return if no framebuffer is set
  if (!frameBuffer) {
    return;
  }

  // Bounds checking (portrait: 480x800)
  if (x < 0 || x >= EInkDisplay::DISPLAY_HEIGHT || y < 0 || y >= EInkDisplay::DISPLAY_WIDTH) {
    return;
  }

  // Rotate coordinates: portrait (480x800) -> landscape (800x480)
  // Rotation: 90 degrees clockwise
  int16_t rotatedX = y;
  int16_t rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;

  // Calculate byte position and bit position
  uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  // Set or clear the bit
  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    frameBuffer[byteIndex] |= (1 << bitPosition);  // Set bit
  }
}

void TextRenderer::setFrameBuffer(uint8_t* buffer) {
  frameBuffer = buffer;
}

void TextRenderer::setBitmapType(BitmapType type) {
  bitmapType = type;
}

void TextRenderer::setFont(const SimpleGFXfont* f) {
  currentFont = f;
  // Reset family and style when setting a single font directly
  currentFamily = nullptr;
  currentStyle = FontStyle::REGULAR;
}

void TextRenderer::setFontFamily(FontFamily* family) {
  currentFamily = family;
  // Automatically set to the current style's variant
  currentFont = getFontVariant(family, currentStyle);
}

void TextRenderer::setFontStyle(FontStyle style) {
  currentStyle = style;
  if (currentFamily) {
    currentFont = getFontVariant(currentFamily, style);
  }
}

void TextRenderer::setTextColor(uint16_t c) {
  textColor = c;
}

void TextRenderer::setCursor(int16_t x, int16_t y) {
  cursorX = x;
  cursorY = y;
}

size_t TextRenderer::print(const char* s) {
  if (!s) {
    return 0;
  }

  size_t written = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);

  while (*p) {
    uint32_t codepoint = decodeUtf8Codepoint(p);
    drawChar(codepoint);
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
    const SimpleGFXfont* f = currentFont;
    uint16_t totalWidth = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(str);

    while (*p) {
      uint32_t codepoint = decodeUtf8Codepoint(p);
      int glyphIndex = findGlyphIndex(f, codepoint);

      if (glyphIndex >= 0) {
        const SimpleGFXglyph* glyph = &f->glyph[glyphIndex];
        totalWidth += glyph->xAdvance + GLYPH_PADDING;
      } else {
        totalWidth += FALLBACK_GLYPH_WIDTH;
      }
    }

    width = totalWidth;
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
  if (!currentFont) {
    return;
  }

  const SimpleGFXfont* f = currentFont;

  // For hidden text, advance cursor without drawing
  if (currentStyle == FontStyle::HIDDEN) {
    int glyphIndex = findGlyphIndex(f, codepoint);
    if (glyphIndex >= 0) {
      const SimpleGFXglyph* glyph = &f->glyph[glyphIndex];
      cursorX += glyph->xAdvance;
    } else {
      cursorX += FALLBACK_GLYPH_WIDTH;
    }
    return;
  }

  int glyphIndex = findGlyphIndex(f, codepoint);

  if (glyphIndex < 0) {
    // Unsupported codepoint; advance by fallback amount
    cursorX += FALLBACK_GLYPH_WIDTH;
    return;
  }

  const SimpleGFXglyph* glyph = &f->glyph[glyphIndex];

  // Select the appropriate bitmap based on bitmapType
  const uint8_t* bitmap = nullptr;
  switch (bitmapType) {
    case BITMAP_BW:
      bitmap = f->bitmap;
      break;
    case BITMAP_GRAY_LSB:
      bitmap = f->bitmap_gray_lsb;
      break;
    case BITMAP_GRAY_MSB:
      bitmap = f->bitmap_gray_msb;
      break;
  }

  // If the selected bitmap doesn't exist, skip rendering
  if (!bitmap) {
    cursorX += glyph->xAdvance + GLYPH_PADDING;
    return;
  }

  uint16_t bitmapOffset = glyph->bitmapOffset;
  uint8_t w = glyph->width;
  uint8_t h = glyph->height;
  int8_t xOffset = glyph->xOffset;
  int8_t yOffset = glyph->yOffset;

  // Calculate row stride in bytes (width rounded up to byte boundary)
  uint8_t rowStride = (w + 7) / 8;

  // Cache the grayscale bitmap pointers outside the loop for better performance
  const uint8_t* bitmap_lsb = f->bitmap_gray_lsb;
  const uint8_t* bitmap_msb = f->bitmap_gray_msb;
  bool isGrayscale = (bitmapType != BITMAP_BW);

  // Determine pixel state based on text color
  // COLOR_BLACK (0) = draw black pixels (state=true)
  // COLOR_WHITE (1) = draw white pixels (state=false)
  bool pixelState = (textColor == COLOR_BLACK);

  // Render each pixel in the glyph
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      int16_t px = cursorX + xOffset + xx;
      int16_t py = cursorY + yOffset + yy;

      // Calculate bitmap byte and bit positions for current pixel
      uint16_t byteIndex = bitmapOffset + (yy * rowStride) + (xx / 8);
      uint8_t bitMask = 1 << (7 - (xx % 8));

      if (isGrayscale) {
        // skip writing over black/white pixels
        if ((bitmap_lsb[byteIndex] & bitMask) == 0 || (bitmap_msb[byteIndex] & bitMask) == 0) {
          drawPixel(px, py, (bitmap[byteIndex] & bitMask) == 0);
        }
      } else {
        // Check if pixel is set (0 = pixel on in our bitmap format)
        if ((bitmap[byteIndex] & bitMask) == 0) {
          drawPixel(px, py, pixelState);
        }
      }
    }
  }

  // Advance cursor by xAdvance
  cursorX += glyph->xAdvance + GLYPH_PADDING;
}

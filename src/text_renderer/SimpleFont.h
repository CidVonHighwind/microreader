#pragma once

#include <cstdint>

// Minimal font struct used by our TextRenderer
typedef struct {
  uint16_t bitmapOffset;  ///< Pointer into font->bitmap
  uint32_t codepoint;     ///< Unicode codepoint for this glyph
  uint8_t width;          ///< Bitmap dimensions in pixels
  uint8_t height;
  uint8_t xAdvance;  ///< Distance to advance cursor (x axis)
  int8_t xOffset;    ///< X dist from cursor pos to UL corner
  int8_t yOffset;    ///< Y dist from cursor pos to UL corner
} SimpleGFXglyph;

typedef struct {
  const uint8_t* bitmap;        ///< Glyph bitmaps, concatenated
  const SimpleGFXglyph* glyph;  ///< Glyph array
  uint16_t glyphCount;          ///< Number of entries in `glyph`.
  uint8_t yAdvance;             ///< Newline distance (y axis)
} SimpleGFXfont;

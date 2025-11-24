#pragma once
#include "../text_renderer/SimpleFont.h"

// Minimal dummy font containing only the space character (0x20).
// Glyph: 6x8 pixels, bitmap filled (all 1s) so it will draw a filled rectangle
// when `textColor` is set to `COLOR_WHITE` (or whatever color the caller chooses).
// This file acts as a placeholder for the project's font implementation.

// For width=6, bytes per row = 1. Height = 8 -> 8 bytes of 0xFF (filled row).
const uint8_t FreeSans12pt7bBitmaps[] PROGMEM = {0xAA, 0xAA, 0xAA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Single glyph: space (0x20)
const SimpleGFXglyph FreeSans12pt7bGlyphs[] PROGMEM = {
    {0,  // bitmapOffset
     6,  // width
     8,  // height
     6,  // xAdvance
     0,  // xOffset
     0}  // yOffset
};

const SimpleGFXfont FreeSans12pt7b PROGMEM = {
    (const uint8_t*)FreeSans12pt7bBitmaps, (const SimpleGFXglyph*)FreeSans12pt7bGlyphs,
    0x20,  // first
    0x20,  // last
    8      // yAdvance
};

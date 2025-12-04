#include <Arduino.h>

#include "Font14.h"
#include "Font27.h"
#include "NotoSans26.h"

// Font definitions
SimpleGFXfont Font14 = {Font14Bitmaps, nullptr, nullptr, Font14Glyphs, 305, 16, nullptr};
SimpleGFXfont Font27 = {Font27Bitmaps, nullptr, nullptr, Font27Glyphs, 305, 29, nullptr};
SimpleGFXfont NotoSans26 = {
    NotoSans26Bitmaps, NotoSans26Bitmaps_lsb, NotoSans26Bitmaps_msb, NotoSans26Glyphs, 308, 26, nullptr};

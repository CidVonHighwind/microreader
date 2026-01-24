#include "FontManager.h"

#include "FontDefinitions.h"
#include "core/Settings.h"
#include "other/MenuFontBig.h"
#include "other/MenuFontSmall.h"
#include "other/MenuHeader.h"

// Font family (default to Bookerly26)
static FontFamily* currentFamily = &bookerly26Family;

FontFamily* getCurrentFontFamily() {
  return currentFamily;
}

void setCurrentFontFamily(FontFamily* family) {
  if (family)
    currentFamily = family;
}

// Simple fonts
static const SimpleGFXfont* titleFont = &MenuHeader;

const SimpleGFXfont* getUIFont(Settings& settings) {
  int uiFontSize = 0;
  settings.getInt("settings.uiFontSize", uiFontSize);
  return (uiFontSize == 0) ? &MenuFontSmall : &MenuFontBig;
}

const SimpleGFXfont* getTitleFont() {
  return titleFont;
}

void setTitleFont(const SimpleGFXfont* font) {
  if (font)
    titleFont = font;
}

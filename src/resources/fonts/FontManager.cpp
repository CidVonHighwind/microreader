#include "FontManager.h"
#include "FontDefinitions.h"

// Font family
static FontFamily* currentFamily = &bookerlyFamily;

FontFamily* getCurrentFontFamily() {
  return currentFamily;
}

void setCurrentFontFamily(FontFamily* family) {
  if (family)
    currentFamily = family;
}

// Simple fonts
static const SimpleGFXfont* mainFont  = &Font14;
static const SimpleGFXfont* titleFont = &Font27;

const SimpleGFXfont* getMainFont() {
  return mainFont;
}

void setMainFont(const SimpleGFXfont* font) {
  if (font)
    mainFont = font;
}

const SimpleGFXfont* getTitleFont() {
  return titleFont;
}

void setTitleFont(const SimpleGFXfont* font) {
  if (font)
    titleFont = font;
}

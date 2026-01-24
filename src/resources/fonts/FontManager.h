#pragma once

#include "rendering/SimpleFont.h"

// Font family
FontFamily* getCurrentFontFamily();
void setCurrentFontFamily(FontFamily* family);

// Simple fonts
class Settings;
const SimpleGFXfont* getUIFont(Settings& settings);

const SimpleGFXfont* getTitleFont();
void setTitleFont(const SimpleGFXfont* font);
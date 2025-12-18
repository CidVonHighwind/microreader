#pragma once

#include "rendering/SimpleFont.h"

// Font family
FontFamily* getCurrentFontFamily();
void setCurrentFontFamily(FontFamily* family);

// Simple fonts
const SimpleGFXfont* getMainFont();
void setMainFont(const SimpleGFXfont* font);

const SimpleGFXfont* getTitleFont();
void setTitleFont(const SimpleGFXfont* font);
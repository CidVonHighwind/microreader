#include "MenuDisplayGray.h"

MenuDisplayGray::MenuDisplayGray(EInk426_Gray& epd_instance) : epd(epd_instance) {}

void MenuDisplayGray::begin() {
  // Any initialization specific to gray display
}

void MenuDisplayGray::drawFullMenu(const char** menuItems, int menuCount, int selectedIndex) {
  // Demo: Show initial gray level based on selected index
  int grayLevel = selectedIndex % 4;
  epd.drawSolidGreyLevel(grayLevel);
}

void MenuDisplayGray::updateCursor(const char** menuItems, int menuCount, int oldIndex, int newIndex) {
  // Demo: Cycle through solid gray colors based on menu index
  int grayLevel = newIndex % 4;
  epd.drawSolidGreyLevel(grayLevel);
}

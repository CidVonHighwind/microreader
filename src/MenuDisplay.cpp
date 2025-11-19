#include "MenuDisplay.h"

MenuDisplay::MenuDisplay(EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT>& disp) : display(disp) {}

void MenuDisplay::begin() {
  // Any initialization specific to this display implementation
}

void MenuDisplay::drawMenuItem(const char* itemText, int y, bool isSelected) {
  display.setCursor(15, y);
  if (isSelected) {
    display.print(">");
    display.print(itemText);
    display.print("<");
  } else {
    display.print(itemText);
  }
}

void MenuDisplay::drawFullMenu(const char** menuItems, int menuCount, int selectedIndex) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextSize(3);
    display.setCursor(30, 30);
    display.println("Library");

    // Draw menu items
    display.setTextSize(2);
    for (int i = 0; i < menuCount; i++) {
      int y = menuStartY + (i * lineHeight);
      drawMenuItem(menuItems[i], y, i == selectedIndex);
    }
  } while (display.nextPage());
}

void MenuDisplay::updateCursor(const char** menuItems, int menuCount, int oldIndex, int newIndex) {
  // Calculate the region that covers both old and new cursor positions
  int minIndex = min(oldIndex, newIndex);
  int maxIndex = max(oldIndex, newIndex);

  int y = menuStartY + (minIndex * lineHeight);
  int h = ((maxIndex - minIndex) + 1) * lineHeight - 5;
  int16_t x = 15;
  int16_t w = 300;  // Width to cover full text lines

  // Single partial update for the affected text lines
  display.setPartialWindow(x, y - 5, w, h);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextSize(2);

    // Redraw text lines with cursor prefix
    for (int i = minIndex; i <= maxIndex; i++) {
      int itemY = menuStartY + (i * lineHeight);
      drawMenuItem(menuItems[i], itemY, i == newIndex);
    }
  } while (display.nextPage());
}

#include "MenuDisplay.h"

MenuDisplay::MenuDisplay(EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT>& disp) : display(disp) {}

void MenuDisplay::begin() {
  // Any initialization specific to this display implementation
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

      // Draw cursor for selected item
      if (i == selectedIndex) {
        display.setCursor(5, y);
        display.print(">");
      }

      display.setCursor(20, y);
      display.println(menuItems[i]);
    }
  } while (display.nextPage());
}

void MenuDisplay::updateCursor(const char** menuItems, int menuCount, int oldIndex, int newIndex) {
  // Calculate the region that covers both old and new cursor positions
  int minIndex = min(oldIndex, newIndex);
  int maxIndex = max(oldIndex, newIndex);

  int y = menuStartY + (minIndex * lineHeight);
  int h = ((maxIndex - minIndex) + 1) * lineHeight;
  int16_t x = 5;
  int16_t w = 10;  // Only need to update cursor column (first 10 pixels)

  // Single partial update for just the cursor area
  display.setPartialWindow(x, y - 5, w, h);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextSize(2);

    // Draw cursor only at new position
    for (int i = minIndex; i <= maxIndex; i++) {
      int itemY = menuStartY + (i * lineHeight);

      if (i == newIndex) {
        display.setCursor(5, itemY);
        display.print(">");
      }
    }
  } while (display.nextPage());
}

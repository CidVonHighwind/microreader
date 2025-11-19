#ifndef MENU_DISPLAY_GRAY_H
#define MENU_DISPLAY_GRAY_H

#include <EInk426_Gray.h>
#include <EInk_Gray_Display.h>

template <typename EInk_Type, const uint16_t page_height>
class EInk_Gray_Display_Class;

class MenuDisplayGray {
 public:
  MenuDisplayGray(EInk426_Gray& epd);

  void begin();
  void drawFullMenu(const char** menuItems, int menuCount, int selectedIndex);
  void updateCursor(const char** menuItems, int menuCount, int oldIndex, int newIndex);

  // Configuration
  static const int lineHeight = 30;
  static const int menuStartY = 80;

 private:
  EInk426_Gray& epd;
};

#endif

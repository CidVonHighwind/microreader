#ifndef MENU_DISPLAY_H
#define MENU_DISPLAY_H

#include <EInk426_BW.h>
#include <EInk_BW_Display.h>

class MenuDisplay {
 public:
  MenuDisplay(EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT>& disp);

  void begin();
  void drawFullMenu(const char** menuItems, int menuCount, int selectedIndex);
  void updateCursor(const char** menuItems, int menuCount, int oldIndex, int newIndex);

  // Configuration
  static const int lineHeight = 30;
  static const int menuStartY = 80;

 private:
  EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT>& display;
};

#endif

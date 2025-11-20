#ifndef MENU_DISPLAY_H
#define MENU_DISPLAY_H

#include <Arduino.h>
#include <EInk426_BW.h>
#include <EInk_BW_Display.h>
#include <SPI.h>

class MenuDisplay {
 public:
  // Constructor with pin configuration
  MenuDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  // Destructor to clean up dynamically allocated display
  ~MenuDisplay();

  // Initialize the display hardware and driver
  void begin();

  // Menu drawing methods
  void drawFullMenu(const char** menuItems, int menuCount, int selectedIndex);
  void updateCursor(const char** menuItems, int menuCount, int oldIndex, int newIndex);

  // Button handling methods
  void handleVolumeUp();
  void handleVolumeDown();
  void handleConfirm();

  // Direct display access for advanced operations (e.g., custom images)
  EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT>* getDisplay() {
    return display;
  }

  // Configuration
  static const int lineHeight = 30;
  static const int menuStartY = 80;

 private:
  // Pin configuration
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Display objects (dynamically allocated)
  EInk_BW_Display<EInk426_BW, EInk426_BW::HEIGHT>* display;
  EInk426_BW* epd;

  // State
  bool bebopImageVisible;

  void drawMenuItem(const char* itemText, int y, bool isSelected);
};

#endif

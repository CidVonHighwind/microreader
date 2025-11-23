#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <vector>

#include "Buttons.h"
#include "EInkDisplay.h"
#include "TextLayout.h"
#include "TextRenderer.h"

class DisplayController {
 public:
  // Constructor
  DisplayController(EInkDisplay& display);

  // Initialize and show startup screen
  void begin();

  // Handle button presses
  void handleButtons(Buttons& buttons);

  // Power management
  void showSleepScreen();

 private:
  enum Mode { DEMO, READER };
  enum Screen { WHITE, BLACK, IMAGE, IMAGE_2 };

  EInkDisplay& display;
  TextRenderer textRenderer;
  TextLayout textLayout;
  Mode currentMode;
  Screen currentScreen;
  int currentPage;            // For reader mode pagination
  std::vector<String> pages;  // Loaded text pages
  int totalPages;

  // Power button timing
  static const unsigned long POWER_BUTTON_SLEEP_MS = 1000;

  void showScreen(Screen screen);
  void showReaderPage(int page);
  void switchMode();
  void loadTextFile();
};

#endif

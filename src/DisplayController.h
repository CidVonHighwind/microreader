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

 private:
  enum Mode { DEMO, READER };
  enum Screen { WHITE, BLACK, IMAGE };

  EInkDisplay& display;
  TextRenderer textRenderer;
  TextLayout textLayout;
  Mode currentMode;
  Screen currentScreen;
  int currentPage;            // For reader mode pagination
  std::vector<String> pages;  // Loaded text pages
  int totalPages;

  void showScreen(Screen screen);
  void showReaderPage(int page);
  void switchMode();
  void loadTextFile();
};

#endif

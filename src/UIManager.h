#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <memory>
#include <vector>

#include "Buttons.h"
#include "EInkDisplay.h"
#include "TextRenderer.h"
#include "screens/Screen.h"
#include "screens/text view/TextLayout.h"

class SDCardManager;

class UIManager {
 public:
  // Constructor
  UIManager(EInkDisplay& display, class SDCardManager& sdManager);

  void begin();
  void handleButtons(Buttons& buttons);
  void showSleepScreen();

 private:
  EInkDisplay& display;
  SDCardManager& sdManager;
  TextRenderer textRenderer;
  TextLayout textLayout;
  int currentScreenIndex;

  void showScreen(int index);

  // Screen components (polymorphic list)
  std::vector<std::unique_ptr<::Screen>> screens;
};

#endif

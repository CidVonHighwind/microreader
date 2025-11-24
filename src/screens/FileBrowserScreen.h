#ifndef FILE_BROWSER_SCREEN_H
#define FILE_BROWSER_SCREEN_H

#include <Arduino.h>

#include <vector>

#include "../SDCardManager.h"
#include "../text_renderer/TextRenderer.h"
#include "EInkDisplay.h"
#include "Screen.h"

class UIManager;

class FileBrowserScreen : public Screen {
 public:
  FileBrowserScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager, UIManager& uiManager);
  void begin() override;
  void show() override;

  // Input helpers
  void confirm();
  void selectNext();
  void selectPrev();
  void offsetSelection(int offset);

  void handleButtons(class Buttons& buttons) override;

 private:
  void loadFolder(int maxFiles = 200);
  void renderSdBrowser();

  EInkDisplay& display;
  TextRenderer& textRenderer;
  SDCardManager& sdManager;
  UIManager& uiManager;

  std::vector<String> sdFiles;
  int sdSelectedIndex = 0;
  int sdScrollOffset = 0;

  static const int SD_LINES_PER_SCREEN = 8;
};

#endif
